"""Dump Vulkan structures by command, including nested members and pNext chains.

    python scripts/study_command_structs.py
    python scripts/study_command_structs.py --queries-only --out study/query_structs.md
    python scripts/study_command_structs.py --command 'vkGetPhysicalDevice*' --json dump.json

Uses the pinned registry, all Vulkan core versions and supported extensions,
including platform/provisional extensions, independently of cvk.policy.json.
"""

from __future__ import annotations

import argparse
from collections import defaultdict
from fnmatch import fnmatchcase
import hashlib
import json
from pathlib import Path

from vkxml import DEFAULT_XML, REPO_ROOT, Registry


def canonical_struct(reg: Registry, name: str):
    seen = set()
    while name in reg.structs and name not in seen:
        seen.add(name)
        struct = reg.structs[name]
        if not struct.alias_of:
            return struct
        name = struct.alias_of
    return None


def origins(entity):
    return ([entity.core_version] if entity.core_version else []) + sorted(entity.extensions)


def build_report(reg: Registry, patterns=(), queries_only=False):
    # Canonicalize before indexing: promoted core and KHR/EXT names describe
    # the same structure and the same chain, not separate extender choices.
    aliases = defaultdict(list)
    available = set()
    for name, struct in reg.structs.items():
        target = canonical_struct(reg, name)
        if target and origins(struct):
            available.add(target.name)
            if struct.alias_of:
                aliases[target.name].append(name)

    extenders = defaultdict(set)
    for name in sorted(available):
        struct = reg.structs[name]
        for base_name in struct.structextends:
            base = canonical_struct(reg, base_name)
            if base:
                extenders[base.name].add(name)

    groups = defaultdict(list)
    for name, command in sorted(reg.commands.items()):
        if not origins(command):
            continue
        if queries_only and not name.startswith(("vkGet", "vkEnumerate")):
            continue
        if patterns and not any(fnmatchcase(name, pattern) for pattern in patterns):
            continue
        canonical = reg.resolve_command(name)
        if canonical:
            groups[canonical.name].append(name)

    commands = []
    used_structs = set()
    for command_name, names in sorted(groups.items()):
        command = reg.commands[command_name]
        rows = []

        def visit(type_name, path, relation, ancestors, expand_chain=True, chain_base=None):
            struct = canonical_struct(reg, type_name)
            if not struct:
                return
            rows.append(dict(structure=struct.name, declared_type=type_name,
                             relation=relation, path=path, chain_base=chain_base))
            used_structs.add(struct.name)
            if struct.name in ancestors:
                return
            ancestors = ancestors | {struct.name}
            for member in struct.members:
                if member.name in ("sType", "pNext"):
                    continue
                suffix = "[]" if member.len or member.array_len else ""
                visit(member.type, f"{path}.{member.name}{suffix}", "member", ancestors)
            if expand_chain and struct.has_pnext:
                for extender in sorted(extenders[struct.name]):
                    # All nodes in this chain extend its original base. Do NOT
                    # take the transitive closure of structextends: an extender
                    # having other extenders does not legalize those here.
                    visit(extender, f"{path}.pNext<{extender}>", "pNext", ancestors,
                          expand_chain=False, chain_base=struct.name)

        parameters = []
        for param in command.params:
            if not canonical_struct(reg, param.type):
                continue
            parameters.append(dict(name=param.name, type=param.type,
                                   declaration=f"{param.full_type} {param.name}",
                                   length=param.len or param.array_len))
            suffix = "[]" if param.len or param.array_len else ""
            visit(param.type, param.name + suffix, "direct", set())
        commands.append(dict(command=command_name, names=names,
                             origins={name: origins(reg.commands[name]) for name in names},
                             parameters=parameters, structures=rows))

    structures = {}
    for name in sorted(used_structs):
        struct = reg.structs[name]
        structures[name] = dict(
            category=struct.category, aliases=sorted(aliases[name]),
            origins=origins(struct),
            alias_origins={alias: origins(reg.structs[alias]) for alias in sorted(aliases[name])},
            sType=struct.stype, returned_only=struct.returnedonly,
            allow_duplicate=struct.allowduplicate, structextends=struct.structextends,
        )
    registry_path = Path(reg.path).resolve()
    try:
        source = registry_path.relative_to(REPO_ROOT).as_posix()
    except ValueError:
        source = registry_path.as_posix()
    return dict(
        registry=source, registry_sha256=hashlib.sha256(registry_path.read_bytes()).hexdigest(),
        api=reg.api, queries_only=queries_only, command_patterns=list(patterns),
        command_count=sum(len(c["names"]) for c in commands),
        canonical_command_count=len(commands), structure_count=len(structures),
        commands=commands, structures=structures,
    )


def markdown(report):
    lines = ["# Vulkan structures by command", "",
             "Generated by `scripts/study_command_structs.py`.", "",
             f"Registry: `{report['registry']}` (API `{report['api']}`).",
             f"Registry SHA-256: `{report['registry_sha256']}`.", "",
             f"{report['command_count']} command names, "
             f"{report['canonical_command_count']} canonical commands, "
             f"{report['structure_count']} distinct canonical structures/unions.", "",
             "Scope: all core versions and API-supported extensions in this registry, "
             "including platform and provisional extensions; independent of the cvk policy.", "",
             "Filters: " + ("vkGet* / vkEnumerate*; " if report['queries_only'] else "") +
             (", ".join(report['command_patterns']) or "all command names") + ".", "",
             "`direct` means a typed command parameter; `member` means a recursively "
             "contained structure/union; `pNext` identifies an allowed extender and its "
             "chain base. Paths use `.` for member access, `[]` for arrays, and "
             "`pNext<Type>` for a node anywhere in that base's chain (not a required order). "
             "Nested members and their own chains are included. No Info/Query name filter "
             "is applied, so feature/property outputs and plain structures are retained.", "",
             "Parameter declarations retain constness; non-const pointers may be output or "
             "input/output. Alias commands share a section; structure aliases are listed below.", "",
             "This is a registry relationship inventory, not a list of combinations legal "
             "on a particular device. Extension/feature enablement, platform guards, union "
             "selection, ignored members, and command-specific valid usage still apply. "
             "Opaque void payloads, callback signatures, and relationships described only "
             "in specification prose are not inferred.", "",
             "The [Khronos registry schema](https://registry.khronos.org/vulkan/specs/latest/registry.html) "
             "defines `structextends` against the base of the whole chain; extender relations "
             "are therefore not followed transitively.", ""]
    for command in report["commands"]:
        lines.extend([f"## `{command['command']}`", "",
                      "Names: " + ", ".join(f"`{n}`" for n in command["names"]), ""])
        if not command["structures"]:
            lines.extend(["No typed structure parameters.", ""])
            continue
        lines.extend(["Parameters: " + "; ".join(f"`{p['declaration']}`" for p in command["parameters"]), "",
                      "| Structure | Use | Parameter/member path | Chain base |",
                      "|---|---|---|---|"])
        for row in command["structures"]:
            base = f"`{row['chain_base']}`" if row["chain_base"] else ""
            lines.append(f"| `{row['structure']}` | {row['relation']} | `{row['path']}` | {base} |")
        lines.append("")
    lines.extend(["## Structure aliases", "",
                  "Each alias has the same structure and chain relationships as its canonical type.", "",
                  "| Canonical structure | Aliases |", "|---|---|"])
    for name, struct in report["structures"].items():
        if struct["aliases"]:
            lines.append(f"| `{name}` | " + ", ".join(f"`{a}`" for a in struct["aliases"]) + " |")
    return "\n".join(lines) + "\n"


def write(path, content):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8", newline="\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--xml", default=DEFAULT_XML)
    parser.add_argument("--api", choices=("vulkan", "vulkansc"), default="vulkan")
    parser.add_argument("--queries-only", action="store_true", help="select vkGet* and vkEnumerate* names")
    parser.add_argument("--command", action="append", default=[], help="command-name glob; repeatable")
    parser.add_argument("--out", default=str(Path(REPO_ROOT) / "study" / "command_structs.md"))
    parser.add_argument("--json", help="also write the machine-readable report")
    args = parser.parse_args()
    report = build_report(Registry.load(args.xml, api=args.api), args.command, args.queries_only)
    write(args.out, markdown(report))
    if args.json:
        write(args.json, json.dumps(report, indent=2) + "\n")
    print(f"wrote {args.out}: {report['command_count']} command names, "
          f"{report['structure_count']} canonical structures/unions")


if __name__ == "__main__":
    main()
