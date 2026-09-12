"""Study the input structs a wrapper must flatten and the pNext extenders that
become tier-1 qualifiers.

    python scripts/study_structs.py [--core VK_VERSION_1_2] [--ext NAME ...] [--out study/structs.md]

Sections:
  * every sType input struct consumed directly by a profile command
  * its flattening hazards (nested sType pointers, arrays of structs, unions,
    void* payloads, fixed arrays, latexmath lengths)
  * its in-profile extenders = candidate tier-1 qualifiers, with the extra
    parameters each would add
  * extender structs that extend more than one base (qualifier reuse)
"""

from __future__ import annotations

import argparse
import os
import sys
from collections import defaultdict
from typing import Dict, List, Set

sys.path.insert(0, os.path.dirname(__file__))
from vkxml import Registry, Struct, REPO_ROOT  # noqa: E402
from study_commands import role_of  # noqa: E402


def hazards(reg: Registry, s: Struct) -> List[str]:
    out = []
    for m in s.members:
        if m.name in ("sType", "pNext"):
            continue
        t = reg.struct_of(m.type)
        if m.is_pointer and t and t.stype:
            out.append(f"{m.name}: pointer to sType struct {m.type}" + (" (array)" if m.len else ""))
        elif m.is_pointer and t and t.category == "union":
            out.append(f"{m.name}: pointer to union {m.type}")
        elif m.is_pointer and t and m.len:
            out.append(f"{m.name}: array of plain struct {m.type}")
        elif t and t.category == "union" and not m.is_pointer:
            out.append(f"{m.name}: union by value {m.type}")
        elif m.type == "void" and m.is_pointer and m.len:
            out.append(f"{m.name}: void payload len={m.len}")
        elif m.array_len:
            out.append(f"{m.name}: fixed array [{m.array_len}]")
        if m.len and ("latexmath" in m.len or m.altlen):
            out.append(f"{m.name}: computed len {m.altlen or m.len}")
        if m.pointer_depth > 1:
            out.append(f"{m.name}: double pointer {m.full_type}")
    return out


def in_profile(reg: Registry, s: Struct, core: str, exts: Set[str]) -> bool:
    if s.core_version and s.core_version <= core:
        return True
    return any(e in exts for e in s.extensions)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--core", default="VK_VERSION_1_2")
    ap.add_argument("--ext", nargs="*", default=[])
    ap.add_argument("--out", default=os.path.join(REPO_ROOT, "study", "structs.md"))
    args = ap.parse_args()
    exts = set(args.ext)

    reg = Registry.load()
    cmds = reg.profile(args.core, args.ext)
    inputs: Dict[str, List[str]] = defaultdict(list)     # struct -> commands consuming it as I / I[]
    for c in cmds:
        for p in c.params:
            r = role_of(reg, c, p, c.params)
            if r in ("I", "I[]"):
                inputs[p.type].append(f"{c.name}{'[]' if r == 'I[]' else ''}")

    L: List[str] = []
    w = L.append
    w(f"# Input structs and pNext extenders ({args.core}{' + ' + ', '.join(sorted(exts)) if exts else ''})")
    w("")
    w(f"sType input structs consumed by profile commands: {len(inputs)}.")
    w("")
    reuse = defaultdict(list)
    total_ext = 0
    for sname in sorted(inputs):
        s = reg.struct_of(sname)
        extenders = [e for e in reg.extenders_of(sname) if in_profile(reg, e, args.core, exts)]
        all_ext = reg.extenders_of(sname)
        total_ext += len(extenders)
        for e in extenders:
            reuse[e.name].append(sname)
        w(f"## `{sname}`")
        w("")
        w(f"Consumed by: {', '.join(inputs[sname])}")
        w("")
        flat = [m for m in s.members if m.name != "sType"]
        w(f"Flattened parameters ({len(flat)}): " + ", ".join(f"`{m.full_type} {m.name}`" for m in flat))
        w("")
        hz = hazards(reg, s)
        if hz:
            w("Hazards:")
            for h in hz:
                w(f"- {h}")
            w("")
        w(f"Extenders in profile: {len(extenders)} (registry total {len(all_ext)})")
        w("")
        if extenders:
            w("| Extender | Origin | Extra parameters | Hazards |")
            w("|---|---|---|---|")
            for e in sorted(extenders, key=lambda x: x.name):
                extra = [m for m in e.members if m.name not in ("sType", "pNext")]
                origin = e.core_version or ", ".join(e.extensions)
                w(f"| `{e.name}` | {origin} | {', '.join(f'`{m.full_type} {m.name}`' for m in extra)} | {'; '.join(hazards(reg, e))} |")
            w("")
    w("## Extenders that extend several bases (one qualifier, several tier-0 targets)")
    w("")
    for e, bases in sorted(reuse.items()):
        if len(bases) > 1:
            w(f"- `{e}` -> {', '.join(bases)}")
    w("")
    w(f"Total in-profile (base, extender) pairs: {total_ext}. Each pair is one candidate tier-1 function.")

    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    with open(args.out, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(L) + "\n")
    print(f"wrote {args.out}: {len(inputs)} input structs, {total_ext} in-profile extender pairs")


if __name__ == "__main__":
    main()
