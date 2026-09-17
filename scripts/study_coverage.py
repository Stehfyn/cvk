"""Map the pinned Vulkan registry to cvk's checked-in public API.

    python scripts/study_coverage.py

Writes study/coverage.md, coverage.json, and a standalone coverage.html explorer.
Generator provenance identifies intent; declarations, definitions, and command
direct vk* call sites in the checked-in files establish observed coverage.
"""

from __future__ import annotations

import argparse
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path
import re

from generate import Generator, load_policy
from study_command_structs import build_report, canonical_struct, write
from study_taxonomy import lifecycle_class
from vkxml import Registry, REPO_ROOT

ROOT = Path(REPO_ROOT)
SIGNATURE = re.compile(
    r"(?m)^[ \t]*(?:[A-Za-z_]\w*[ \t*]+)+(cvk[A-Z]\w*)[ \t]*"
    r"\([^;{}]*\)\s*([;{])"
)
NON_CODE = re.compile(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', re.S)


def scan_file(path):
    """Small scanner for generated C; mask comments/literals without moving offsets."""
    text = path.read_text(encoding="utf-8")
    code = NON_CODE.sub(lambda m: re.sub(r"[^\n]", " ", m.group()), text)
    found = {}
    for match in SIGNATURE.finditer(code):
        name, ending = match.groups()
        if ending != ("{" if path.suffix == ".c" else ";"):
            continue
        body = ""
        if ending == "{":
            start, pos, depth = match.end() - 1, match.end(), 1
            while depth and pos < len(code):
                depth += (code[pos] == "{") - (code[pos] == "}")
                pos += 1
            if depth:
                raise ValueError(f"Unclosed function {name}: {path}")
            body = code[start:pos]
        if name in found:
            raise ValueError(f"Duplicate function {name}: {path}")
        found[name] = dict(path=path.relative_to(ROOT).as_posix(),
                           line=text.count("\n", 0, match.start(1)) + 1, body=body)
    return found


def kind(origin):
    if origin.startswith("vk"):
        return "extender convenience" if "+" in origin else (
            "command convenience" if ":" in origin else "command wrapper")
    if origin.startswith("Vk"):
        return "structure builder"
    return {"json": "JSON parser", "policy": "fixed convenience", "chain": "composition",
            "protocol": "protocol"}.get(origin.split(":")[0], "infrastructure")


def build_coverage():
    reg = Registry.load()
    inventory = build_report(reg)
    # Refuse to silently map stale study data to a different registry snapshot.
    inventory_path = ROOT / "study/command_structs.json"
    if json.loads(inventory_path.read_text(encoding="utf-8")) != inventory:
        raise ValueError("Regenerate study/command_structs.json with study_command_structs.py first")
    policy = load_policy(str(ROOT / "cvk.policy.json"))
    gen = Generator(reg, policy)
    gen.run()  # In memory only; do not rewrite include/ or src/.
    if gen.errors:
        raise ValueError("; ".join(gen.errors))

    headers, sources, fingerprints = {}, {}, {}
    for folder, suffix, output in (("include", ".h", headers), ("src", ".c", sources)):
        for path in sorted((ROOT / folder).rglob("cvk*" + suffix)):
            entries = scan_file(path)
            duplicates = output.keys() & entries.keys()
            if duplicates:
                raise ValueError(f"Duplicate public functions: {sorted(duplicates)}")
            output.update(entries)
            fingerprints[path.relative_to(ROOT).as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()

    functions = {}
    for name in sorted(gen.names.keys() | headers.keys() | sources.keys()):
        origin = gen.names.get(name, "infrastructure")
        functions[name] = dict(
            origin=origin, kind=kind(origin),
            present=name in headers and name in sources,
            header={k: v for k, v in headers.get(name, {}).items() if k != "body"},
            source={k: v for k, v in sources.get(name, {}).items() if k != "body"},
        )
    wrappers, associated, builders, extender_helpers = defaultdict(list), defaultdict(list), defaultdict(list), defaultdict(list)
    for name, function in functions.items():
        origin = function["origin"]
        if origin.startswith("vk"):
            command_name = re.match(r"vk\w+", origin).group()
            associated[command_name].append(name)
            if origin == command_name:
                wrappers[command_name].append(name)
            if "+" in origin:
                struct_name = re.search(r"\+(Vk\w+)", origin).group(1)
                extender_helpers[(command_name, struct_name)].append(name)
        elif origin.startswith("Vk"):
            builders[origin.split("::")[0]].append(name)

    def present(names):
        return [n for n in names if functions[n]["present"]]

    selected = {cmd.name for cmd in gen.cmds}
    commands = []
    for item in inventory["commands"]:
        name = item["command"]
        cmd = reg.commands[name]
        covered_wrappers = []
        for wrapper in present(wrappers[name]):
            body = sources[wrapper]["body"]
            # cvk calls Volk's loaded vk* globals directly.
            direct = re.search(rf"\b{re.escape(name)}\s*\(", body)
            if direct:
                covered_wrappers.append(wrapper)
        status = "covered" if covered_wrappers else ("missing wrapper" if name in selected else "outside profile")
        providers = sorted({e for spelling in item["names"] for e in reg.commands[spelling].extensions})
        excluded_platforms = sorted({reg.extensions[e].platform for e in providers
                                     if reg.extensions[e].platform and e not in gen.exts})
        reason = "" if status == "covered" else (
            "Selected command lacks a verified public wrapper." if name in selected else
            "Excluded platform/beta category: " + ", ".join(excluded_platforms)
            if excluded_platforms else "No provider selected by policy.")
        roles = gen.roles(cmd)
        root_parameters = []
        for param, role in zip(cmd.params, roles):
            st = canonical_struct(reg, param.type)
            if not st:
                continue
            resets = any(re.search(rf"\b{re.escape(param.name)}\s*\[[^]]+\]\.pNext\s*=\s*NULL", sources[w]["body"])
                         for w in covered_wrappers)
            root_parameters.append(dict(name=param.name, structure=st.name, role=role,
                                        wrapper_resets_array_pnext=resets))
        commands.append(dict(
            command=name, names=item["names"], status=status, reason=reason,
            selected=name in selected, core_version=cmd.core_version,
            extensions=providers, dispatch=cmd.dispatch_handle or "global",
            lifecycle=lifecycle_class(cmd, roles), wrappers=covered_wrappers,
            expected_wrappers=wrappers[name], related_functions=present(associated[name]),
            extender_helpers={s: present(names) for (c, s), names in sorted(extender_helpers.items())
                              if c == name and present(names)},
            parameters=root_parameters, structures=item["structures"],
        ))

    structures = {}
    for name, data in inventory["structures"].items():
        st = reg.structs[name]
        names = present(builders[name])
        if names:
            state = "builder present"
        elif not gen.struct_in_profile(st):
            state = "outside profile"
        elif st.returnedonly and not st.stype:
            state = "plain output; no initializer needed"
        elif name in ("VkAllocationCallbacks", "VkBaseInStructure", "VkBaseOutStructure"):
            state = "manual initialization by generator policy"
        elif any(":" in m.full_type for m in st.members):
            state = "bitfield structure; manual initialization"
        else:
            state = "no builder observed"
        structures[name] = dict(**data, selected=gen.struct_in_profile(st), builders=names, status=state)

    extensions = []
    by_name = {c["command"]: c for c in commands}
    for name, ext in sorted(reg.extensions.items()):
        canonical = sorted({reg.resolve_command(c).name for c in ext.commands if reg.resolve_command(c)})
        canonical = [c for c in canonical if c in by_name]
        extensions.append(dict(extension=name, selected=name in gen.exts, platform=ext.platform,
                               commands=canonical, covered=sum(by_name[c]["status"] == "covered" for c in canonical)))
    return dict(
        registry=inventory["registry"], registry_sha256=inventory["registry_sha256"],
        policy="cvk.policy.json", core=gen.core, platforms=sorted(gen.platforms),
        input_hashes={**fingerprints, **{p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest()
                                      for p in ("cvk.policy.json", "scripts/generate.py", "scripts/vkxml.py", "study/command_structs.json")}},
        summary=dict(canonical_commands=len(commands), command_spellings=inventory["command_count"],
                     selected_commands=len(selected), statuses=dict(Counter(c["status"] for c in commands)),
                     structures=len(structures), structure_statuses=dict(Counter(s["status"] for s in structures.values())),
                     public_functions=sum(f["present"] for f in functions.values()),
                     function_kinds=dict(Counter(f["kind"] for f in functions.values() if f["present"])),
                     missing_generated_functions=[n for n in gen.names if not functions[n]["present"]]),
        commands=commands, structures=structures, functions=functions, extensions=extensions,
    )


def source_link(name, report):
    source = report["functions"][name]["source"]
    return f"[`{name}`](../{source['path']}#L{source['line']})" if source else f"`{name}`"


def markdown(report):
    summary = report["summary"]
    covered = summary["statuses"].get("covered", 0)
    lines = ["# cvk coverage of the pinned Vulkan registry", "",
             "Generated by `python scripts/study_coverage.py`.", "",
             "[Interactive command/structure map](coverage.html) | [Complete JSON](coverage.json) | "
             "[Vulkan command/structure inventory](command_structs.md)", "",
             f"**{covered}/{summary['canonical_commands']} canonical Vulkan commands "
             f"({covered / summary['canonical_commands']:.1%}) have verified public wrappers.** "
             f"The policy selects {summary['selected_commands']} commands; "
             f"{summary['statuses'].get('missing wrapper', 0)} selected commands lack a verified wrapper.", "",
             f"Source: `{report['registry']}`, SHA-256 `{report['registry_sha256']}`. "
             f"[Policy](../cvk.policy.json): `{report['core']}`, all supported extensions in "
             f"the neutral, {', '.join(report['platforms'])} categories.", "",
             "## What coverage means", "",
             "A covered command has a generator provenance record, a public declaration, "
             "a source definition, and an observed direct call through Volk's loaded `vk*` "
             "function pointer. Alias spellings share one canonical "
             "command; they do not imply separate cvk entry points.", "",
             "The structure map separately records direct parameters, nested members, and "
             "registry `pNext` relationships, plus observed builders and command/extender "
             "conveniences. A builder is not proof of a command-specific convenience. "
             "Conveniences are indexed by command and extender, not by every possible nested path.", "",
             "This measures the repository's API surface, not Vulkan conformance or complete "
             "specification semantics. It does not test driver behavior, feature enablement, "
             "all chain combinations, opaque payloads, callback signatures, or valid usage. "
             "The map includes all generated platform files; a particular CMake build selects "
             "only its platform sources ([CMakeLists.txt](../CMakeLists.txt)). Vulkan SC is separate.", "",
             "## Command coverage by dispatch handle", "",
             "| Dispatch | Covered | Selected | Registry total |", "|---|---:|---:|---:|"]
    for dispatch in sorted({c["dispatch"] for c in report["commands"]}):
        group = [c for c in report["commands"] if c["dispatch"] == dispatch]
        lines.append(f"| {dispatch} | {sum(c['status'] == 'covered' for c in group)} | {sum(c['selected'] for c in group)} | {len(group)} |")
    lines.extend(["", "## Core-version command coverage", "",
                  "Counts are cumulative canonical commands; extension aliases are counted once.", "",
                  "| Core version | Covered | Total |", "|---|---:|---:|"])
    for version in sorted({c["core_version"] for c in report["commands"] if c["core_version"]}):
        group = [c for c in report["commands"] if c["core_version"] and c["core_version"] <= version]
        lines.append(f"| {version} | {sum(c['status'] == 'covered' for c in group)} | {len(group)} |")
    lines.extend(["", "## Structure support", "", "| Builder classification | Structures/unions used by commands |", "|---|---:|"])
    for state, count in sorted(summary["structure_statuses"].items()):
        lines.append(f"| {state} | {count} |")
    lines.extend(["", "## Output-array chain limitation", "",
                  "The following base wrappers explicitly reset output-array elements' `pNext` "
                  "to `NULL`. Caller-prepared chains on those arrays are overwritten. "
                  "Available extender conveniences appear in the interactive map; their presence "
                  "does not imply arbitrary multi-extender chain support. See the generation rule "
                  "in [generate.py](../scripts/generate.py).", "",
                  "| Vulkan command | Output parameter | Wrapper evidence |", "|---|---|---|"])
    for c in report["commands"]:
        for p in c["parameters"]:
            if p["wrapper_resets_array_pnext"]:
                lines.append(f"| `{c['command']}` | `{p['name']}` | {', '.join(source_link(n, report) for n in c['wrappers'])} |")
    lines.extend(["", "## Commands without wrappers", "", "| Command | Classification | Reason |", "|---|---|---|"])
    for c in report["commands"]:
        if c["status"] != "covered":
            lines.append(f"| `{c['command']}` | {c['status']} | {c['reason']} |")
    lines.extend(["", "## Public function inventory", "", "| Generator category | Observed declarations + definitions |", "|---|---:|"])
    for category, count in sorted(summary["function_kinds"].items()):
        lines.append(f"| {category} | {count} |")
    if summary["missing_generated_functions"]:
        lines.extend(["", "Missing expected functions: " + ", ".join(summary["missing_generated_functions"])])
    lines.extend(["", "## Every Vulkan command", "",
                  "Structure counts include distinct canonical types reachable by typed members "
                  "and chains. Aliases, parameter paths, builders, and convenience functions are "
                  "expanded in the interactive map and JSON.", "",
                  "| Vulkan command | Status | cvk wrapper / source evidence | Structures |",
                  "|---|---|---|---:|"])
    for c in report["commands"]:
        links = ", ".join(source_link(n, report) for n in c["wrappers"])
        lines.append(f"| `{c['command']}` | {c['status']} | {links} | {len({s['structure'] for s in c['structures']})} |")
    lines.extend(["", "## Extension command coverage", "",
                  "Only extensions that introduce/require commands appear here. Commandless "
                  "extensions remain in JSON and must not be interpreted as fully covered from "
                  "a 0/0 command count. Counts overlap when extensions share promoted commands.", "",
                  "| Extension | Selected | Platform/beta category | Covered / canonical commands |", "|---|---|---|---:|"])
    for ext in report["extensions"]:
        if ext["commands"]:
            lines.append(f"| `{ext['extension']}` | {'yes' if ext['selected'] else 'no'} | {ext['platform'] or 'neutral'} | {ext['covered']} / {len(ext['commands'])} |")
    lines.extend(["", "## Evidence and reproducibility", "",
                  "The JSON records input hashes and header/source line numbers for every function. "
                  "The script rejects a stale `study/command_structs.json`. Historical "
                  "`study/generated.md` and `study/FINDINGS.md` are not current coverage baselines.", "",
                  "Sources: [command_structs.json](command_structs.json), [vkxml.py](../scripts/vkxml.py), "
                  "[generate.py](../scripts/generate.py), [cvk.policy.json](../cvk.policy.json), "
                  "`include/**/cvk*.h`, `src/**/cvk*.c`, and [CMakeLists.txt](../CMakeLists.txt).", ""])
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out-dir", type=Path, default=ROOT / "study")
    args = parser.parse_args()
    report = build_coverage()
    write(args.out_dir / "coverage.json", json.dumps(report, indent=2) + "\n")
    write(args.out_dir / "coverage.md", markdown(report))
    template = (ROOT / "scripts/templates/coverage.html").read_text(encoding="utf-8")
    payload = json.dumps(report, separators=(",", ":")).replace("<", "\\u003c")
    write(args.out_dir / "coverage.html", template.replace("__COVERAGE_DATA__", payload))
    print(json.dumps(report["summary"], indent=2))


if __name__ == "__main__":
    main()
