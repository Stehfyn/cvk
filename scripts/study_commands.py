"""Classify every Vulkan command by the *shape* a tier-0 cvk wrapper would take.

    python scripts/study_commands.py [--core VK_VERSION_1_2] [--ext NAME ...] [--all] [--out study/commands.md]

For each command we derive a role for every parameter, then a compact shape
signature. The histogram of shapes is the list of code templates a generator
needs; the "irregular" section is the list of commands that break a template.
"""

from __future__ import annotations

import argparse
import os
import sys
from collections import Counter, defaultdict
from typing import List, Tuple

sys.path.insert(0, os.path.dirname(__file__))
from vkxml import Command, Param, Registry, REPO_ROOT  # noqa: E402

SCALARS = {"uint32_t", "uint64_t", "int32_t", "int64_t", "size_t", "float", "VkDeviceSize",
           "VkDeviceAddress", "VkBool32", "VkSampleMask", "uint8_t", "int", "char", "double", "uint16_t"}


def role_of(reg: Registry, cmd: Command, p: Param, params: List[Param]) -> str:
    """Return a role tag for a parameter (see SHAPE LEGEND in the report)."""
    t = p.type
    is_handle = t in reg.handles
    struct = reg.struct_of(t)
    is_struct = struct is not None
    count_of = [q for q in params if q.len and q.len.split(",")[0] == p.name]

    if p.name == "pAllocator":
        return "alloc"
    if p.name == "pNext":
        return "pnext"
    if not p.is_pointer and p.array_len is None:
        if is_handle:
            return "H"                         # in handle by value
        if t in reg.bitmasks or t.endswith("Flags") or t.endswith("FlagBits"):
            return "flags"
        if t in reg.enums:
            return "enum"
        if is_struct:
            return "S"                         # struct by value (VkExtent3D etc.)
        if t in SCALARS:
            if count_of:
                return "n"                     # count for an in-array
            return "s"                         # scalar
        return "?" + t
    # pointers / arrays
    if p.is_const:
        if t == "void":
            return "void*in" if p.len else "?constvoid"
        if t == "char":
            return "str"
        if p.len:
            if is_handle:
                return "H[]"
            if is_struct and struct.stype:
                return "I[]"                   # array of sType structs
            if is_struct:
                return "S[]"
            return "s[]"
        if is_struct and struct.stype:
            return "I"                         # single input info struct with sType
        if is_struct:
            return "S*"                        # plain struct pointer in (no sType)
        return "s*in"
    # non-const pointer => output
    if t == "void":
        return "void*out"
    if count_of:
        # a count that another (output) parameter is sized by
        return "n*" if p.optional and p.optional[0] is False else "n*"
    if p.len:
        if is_handle:
            return "H[]out"
        if is_struct and struct.stype:
            return "P[]"                       # array of sType output structs
        if is_struct:
            return "S[]out"
        return "s[]out"
    if is_handle:
        return "H*out"
    if is_struct and struct.stype:
        return "P"                             # single sType output struct
    if is_struct:
        return "S*out"
    if t in SCALARS or t in reg.enums or t.endswith("Flags"):
        return "s*out"
    return "?out" + t


def shape_of(reg: Registry, cmd: Command) -> Tuple[str, List[str]]:
    roles = [role_of(reg, cmd, p, cmd.params) for p in cmd.params]
    ret = cmd.return_type
    ret_tag = {"VkResult": "R", "void": "v"}.get(ret, ret)
    return f"{cmd.verb}:{ret_tag}({','.join(roles)})", roles


def two_call_enumeration(roles: List[str]) -> bool:
    return "n*" in roles and any(r.endswith("[]out") or r == "P[]" for r in roles)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--core", default="VK_VERSION_1_2")
    ap.add_argument("--ext", nargs="*", default=[])
    ap.add_argument("--all", action="store_true", help="every command in the registry, ignoring --core/--ext")
    ap.add_argument("--out", default=os.path.join(REPO_ROOT, "study", "commands.md"))
    args = ap.parse_args()

    reg = Registry.load()
    cmds = [c for c in reg.commands.values() if not c.alias_of] if args.all else reg.profile(args.core, args.ext)

    shapes = {}
    by_shape = defaultdict(list)
    irregular = []
    multi_success = []
    for c in cmds:
        sig, roles = shape_of(reg, c)
        shapes[c.name] = (sig, roles)
        # normalize: collapse the leading handle so shapes group across dispatch levels
        key = sig
        by_shape[key].append(c.name)
        if any(r.startswith("?") for r in roles):
            irregular.append((c.name, sig, "unclassified parameter"))
        infos = [r for r in roles if r in ("I", "I[]")]
        if len(infos) > 1:
            irregular.append((c.name, sig, "more than one sType input struct"))
        if c.return_type not in ("VkResult", "void"):
            irregular.append((c.name, sig, f"returns {c.return_type}"))
        if len([r for r in roles if r.endswith("out") or r in ("P", "P[]")]) > 1:
            irregular.append((c.name, sig, "more than one output"))
        extra_success = [s for s in c.successcodes if s != "VK_SUCCESS"]
        if extra_success:
            multi_success.append((c.name, extra_success))

    verbs = Counter(c.verb for c in cmds)
    out_lines = []
    w = out_lines.append
    w(f"# Command shapes ({'all' if args.all else args.core + ' + ' + ','.join(args.ext) if args.ext else args.core})")
    w("")
    w(f"Commands: {len(cmds)}. Distinct shapes: {len(by_shape)}.")
    w("")
    w("## Shape legend")
    w("")
    w("`H` in-handle, `s` scalar, `n` count for an in-array, `flags`, `enum`, `S` struct by value, "
      "`I` single sType info struct (flattenable), `I[]` array of sType info structs, `S*` plain struct pointer, "
      "`S[]` array of plain structs, `H[]` handle array, `s[]` scalar array, `str` C string, `alloc` pAllocator, "
      "`H*out` created handle, `P` single sType output struct, `P[]` output sType array, `n*` count in/out for two-call enumeration, "
      "`*out` other outputs. Return: `R` VkResult, `v` void, else the type.")
    w("")
    w("## Verb histogram")
    w("")
    w("| Verb | Count |")
    w("|---|---|")
    for v, n in verbs.most_common():
        w(f"| {v} | {n} |")
    w("")
    w("## Shape histogram (templates a generator must implement)")
    w("")
    w("| # | Shape | Members |")
    w("|---|---|---|")
    for sig, names in sorted(by_shape.items(), key=lambda kv: (-len(kv[1]), kv[0])):
        w(f"| {len(names)} | `{sig}` | {', '.join(names)} |")
    w("")
    w("## Two-call enumerations (need CVK_MAX_* capacity + VK_INCOMPLETE handling)")
    w("")
    for c in cmds:
        sig, roles = shapes[c.name]
        if two_call_enumeration(roles):
            w(f"- `{c.name}` succ={','.join(c.successcodes)}")
    w("")
    w("## Commands with non-error success codes other than VK_SUCCESS (conflict with fatal-on-non-success)")
    w("")
    for name, codes in multi_success:
        w(f"- `{name}`: {', '.join(codes)}")
    w("")
    w("## Irregular commands (break a simple template)")
    w("")
    for name, sig, why in irregular:
        w(f"- `{name}` — {why} — `{sig}`")
    w("")
    w("## Per-command table")
    w("")
    w("| Command | Since | Ext | Return | Shape | Success | Queues |")
    w("|---|---|---|---|---|---|---|")
    for c in cmds:
        sig, _ = shapes[c.name]
        w(f"| `{c.name}` | {c.core_version or ''} | {', '.join(c.extensions)} | {c.return_type} | `{sig}` | {', '.join(c.successcodes)} | {', '.join(c.queues)} |")

    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    with open(args.out, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out_lines) + "\n")
    print(f"wrote {args.out}: {len(cmds)} commands, {len(by_shape)} shapes, {len(irregular)} irregular, {len(multi_success)} multi-success")


if __name__ == "__main__":
    main()
