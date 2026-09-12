"""Study a hand-written reference wrapper module against the registry.

    python scripts/study_reference.py [--ref C:/dev/vulkan-va-zero-copy] [--prefix Gfx_Vulkan_] [--out study/reference.md]

What it extracts from the reference C sources:
  * every wrapper function, its parameters, and the file that defines it
  * the call graph (wrapper -> wrapper, wrapper -> vk command, including commands
    resolved by name through *ProcAddr string literals)
  * a mechanical tier for each function, derived only from its body shape
  * for tier 0 functions: whether the parameter list equals the mechanically
    flattened Vulkan signature (name-for-name, in order)
  * Vulkan coverage: which commands of the reference's profile are wrapped
  * Doxygen completeness on every prototype
"""

from __future__ import annotations

import argparse
import os
import re
import sys
from collections import Counter, defaultdict
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Set

sys.path.insert(0, os.path.dirname(__file__))
from vkxml import Registry, REPO_ROOT  # noqa: E402
from study_commands import role_of  # noqa: E402

DEFAULT_REF = "C:/dev/vulkan-va-zero-copy"
REF_FILES = [
    ("core", "include/gfx/gfx_vulkan.h", "src/gfx/gfx_vulkan.c"),
    ("platform", "include/gfx/gfx_vulkan_platform.h", None),
    ("linux", "include/gfx/linux/gfx_vulkan_linux.h", "src/gfx/linux/gfx_vulkan_linux.c"),
    ("linux-platform", None, "src/gfx/linux/gfx_vulkan_platform.c"),
    ("win32", "include/gfx/win32/gfx_vulkan_win32.h", "src/gfx/win32/gfx_vulkan_win32.c"),
    ("win32-platform", None, "src/gfx/win32/gfx_vulkan_platform.c"),
]
INFRA = {"CheckError", "InitAllocator", "GetInstanceProcAddr", "GetDeviceProcAddr"}


@dataclass
class Fn:
    name: str                      # without prefix
    file: str
    group: str
    params: List[str]              # parameter names in order
    param_types: List[str]
    ret: str
    body: str = ""
    vk_calls: List[str] = field(default_factory=list)
    vk_literals: List[str] = field(default_factory=list)
    wrapper_calls: List[str] = field(default_factory=list)
    stype_assigns: int = 0
    asserts: int = 0
    fatals: int = 0
    doc_params: int = -1
    has_brief: bool = False
    tier: str = ""
    depth: int = 0
    control: int = 0


def strip_comments(src: str) -> str:
    src = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), src, flags=re.S)
    return re.sub(r"//[^\n]*", "", src)


def split_params(text: str) -> (List[str], List[str]):
    names, types = [], []
    text = text.strip()
    if not text or text == "void":
        return names, types
    for raw in text.split(","):
        raw = re.sub(r"\s+", " ", raw.strip())
        m = re.match(r"(.*?)\b([A-Za-z_]\w*)\s*(\[[^\]]*\])?$", raw)
        if not m:
            names.append(raw)
            types.append(raw)
            continue
        names.append(m.group(2))
        types.append((m.group(1).strip() + (m.group(3) or "")).strip())
    return names, types


def parse_definitions(path: str, prefix: str, group: str) -> Dict[str, Fn]:
    with open(path, encoding="utf-8", errors="replace") as f:
        src = strip_comments(f.read())
    fns: Dict[str, Fn] = {}
    for m in re.finditer(rf"([A-Za-z_][\w\s\*]*?)\b{re.escape(prefix)}(\w+)\s*\(", src):
        name = m.group(2)
        ret = re.sub(r"\s+", " ", m.group(1)).strip()
        if ret.endswith("return") or "=" in ret or ret.endswith("(") or ret.endswith(","):
            continue                                     # a call, not a definition
        # find the matching ')' then require '{'
        i = m.end()
        depth = 1
        while i < len(src) and depth:
            depth += {"(": 1, ")": -1}.get(src[i], 0)
            i += 1
        params_text = src[m.end():i - 1]
        j = i
        while j < len(src) and src[j].isspace():
            j += 1
        if j >= len(src) or src[j] != "{":
            continue
        k = j
        depth = 0
        while k < len(src):
            depth += {"{": 1, "}": -1}.get(src[k], 0)
            k += 1
            if depth == 0:
                break
        body = src[j:k]
        names, types = split_params(params_text)
        fn = Fn(name=name, file=os.path.basename(path), group=group, params=names, param_types=types, ret=ret.split()[-1], body=body)
        fn.vk_calls = sorted(set(re.findall(r"\b(vk[A-Z]\w*)\s*\(", body)))
        fn.vk_literals = sorted(set(re.findall(r'"(vk[A-Z]\w*)"', body)))
        fn.wrapper_calls = sorted(set(re.findall(rf"\b{re.escape(prefix)}(\w+)\s*\(", body)) - {name})
        fn.stype_assigns = len(re.findall(r"\.sType\s*=", body))
        fn.asserts = len(re.findall(r"\bLOGASSERT\s*\(", body))
        fn.fatals = len(re.findall(r"\bLOGFATAL\s*\(", body))
        fn.control = len(re.findall(r"\b(if|for|while|switch)\s*\(", body))
        fns[name] = fn
    return fns


VENDOR = ("KHR", "EXT", "NV", "NVX", "AMD", "INTEL", "GOOGLE", "ARM", "QCOM", "MESA", "VALVE", "HUAWEI", "FUCHSIA", "SEC", "LUNARG", "ANDROID", "IMG", "MSFT", "NN", "MVK", "GGP", "QNX", "OHOS", "KHX")
PLATFORM_TOKENS = ("Win32", "Xlib", "Xcb", "Wayland", "Fd", "Android", "Metal", "DirectFB", "Zircon", "Screen", "ViSci")


def canonical_name(vkname: str) -> str:
    """SPEC.md section 3: strip 'vk' and the vendor suffix; keep Vulkan's word order."""
    n = vkname[2:]
    for v in VENDOR:
        if n.endswith(v):
            n = n[: -len(v)]
            break
    return n


def parse_docs(path: str, prefix: str) -> Dict[str, dict]:
    """Map function name -> {brief: bool, params: int} from the Doxygen block preceding each prototype."""
    with open(path, encoding="utf-8", errors="replace") as f:
        src = f.read()
    docs = {}
    # A doc block is "/**" followed by whitespace (the "/*****" separator lines are not docs).
    for m in re.finditer(r"/\*\*(?=\s)(.*?)\*/\s*([^;{/]*?)\b" + re.escape(prefix) + r"(\w+)\s*\(", src, flags=re.S):
        block, name = m.group(1), m.group(3)
        docs[name] = {"brief": "\\brief" in block, "params": len(re.findall(r"\\param\[", block))}
    return docs


def assign_tiers(fns: Dict[str, Fn]) -> None:
    def depth(n: str, seen: Set[str]) -> int:
        f = fns.get(n)
        if not f or n in seen:
            return 0
        if f.depth:
            return f.depth
        seen.add(n)
        callees = [c for c in f.wrapper_calls if c not in INFRA]
        f.depth = 1 + max([depth(c, seen) for c in callees], default=0)
        return f.depth

    for n in fns:
        depth(n, set())
    for f in fns.values():
        callees = [c for c in f.wrapper_calls if c not in INFRA]
        direct = f.vk_calls or f.vk_literals
        if f.name in INFRA:
            f.tier = "infra"
        elif direct and not callees:
            f.tier = "0"
        elif direct and callees:
            f.tier = "0+mixed"
        elif not callees:
            f.tier = "util"
        elif f.control:
            f.tier = "3"                       # branching/looping = policy, whatever the callee count
        elif len(callees) == 1 and f.stype_assigns > 0:
            f.tier = "1"
        elif len(callees) == 1:
            f.tier = "1'"
        elif f.name.startswith("Setup"):
            f.tier = "2"
        else:
            f.tier = "3"


def expected_flat_params(reg: Registry, cmd_name: str) -> Optional[List[str]]:
    """SPEC.md section 4: the mechanical tier-0 parameter list."""
    cmd = reg.resolve_command(cmd_name)
    if not cmd:
        return None
    roles = [role_of(reg, cmd, p, cmd.params) for p in cmd.params]
    filled = sum(1 for r in roles if r in ("I", "P"))
    enum_counts = {p.name for p, r in zip(cmd.params, roles) if r == "n*"
                   and any(q.len and q.len.split(",")[0] == p.name and q.type != "void" for q in cmd.params)}
    out: List[str] = []
    for p, role in zip(cmd.params, roles):
        stem = p.name[1:] if p.name.startswith("p") else p.name
        pn = "pNext" if filled == 1 else f"p{stem}Next"
        if role == "I":
            s = reg.struct_of(p.type)
            out += [pn if m.name == "pNext" else m.name for m in s.members if m.name != "sType"]
        elif role == "P":
            out += [pn, p.name]
        elif p.name in enum_counts:
            continue
        else:
            out.append(p.name)
    return out


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--ref", default=DEFAULT_REF)
    ap.add_argument("--prefix", default="Gfx_Vulkan_")
    ap.add_argument("--core", default="VK_VERSION_1_2")
    ap.add_argument("--out", default=os.path.join(REPO_ROOT, "study", "reference.md"))
    args = ap.parse_args()

    reg = Registry.load()
    fns: Dict[str, Fn] = {}
    docs: Dict[str, dict] = {}
    files = REF_FILES
    if os.path.exists(os.path.join(args.ref, "src", "cvk.c")):
        files = [("generated", "include/cvk.h", "src/cvk.c")]
    for group, hdr, src in files:
        if src and os.path.exists(os.path.join(args.ref, src)):
            fns.update(parse_definitions(os.path.join(args.ref, src), args.prefix, group))
        if hdr and os.path.exists(os.path.join(args.ref, hdr)):
            docs.update(parse_docs(os.path.join(args.ref, hdr), args.prefix))
    for n, f in fns.items():
        d = docs.get(n)
        if d:
            f.has_brief, f.doc_params = d["brief"], d["params"]
    assign_tiers(fns)

    # Extension profile of the reference: every *_EXTENSION_NAME token in its sources.
    ext_names: Set[str] = set()
    upper_to_ext = {e.upper(): e for e in reg.extensions}
    for root_dir in ("src", "include"):
        for dp, _, files in os.walk(os.path.join(args.ref, root_dir)):
            for fn in files:
                if fn.endswith((".c", ".h")):
                    with open(os.path.join(dp, fn), encoding="utf-8", errors="replace") as fh:
                        for tok in re.findall(r"\bVK_(\w+)_EXTENSION_NAME\b", fh.read()):
                            e = upper_to_ext.get("VK_" + tok)
                            if e:
                                ext_names.add(e)
    profile = reg.profile(args.core, sorted(ext_names))
    profile_names = {c.name for c in profile}
    # Also treat any alias in the profile's extensions as covered by its target.
    used_vk: Set[str] = set()
    for f in fns.values():
        for v in f.vk_calls + f.vk_literals:
            c = reg.resolve_command(v)
            used_vk.add(c.name if c else v)

    tiers = Counter(f.tier for f in fns.values())
    L: List[str] = []
    w = L.append
    w(f"# Reference module study: `{args.prefix}*` in `{args.ref}`")
    w("")
    w(f"Functions: {len(fns)}. Vulkan commands reached: {len(used_vk)}. Reference extension set: {len(ext_names)}.")
    w("")
    w("## Tier histogram (derived mechanically from body shape)")
    w("")
    w("| Tier | Meaning | Count |")
    w("|---|---|---|")
    meaning = {"0": "calls one vk* directly, no wrapper callees", "0+mixed": "calls vk* AND wrappers (should be split)",
               "1": "one wrapper callee + builds an sType struct", "1'": "one wrapper callee, constants only",
               "2": "several callees, Setup-named", "3": "several callees, domain verb", "util": "pure C helper", "infra": "check/procaddr"}
    for t in ["0", "1", "1'", "2", "3", "0+mixed", "util", "infra"]:
        w(f"| {t} | {meaning[t]} | {tiers.get(t, 0)} |")
    w("")
    w("## Tier 0: name and parameter fidelity against mechanical flattening")
    w("")
    w("Expected = Vulkan params in order, with each single sType input struct replaced by its members (minus sType). "
      "A tier-0 function that differs is a *decision* the pattern must either canonicalize or forbid.")
    w("")
    w("| Function | vk command | Name = vk name? | Params match? | Difference |")
    w("|---|---|---|---|---|")
    exact_names = 0
    exact_params = 0
    t0 = [f for f in fns.values() if f.tier == "0"]
    for f in sorted(t0, key=lambda x: x.name):
        vk = (f.vk_calls + f.vk_literals)
        if len(vk) != 1:
            w(f"| `{f.name}` | {', '.join(vk)} | | | multiple vk calls |")
            continue
        vkname = vk[0]
        cmd = reg.resolve_command(vkname)
        stem = canonical_name(vkname)
        name_ok = f.name == stem
        exact_names += name_ok
        exp = expected_flat_params(reg, vkname)
        diff = ""
        actual = f.params
        # SPEC 4 dispatch: a non-linked extension command on a child handle gains its parent first.
        if actual and exp and actual[0] in ("device", "instance") and actual[0] != exp[0] and actual[1:] == exp:
            actual = actual[1:]
        ok = exp is not None and exp == actual
        exact_params += ok
        if exp is not None and not ok:
            missing = [p for p in exp if p not in f.params]
            extra = [p for p in f.params if p not in exp]
            order = "" if sorted(exp) != sorted(f.params) else "reordered"
            diff = "; ".join(x for x in [f"missing {missing}" if missing else "", f"extra {extra}" if extra else "", order] if x)
        w(f"| `{f.name}` | `{vkname}` | {'yes' if name_ok else 'no: ' + stem} | {'yes' if ok else 'no'} | {diff} |")
    w("")
    w(f"Tier-0 functions: {len(t0)}; exact vk name: {exact_names}; exact mechanical params: {exact_params}.")
    w("")
    w("## Mixed functions (call vk* directly and also call wrappers)")
    w("")
    for f in sorted(fns.values(), key=lambda x: x.name):
        if f.tier == "0+mixed":
            w(f"- `{f.name}` vk={f.vk_calls + f.vk_literals} wrappers={[c for c in f.wrapper_calls if c not in INFRA]}")
    w("")
    w("## Coverage of the reference profile")
    w("")
    w(f"Profile = {args.core} + {', '.join(sorted(ext_names))}")
    w("")
    missing = sorted(profile_names - used_vk)
    outside = sorted(used_vk - profile_names)
    w(f"Wrapped: {len(profile_names & used_vk)} / {len(profile_names)}. Missing: {len(missing)}. Used outside profile: {len(outside)}.")
    w("")
    by_verb = defaultdict(list)
    for n in missing:
        by_verb[reg.commands[n].verb].append(n)
    for v in sorted(by_verb):
        w(f"- **{v}** ({len(by_verb[v])}): {', '.join(by_verb[v])}")
    if outside:
        w("")
        w("Used but outside the computed profile (extension not named in sources, or platform-only):")
        for n in outside:
            w(f"- `{n}` {reg.commands[n].extensions}")
    w("")
    w("## Documentation completeness (header prototypes)")
    w("")
    bad = [f for f in fns.values() if f.doc_params >= 0 and (not f.has_brief or f.doc_params != len(f.params))]
    undoc = [f for f in fns.values() if f.doc_params < 0]
    w(f"Prototypes with a doc block: {len(fns) - len(undoc)}; param-count mismatches or missing brief: {len(bad)}; no doc block found: {len(undoc)}.")
    for f in sorted(bad, key=lambda x: x.name):
        w(f"- `{f.name}`: {f.doc_params} \\param vs {len(f.params)} params{'' if f.has_brief else ', no brief'}")
    for f in sorted(undoc, key=lambda x: x.name):
        w(f"- `{f.name}`: no doc block ({f.file})")
    w("")
    w("## Name-shape conventions observed")
    w("")
    conv = Counter()
    for f in fns.values():
        m = re.match(r"(Cmd|Setup|Create|Allocate|Bind|Get|Queue|Wait|Reset|Map|Unmap|Begin|End|Flush|Invalidate|Import|Update|Write|Select|Calculate|Read|Acquire|Release|Device|Init|Check|Platform)", f.name)
        conv[m.group(1) if m else "(other)"] += 1
    w("| Leading verb | Count |")
    w("|---|---|")
    for v, n in conv.most_common():
        w(f"| {v} | {n} |")
    w("")
    w("## Per-function table")
    w("")
    w("| Function | Group | Tier | Depth | vk | Wrapper callees | sType | Asserts | Params |")
    w("|---|---|---|---|---|---|---|---|---|")
    for f in sorted(fns.values(), key=lambda x: (x.group, x.name)):
        w(f"| `{f.name}` | {f.group} | {f.tier} | {f.depth} | {', '.join(f.vk_calls + f.vk_literals)} | "
          f"{', '.join(c for c in f.wrapper_calls if c not in INFRA)} | {f.stype_assigns} | {f.asserts} | {len(f.params)} |")

    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    with open(args.out, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(L) + "\n")
    print(f"wrote {args.out}: {len(fns)} functions, tiers={dict(tiers)}, coverage {len(profile_names & used_vk)}/{len(profile_names)}")


if __name__ == "__main__":
    main()
