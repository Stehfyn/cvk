"""cvk generator prototype: registry + policy -> include/cvk.h, src/cvk.c

    python scripts/generate.py [--policy cvk.policy.json] [--out-dir .]

Implements SPEC.md sections 3-6 and 9 (tier 0, builders, single-qualifier
conveniences, documentation). Tier 1' and tier 2 tables are not generated yet.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
from collections import defaultdict
from dataclasses import dataclass
from typing import Dict, List, Optional, Set, Tuple

sys.path.insert(0, os.path.dirname(__file__))
from vkxml import Command, Param, Registry, Struct, REPO_ROOT  # noqa: E402
from study_commands import role_of  # noqa: E402

VENDORS = ("KHR", "EXT", "NV", "NVX", "AMD", "INTEL", "GOOGLE", "ARM", "QCOM", "MESA", "VALVE", "HUAWEI",
           "FUCHSIA", "SEC", "LUNARG", "ANDROID", "IMG", "MSFT", "NN", "MVK", "GGP", "QNX", "OHOS", "KHX")
# Extensions whose commands the Vulkan loader exports as trampolines; linked directly.
LOADER_LINKED = {"VK_KHR_surface", "VK_KHR_swapchain", "VK_KHR_display", "VK_KHR_display_swapchain",
                 "VK_KHR_win32_surface", "VK_KHR_xlib_surface", "VK_KHR_xcb_surface", "VK_KHR_wayland_surface",
                 "VK_KHR_android_surface", "VK_EXT_headless_surface", "VK_KHR_get_physical_device_properties2",
                 "VK_KHR_device_group_creation", "VK_KHR_external_memory_capabilities",
                 "VK_KHR_external_semaphore_capabilities", "VK_KHR_external_fence_capabilities",
                 "VK_KHR_get_surface_capabilities2", "VK_KHR_get_display_properties2", "VK_EXT_direct_mode_display",
                 "VK_EXT_display_surface_counter", "VK_EXT_acquire_xlib_display", "VK_EXT_debug_report",
                 "VK_EXT_debug_utils"}
DEVICE_LEVEL = ("VkDevice", "VkQueue", "VkCommandBuffer")


def strip_vendor(name: str) -> str:
    for v in VENDORS:
        if name.endswith(v) and len(name) > len(v):
            return name[: -len(v)]
    return name


def tokens(name: str) -> List[str]:
    """CamelCase words with digit runs as their own tokens: 'ImageMemoryRequirementsInfo2' -> [..., 'Info', '2']."""
    return re.findall(r"[A-Z]+[a-z]*|[0-9]+", name)


def upper_snake(name: str) -> str:
    return "_".join(t.upper() for t in tokens(name))


def snake(name: str) -> str:
    """Block-scope variable name from a Vulkan identifier: 'pCreateInfo' -> 'create_info', 'Win32HandleInfo' -> 'win32_handle_info'."""
    if name.startswith("p") and name[1:2].isupper():
        name = name[1:]
    return re.sub(r"(?<=[a-z0-9])(?=[A-Z])|(?<=[A-Z])(?=[A-Z][a-z])", "_", name).lower()


def indent(text: str, n: int = 4) -> str:
    pad = " " * n
    return "\n".join(pad + l if l else l for l in text.split("\n"))


ASSIGN = re.compile(r"^(\s*)([A-Za-z_][\w\.\->\[\]\(\)\*]*)\s*=\s*(.*;)$")


def align_assignments(lines: List[str]) -> List[str]:
    """Pad the left-hand side of every run of consecutive simple assignments so the '=' align."""
    out: List[str] = []
    run: List[Tuple[str, str, str]] = []

    def flush() -> None:
        if run:
            width = max(len(lhs) for _, lhs, _ in run)
            for ind, lhs, rhs in run:
                out.append(f"{ind}{lhs.ljust(width)} = {rhs}")
            run.clear()

    for line in lines:
        m = ASSIGN.match(line)
        if m and not m.group(2).startswith(("CVK_", "return")):
            if run and run[0][0] != m.group(1):
                flush()
            run.append((m.group(1), m.group(2), m.group(3)))
        else:
            flush()
            out.append(line)
    flush()
    return out


@dataclass
class EmittedParam:
    text: str            # "const void* pNext"
    name: str
    doc: str
    is_out: bool


class Generator:
    def __init__(self, reg: Registry, policy: dict):
        self.reg = reg
        self.policy = policy
        self.core = policy["core"]
        self.exts: Set[str] = set(policy["extensions"])
        self.cmds: List[Command] = reg.profile(self.core, sorted(self.exts))
        self.capacities: Dict[str, int] = dict(policy.get("capacities", {}))
        self.default_capacity = policy.get("default_capacity", 64)
        self.input_asserts: bool = bool(policy.get("input_asserts", False))
        self.used_capacities: Dict[str, int] = {}
        self.names: Dict[str, str] = {}
        self.decls: List[str] = []
        self.defs: List[str] = []
        self.errors: List[str] = []

    # ------------------------------------------------------------ profile
    def struct_in_profile(self, s: Struct) -> bool:
        return bool((s.core_version and s.core_version <= self.core) or any(e in self.exts for e in s.extensions))

    def guard_for(self, exts: List[str], core_version: Optional[str]) -> Optional[str]:
        """#ifdef protect macro when every providing extension is platform-specific."""
        if core_version and core_version <= self.core:
            return None
        plats = set()
        for e in exts:
            x = self.reg.extensions.get(e)
            if e in self.exts and x:
                if not x.platform:
                    return None
                plats.add(self.reg.platforms[x.platform])
        return " || ".join(f"defined({p})" for p in sorted(plats)) if plats else None

    def is_core(self, cmd: Command) -> bool:
        return bool(cmd.core_version and cmd.core_version <= self.core)

    def linked(self, cmd: Command) -> bool:
        return self.is_core(cmd) or any(e in LOADER_LINKED for e in cmd.extensions if e in self.exts)

    # -------------------------------------------------------------- names
    def register(self, name: str, origin: str) -> str:
        if name in self.names and self.names[name] != origin:
            self.errors.append(f"name collision: {name} from {origin} and {self.names[name]}")
        self.names[name] = origin
        return name

    def cmd_name(self, cmd: Command) -> str:
        return "cvk" + strip_vendor(cmd.name[2:])

    def struct_short(self, s: Struct) -> str:
        return strip_vendor(s.name[2:])

    def qualifier(self, ext: Struct, base: Struct) -> str:
        base_tokens = tokens(strip_vendor(base.name[2:]))
        out = []
        for t in tokens(strip_vendor(ext.name[2:])):
            if t in base_tokens:
                base_tokens.remove(t)
            else:
                out.append(t)
        return "".join(out)

    def convenience_name(self, cmd: Command, base: Struct, ext: Struct) -> str:
        """SPEC.md section 3: a convenience is named after the struct it builds, verb first:
        cvk<Verb><ExtenderStruct>. When the same verb and extender serve more than one base
        command in the profile, the name is <tier-0>With<ExtenderStruct> instead."""
        short = self.cmd_name(cmd) + "With" + self.struct_short(ext)
        if self.convenience_pairs.get((cmd.verb, ext.name), 0) > 1:
            return short
        return "cvk" + cmd.verb + self.struct_short(ext)

    def capacity_name(self, array_param: str) -> str:
        stem = array_param[1:] if array_param.startswith("p") and array_param[1:2].isupper() else array_param
        name = "CVK_MAX_" + upper_snake(stem)
        self.used_capacities[name] = self.capacities.get(name, self.default_capacity)
        return name

    # ---------------------------------------------------------- documentation
    def brief(self, cmd: Command) -> str:
        v, rest = cmd.verb, re.sub(r"^vk[A-Z][a-z]+", "", strip_vendor(cmd.name))
        noun = " ".join(t.lower() for t in tokens(rest)) or "the object"
        return {
            "Create": f"Creates a {noun}.", "Allocate": f"Allocates {noun}.", "Destroy": f"Destroys a {noun}.",
            "Free": f"Frees {noun}.", "Get": f"Retrieves {noun}.", "Enumerate": f"Enumerates {noun} into a fixed-capacity array.",
            "Cmd": f"Records {noun}.", "Bind": f"Binds {noun}.", "Queue": f"Queues {noun}.", "Wait": f"Waits for {noun}.",
            "Reset": f"Resets {noun}.", "Begin": f"Begins {noun}.", "End": f"Ends {noun}.", "Map": f"Maps {noun}.",
            "Unmap": f"Unmaps {noun}.", "Import": f"Imports {noun}.", "Set": f"Sets {noun}.", "Update": f"Updates {noun}.",
            "Signal": f"Signals {noun}.", "Acquire": f"Acquires {noun}.", "Release": f"Releases {noun}.",
        }.get(v, f"Calls {cmd.name}.")

    # --------------------------------------------------------------- roles
    def roles(self, cmd: Command) -> List[str]:
        return [role_of(self.reg, cmd, p, cmd.params) for p in cmd.params]

    def member_assert(self, m: Param, prefix: str, siblings: List[Param]) -> List[str]:
        """Assertions for a flattened member or a command parameter named by its wrapper name."""
        out = []
        name = prefix + m.name
        opt = m.optional[0] if m.optional else False
        if m.name in ("sType", "pNext") or opt:
            return out
        if m.len and not m.len.startswith("latexmath") and "," not in m.len and "->" not in m.len:
            count = m.len
            if count == "null-terminated":
                out.append(f"CVK_ASSERT(NULL != {name});")
            elif any(s.name == count for s in siblings):
                out.append(f"CVK_ASSERT(0 == {prefix}{count} || NULL != {name});")
            return out
        if m.is_pointer:
            out.append(f"CVK_ASSERT(NULL != {name});")
        elif m.type in self.reg.handles:
            out.append(f"CVK_ASSERT(VK_NULL_HANDLE != {name});")
        return out

    # -------------------------------------------------------------- tier 0
    def emit_tier0(self, cmd: Command) -> None:
        reg = self.reg
        roles = self.roles(cmd)
        name = self.register(self.cmd_name(cmd), cmd.name)
        filled = sum(1 for r in roles if r in ("I", "P"))
        params: List[EmittedParam] = []
        pre: List[str] = []
        post: List[str] = []
        locals_: List[str] = []
        call_args: List[str] = []
        enum_count: Optional[Param] = None
        enum_arrays: List[Tuple[Param, str]] = []

        # Two-call enumeration detection: a count parameter that sizes an output array.
        for p, r in zip(cmd.params, roles):
            if r == "n*":
                arrays = [q for q in cmd.params if q.len and q.len.split(",")[0] == p.name and q.type != "void"]
                if arrays:
                    enum_count = p
                    enum_arrays = [(q, self.capacity_name(q.name)) for q in arrays]

        # Device/instance-level extension commands dispatched on a child handle need the parent.
        parent: Optional[str] = None
        if not self.linked(cmd):
            dh = cmd.dispatch_handle
            if dh in ("VkQueue", "VkCommandBuffer"):
                parent = "VkDevice device"
            elif dh == "VkPhysicalDevice":
                parent = "VkInstance instance"
            if parent:
                params.append(EmittedParam(parent, parent.split()[1], "Parent handle used to resolve the extension command", False))
                pre.append(f"CVK_ASSERT(VK_NULL_HANDLE != {parent.split()[1]});")

        for p, r in zip(cmd.params, roles):
            if r == "I":
                s = reg.struct_of(p.type)
                var = snake(p.name)
                stem = p.name[1:] if p.name.startswith("p") else p.name
                locals_.append(f"{s.name} {var};")
                locals_.append(f"{var}.sType = {s.stype};")
                for m in s.members:
                    if m.name == "sType":
                        continue
                    if m.name == "pNext":
                        pn = "pNext" if filled == 1 else f"p{stem}Next"
                        params.append(EmittedParam(f"const void* {pn}", pn, f"Optional extension chain attached to {s.name}, or NULL", False))
                        locals_.append(f"{var}.pNext = {pn};")
                        continue
                    if m.array_len:
                        params.append(EmittedParam(f"const {m.full_type} {m.name}[{m.array_len}]", m.name, f"{s.name}::{m.name}", False))
                        locals_.append(f"memcpy({var}.{m.name}, {m.name}, sizeof({var}.{m.name}));")
                    else:
                        params.append(EmittedParam(f"{m.full_type} {m.name}", m.name, f"{s.name}::{m.name}", False))
                        locals_.append(f"{var}.{m.name} = {m.name};")
                    pre += self.member_assert(m, "", s.members)
                call_args.append(f"&{var}")
            elif r == "P":
                s = reg.struct_of(p.type)
                stem = p.name[1:] if p.name.startswith("p") else p.name
                pn = "pNext" if filled == 1 else f"p{stem}Next"
                params.append(EmittedParam(f"void* {pn}", pn, f"Optional output extension chain attached to {s.name}, or NULL", False))
                params.append(EmittedParam(f"{p.full_type} {p.name}", p.name, f"Receives the {s.name}", True))
                pre.append(f"CVK_ASSERT(NULL != {p.name});")
                locals_.append(f"{p.name}->sType = {s.stype};")
                locals_.append(f"{p.name}->pNext = {pn};")
                call_args.append(p.name)
            elif p is enum_count:
                call_args.append("&" + snake(p.name))
            elif any(p is q for q, _ in enum_arrays):
                cap = dict((id(q), c) for q, c in enum_arrays)[id(p)]
                elem = p.full_type.rstrip("*").strip()
                params.append(EmittedParam(f"{elem} {p.name}[{cap}]", p.name, f"Receives up to {cap} entries", True))
                pre.append(f"CVK_ASSERT(NULL != {p.name});")
                call_args.append(p.name)
            else:
                text = f"{p.full_type} {p.name}[{p.array_len}]" if p.array_len else f"{p.full_type} {p.name}"
                params.append(EmittedParam(text, p.name, self.param_doc(p, r), p.is_output))
                if p.is_output:
                    if not (p.optional and p.optional[0]):
                        pre.append(f"CVK_ASSERT(NULL != {p.name});")
                    if r == "H*out":
                        post.append(f"CVK_ASSERT(VK_NULL_HANDLE != (*{p.name}));")
                else:
                    pre += self.member_assert(p, "", cmd.params)
                call_args.append(p.name)

        # Call expression and result policy.
        fn = cmd.name
        if not self.linked(cmd):
            dh = cmd.dispatch_handle
            if dh in DEVICE_LEVEL:
                owner = "device"
                locals_.insert(0, f"PFN_{cmd.name} function = (PFN_{cmd.name})cvkGetDeviceProcAddr({owner}, \"{cmd.name}\");")
            else:
                owner = "instance"
                locals_.insert(0, f"PFN_{cmd.name} function = (PFN_{cmd.name})cvkGetInstanceProcAddr({owner}, \"{cmd.name}\");")
            fn = "function"
        call = f"{fn}({', '.join(call_args)})"

        informational = [c for c in cmd.successcodes if c not in ("VK_SUCCESS",)]
        if enum_count:
            informational = [c for c in informational if c != "VK_INCOMPLETE"]
        ret = "void"
        body: List[str] = list(locals_)
        if enum_count:
            ret = "uint32_t"
            count_var = snake(enum_count.name)
            first = call.replace(enum_arrays[0][0].name, "NULL")
            body.insert(0, f"uint32_t {count_var} = 0;")
            if cmd.return_type == "VkResult":
                body.append(f"CVK_CHECK({first});")
            else:
                body.append(f"{first};")
            for q, cap in enum_arrays:
                body.append(f"CVK_ASSERT({cap} >= {count_var});")
                s = reg.struct_of(q.type)
                if s and s.stype:
                    body.append("{")
                    body.append("    uint32_t index;")
                    body.append(f"    for (index = 0; index < {count_var}; ++index)")
                    body.append("    {")
                    body.append(f"        {q.name}[index].sType = {s.stype};")
                    body.append(f"        {q.name}[index].pNext = NULL;")
                    body.append("    }")
                    body.append("}")
            if cmd.return_type == "VkResult":
                body.append(f"CVK_CHECK({call});")
            else:
                body.append(f"{call};")
            body += post
            body.append(f"return {count_var};")
        elif cmd.return_type == "VkResult" and informational:
            ret = "VkResult"
            body.insert(0, "VkResult result;")
            body.append(f"result = {call};")
            body.append("CVK_ASSERT(0 <= result);")
            body += post
            body.append("return result;")
        elif cmd.return_type == "VkResult":
            body.append(f"CVK_CHECK({call});")
            body += post
        elif cmd.return_type == "void":
            body.append(f"{call};")
            body += post
        elif cmd.return_type == "PFN_vkVoidFunction":
            # The generated *ProcAddr wrappers are the resolution infrastructure: fatal when absent.
            ret = cmd.return_type
            body.insert(0, "PFN_vkVoidFunction function;")
            body.append(f"function = {call};")
            body.append("CVK_ASSERT(NULL != function);")
            body.append("return function;")
        else:
            ret = cmd.return_type
            body.append(f"return {call};")

        # Order: declarations first (C89-style block), then asserts, then statements.
        decl_lines = [l for l in body if re.match(r"^(PFN_\w+ function;|VkResult result;|uint32_t \w+ = 0;|Vk\w+ \w+;)", l)]
        other = [l for l in body if l not in decl_lines]
        if not self.input_asserts:
            pre = []
        body = decl_lines + ([""] if decl_lines else []) + pre + ([""] if pre else []) + other

        doc = self.doc_block(self.brief(cmd), params, ret, cmd, informational if ret == "VkResult" else [])
        sig = self.signature(ret, name, params)
        guard = self.guard_for(cmd.extensions, cmd.core_version)
        self.decls.append(self.guarded(guard, doc + sig + ";"))
        self.defs.append(self.guarded(guard, sig + "\n{\n" + indent("\n".join(body)) + "\n}"))

    def param_doc(self, p: Param, role: str) -> str:
        if p.name == "pAllocator":
            return "Optional allocation callbacks, or NULL"
        if role == "H*out":
            return f"Receives the created {p.type}"
        if p.is_output:
            return f"Receives the {p.type}"
        if p.len:
            return f"Array of {p.len} entries"
        return f"{p.type} value"

    def signature(self, ret: str, name: str, params: List[EmittedParam]) -> str:
        """One parameter per line; parameter names aligned in a column after the widest type."""
        if not params:
            return f"{ret} {name}(void)"
        split = []
        for p in params:
            t, _, n = p.text.rpartition(" ")
            split.append((t, n))
        width = max(len(t) for t, _ in split)
        pad = " " * (len(ret) + 1 + len(name) + 1)
        return f"{ret} {name}(" + (",\n" + pad).join(f"{t.ljust(width)} {n}" for t, n in split) + ")"

    def doc_block(self, brief: str, params: List[EmittedParam], ret: str, cmd: Optional[Command], informational: List[str]) -> str:
        lines = ["/**", f" * \\brief {brief}", " *"]
        if cmd:
            lines.append(f" * \\par Details:\n *  Wraps {cmd.name}." + (" Aborts on any error result." if cmd.return_type == "VkResult" else ""))
            lines.append(" *")
        width = max((len(p.name) for p in params), default=0)
        for p in params:
            lines.append(f" * \\param[{'out' if p.is_out else 'in'}] {p.name.ljust(width)}  {p.doc}")
        if ret == "void":
            lines.append(" *\n * \\retval None")
        elif ret == "VkResult":
            lines.append(" *\n * \\return VK_SUCCESS or one of: " + ", ".join(informational) + ". Error results abort.")
        elif ret == "uint32_t" and cmd and any(r == "n*" for r in self.roles(cmd)):
            lines.append(" *\n * \\return Number of entries written.")
        else:
            lines.append(f" *\n * \\return The {ret} returned by the command.")
        lines.append(" */")
        return "\n".join(lines) + "\n"

    def guarded(self, guard: Optional[str], text: str) -> str:
        return f"#if {guard}\n{text}\n#endif" if guard else text

    # ------------------------------------------------------------ builders
    def emit_builder(self, s: Struct) -> None:
        name = self.register("cvkInit" + self.struct_short(s), s.name)
        pnext_type = next((m.full_type for m in s.members if m.name == "pNext"), "const void*")
        params = [EmittedParam(f"{s.name}* pInfo", "pInfo", f"Receives the initialized {s.name}", True),
                  EmittedParam(f"{pnext_type} pNext", "pNext", f"Optional extension chain attached to {s.name}, or NULL", False)]
        body = (["CVK_ASSERT(NULL != pInfo);", ""] if self.input_asserts else []) + [f"pInfo->sType = {s.stype};", "pInfo->pNext = pNext;"]
        if not s.returnedonly:
            for m in s.members:
                if m.name in ("sType", "pNext"):
                    continue
                if m.array_len:
                    params.append(EmittedParam(f"const {m.full_type} {m.name}[{m.array_len}]", m.name, f"{s.name}::{m.name}", False))
                    body.append(f"memcpy(pInfo->{m.name}, {m.name}, sizeof(pInfo->{m.name}));")
                else:
                    params.append(EmittedParam(f"{m.full_type} {m.name}", m.name, f"{s.name}::{m.name}", False))
                    body.append(f"pInfo->{m.name} = {m.name};")
        doc = self.doc_block(f"Initializes a {s.name}.", params, "void", None, [])
        sig = self.signature("void", name, params)
        guard = self.guard_for(s.extensions, s.core_version)
        self.decls.append(self.guarded(guard, doc + sig + ";"))
        self.defs.append(self.guarded(guard, sig + "\n{\n" + indent("\n".join(body)) + "\n}"))

    # -------------------------------------------------------- conveniences
    def emit_convenience(self, cmd: Command, ip: Param, ext: Struct) -> None:
        reg = self.reg
        base = reg.struct_of(ip.type)
        q = self.qualifier(ext, base)
        t0 = self.cmd_name(cmd)
        name = self.register(self.convenience_name(cmd, base, ext), f"{cmd.name}+{ext.name}")
        # Rebuild the tier-0 parameter list by re-running emit logic on a scratch generator state.
        saved = (self.decls, self.defs, self.names.copy())
        self.decls, self.defs = [], []
        self.emit_tier0(cmd)
        sig_text = self.decls[-1]
        self.decls, self.defs, self.names = saved[0], saved[1], saved[2]
        self.names[name] = f"{cmd.name}+{ext.name}"
        m = re.search(r"\n(\w[\w ]*?) (cvk\w+)\((.*?)\);", sig_text, flags=re.S)
        ret, _, plist = m.group(1), m.group(2), m.group(3)
        base_params = [p.strip() for p in plist.split(",\n")] if plist.strip() != "void" else []
        filled = sum(1 for r in self.roles(cmd) if r in ("I", "P"))
        stem = ip.name[1:] if ip.name.startswith("p") else ip.name
        pn = "pNext" if filled == 1 else f"p{stem}Next"
        idx = next(i for i, p in enumerate(base_params) if p.split()[-1] == pn)
        ext_params: List[str] = []
        ext_var = snake(self.struct_short(ext))
        body = [f"{ext.name} {ext_var};", "", f"{ext_var}.sType = {ext.stype};", f"{ext_var}.pNext = {pn};"]
        base_names = {p.split()[-1].split("[")[0] for p in base_params}
        qlower = q[0].lower() + q[1:]
        for em in ext.members:
            if em.name in ("sType", "pNext"):
                continue
            # SPEC 6: an extender member whose name collides with a base parameter is prefixed with the qualifier.
            pname = em.name if em.name not in base_names else qlower + em.name[0].upper() + em.name[1:]
            if em.array_len:
                ext_params.append(f"const {em.full_type} {pname}[{em.array_len}]")
                body.append(f"memcpy({ext_var}.{em.name}, {pname}, sizeof({ext_var}.{em.name}));")
            else:
                ext_params.append(f"{em.full_type} {pname}")
                body.append(f"{ext_var}.{em.name} = {pname};")
        all_params = base_params[: idx + 1] + ext_params + base_params[idx + 1:]
        args = [p.split()[-1].split("[")[0] for p in base_params]
        args[idx] = "&" + ext_var
        call = f"{t0}({', '.join(args)})"
        body.append(("return " if ret != "void" else "") + call + ";")
        eparams = [EmittedParam(p, p.split()[-1].split("[")[0], "", False) for p in all_params]
        for e in eparams:
            e.doc = f"{ext.name}::{e.name}" if e.text in ext_params else f"See {t0}"
            e.is_out = e.text.split()[-1].startswith("p") and not e.text.startswith("const") and "*" in e.text
        doc = self.doc_block(f"{self.brief(cmd)[:-1]} with {ext.name} chained.", eparams, ret, None, [])
        sig = self.signature(ret, name, eparams)
        guards = [g for g in (self.guard_for(cmd.extensions, cmd.core_version), self.guard_for(ext.extensions, ext.core_version)) if g]
        guard = " && ".join(f"({g})" for g in guards) if guards else None
        self.decls.append(self.guarded(guard, doc + sig + ";"))
        self.defs.append(self.guarded(guard, sig + "\n{\n" + indent("\n".join(body)) + "\n}"))

    # ---------------------------------------------------------------- run
    def run(self) -> None:
        for cmd in self.cmds:
            self.emit_tier0(cmd)
        structs = [s for s in self.reg.structs.values() if s.stype and not s.alias_of and s.category == "struct" and self.struct_in_profile(s)]
        for s in sorted(structs, key=lambda s: s.name):
            self.emit_builder(s)
        pairs: List[Tuple[Command, Param, Struct]] = []
        for cmd in self.cmds:
            roles = self.roles(cmd)
            iparams = [p for p, r in zip(cmd.params, roles) if r == "I"]
            if len(iparams) != 1:
                continue
            base = self.reg.struct_of(iparams[0].type)
            for ext in sorted(self.reg.extenders_of(base.name), key=lambda e: e.name):
                if not self.struct_in_profile(ext) or re.match(r"VkPhysicalDevice\w*Features", ext.name):
                    continue
                pairs.append((cmd, iparams[0], ext))
        self.convenience_pairs = defaultdict(int)
        for cmd, _, ext in pairs:
            self.convenience_pairs[(cmd.verb, ext.name)] += 1
        for cmd, ip, ext in pairs:
            self.emit_convenience(cmd, ip, ext)

    def header(self) -> str:
        caps = "\n".join(f"#ifndef {k}\n#define {k} ({v}U)\n#endif" for k, v in sorted(self.used_capacities.items()))
        return f"""#ifndef CVK_H
#define CVK_H

/* Generated by scripts/generate.py from the Vulkan registry. Do not edit. */

#include <vulkan/vulkan.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {{
#endif

/* Fatal diagnostics. Override CVK_ASSERT before including to change the policy. */
void cvkFatal(const char* pExpression, const char* pFile, int line);
void cvkCheckResult(VkResult result, const char* pExpression, const char* pFile, int line);

#ifndef CVK_ASSERT
#define CVK_ASSERT(expression) ((expression) ? (void)0 : cvkFatal(#expression, __FILE__, __LINE__))
#endif
#define CVK_CHECK(expression) cvkCheckResult((expression), #expression, __FILE__, __LINE__)

/* Fixed enumeration capacities; override any before including. */
{caps}

{chr(10).join(self.decls)}

#ifdef __cplusplus
}}
#endif

#endif /* CVK_H */
"""

    def source(self) -> str:
        return f"""/* Generated by scripts/generate.py from the Vulkan registry. Do not edit. */

#include "cvk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void cvkFatal(const char* pExpression, const char* pFile, int line)
{{
    (void)fprintf(stderr, "%s:%d: cvk fatal: %s\\n", pFile, line, pExpression);
    abort();
}}

void cvkCheckResult(VkResult result, const char* pExpression, const char* pFile, int line)
{{
    if (VK_SUCCESS != result)
    {{
        (void)fprintf(stderr, "%s:%d: cvk fatal: %s returned %d\\n", pFile, line, pExpression, (int)result);
        abort();
    }}
}}

{chr(10).join(self.defs)}
"""


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--policy", default=os.path.join(REPO_ROOT, "cvk.policy.json"))
    ap.add_argument("--out-dir", default=REPO_ROOT)
    args = ap.parse_args()
    with open(args.policy, encoding="utf-8") as f:
        policy = json.load(f)
    reg = Registry.load()
    g = Generator(reg, policy)
    g.run()
    if g.errors:
        for e in g.errors:
            print("error:", e)
        sys.exit(1)
    os.makedirs(os.path.join(args.out_dir, "include"), exist_ok=True)
    os.makedirs(os.path.join(args.out_dir, "src"), exist_ok=True)
    with open(os.path.join(args.out_dir, "include", "cvk.h"), "w", encoding="utf-8", newline="\n") as f:
        f.write(g.header())
    with open(os.path.join(args.out_dir, "src", "cvk.c"), "w", encoding="utf-8", newline="\n") as f:
        f.write(g.source())
    # Final formatting is clang-format's (repository .clang-format); the generator's own alignment is a fallback.
    import shutil
    import subprocess
    clang_format = shutil.which("clang-format")
    if clang_format:
        subprocess.run([clang_format, "-i", "--style=file",
                        os.path.join(args.out_dir, "include", "cvk.h"), os.path.join(args.out_dir, "src", "cvk.c")],
                       check=True, cwd=args.out_dir)
    else:
        print("warning: clang-format not found; output left with generator alignment only")
    t0 = sum(1 for n in g.names.values() if n.startswith("vk") and "+" not in n)
    builders = sum(1 for n in g.names if n.startswith("cvkInit"))
    conv = sum(1 for n in g.names.values() if "+" in n)
    print(f"generated include/cvk.h and src/cvk.c: tier0={t0} builders={builders} conveniences={conv} capacities={len(g.used_capacities)}")


if __name__ == "__main__":
    main()
