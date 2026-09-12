"""Print the call tree of a wrapper function family with the sType structs each
level builds, so a compositional hierarchy can be read top-down.

    python scripts/study_family.py [--ref PATH] [--prefix Gfx_Vulkan_] ROOT [ROOT ...]
    python scripts/study_family.py --match 'Memory|Import|Export'   # every function matching, as roots

Each line: name [file group] tier=<tier> params=<n> vk=[direct vk calls]
           structs=[sType structs filled] pNext=[(struct, what its pNext is set to)]
"""

from __future__ import annotations

import argparse
import os
import re
import sys
from typing import Dict, Set

sys.path.insert(0, os.path.dirname(__file__))
from study_reference import DEFAULT_REF, REF_FILES, Fn, INFRA, assign_tiers, parse_definitions  # noqa: E402


def load(ref: str, prefix: str) -> Dict[str, Fn]:
    fns: Dict[str, Fn] = {}
    for group, _, src in REF_FILES:
        if src and os.path.exists(os.path.join(ref, src)):
            fns.update(parse_definitions(os.path.join(ref, src), prefix, group))
    assign_tiers(fns)
    return fns


def show(fns: Dict[str, Fn], name: str, seen: Set[str], depth: int = 0) -> None:
    f = fns.get(name)
    pad = "  " * depth
    if not f:
        print(f"{pad}{name} (not found)")
        return
    if name in seen:
        print(f"{pad}{name} (see above)")
        return
    seen.add(name)
    structs = [s.replace("VK_STRUCTURE_TYPE_", "") for _, s in re.findall(r"(\w+)\.sType\s*=\s*(VK_STRUCTURE_TYPE_\w+)", f.body)]
    nexts = re.findall(r"(\w+)\.pNext\s*=\s*([^;]+);", f.body)
    print(f"{pad}{name} [{f.group}] tier={f.tier} params={len(f.params)} vk={f.vk_calls + f.vk_literals} structs={structs} pNext={nexts}")
    for c in sorted(set(f.wrapper_calls)):
        if c in fns and c not in INFRA:
            show(fns, c, seen, depth + 1)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--ref", default=DEFAULT_REF)
    ap.add_argument("--prefix", default="Gfx_Vulkan_")
    ap.add_argument("--match", help="regex over function names; every match becomes a root")
    ap.add_argument("roots", nargs="*")
    args = ap.parse_args()
    fns = load(args.ref, args.prefix)
    roots = list(args.roots)
    if args.match:
        roots += sorted(n for n in fns if re.search(args.match, n))
    seen: Set[str] = set()
    for r in roots:
        print("=" * 100)
        show(fns, r, seen)


if __name__ == "__main__":
    main()
