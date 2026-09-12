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
    """Whole text of an element including nested <type>/<name>/<enum> tags."""
    return "".join(elem.itertext())


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

    # ---------------------------------------------------------------- loading
    @staticmethod
    def load(path: str = DEFAULT_XML, api: str = "vulkan") -> "Registry":
        root = ET.parse(path).getroot()
        platforms = {p.get("name"): p.get("protect") for p in root.findall("platforms/platform")}

        def api_ok(elem: ET.Element) -> bool:
            a = elem.get("api")
            return a is None or api in a.split(",")

        handles: Dict[str, Optional[str]] = {}
        dispatchable: Set[str] = set()
        structs: Dict[str, Struct] = {}
        enums: Dict[str, List[str]] = {}
        bitmasks: Dict[str, Optional[str]] = {}

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
                bitmasks[name] = t.get("requires") or t.get("bitvalues")

        for e in root.findall("enums"):
            enums[e.get("name")] = [v.get("name") for v in e.findall("enum")]

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
        features: Dict[str, List[str]] = {}
        for f in root.findall("feature"):
            if not api_ok(f):
                continue
            number = f.get("number")
            if not number or not re.fullmatch(r"(VK_|VK_[A-Z]+_)VERSION_\d+_\d+", f.get("name")):
                continue
            fname = "VK_VERSION_" + number.replace(".", "_")
            names = features.setdefault(fname, [])
            for req in f.findall("require"):
                if not api_ok(req):
                    continue
                for cn in req.findall("command"):
                    names.append(cn.get("name"))
        feature_types: Dict[str, List[str]] = {}
        for f in root.findall("feature"):
            if not api_ok(f):
                continue
            number = f.get("number")
            if not number or not re.fullmatch(r"(VK_|VK_[A-Z]+_)VERSION_\d+_\d+", f.get("name")):
                continue
            fname = "VK_VERSION_" + number.replace(".", "_")
            tl = feature_types.setdefault(fname, [])
            for req in f.findall("require"):
                if api_ok(req):
                    tl += [x.get("name") for x in req.findall("type")]
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
                if not api_ok(req):
                    continue
                cmds += [x.get("name") for x in req.findall("command")]
                types += [x.get("name") for x in req.findall("type")]
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

        return Registry(
            commands=commands, structs=structs, handles=handles, dispatchable_handles=dispatchable,
            extensions=extensions, features=features, platforms=platforms, enums=enums,
            bitmasks=bitmasks, path=path,
        )

    @staticmethod
    def _parse_param(elem: ET.Element, cls):
        raw = _text(elem)
        name = elem.findtext("name") or ""
        typ = elem.findtext("type") or ""
        # Fixed-size array suffix: "name[3]" or "name[VK_UUID_SIZE]"
        array_len = None
        m = re.search(r"\[\s*([A-Za-z0-9_]+)\s*\]", raw.split(name, 1)[1] if name in raw else "")
        if m:
            array_len = m.group(1)
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
        order = sorted(self.features)
        for f in order:
            wanted.update(self.features[f])
            if f == core:
                break
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
