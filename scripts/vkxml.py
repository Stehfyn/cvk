"""Normalized model of the Vulkan registry (vk.xml) for the cvk study scripts.

Usage as a module:

    from vkxml import Registry
    reg = Registry.load()            # defaults to external/Vulkan-Headers/registry/vk.xml
    cmd = reg.commands["vkCreateBuffer"]
    for p in cmd.params: print(p.name, p.full_type, p.len, p.optional)

Everything here is deliberately a plain dataclass so that study scripts can be
written as list comprehensions over the model rather than XML walks.
"""

from __future__ import annotations

import os
import re
import xml.etree.ElementTree as ET
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Set

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), os.pardir))
DEFAULT_XML = os.path.join(REPO_ROOT, "external", "Vulkan-Headers", "registry", "vk.xml")

DISPATCHABLE = ("VkInstance", "VkPhysicalDevice", "VkDevice", "VkQueue", "VkCommandBuffer")


def _text(elem: ET.Element) -> str:
    """Whole text of an element including nested <type>/<name>/<enum> tags, minus <comment>."""
    parts = [elem.text or ""]
    for child in elem:
        if child.tag != "comment":
            parts.append("".join(child.itertext()))
        parts.append(child.tail or "")
    return "".join(parts)


@dataclass
class Param:
    name: str
    type: str               # bare type name, e.g. VkBuffer, uint32_t, void
    full_type: str          # declaration text minus the name, e.g. "const VkBufferCreateInfo*"
    is_const: bool
    pointer_depth: int
    array_len: Optional[str]      # fixed array like "[VK_UUID_SIZE]" -> "VK_UUID_SIZE"
    len: Optional[str]            # registry len= attribute (dynamic length)
    altlen: Optional[str]
    optional: List[bool]          # registry optional= as list ("false,true" -> [False, True])
    externsync: Optional[str]
    noautovalidity: bool
    values: Optional[str]         # fixed sType value for struct members

    @property
    def is_pointer(self) -> bool:
        return self.pointer_depth > 0

    @property
    def is_output(self) -> bool:
        """Non-const pointer to something other than void = written by the command."""
        return self.pointer_depth > 0 and not self.is_const


@dataclass
class Command:
    name: str
    return_type: str
    params: List[Param]
    successcodes: List[str]
    errorcodes: List[str]
    queues: List[str]
    cmdbufferlevel: List[str]
    tasks: List[str]
    renderpass: Optional[str]
    alias_of: Optional[str]
    api: List[str]
    # Filled by Registry: which core version / extensions require this command.
    core_version: Optional[str] = None
    extensions: List[str] = field(default_factory=list)

    @property
    def verb(self) -> str:
        """Leading CamelCase word after 'vk', e.g. Create, Get, Cmd, Enumerate."""
        m = re.match(r"vk([A-Z][a-z]+)", self.name)
        return m.group(1) if m else ""

    @property
    def dispatch_handle(self) -> Optional[str]:
        if self.params and self.params[0].type in DISPATCHABLE and not self.params[0].is_pointer:
            return self.params[0].type
        return None


@dataclass
class Member(Param):
    pass


@dataclass
class Struct:
    name: str
    category: str                 # struct | union
    members: List[Member]
    stype: Optional[str]          # VK_STRUCTURE_TYPE_* when the struct has sType
    structextends: List[str]
    returnedonly: bool
    alias_of: Optional[str]
    api: List[str]
    allowduplicate: bool
    core_version: Optional[str] = None
    extensions: List[str] = field(default_factory=list)

    @property
    def has_pnext(self) -> bool:
        return any(m.name == "pNext" for m in self.members)


@dataclass
class Extension:
    name: str
    number: int
    type: Optional[str]           # instance | device
    supported: List[str]
    platform: Optional[str]
    promotedto: Optional[str]
    deprecatedby: Optional[str]
    obsoletedby: Optional[str]
    depends: Optional[str]
    commands: List[str]
    types: List[str]


@dataclass
class Registry:
    commands: Dict[str, Command]
    structs: Dict[str, Struct]
    handles: Dict[str, Optional[str]]       # handle -> parent handle
    dispatchable_handles: Set[str]
    extensions: Dict[str, Extension]
    features: Dict[str, List[str]]          # "VK_VERSION_1_2" -> [command names]
    platforms: Dict[str, str]               # platform -> protect macro
    enums: Dict[str, List[str]]             # enum type -> values
    bitmasks: Dict[str, Optional[str]]      # VkXFlags -> VkXFlagBits (requires)
    path: str
    feature_depends: Dict[str, Set[str]] = field(default_factory=dict)   # public version -> versions it depends on
    api: str = "vulkan"
    enum_provenance: Dict[str, str] = field(default_factory=dict)         # enum value -> feature or extension defining it
    type_provenance: Dict[str, str] = field(default_factory=dict)         # type -> first extension requiring it
    enum_extends: Dict[str, List[str]] = field(default_factory=dict)      # enum type -> values added by features and extensions
    type_aliases: Dict[str, str] = field(default_factory=dict)            # alias enum or bitmask type -> its target
    type_feature: Dict[str, str] = field(default_factory=dict)            # type -> first public version requiring it
    removed: Set[str] = field(default_factory=set)                        # names an API variant's <remove> blocks drop
    enums64: Set[str] = field(default_factory=set)                        # 64-bit flag bit enumerations (values are static const objects, not enumerators)
    constants: Dict[str, str] = field(default_factory=dict)               # API constant name -> C type (VK_SUBPASS_EXTERNAL -> uint32_t)
    constant_values: Dict[str, str] = field(default_factory=dict)         # API constant name -> registry C expression
    bitmask_bases: Dict[str, str] = field(default_factory=dict)           # canonical flags type -> VkFlags or VkFlags64

    def feature_closure(self, name: str) -> Set[str]:
        """Every public version a version includes: VK_VERSION_1_4 -> 1_0..1_4,
        VKSC_VERSION_1_0 -> VK_VERSION_1_0..1_2 and itself (its `depends`)."""
        out, todo = set(), [name]
        while todo:
            n = todo.pop()
            if n in out:
                continue
            out.add(n)
            todo += list(self.feature_depends.get(n, ()))
        return out

    # ---------------------------------------------------------------- loading
    @staticmethod
    def load(path: str = DEFAULT_XML, api: str = "vulkan") -> "Registry":
        root = ET.parse(path).getroot()
        platforms = {p.get("name"): p.get("protect") for p in root.findall("platforms/platform")}

        def api_ok(elem: ET.Element) -> bool:
            a = elem.get("api")
            return a is None or api in a.split(",")

        # A <require> block may depend on other extensions or versions being available
        # ("VK_KHR_format_feature_flags2,VK_VERSION_1_3": any of; "+" is all of). For an API
        # variant that lacks them the block is not part of the registry's headers either.
        supported_exts = {e.get("name") for e in root.findall("extensions/extension") if api in (e.get("supported") or "").split(",")}

        def available(dep: str) -> bool:
            m = re.fullmatch(r"VK_VERSION_(\d+)_(\d+)", dep)
            if m:
                return api == "vulkan" or (int(m.group(1)), int(m.group(2))) <= (1, 2)
            if dep.startswith("VKSC_VERSION_"):
                return api == "vulkansc"
            return dep in supported_exts

        def deps_ok(elem: ET.Element) -> bool:
            expr = (elem.get("depends") or "").replace("(", "").replace(")", "")
            if not expr:
                return True
            return any(all(available(t) for t in part.split("+")) for part in expr.split(","))

        handles: Dict[str, Optional[str]] = {}
        type_aliases: Dict[str, str] = {}
        dispatchable: Set[str] = set()
        structs: Dict[str, Struct] = {}
        enums: Dict[str, List[str]] = {}
        bitmasks: Dict[str, Optional[str]] = {}
        bitmask_bases: Dict[str, str] = {}

        for t in root.findall("types/type"):
            if not api_ok(t):
                continue
            cat = t.get("category")
            if cat == "handle":
                name = t.get("name") or t.findtext("name")
                if t.get("alias"):
                    handles[name] = handles.get(t.get("alias"))
                    if t.get("alias") in dispatchable:
                        dispatchable.add(name)
                    continue
                handles[name] = t.get("parent")
                if t.findtext("type") == "VK_DEFINE_HANDLE":
                    dispatchable.add(name)
            elif cat in ("struct", "union"):
                name = t.get("name")
                members: List[Member] = []
                for m in t.findall("member"):
                    if not api_ok(m):
                        continue
                    members.append(Registry._parse_param(m, Member))
                stype = None
                for m in members:
                    if m.name == "sType" and m.values:
                        stype = m.values
                se = t.get("structextends")
                structs[name] = Struct(
                    name=name,
                    category=cat,
                    members=members,
                    stype=stype,
                    structextends=se.split(",") if se else [],
                    returnedonly=t.get("returnedonly") == "true",
                    alias_of=t.get("alias"),
                    api=(t.get("api") or "").split(",") if t.get("api") else [],
                    allowduplicate=t.get("allowduplicate") == "true",
                )
            elif cat == "bitmask":
                name = t.get("name") or t.findtext("name")
                if t.get("alias"):
                    type_aliases[name] = t.get("alias")
                bitmasks[name] = t.get("requires") or t.get("bitvalues")
                if t.findtext("type"):
                    bitmask_bases[name] = t.findtext("type")
            elif cat == "enum" and t.get("alias"):
                type_aliases[t.get("name")] = t.get("alias")

        enums64: Set[str] = set()
        constants: Dict[str, str] = {}
        constant_values: Dict[str, str] = {}
        for e in root.findall("enums"):
            enums[e.get("name")] = [v.get("name") for v in e.findall("enum") if api_ok(v)]
            if e.get("name") == "API Constants":
                for v in e.findall("enum"):
                    if api_ok(v) and v.get("type") and not v.get("alias"):
                        constants[v.get("name")] = v.get("type")
                        constant_values[v.get("name")] = v.get("value")
            if e.get("bitwidth") == "64":
                enums64.add(e.get("name"))

        commands: Dict[str, Command] = {}
        for c in root.findall("commands/command"):
            if not api_ok(c):
                continue
            if c.get("alias"):
                commands[c.get("name")] = Command(
                    name=c.get("name"), return_type="", params=[], successcodes=[], errorcodes=[],
                    queues=[], cmdbufferlevel=[], tasks=[], renderpass=None,
                    alias_of=c.get("alias"), api=(c.get("api") or "").split(",") if c.get("api") else [],
                )
                continue
            proto = c.find("proto")
            name = proto.findtext("name")
            ret = proto.findtext("type")
            params = [Registry._parse_param(p, Param) for p in c.findall("param") if api_ok(p)]

            def split(attr: str) -> List[str]:
                v = c.get(attr)
                return v.split(",") if v else []

            commands[name] = Command(
                name=name, return_type=ret, params=params,
                successcodes=split("successcodes"), errorcodes=split("errorcodes"),
                queues=split("queues"), cmdbufferlevel=split("cmdbufferlevel"),
                tasks=split("tasks"), renderpass=c.get("renderpass"),
                alias_of=None, api=(c.get("api") or "").split(",") if c.get("api") else [],
            )
        # Resolve aliases so alias commands carry their target's shape.
        for cmd in commands.values():
            if cmd.alias_of and cmd.alias_of in commands:
                tgt = commands[cmd.alias_of]
                cmd.return_type = tgt.return_type
                cmd.params = tgt.params
                cmd.successcodes = tgt.successcodes
                cmd.errorcodes = tgt.errorcodes
                cmd.queues, cmd.cmdbufferlevel, cmd.tasks, cmd.renderpass = tgt.queues, tgt.cmdbufferlevel, tgt.tasks, tgt.renderpass

        # Newer registries split each core version into internal BASE/COMPUTE/GRAPHICS
        # features that VK_VERSION_x_y depends on. Merge everything with the same
        # number into the public VK_VERSION_x_y bucket.
        def public_name(raw: str) -> Optional[str]:
            """VK_GRAPHICS_VERSION_1_2 -> VK_VERSION_1_2; VKSC_VERSION_1_0 stays; anything else None."""
            m = re.fullmatch(r"(VK|VKSC)_(?:[A-Z]+_)?VERSION_(\d+_\d+)", raw)
            return f"{m.group(1)}_VERSION_{m.group(2)}" if m else None

        features: Dict[str, List[str]] = {}
        feature_depends: Dict[str, Set[str]] = {}
        enum_provenance: Dict[str, str] = {}
        # Where every enum value and type comes from, across every feature and extension of the
        # registry: a value the profile's headers do not define is then recognizable.
        for f in root.findall("feature"):
            pn = public_name(f.get("name") or "")
            for req in f.findall("require"):
                for en in req.findall("enum"):
                    if pn:                                   # extending values and required constants alike
                        enum_provenance.setdefault(en.get("name"), pn)
        type_provenance: Dict[str, str] = {}
        enum_extends: Dict[str, List[str]] = {}
        for f in root.findall("feature"):
            for req in f.findall("require"):
                if not api_ok(req) or not deps_ok(req):
                    continue
                for en in req.findall("enum"):
                    if en.get("extends") and api_ok(en) and not en.get("protect") and en.get("name") not in enum_extends.setdefault(en.get("extends"), []):
                        enum_extends[en.get("extends")].append(en.get("name"))
        for e in root.findall("extensions/extension"):
            for req in e.findall("require"):
                for en in req.findall("enum"):
                    enum_provenance.setdefault(en.get("name"), e.get("name"))
                    if en.get("extends"):
                        if api_ok(req) and deps_ok(req) and api_ok(en) and not en.get("protect") and en.get("name") not in enum_extends.setdefault(en.get("extends"), []):
                            enum_extends[en.get("extends")].append(en.get("name"))
                for ty in req.findall("type"):
                    type_provenance.setdefault(ty.get("name"), e.get("name"))
        removed_commands: Set[str] = set()
        removed_types: Set[str] = set()
        removed_enums: Set[str] = set()
        for f in root.findall("feature"):
            if not api_ok(f):
                continue
            fname = public_name(f.get("name") or "")
            if not fname:
                continue
            deps = feature_depends.setdefault(fname, set())
            for d in (f.get("depends") or "").split(","):
                pn = public_name(d.strip())
                if pn and pn != fname:
                    deps.add(pn)
            for rem in f.findall("remove"):
                removed_commands.update(c.get("name") for c in rem.findall("command"))
                removed_types.update(t.get("name") for t in rem.findall("type"))
                removed_enums.update(en.get("name") for en in rem.findall("enum"))
            names = features.setdefault(fname, [])
            for req in f.findall("require"):
                if not api_ok(req) or not deps_ok(req):
                    continue
                for cn in req.findall("command"):
                    names.append(cn.get("name"))
                for en in req.findall("enum"):
                    if en.get("extends"):
                        enum_provenance.setdefault(en.get("name"), fname)
        feature_types: Dict[str, List[str]] = {}
        for f in root.findall("feature"):
            if not api_ok(f):
                continue
            fname = public_name(f.get("name") or "")
            if not fname:
                continue
            tl = feature_types.setdefault(fname, [])
            for req in f.findall("require"):
                if api_ok(req) and deps_ok(req):
                    tl += [x.get("name") for x in req.findall("type")]
        type_feature: Dict[str, str] = {}
        for fname in sorted(feature_types):
            for n in feature_types[fname]:
                type_feature.setdefault(n, fname)
        for fname in sorted(features):
            for n in features[fname]:
                if n in commands and commands[n].core_version is None:
                    commands[n].core_version = fname
            for n in feature_types.get(fname, []):
                if n in structs and structs[n].core_version is None:
                    structs[n].core_version = fname

        extensions: Dict[str, Extension] = {}
        for e in root.findall("extensions/extension"):
            supported = (e.get("supported") or "").split(",")
            if api not in supported:
                continue
            cmds, types = [], []
            for req in e.findall("require"):
                if not api_ok(req) or not deps_ok(req):
                    continue
                cmds += [x.get("name") for x in req.findall("command")]
                types += [x.get("name") for x in req.findall("type")]
                for en in req.findall("enum"):
                    if en.get("extends"):
                        enum_provenance.setdefault(en.get("name"), e.get("name"))
            for rem in e.findall("remove"):
                removed_commands.update(c.get("name") for c in rem.findall("command"))
                removed_types.update(t.get("name") for t in rem.findall("type"))
                removed_enums.update(en.get("name") for en in rem.findall("enum"))
            ext = Extension(
                name=e.get("name"), number=int(e.get("number")), type=e.get("type"),
                supported=supported, platform=e.get("platform"), promotedto=e.get("promotedto"),
                deprecatedby=e.get("deprecatedby"), obsoletedby=e.get("obsoletedby"),
                depends=e.get("depends"), commands=cmds, types=types,
            )
            extensions[ext.name] = ext
            for n in cmds:
                if n in commands:
                    commands[n].extensions.append(ext.name)
            for n in types:
                if n in structs:
                    structs[n].extensions.append(ext.name)

        # An API variant's <remove> blocks (Vulkan SC drops shader modules, sparse binding,
        # descriptor update templates): the entity leaves the model entirely.
        for n in removed_commands:
            commands.pop(n, None)
            for lst in list(features.values()) + [x.commands for x in extensions.values()]:
                while n in lst:
                    lst.remove(n)
        for n in removed_types:
            st = structs.pop(n, None)
            if st is not None and st.stype:
                removed_enums.add(st.stype)
            for x in extensions.values():
                while n in x.types:
                    x.types.remove(n)
        return Registry(
            commands=commands, structs=structs, handles=handles, dispatchable_handles=dispatchable,
            extensions=extensions, features=features, platforms=platforms, enums=enums,
            bitmasks=bitmasks, path=path, feature_depends=feature_depends, api=api,
            enum_provenance=enum_provenance, type_provenance=type_provenance, enum_extends=enum_extends,
            type_aliases=type_aliases, type_feature=type_feature, removed=removed_types | removed_enums | removed_commands,
            enums64=enums64, constants=constants, constant_values=constant_values, bitmask_bases=bitmask_bases,
        )

    @staticmethod
    def _parse_param(elem: ET.Element, cls):
        raw = _text(elem)
        name = elem.findtext("name") or ""
        typ = elem.findtext("type") or ""
        # Fixed-size array suffix: "name[3]", "name[VK_UUID_SIZE]" or "name[3][4]"; a multi-
        # dimensional array keeps every dimension ("3][4") so "[{array_len}]" declares it whole.
        array_len = None
        dims = re.findall(r"\[\s*([A-Za-z0-9_]+)\s*\]", raw.split(name, 1)[1] if name in raw else "")
        if dims:
            array_len = "][".join(dims)
        decl = raw
        if name:
            idx = decl.rfind(name)
            decl = decl[:idx] if idx >= 0 else decl
        decl = re.sub(r"\s+", " ", decl).strip()
        ptr_depth = decl.count("*")
        is_const = decl.startswith("const ") or decl.startswith("const*") or (" const" in decl and ptr_depth > 0 and decl.strip().startswith("const"))
        opt_attr = elem.get("optional")
        optional = [v == "true" for v in opt_attr.split(",")] if opt_attr else []
        return cls(
            name=name, type=typ, full_type=decl, is_const=decl.startswith("const"),
            pointer_depth=ptr_depth, array_len=array_len, len=elem.get("len"), altlen=elem.get("altlen"),
            optional=optional, externsync=elem.get("externsync"),
            noautovalidity=elem.get("noautovalidity") == "true", values=elem.get("values"),
        )

    # --------------------------------------------------------------- helpers
    def struct_of(self, type_name: str) -> Optional[Struct]:
        s = self.structs.get(type_name)
        if s and s.alias_of:
            return self.structs.get(s.alias_of, s)
        return s

    def extenders_of(self, struct_name: str) -> List[Struct]:
        """Structs that may appear in struct_name's pNext chain."""
        return [s for s in self.structs.values() if struct_name in s.structextends and not s.alias_of]

    def resolve_command(self, name: str) -> Optional[Command]:
        c = self.commands.get(name)
        while c and c.alias_of:
            c = self.commands.get(c.alias_of)
        return c

    def profile(self, core: str = "VK_VERSION_1_2", extensions: Optional[List[str]] = None) -> List[Command]:
        """Commands available in a core version plus explicit extensions (no aliases)."""
        wanted: Set[str] = set()
        for f in self.feature_closure(core):
            wanted.update(self.features.get(f, []))
        for e in extensions or []:
            if e in self.extensions:
                wanted.update(self.extensions[e].commands)
        return [self.commands[n] for n in sorted(wanted) if n in self.commands and not self.commands[n].alias_of]


if __name__ == "__main__":
    import sys
    reg = Registry.load(sys.argv[1] if len(sys.argv) > 1 else DEFAULT_XML)
    print(f"registry: {reg.path}")
    print(f"commands={len(reg.commands)} structs={len(reg.structs)} handles={len(reg.handles)} extensions={len(reg.extensions)}")
    for f, names in reg.features.items():
        print(f"  {f}: {len(names)} commands")
