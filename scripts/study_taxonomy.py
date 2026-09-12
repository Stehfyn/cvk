"""Extract Vulkan's own explicit structure from the registry.

    python scripts/study_taxonomy.py [--core VK_VERSION_1_2] [--ext NAME ...] [--out study/taxonomy.md]

Sections written:
  1. Object model: every handle, its parent, dispatchability, constructor and
     destructor commands, and the commands that consume it.
  2. Command classes: dispatch level x verb x registry `tasks`, plus a derived
     lifecycle class (constructor, destructor, query, enumeration, record, ...).
  3. Struct classes: input sType structs (direct parameter / extender-only /
     nested-only), output sType structs, POD carriers, region structs.
  4. X vs X2 families: Vulkan's own move from positional parameters to
     sType info structs, with member-level correspondence.
  5. Handle flow: which handles each command consumes and produces, and the
     minimal "bring to usable state" chains this implies.
  6. Extension classes in the profile.
"""

from __future__ import annotations

import argparse
import os
import re
import sys
from collections import Counter, defaultdict
from typing import Dict, List, Set

sys.path.insert(0, os.path.dirname(__file__))
from vkxml import Registry, REPO_ROOT  # noqa: E402
from study_commands import role_of  # noqa: E402

LEVEL_ORDER = [None, "VkInstance", "VkPhysicalDevice", "VkDevice", "VkQueue", "VkCommandBuffer"]


def lifecycle_class(cmd, roles: List[str]) -> str:
    v = cmd.verb
    outs = [r for r in roles if r in ("H*out", "H[]out")]
    if v in ("Create", "Allocate") and outs:
        return "constructor"
    if v in ("Destroy", "Free"):
        return "destructor"
    if v == "Cmd":
        return "record"
    if v == "Queue" or v == "Submit":
        return "submit"
    if "n*" in roles:
        return "enumeration"
    if v in ("Enumerate",):
        return "enumeration"
    if v in ("Begin", "End", "Map", "Unmap", "Bind", "Reset", "Trim", "Merge", "Update", "Import", "Signal", "Set"):
        return "state-transition"
    if v in ("Wait", "Device"):
        return "host-sync"
    if v in ("Acquire", "Present", "Release"):
        return "wsi"
    if v in ("Get", "Enumerate"):
        return "query"
    return "other"


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--core", default="VK_VERSION_1_2")
    ap.add_argument("--ext", nargs="*", default=[])
    ap.add_argument("--out", default=os.path.join(REPO_ROOT, "study", "taxonomy.md"))
    args = ap.parse_args()
    exts = set(args.ext)

    reg = Registry.load()
    cmds = reg.profile(args.core, args.ext)
    roles = {c.name: [role_of(reg, c, p, c.params) for p in c.params] for c in cmds}
    L: List[str] = []
    w = L.append

    # ------------------------------------------------------------ 1. objects
    produced: Dict[str, List[str]] = defaultdict(list)
    destroyed: Dict[str, List[str]] = defaultdict(list)
    consumed: Dict[str, List[str]] = defaultdict(list)
    for c in cmds:
        for p, r in zip(c.params, roles[c.name]):
            if r in ("H*out", "H[]out"):
                produced[p.type].append(c.name)
            elif r in ("H", "H[]"):
                if c.verb in ("Destroy", "Free") and p is not c.params[0]:
                    destroyed[p.type].append(c.name)
                else:
                    consumed[p.type].append(c.name)
    handles_in_profile = sorted({h for h in reg.handles if h in produced or h in consumed or h in destroyed},
                                key=lambda h: (LEVEL_ORDER.index(h) if h in LEVEL_ORDER else 99, h))

    w(f"# Vulkan structure ({args.core}{' + ' + ', '.join(sorted(exts)) if exts else ''})")
    w("")
    w("## 1. Object model (handles)")
    w("")
    w("| Handle | Parent | Dispatch | Constructor | Destructor | Consumed by (count) |")
    w("|---|---|---|---|---|---|")
    for h in handles_in_profile:
        w(f"| `{h}` | {reg.handles.get(h) or ''} | {'yes' if h in reg.dispatchable_handles else ''} | "
          f"{', '.join(produced.get(h, [])) or '(root)' if h == 'VkInstance' else ', '.join(produced.get(h, []))} | "
          f"{', '.join(destroyed.get(h, []))} | {len(consumed.get(h, []))} |")
    w("")
    orphans = [h for h in handles_in_profile if h in produced and not destroyed.get(h)]
    w(f"Handles with a constructor but no destructor in profile: {', '.join(orphans) or 'none'} "
      "(VkQueue and VkPhysicalDevice are retrieved, not created; VkDescriptorSet/VkCommandBuffer are freed by pool).")
    w("")

    # ----------------------------------------------------------- 2. commands
    w("## 2. Command classes")
    w("")
    w("Registry `tasks` is Vulkan's own classification of recorded commands: action, state, synchronization, indirection.")
    w("")
    grid = Counter()
    life = Counter()
    for c in cmds:
        lc = lifecycle_class(c, roles[c.name])
        life[(c.dispatch_handle or "(global)", lc)] += 1
        if c.verb == "Cmd":
            grid[(",".join(c.tasks) or "(none)", c.renderpass or "?", ",".join(c.cmdbufferlevel))] += 1
    w("| Dispatch level | Lifecycle class | Count |")
    w("|---|---|---|")
    for (lvl, lc), n in sorted(life.items(), key=lambda kv: (LEVEL_ORDER.index(kv[0][0]) if kv[0][0] in LEVEL_ORDER else 0, kv[0][1])):
        w(f"| {lvl} | {lc} | {n} |")
    w("")
    w("Recorded commands (`vkCmd*`) by registry tasks x renderpass scope x level:")
    w("")
    w("| tasks | renderpass | level | Count |")
    w("|---|---|---|---|")
    for (t, rp, lv), n in sorted(grid.items()):
        w(f"| {t} | {rp} | {lv} | {n} |")
    w("")
    w("Per-command lifecycle table:")
    w("")
    w("| Command | Level | Verb | Lifecycle | tasks | Produces | Consumes |")
    w("|---|---|---|---|---|---|---|")
    for c in sorted(cmds, key=lambda c: (LEVEL_ORDER.index(c.dispatch_handle) if c.dispatch_handle in LEVEL_ORDER else 0, c.name)):
        prod = [p.type for p, r in zip(c.params, roles[c.name]) if r in ("H*out", "H[]out")]
        cons = [p.type for p, r in zip(c.params, roles[c.name]) if r in ("H", "H[]")][1:]
        w(f"| `{c.name}` | {c.dispatch_handle or '(global)'} | {c.verb} | {lifecycle_class(c, roles[c.name])} | {','.join(c.tasks)} | {', '.join(prod)} | {', '.join(cons)} |")
    w("")

    # ------------------------------------------------------------ 3. structs
    w("## 3. Struct classes")
    w("")
    in_profile = lambda s: (s.core_version and s.core_version <= args.core) or any(e in exts for e in s.extensions)
    structs = [s for s in reg.structs.values() if not s.alias_of and in_profile(s)]
    direct_inputs: Set[str] = set()
    direct_outputs: Set[str] = set()
    by_value: Set[str] = set()
    region: Set[str] = set()
    for c in cmds:
        for p, r in zip(c.params, roles[c.name]):
            if r in ("I", "I[]"):
                direct_inputs.add(p.type)
            elif r in ("P", "P[]"):
                direct_outputs.add(p.type)
            elif r == "S":
                by_value.add(p.type)
            elif r in ("S[]", "S*") and c.verb == "Cmd":
                region.add(p.type)
    nested: Set[str] = set()
    for s in structs:
        for m in s.members:
            t = reg.struct_of(m.type)
            if t and t.stype and m.is_pointer:
                nested.add(t.name)
    extender_only = [s.name for s in structs if s.stype and s.structextends and s.name not in direct_inputs and s.name not in nested]
    input_direct = sorted(s for s in direct_inputs)
    nested_only = sorted(n for n in nested if n not in direct_inputs)
    outputs = sorted(s.name for s in structs if s.stype and s.returnedonly)
    pod = sorted(s.name for s in structs if not s.stype and s.category == "struct")
    unions = sorted(s.name for s in structs if s.category == "union")
    w("| Class | Rule | Count | Members |")
    w("|---|---|---|---|")
    w(f"| input, direct | sType struct passed as a command parameter | {len(input_direct)} | {', '.join(input_direct)} |")
    w(f"| input, nested only | sType struct reachable only through another struct's pointer member | {len(nested_only)} | {', '.join(nested_only)} |")
    w(f"| extender only | sType struct with `structextends`, never a direct parameter | {len(extender_only)} | {', '.join(sorted(extender_only))} |")
    w(f"| output | `returnedonly` sType struct | {len(outputs)} | {', '.join(outputs)} |")
    w(f"| POD carrier | no sType | {len(pod)} | {', '.join(pod)} |")
    w(f"| region (Cmd arrays) | POD struct passed as an array to a vkCmd* | {len(region)} | {', '.join(sorted(region))} |")
    w(f"| by value | POD struct passed by value to a command | {len(by_value)} | {', '.join(sorted(by_value))} |")
    w(f"| union | | {len(unions)} | {', '.join(unions)} |")
    w("")

    # ---------------------------------------------------------- 4. X vs X2
    w("## 4. X vs X2 families (Vulkan's own flattening direction)")
    w("")
    w("Where `vkX2` exists, it replaces `vkX`'s positional parameters with one sType struct. "
      "The member correspondence below is how the registry itself documents flattening; it is the "
      "evidence that a flattened tier 0 is faithful to the API rather than an invention.")
    w("")
    w("| vkX | vkX2 (since) | X positional params | X2 info members | Unmatched |")
    w("|---|---|---|---|---|")
    for c in sorted(reg.commands.values(), key=lambda c: c.name):
        if c.alias_of or not c.name.endswith("2"):
            continue
        base = reg.commands.get(c.name[:-1])
        if not base or base.alias_of:
            continue
        infos = [p for p, r in zip(c.params, [role_of(reg, c, p, c.params) for p in c.params]) if r in ("I", "I[]")]
        members = []
        for p in infos:
            members += [m.name for m in reg.struct_of(p.type).members if m.name not in ("sType", "pNext")]
        pos = [p.name for p in base.params[1:]]
        unmatched = [p for p in pos if p not in members and p.lstrip("p").lower() not in [m.lower() for m in members]]
        w(f"| `{base.name}` | `{c.name}` ({c.core_version or ','.join(c.extensions)}) | {', '.join(pos)} | {', '.join(members)} | {', '.join(unmatched)} |")
    w("")

    # ------------------------------------------------------- 5. handle flow
    w("## 5. Handle flow and implied composition chains")
    w("")
    w("A handle is *usable* when every command that must run before its first action-use has run. "
      "The registry does not name these chains, but they follow from which commands consume a handle "
      "without an action task.")
    w("")
    for h in handles_in_profile:
        if h in reg.dispatchable_handles and h != "VkCommandBuffer":
            continue
        pre = sorted({c for c in consumed.get(h, []) if reg.commands[c].verb in ("Bind", "Begin", "Update", "Map", "Get", "Import", "Reset", "Set")})
        w(f"- `{h}`: constructor {', '.join(produced.get(h, [])) or '-'}; preparatory consumers: {', '.join(pre) or '-'}")
    w("")

    # -------------------------------------------------------- 6. extensions
    w("## 6. Extensions in profile")
    w("")
    w("| Extension | Type | Platform | Promoted to | Commands | Extender structs |")
    w("|---|---|---|---|---|---|")
    for e in sorted(exts):
        x = reg.extensions.get(e)
        if not x:
            continue
        ext_structs = [t for t in x.types if t in reg.structs and reg.structs[t].structextends]
        w(f"| `{e}` | {x.type} | {x.platform or ''} | {x.promotedto or ''} | {len(x.commands)} | {len(ext_structs)} |")
    w("")

    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    with open(args.out, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(L) + "\n")
    print(f"wrote {args.out}: {len(handles_in_profile)} handles, {len(cmds)} commands, {len(structs)} structs")


if __name__ == "__main__":
    main()
