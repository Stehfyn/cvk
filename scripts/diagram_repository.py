"""Generate standalone SVG tier and call diagrams for the whole repository.

    python scripts/diagram_repository.py

Tier provenance comes from the generator and policy, not function-name guesses.
Calls are extracted from every generated C source and test. Protocol definitions
are represented at their generated locations, with their authored origin retained.
Dependency implementations are boundaries, not recursively expanded vendor code.
"""

from __future__ import annotations

import argparse
from collections import Counter, defaultdict
import json
from pathlib import Path
import re
import shutil
import subprocess

from generate import Generator, load_policy
from vkxml import Registry, REPO_ROOT


ROOT = Path(REPO_ROOT)
TIERS = {
    "3": ("Tier 3", "Authored domain protocols", "#a855f7"),
    "2": ("Tier 2", "Multi-step policy compositions", "#2563eb"),
    "1p": ("Tier 1'", "Singulars and fixed-argument chains", "#0891b2"),
    "1": ("Tier 1", "Builders and extension conveniences", "#059669"),
    "0": ("Tier 0", "Vulkan commands and flattened siblings", "#ea580c"),
    "json": ("JSON", "Generated serialization support", "#db2777"),
    "util": ("Helpers", "Fixed algorithms and diagnostics", "#64748b"),
    "test": ("Tests", "Repository test entry points", "#475569"),
    "vk": ("Vulkan", "Direct and resolved API entry points", "#dc2626"),
    "external": ("Dependencies", "C, OS, utility and indirect calls", "#78716c"),
}
ORDER = list(TIERS)
TOKEN = re.compile(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', re.S)
IDENT_CALL = re.compile(r"\b([A-Za-z_]\w*)\s*\(")
DEFINITION = re.compile(r"^[A-Za-z_][\w \t*]*?\b([A-Za-z_]\w*)\s*\(", re.M)
SKIP_CALLS = {"if", "for", "while", "switch", "return", "sizeof", "_Alignof", "CHECK"}


def masked(text):
    return TOKEN.sub(lambda m: "".join("\n" if c == "\n" else " " for c in m.group()), text)


def matching(text, start, opening, closing):
    depth = 0
    for pos in range(start, len(text)):
        if text[pos] == opening:
            depth += 1
        elif text[pos] == closing:
            depth -= 1
            if not depth:
                return pos
    raise ValueError(f"Unmatched {opening} at {start}")


def definitions(path):
    source = path.read_text(encoding="utf-8")
    code = masked(source)
    end = 0
    for match in DEFINITION.finditer(code):
        if match.start() < end:
            continue
        close = matching(code, match.end() - 1, "(", ")")
        start = close + 1
        while start < len(code) and code[start].isspace():
            start += 1
        if start == len(code) or code[start] != "{":
            continue
        end = matching(code, start, "{", "}") + 1
        yield {
            "name": match.group(1), "line": source.count("\n", 0, match.start()) + 1,
            "body": source[start:end], "code": code[start:end],
            "signature": source[match.start():close + 1],
        }


def tier(origin, policy):
    if origin.startswith("protocol:"):
        return "3"
    if origin.startswith("json:"):
        return "json"
    if origin.startswith("policy:") or origin == "infrastructure":
        return "util"
    if origin.startswith("chain:"):
        steps = policy["chains"][origin.split(":", 1)[1]]["steps"]
        return "1p" if len(steps) == 1 and "call" in steps[0] and steps[0].get("bind") else "2"
    if origin.startswith("Vk"):
        return "1"
    if origin.startswith("vk"):
        if "+" in origin:
            return "1p" if origin.endswith("[1]") else "1"
        return "1p" if origin.endswith(":singular") else "0"
    raise ValueError(f"Unknown tier provenance: {origin}")


def collect():
    nodes, bodies, emitted = {}, {}, {}
    source_files = []
    for profile in ("cvk", "scvk"):
        policy = load_policy(str(ROOT / f"{profile}.policy.json"))
        registry = Registry.load(str(ROOT / policy.get("registry", "external/Vulkan-Headers/registry/vk.xml")), policy.get("api", "vulkan"))
        generator = Generator(registry, policy)
        generator.run()
        if generator.errors:
            raise ValueError("\n".join(generator.errors))
        origins = {profile + name[3:]: origin for name, origin in generator.names.items()}
        origins.update({profile + name: "infrastructure" for name in ("Fatal", "CheckResult")})
        emitted[profile] = set(origins)
        for path in sorted((ROOT / "src").rglob(f"{profile}*.c")):
            if not (path.stem == profile or path.stem.startswith(profile + "_")):
                continue
            relative = path.relative_to(ROOT).as_posix()
            source_files.append(relative)
            for fn in definitions(path):
                name = fn["name"]
                origin = origins[name]
                nodes[name] = {"id": name, "name": name, "profile": profile,
                               "platform": "core" if path.parent == ROOT / "src" else path.parent.name,
                               "tier": tier(origin, policy), "origin": origin, "file": relative,
                               "line": fn["line"], "signature": fn["signature"]}
                bodies[name] = fn
        actual = {n for n in nodes if nodes[n]["profile"] == profile}
        if actual != emitted[profile]:
            raise ValueError(f"Definition coverage mismatch for {profile}: {actual ^ emitted[profile]}")

    for path in sorted((ROOT / "tests").rglob("*.c")):
        relative = path.relative_to(ROOT).as_posix()
        source_files.append(relative)
        for fn in definitions(path):
            name = relative + ":" + fn["name"]
            nodes[name] = {"id": name, "name": fn["name"], "profile": "tests", "platform": "core",
                           "tier": "test", "origin": relative, "file": relative,
                           "line": fn["line"], "signature": fn["signature"]}
            bodies[name] = fn

    edges = Counter()
    for caller, fn in bodies.items():
        profile = nodes[caller]["profile"]
        prefix = "scvk" if profile == "scvk" else "cvk"
        resolved = set(re.findall(r'\b' + prefix + r'Get(?:Device|Instance)ProcAddr\([^;]*?"(vk\w+)"', fn["body"]))
        for command in resolved:
            callee = f"{profile}:{command}"
            nodes.setdefault(callee, {"id": callee, "name": command, "profile": profile, "tier": "vk", "platform": "API", "origin": "resolved Vulkan entry point"})
            edges[caller, callee, "resolved"] += 1
        resolved_locals = set(re.findall(r'\b(\w+)\s*=\s*\(PFN_vk\w+\)\s*' + prefix + r'Get(?:Device|Instance)ProcAddr', fn["code"]))
        for match in IDENT_CALL.finditer(fn["code"]):
            called = match.group(1)
            if called in SKIP_CALLS or called in resolved_locals:
                continue
            kind = "call"
            if called in ("CVK_CHECK", "SCVK_CHECK"):
                callee, kind = prefix + "CheckResult", "macro"
            elif called in ("CVK_ASSERT", "SCVK_ASSERT"):
                callee, kind = prefix + "Fatal", "macro"
            elif called in bodies:
                callee = called
            elif called.startswith(("cvk", "scvk")):
                raise ValueError(f"Unresolved wrapper call: {caller} -> {called}")
            else:
                is_vk = bool(re.match(r"vk[A-Z]", called))
                before = fn["code"][max(0, match.start() - 2):match.start()]
                if before in ("->",) or before.endswith(".") or called.startswith("pfn"):
                    kind = "indirect"
                callee = f"{profile}:{'indirect:' if kind == 'indirect' else ''}{called}"
                nodes.setdefault(callee, {"id": callee, "name": called, "profile": profile,
                                         "tier": "vk" if is_vk else "external", "platform": "API",
                                         "origin": "indirect callback; target determined at runtime" if kind == "indirect" else "dependency boundary"})
            edges[caller, callee, kind] += 1
        # References passed as callbacks are distinct from executed calls.
        for referenced in set(re.findall(r"\b(?:s?cvk)[A-Z]\w*\b", fn["code"])):
            if referenced in bodies and not re.search(r"\b" + referenced + r"\s*\(", fn["code"]):
                edges[caller, referenced, "reference"] += 1
    return {"nodes": sorted(nodes.values(), key=lambda n: n["id"]),
            "edges": [{"from": a, "to": b, "kind": k, "sites": count} for (a, b, k), count in sorted(edges.items())],
            "source_files": source_files,
            "classification": "Generator origin; single-step chains with argument bindings are tier 1'. JSON and fixed helpers remain separate support groups.",
            "scope": "All generated cvk/scvk C definitions and repository C tests. Authored protocols are represented by their generated definitions. Vendor internals and runtime callback targets are dependency boundaries. Python/build tools are shown in the repository tier tree, not treated as runtime wrappers.",
            "edge_semantics": "Static call/reference relationships, not execution frequency. Resolved Vulkan edges use explicit ProcAddr names. Macro edges are conditional diagnostics. Indirect callback targets cannot be determined statically."}


class DOT:
    def __init__(self, title, subtitle):
        self.lines = ["digraph repository {", 'graph [rankdir=TB, bgcolor="#f8fafc", pad=.5, nodesep=.35, ranksep=.8, fontname="Segoe UI", fontsize=20, labelloc=t, compound=true, outputorder=edgesfirst, splines=polyline, nslimit=2, nslimit1=2, mclimit=.2, remincross=false];',
                      'node [shape=box, style="rounded,filled", fillcolor=white, fontname="Segoe UI", fontsize=11, margin=".15,.1", penwidth=1.4];',
                      'edge [color="#94a3b8", arrowsize=.65, fontname="Segoe UI", fontsize=9];',
                      "label=" + json.dumps(title + "\n" + subtitle) + ";"]

    def node(self, ident, label, key="util", **attrs):
        attrs = {"label": label, "color": TIERS[key][2], "id": ident, **attrs}
        self.lines.append(json.dumps(ident) + " [" + ",".join(k + "=" + json.dumps(str(v)) for k, v in attrs.items()) + "];")

    def edge(self, source, target, **attrs):
        self.lines.append(json.dumps(source) + " -> " + json.dumps(target) + " [" + ",".join(k + "=" + json.dumps(str(v)) for k, v in attrs.items()) + "];")

    def save(self, path):
        path.write_text("\n".join(self.lines + ["}"]) + "\n", encoding="utf-8")


def tier_tree(data, out):
    owned = [n for n in data["nodes"] if "file" in n]
    graph = DOT("Whole-repository tier tree", f'{len(owned):,} C definitions / both profiles / all platforms. Click a tier for the complete inventory.')
    graph.node("repo", "cvk repository")
    for profile in ("cvk", "scvk", "tests"):
        members = [n for n in owned if n["profile"] == profile]
        graph.node(profile, f"{profile}\n{len(members):,} definitions")
        graph.edge("repo", profile)
        for key in ORDER:
            subset = [n for n in members if n["tier"] == key]
            if not subset:
                continue
            ident = f"{profile}-{key}"
            graph.node(ident, f"{TIERS[key][0]} / {len(subset):,}\n{TIERS[key][1]}", key, URL=f"tier-functions.svg#{ident}")
            graph.edge(profile, ident, color=TIERS[key][2])
            for platform, count in sorted(Counter(n["platform"] for n in subset).items()):
                group = ident + "-" + platform
                graph.node(group, f"{platform}\n{count:,} functions", key)
                graph.edge(ident, group, color=TIERS[key][2])
    graph.node("tooling", "Generation, build and research")
    graph.edge("repo", "tooling")
    for ident, label in [
        ("inputs", "external/*/registry\ncvk.policy.json\nscvk.policy.json"),
        ("protocols", "scripts/protocols/\nAuthored protocol implementations"),
        ("generator", "scripts/generate.py\nWrites include/ and src/"),
        ("build", "CMakeLists.txt; tests/\nscripts/check_generated.py"),
        ("research", "scripts/study_*.py; study/\nREADME.md; SPEC.md"),
    ]:
        graph.node(ident, label)
        graph.edge("tooling", ident)
    graph.save(out / "tier-tree.dot")


def full_tree(data, out):
    graph = DOT("Complete repository tier tree", "Every C function / profile > tier > platform > definition. Hover for source and provenance; use browser Find.")
    graph.node("repo", "cvk repository")
    for profile in ("cvk", "scvk", "tests"):
        graph.node(profile, profile)
        graph.edge("repo", profile)
        members = [n for n in data["nodes"] if n["profile"] == profile and "file" in n]
        for key in ORDER:
            subset = [n for n in members if n["tier"] == key]
            if not subset:
                continue
            tier_id = f"{profile}-{key}"
            graph.node(tier_id, f"{TIERS[key][0]} / {len(subset):,}", key)
            graph.edge(profile, tier_id)
            for platform in sorted({n["platform"] for n in subset}):
                platform_id = tier_id + "-" + platform
                graph.node(platform_id, platform, key)
                graph.edge(tier_id, platform_id)
                for node in (n for n in subset if n["platform"] == platform):
                    graph.node(node["id"], node["name"], key, tooltip=f'{node["file"]}:{node["line"]} | {node["origin"]}')
                    graph.edge(platform_id, node["id"], color=TIERS[key][2])
    graph.save(out / "tier-functions.dot")


def overview(data, out):
    nodes = {n["id"]: n for n in data["nodes"]}
    graph = DOT("Whole-repository wrapper call reference", "Caller to callee / both profiles and tests / every relationship contributes. Counts are static relationships, not execution frequency.")
    for profile in ("cvk", "scvk", "tests"):
        graph.lines.append("subgraph cluster_" + profile + " {")
        graph.lines.append("label=" + json.dumps(profile) + "; color=\"#cbd5e1\";")
        previous = None
        for key in ORDER:
            subset = [n for n in nodes.values() if n["profile"] == profile and n["tier"] == key]
            if not subset:
                continue
            ident = f"{profile}-{key}"
            graph.node(ident, f"{profile} / {TIERS[key][0]}\n{len(subset):,} functions or endpoints", key, URL=f'wrapper-calls-full.svg#{subset[0]["id"]}')
            if previous:
                graph.edge(previous, ident, style="invis", weight=100)
            previous = ident
        graph.lines.append("}")
    aggregated = Counter()
    for edge in data["edges"]:
        a, b = nodes[edge["from"]], nodes[edge["to"]]
        aggregated[f'{a["profile"]}-{a["tier"]}', f'{b["profile"]}-{b["tier"]}'] += 1
    for (a, b), count in sorted(aggregated.items()):
        key = a.split("-", 1)[1]
        graph.edge(a, b, color=TIERS[key][2], penwidth=1 + min(4, count ** .25), label=f"{count:,}", constraint="false", tooltip=f"{a} -> {b}: {count:,} typed relationships")
    graph.save(out / "wrapper-calls.dot")


def full_calls(data, out):
    graph = DOT("Complete repository wrapper call graph", f'{len(data["nodes"]):,} nodes / {len(data["edges"]):,} typed references. Top-down call flow; cycles may return upward. No page or raster size limit.')
    nodes = {n["id"]: n for n in data["nodes"]}
    for profile in ("cvk", "scvk", "tests"):
        graph.lines.append("subgraph cluster_" + profile + " {")
        graph.lines.append("label=" + json.dumps(profile) + "; color=\"#cbd5e1\";")
        for node in (n for n in nodes.values() if n["profile"] == profile):
            tip = node["origin"]
            if "file" in node:
                tip += f' | {node["file"]}:{node["line"]}'
            graph.node(node["id"], node["name"] + "\n" + TIERS[node["tier"]][0] + " / " + node["platform"], node["tier"], tooltip=tip)
        graph.lines.append("}")
    for edge in data["edges"]:
        graph.edge(edge["from"], edge["to"], color=TIERS[nodes[edge["from"]]["tier"]][2],
                   style="solid" if edge["kind"] == "call" else "dashed",
                   tooltip=f'{edge["kind"]} / {edge["sites"]} static site(s)',
                   constraint="false" if edge["kind"] in ("macro", "reference") else "true")
    graph.save(out / "wrapper-calls-full.dot")


def trees(data, out):
    """Lossless call forest: expand each node once, retain every repeated edge."""
    nodes = {n["id"]: n for n in data["nodes"]}
    children = defaultdict(list)
    incoming = Counter()
    for edge in data["edges"]:
        children[edge["from"]].append(edge)
        incoming[edge["to"]] += 1
    expanded = set()
    rows = []

    def row(prefix, last, label, ident=None, ref=None, tier_key="util"):
        text = prefix + ("`-- " if last else "|-- ") + label
        rows.append({"text": text, "id": ident, "ref": ref, "tier": tier_key})

    def visit(ident, prefix, last, kind=None):
        node = nodes[ident]
        label = node["name"] + " [" + TIERS[node["tier"]][0] + "; " + node["platform"] + "]"
        if kind and kind != "call":
            label += " {" + kind + "}"
        if ident in expanded:
            row(prefix, last, label + " -> see first occurrence", ref=ident, tier_key=node["tier"])
            return
        expanded.add(ident)
        row(prefix, last, label, ident=ident, tier_key=node["tier"])
        next_prefix = prefix + ("    " if last else "|   ")
        for index, edge in enumerate(children[ident]):
            visit(edge["to"], next_prefix, index == len(children[ident]) - 1, edge["kind"])

    rows.append({"text": "cvk repository / wrapper call trees", "id": None, "ref": None, "tier": "util"})
    for index, profile in enumerate(("cvk", "scvk", "tests")):
        last_profile = index == 2
        row("", last_profile, profile)
        prefix = "    " if last_profile else "|   "
        candidates = sorted((n for n in nodes.values() if n["profile"] == profile),
                            key=lambda n: (incoming[n["id"]] != 0, ORDER.index(n["tier"]), n["name"]))
        for node in candidates:
            if node["id"] not in expanded:
                visit(node["id"], prefix, False)
        row(prefix, True, "end " + profile)
    assert expanded == set(nodes), "Call tree omitted graph nodes"
    assert sum(bool(r["id"] or r["ref"]) for r in rows) >= len(data["edges"])
    (out / "wrapper-call-tree.txt").write_text("\n".join(r["text"] for r in rows) + "\n", encoding="utf-8")
    graph = DOT("Whole-repository wrapper call trees", "Every definition and relationship. Repeated/cyclic callees link to their first occurrence. Top-down branches with no page-size limit.")
    stack = []
    first = {r["id"]: f"tree-{index}" for index, r in enumerate(rows) if r["id"]}
    for index, r in enumerate(rows):
        ident = f"tree-{index}"
        # ASCII connector columns encode the exact parent/child structure.
        match = re.search(r"[|`]-- ", r["text"])
        depth = match.start() // 4 + 1 if match else 0
        label = r["text"][match.end():] if match else r["text"]
        attrs = {}
        if r["ref"]:
            attrs.update(URL="#" + first[r["ref"]], style="rounded,dashed,filled")
        if r["id"]:
            node = nodes[r["id"]]
            attrs["tooltip"] = node.get("file", "dependency") + (":" + str(node["line"]) if "line" in node else "") + " | " + node["origin"]
        graph.node(ident, label, r["tier"], **attrs)
        while len(stack) > depth:
            stack.pop()
        if stack:
            graph.edge(stack[-1], ident, color=TIERS[r["tier"]][2])
        stack.append(ident)
    graph.save(out / "wrapper-call-tree.dot")

    lines = ["cvk repository / tiers and functions"]
    for p_index, profile in enumerate(("cvk", "scvk", "tests")):
        prefix = "    " if p_index == 2 else "|   "
        lines.append(("`-- " if p_index == 2 else "|-- ") + profile)
        tiers = [key for key in ORDER if any(n["profile"] == profile and n["tier"] == key and "file" in n for n in nodes.values())]
        for t_index, key in enumerate(tiers):
            tier_prefix = prefix + ("    " if t_index == len(tiers) - 1 else "|   ")
            members = [n for n in nodes.values() if n["profile"] == profile and n["tier"] == key and "file" in n]
            lines.append(prefix + ("`-- " if t_index == len(tiers) - 1 else "|-- ") + f"{TIERS[key][0]} ({len(members)} functions)")
            platforms = sorted({n["platform"] for n in members})
            for g_index, platform in enumerate(platforms):
                platform_prefix = tier_prefix + ("    " if g_index == len(platforms) - 1 else "|   ")
                lines.append(tier_prefix + ("`-- " if g_index == len(platforms) - 1 else "|-- ") + platform)
                functions = sorted((n for n in members if n["platform"] == platform), key=lambda n: n["name"])
                for f_index, node in enumerate(functions):
                    lines.append(platform_prefix + ("`-- " if f_index == len(functions) - 1 else "|-- ") + f'{node["name"]} ({node["file"]}:{node["line"]})')
    (out / "tier-tree.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out-dir", type=Path, default=ROOT / "study" / "diagrams")
    args = parser.parse_args()
    data = collect()
    args.out_dir.mkdir(parents=True, exist_ok=True)
    (args.out_dir / "repository-graph.json").write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
    tier_tree(data, args.out_dir)
    full_tree(data, args.out_dir)
    overview(data, args.out_dir)
    full_calls(data, args.out_dir)
    trees(data, args.out_dir)
    for path in sorted(args.out_dir.glob("*.dot")):
        target = path.with_suffix(".svg")
        dot = shutil.which("dot")
        if dot:
            subprocess.run([dot, "-Tsvg", str(path), "-o", str(target)], check=True)
            navigation = (ROOT / "scripts/svg_navigation.js").read_text(encoding="utf-8")
            svg = target.read_text(encoding="utf-8")
            target.write_text(svg.replace("</svg>", '<script type="application/ecmascript"><![CDATA[\n' + navigation + '\n]]></script>\n</svg>'), encoding="utf-8")
        else:
            subprocess.run(["node", str(ROOT / "scripts/render_diagram.mjs"), str(path), str(target)], check=True)
    print(f'{sum("file" in n for n in data["nodes"]):,} repository definitions; {len(data["nodes"]):,} total nodes; {len(data["edges"]):,} relationships; {len(data["source_files"])} C sources')
    print(f"SVG diagrams written to {args.out_dir}")


if __name__ == "__main__":
    main()
