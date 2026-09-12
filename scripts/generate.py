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
    """CamelCase words with digit runs and acronyms as their own tokens:
    'ImageMemoryRequirementsInfo2' -> [..., 'Info', '2'], 'PhysicalDeviceIDProperties' -> [..., 'ID', 'Properties']."""
    return re.findall(r"[A-Z]+(?![a-z])|[A-Z][a-z]*|[0-9]+", name)


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
        self.decls: Dict[str, List[str]] = defaultdict(list)   # "" = core, else platform name
        self.defs: Dict[str, List[str]] = defaultdict(list)
        self.sigs: Dict[str, Tuple[str, List[EmittedParam]]] = {}   # every emitted signature, for chains
        self.errors: List[str] = []

    # ------------------------------------------------------------ profile
    def struct_in_profile(self, s: Struct) -> bool:
        return bool((s.core_version and s.core_version <= self.core) or any(e in self.exts for e in s.extensions))

    def platforms_for(self, exts: List[str], core_version: Optional[str]) -> frozenset:
        """Platforms an entity belongs to: empty when core or provided by any platform-neutral extension."""
        if core_version and core_version <= self.core:
            return frozenset()
        plats = set()
        for e in exts:
            x = self.reg.extensions.get(e)
            if e in self.exts and x:
                if not x.platform:
                    return frozenset()
                plats.add(x.platform)
        return frozenset(plats)

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

    def with_base(self, cmd: Command) -> str:
        """Tier-0 name minus the command's trailing version digit: a chained extender already
        implies the extensible form (cvkBindImageMemory2 -> cvkBindImageMemoryWith...)."""
        return re.sub(r"\d+$", "", self.cmd_name(cmd))

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
        """SPEC.md section 3. Distinct part D of the extender E against its base struct B:
        align E with B by matching blocks; tokens of E outside any block form D, except a
        one-token block strictly inside E, which stays (compound-term protection: the Format
        inside DrmFormatModifier). Strip the struct-kind word and Info/version tokens from D.

        Placement: constructors and non-queries -> <tier-0>With<D>Info. Queries: a plain Info
        extender covering B's noun run is the subject -> cvk<Verb><ExtenderStruct>; an add-on
        (CreateInfo/AllocateInfo/BeginInfo) is spliced between B's noun run and the command's
        tail, with the command's version digit dropped."""
        import difflib

        kind = ("Info", "Create", "Allocate", "Begin", "Bind", "Submit")

        def norm(t: str) -> str:
            return t[:-1] if len(t) > 3 and t.endswith("s") else t

        e = tokens(self.struct_short(ext))
        b = tokens(self.struct_short(base))
        blocks = [m for m in difflib.SequenceMatcher(None, e, b, autojunk=False).get_matching_blocks() if m.size]
        matched = set()
        for m in blocks:
            if m.size == 1 and 0 < m.a < len(e) - 1:
                continue
            matched.update(range(m.a, m.a + m.size))
        d_idx = [i for i in range(len(e)) if i not in matched]
        d = [e[i] for i in d_idx]
        while d and (d[-1] in kind or d[-1].isdigit()):
            d.pop()
        if not d:
            self.errors.append(f"extender {ext.name} has no distinct part against {base.name}")
            return self.with_base(cmd) + "With" + self.struct_short(ext)

        cmd_tokens = tokens(strip_vendor(cmd.name[2:]))
        verb, rest = cmd_tokens[0], cmd_tokens[1:]
        run = [t for t in b if t not in kind and not t.isdigit()]
        if verb not in ("Get", "Enumerate"):
            return self.with_base(cmd) + "With" + "".join(d) + "Info"
        # Query: a plain Info extender whose name covers the base noun run is the subject of
        # the query. A CreateInfo/AllocateInfo/BeginInfo extender is an add-on and is spliced.
        ne, nrun = [norm(t) for t in e], [norm(t) for t in run]
        plain_info = len(e) >= 2 and e[-1] == "Info" and e[-2] not in kind
        if plain_info and all(t in ne for t in nrun):
            return "cvk" + verb + self.struct_short(ext)
        # A chained extender already implies the extensible (X2) form: drop the version digit.
        while rest and rest[-1].isdigit():
            rest.pop()
        nrest = [norm(t) for t in rest]
        start = next((i for i in range(len(nrest) - len(nrun) + 1) if nrest[i:i + len(nrun)] == nrun), -1)
        if start < 0:
            return self.with_base(cmd) + "With" + "".join(d) + "Info"

        # Splice: the distinct part wraps the base run's unmatched remainder. Its first word
        # follows the last matched base word; the remainder of the run follows; then the rest
        # of the distinct part; then the command's tail.
        matched_norm = {norm(e[i]) for i in matched}
        last = max((i for i, t in enumerate(run) if norm(t) in matched_norm), default=len(run) - 1)
        head, run_rest = run[: last + 1], run[last + 1:]
        spliced = rest[:start] + head + d[:1] + run_rest + d[1:] + rest[start + len(run):]
        return "cvk" + verb + "".join(spliced)

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
    KIND = ("Create", "Allocate", "Begin", "Bind", "Submit", "End")

    def is_plain_info(self, s: Struct) -> bool:
        """A '<Noun>Info[N]' struct that is not a construction/recording kind: a query subject."""
        t = tokens(self.struct_short(s))
        while t and t[-1].isdigit():
            t.pop()
        return len(t) >= 2 and t[-1] == "Info" and t[-2] not in self.KIND

    def flip_param(self, cmd: Command) -> Optional[Param]:
        """The single plain-Info input whose tier 0 keeps the pointer (SPEC.md section 4):
        only when the command also fills an output sType struct, so that tier 0 does the
        output and the flattened sibling does the input. A command with a plain-Info input and
        no output struct flattens at tier 0 like everything else."""
        roles = self.roles(cmd)
        iparams = [p for p, r in zip(cmd.params, roles) if r == "I"]
        fills_output = any(r in ("P", "P[]") for r in roles)
        if len(iparams) == 1 and fills_output and self.is_plain_info(self.reg.struct_of(iparams[0].type)):
            return iparams[0]
        return None

    def flat_name(self, cmd: Command, s: Struct) -> str:
        """cvk<Verb><InfoStruct> with the verb token removed once from the struct name. When
        Vulkan reuses one Info struct for several commands of the same verb
        (VkBufferDeviceAddressInfo), the sibling is named after the command: <tier-0>Info."""
        if self.flat_pairs.get((cmd.verb, s.name), 0) > 1:
            return self.cmd_name(cmd) + "Info"
        t = tokens(self.struct_short(s))
        if cmd.verb in t:
            t.remove(cmd.verb)
        return "cvk" + cmd.verb + "".join(t)

    def emit_tier0(self, cmd: Command) -> None:
        reg = self.reg
        roles = self.roles(cmd)
        name = self.register(self.cmd_name(cmd), cmd.name)
        keep = self.flip_param(cmd)
        roles = ["S*" if (keep is not None and p is keep) else r for p, r in zip(cmd.params, roles)]
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
                locals_.append("")
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
                var = snake(p.name)
                locals_.append(f"{s.name} {var};")
                locals_.append("")
                locals_.append(f"{var}.sType = {s.stype};")
                locals_.append(f"{var}.pNext = {pn};")
                call_args.append(f"&{var}")
                post.append(f"(*{p.name}) = {var};")
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
        # One blank line between struct fills; none leading, trailing, or doubled.
        collapsed: List[str] = []
        for l in other:
            if l == "" and (not collapsed or collapsed[-1] == ""):
                continue
            collapsed.append(l)
        other = collapsed[:-1] if collapsed and collapsed[-1] == "" else collapsed
        if not self.input_asserts:
            pre = []
        body = decl_lines + ([""] if decl_lines else []) + pre + ([""] if pre else []) + other

        doc = self.doc_block(self.brief(cmd), params, ret, cmd, informational if ret == "VkResult" else [])
        sig = self.signature(ret, name, params)
        self.sigs[name] = (ret, params)
        self.place(self.platforms_for(cmd.extensions, cmd.core_version), doc + sig + ";",
                   sig + "\n{\n" + indent("\n".join(align_assignments(body))) + "\n}")

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

    def place(self, plats: frozenset, decl: str, defn: str) -> None:
        """Route an emitted function to the core files or to exactly one platform's files."""
        if len(plats) > 1:
            self.errors.append(f"entity spans several platforms {sorted(plats)}: {decl.splitlines()[-1]}")
            return
        key = next(iter(plats)) if plats else ""
        self.decls[key].append(decl)
        self.defs[key].append(defn)

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
        self.place(self.platforms_for(s.extensions, s.core_version), doc + sig + ";",
                   sig + "\n{\n" + indent("\n".join(align_assignments(body))) + "\n}")

    # -------------------------------------------------------- conveniences
    def emit_flat(self, cmd: Command, ip: Param) -> None:
        """Flattened sibling of a pointer-keeping tier 0: builds the plain-Info struct from
        its members and calls tier 0 (SPEC.md section 4)."""
        s = self.reg.struct_of(ip.type)
        name = self.register(self.flat_name(cmd, s), f"{cmd.name}:flat")
        t0 = self.cmd_name(cmd)
        ret, t0_params = self.sigs[t0]
        has_p = any(r == "P" for r in self.roles(cmd))
        stem = ip.name[1:] if ip.name.startswith("p") else ip.name
        pn = f"p{stem}Next" if has_p else "pNext"
        var = snake(ip.name)
        params: List[EmittedParam] = []
        args: List[str] = []
        body: List[str] = [f"{s.name} {var};", "", f"{var}.sType = {s.stype};"]
        p_out = next((q for q, r in zip(cmd.params, self.roles(cmd)) if r == "P"), None)
        for p in t0_params:
            if p.name != ip.name:
                if p.name == "pNext" and p_out is not None:
                    # Two structs are filled here, so the output chain takes its stem name.
                    out_stem = p_out.name[1:] if p_out.name.startswith("p") else p_out.name
                    renamed = f"p{out_stem}Next"
                    params.append(EmittedParam(f"void* {renamed}", renamed, p.doc, False))
                    args.append(renamed)
                    continue
                params.append(p)
                args.append(p.name)
                continue
            for m in s.members:
                if m.name == "sType":
                    continue
                if m.name == "pNext":
                    params.append(EmittedParam(f"const void* {pn}", pn, f"Optional extension chain attached to {s.name}, or NULL", False))
                    body.append(f"{var}.pNext = {pn};")
                elif m.array_len:
                    params.append(EmittedParam(f"const {m.full_type} {m.name}[{m.array_len}]", m.name, f"{s.name}::{m.name}", False))
                    body.append(f"memcpy({var}.{m.name}, {m.name}, sizeof({var}.{m.name}));")
                else:
                    params.append(EmittedParam(f"{m.full_type} {m.name}", m.name, f"{s.name}::{m.name}", False))
                    body.append(f"{var}.{m.name} = {m.name};")
            args.append(f"&{var}")
        call = f"{t0}({', '.join(args)})"
        body.append(("return " if ret != "void" else "") + call + ";")
        doc = self.doc_block(f"{self.brief(cmd)[:-1]} from the fields of {s.name}.", params, ret, None, [])
        sig = self.signature(ret, name, params)
        self.sigs[name] = (ret, params)
        self.place(self.platforms_for(cmd.extensions, cmd.core_version), doc + sig + ";",
                   sig + "\n{\n" + indent("\n".join(align_assignments(body))) + "\n}")

    def emit_convenience(self, cmd: Command, ip: Param, ext: Struct) -> None:
        reg = self.reg
        base = reg.struct_of(ip.type)
        q = self.qualifier(ext, base)
        # Chain onto the flattened form: the flat sibling when tier 0 keeps the pointer.
        t0 = self.flat_name(cmd, base) if self.flip_param(cmd) is not None else self.cmd_name(cmd)
        name = self.register(self.convenience_name(cmd, base, ext), f"{cmd.name}+{ext.name}")
        ret, t0_params = self.sigs[t0]
        base_params = [p.text for p in t0_params]
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
        self.sigs[name] = (ret, eparams)
        plats = self.platforms_for(cmd.extensions, cmd.core_version) | self.platforms_for(ext.extensions, ext.core_version)
        self.place(plats, doc + sig + ";", sig + "\n{\n" + indent("\n".join(align_assignments(body))) + "\n}")

    # ------------------------------------------------ array and output extenders
    @staticmethod
    def plural(name: str) -> str:
        """Parallel-array parameter name for a member: image -> pImages, memory -> pMemories, imageIndex -> pImageIndices."""
        cap = name[0].upper() + name[1:]
        if cap.endswith("Index"):
            return "p" + cap[:-5] + "Indices"
        if cap.endswith("y"):
            return "p" + cap[:-1] + "ies"
        return "p" + cap + "s"

    KINDS_OUT = ("Properties", "Requirements", "Features", "Support", "Capabilities")

    def distinct_part(self, ext: Struct, base: Struct, strip_info: bool = True) -> List[str]:
        """The extender's tokens outside the blocks matching its base (see convenience_name).
        A trailing version digit is a struct suffix only when it follows a kind word
        (Info2, Properties2); a digit inside a name (Vulkan12) is part of that name."""
        import difflib
        e = tokens(self.struct_short(ext))
        if len(e) >= 2 and e[-1].isdigit() and (e[-2] in self.KINDS_OUT or e[-2] == "Info"):
            e.pop()
        b = tokens(self.struct_short(base))
        blocks = [m for m in difflib.SequenceMatcher(None, e, b, autojunk=False).get_matching_blocks() if m.size]
        matched = set()
        for m in blocks:
            if m.size == 1 and 0 < m.a < len(e) - 1:
                continue
            if m.size == 1 and e[m.a].isdigit():
                continue                                 # a lone digit never matches a version digit
            matched.update(range(m.a, m.a + m.size))
        d = [e[i] for i in range(len(e)) if i not in matched]
        strip = self.KIND + (("Info",) if strip_info else ())
        while d and d[-1] in strip:
            d.pop()
        return d

    def emit_array_convenience(self, cmd: Command, ap: Param, ext: Struct) -> None:
        """SPEC.md section 6: <tier-0>With<D>Info for an array input, as parallel arrays.
        Each element of the base struct gets the extender chained; the base members and the
        extender members become parallel arrays sized by the command's count parameter."""
        base = self.reg.struct_of(ap.type)
        d = "".join(self.distinct_part(ext, base))
        name = self.register(self.with_base(cmd) + "With" + d + "Info", f"{cmd.name}+{ext.name}[]")
        t0 = self.cmd_name(cmd)
        ret, t0_params = self.sigs[t0]
        count = ap.len.split(",")[0]
        cap = self.capacity_name(ap.name)
        base_var = snake(ap.name)                               # bind_infos
        ext_var = snake(self.struct_short(ext)) + "s"           # bind_image_plane_memory_infos
        params: List[EmittedParam] = []
        args: List[str] = []
        fill: List[str] = []
        for p in t0_params:
            if p.name != ap.name:
                params.append(p)
                args.append(p.name)
                continue
            params.append(EmittedParam("const void* const* ppNexts", "ppNexts", f"Optional per-element extension chains attached to {ext.name}, or NULL", False))
            for m in ext.members:
                if m.name in ("sType", "pNext"):
                    continue
                pn = self.plural(m.name)
                params.append(EmittedParam(f"const {m.full_type}* {pn}", pn, f"{ext.name}::{m.name} per element", False))
                fill.append(f"{ext_var}[index].{m.name} = {pn}[index];")
            for m in base.members:
                if m.name in ("sType", "pNext"):
                    continue
                pn = self.plural(m.name)
                params.append(EmittedParam(f"const {m.full_type}* {pn}", pn, f"{base.name}::{m.name} per element", False))
                fill.append(f"{base_var}[index].{m.name} = {pn}[index];")
            args.append(base_var)
        body = [f"{base.name} {base_var}[{cap}];", f"{ext.name} {ext_var}[{cap}];", "uint32_t index;", "",
                f"CVK_ASSERT({cap} >= {count});",
                f"for (index = 0; index < {count}; ++index)", "{",
                f"    {ext_var}[index].sType = {ext.stype};",
                f"    {ext_var}[index].pNext = (NULL != ppNexts) ? ppNexts[index] : NULL;"]
        body += ["    " + l for l in fill if l.startswith(ext_var)]
        body += [f"    {base_var}[index].sType = {base.stype};", f"    {base_var}[index].pNext = &{ext_var}[index];"]
        body += ["    " + l for l in fill if l.startswith(base_var)]
        body += ["}", ("return " if ret != "void" else "") + f"{t0}({', '.join(args)});"]
        doc = self.doc_block(f"{self.brief(cmd)[:-1]} with {ext.name} chained to every element.", params, ret, None, [])
        sig = self.signature(ret, name, params)
        self.sigs[name] = (ret, params)
        plats = self.platforms_for(cmd.extensions, cmd.core_version) | self.platforms_for(ext.extensions, ext.core_version)
        self.place(plats, doc + sig + ";", sig + "\n{\n" + indent("\n".join(align_assignments(body))) + "\n}")

    def emit_output_convenience(self, cmd: Command, op: Param, ext: Struct) -> None:
        """SPEC.md section 6: <target>With<D><Kind> chains an extender onto the output struct
        and returns it as an additional out-parameter. <target> is the flattened sibling when
        one exists, else tier 0; <Kind> is the extender's trailing word (Requirements,
        Properties, Features)."""
        base = self.reg.struct_of(op.type)
        d_tokens = self.distinct_part(ext, base, strip_info=False)
        d = "".join(d_tokens)
        # The kind is the base struct's (Properties, Requirements, Features), appended unless
        # the distinct part already carries it (VkDrmFormatModifierPropertiesListEXT).
        base_tokens = [t for t in tokens(self.struct_short(base)) if not t.isdigit()]
        kind = base_tokens[-1] if base_tokens and base_tokens[-1] in self.KINDS_OUT else ""
        if kind in d_tokens:
            kind = ""
        target = self.flat_name(cmd, self.reg.struct_of(self.flip_param(cmd).type)) if self.flip_param(cmd) is not None else self.cmd_name(cmd)
        prefix = target if self.flip_param(cmd) is not None else self.with_base(cmd)
        name = self.register(prefix + "With" + d + kind, f"{cmd.name}+{ext.name}:out")
        ret, t_params = self.sigs[target]
        out_stem = op.name[1:] if op.name.startswith("p") else op.name
        chain_param = next((p for p in t_params if p.name in (f"p{out_stem}Next", "pNext") and p.text.startswith("void*")), None)
        if chain_param is None:
            self.errors.append(f"output convenience {name}: no output chain parameter on {target}")
            return
        ext_out = "p" + d + kind
        ext_next = f"p{d}{kind}Next"
        var = snake(ext_out)
        params: List[EmittedParam] = []
        args: List[str] = []
        for p in t_params:
            if p is chain_param:
                params.append(EmittedParam(f"void* {ext_next}", ext_next, f"Optional output extension chain attached to {ext.name}, or NULL", False))
                args.append(f"&{var}")
            else:
                params.append(p)
                args.append(p.name)
        params.append(EmittedParam(f"{ext.name}* {ext_out}", ext_out, f"Receives the {ext.name}", True))
        body = [f"{ext.name} {var};", "", f"{var}.sType = {ext.stype};", f"{var}.pNext = {ext_next};"]
        call = f"{target}({', '.join(args)})"
        if ret != "void":
            body.insert(0, f"{ret} result;")
            body += [f"result = {call};", f"(*{ext_out}) = {var};", "return result;"]
        else:
            body += [f"{call};", f"(*{ext_out}) = {var};"]
        doc = self.doc_block(f"{self.brief(cmd)[:-1]} with {ext.name} chained to the output.", params, ret, None, [])
        sig = self.signature(ret, name, params)
        self.sigs[name] = (ret, params)
        plats = self.platforms_for(cmd.extensions, cmd.core_version) | self.platforms_for(ext.extensions, ext.core_version)
        self.place(plats, doc + sig + ";", sig + "\n{\n" + indent("\n".join(align_assignments(body))) + "\n}")

    # ------------------------------------------------------ policy functions
    # Fixed logic the registry cannot supply (SPEC.md section 8). Declared here so that chains
    # can compose it like any generated function; the text is policy, recorded in SPEC.md.
    FIXED = [
        {
            "name": "cvkGetMemoryTypeIndex",
            "ret": "uint32_t",
            "brief": "Returns the first memory type in the mask whose property flags equal the given flags exactly, or VK_INVALID.",
            "params": [("uint32_t", "memoryTypeCount"), ("const VkMemoryType*", "pMemoryTypes"),
                       ("uint32_t", "memoryTypeBits"), ("VkMemoryPropertyFlags", "properties")],
            "body": [
                "uint32_t memory_type_index;",
                "",
                "for (memory_type_index = 0; memory_type_index < memoryTypeCount; ++memory_type_index)",
                "{",
                "    if ((memoryTypeBits & (1u << memory_type_index)) && (pMemoryTypes[memory_type_index].propertyFlags == properties))",
                "    {",
                "        return memory_type_index;",
                "    }",
                "}",
                "return VK_INVALID;",
            ],
        },
        {
            "name": "cvkGetCompatibleMemoryTypeIndex",
            "ret": "uint32_t",
            "brief": "Returns the first memory type in the mask whose property flags cover the required flags, or VK_INVALID.",
            "params": [("uint32_t", "memoryTypeCount"), ("const VkMemoryType*", "pMemoryTypes"),
                       ("uint32_t", "memoryTypeBits"), ("VkMemoryPropertyFlags", "requiredProperties")],
            "body": [
                "uint32_t memory_type_index;",
                "",
                "for (memory_type_index = 0; memory_type_index < memoryTypeCount; ++memory_type_index)",
                "{",
                "    if ((memoryTypeBits & (1u << memory_type_index)) && ((pMemoryTypes[memory_type_index].propertyFlags & requiredProperties) == requiredProperties))",
                "    {",
                "        return memory_type_index;",
                "    }",
                "}",
                "return VK_INVALID;",
            ],
        },
    ]

    def emit_fixed(self, f: dict) -> None:
        name = self.register(f["name"], "policy:" + f["name"])
        params = [EmittedParam(f"{t} {n}", n, f"{n} value", False) for t, n in f["params"]]
        doc = self.doc_block(f["brief"], params, f["ret"], None, [])
        sig = self.signature(f["ret"], name, params)
        self.sigs[name] = (f["ret"], params)
        self.place(frozenset(), doc + sig + ";", sig + "\n{\n" + indent("\n".join(f["body"])) + "\n}")

    # ---------------------------------------------------------------- chains
    LEVELS = ["instance", "physicalDevice", "device", "queue", "commandBuffer"]

    def emit_chain(self, name: str, spec: dict) -> None:
        """SPEC.md section 8: compose a straight-line chain from a step table.

        The chain's parameters are computed from the steps' signatures: an input is satisfied
        by an earlier output whose stem matches (image <- pImage, allocationSize <-
        pAllocationSize), a step's pNext is renamed after that step's product (pImageNext,
        pMemoryNext), every unsatisfied input becomes a chain parameter, and every produced
        handle becomes a chain out-parameter. Dispatch handles come first by level, then inputs
        in order of appearance, then pAllocator, then outputs."""
        name = self.register(name, "chain:" + name)
        inputs: Dict[str, EmittedParam] = {}
        outputs: Dict[str, EmittedParam] = {}
        locals_: Dict[str, str] = {}
        available: Dict[str, str] = {}
        body: List[str] = []
        ret = "void"

        def stem(n: str) -> str:
            s = n[1:] if n.startswith("p") and n[1:2].isupper() else n
            return s[0].lower() + s[1:]

        declared = [EmittedParam(f"{x['type']} {x['name']}", x["name"], x.get("doc", f"{x['name']} value"), False) for x in spec.get("inputs", [])]
        for out in spec.get("outputs", []):
            outputs[out["name"]] = EmittedParam(f"{out['type']}* {out['name']}", out["name"], f"Receives the {stem(out['name'])}", True)
            available[stem(out["name"])] = f"(*{out['name']})"
        for step in spec["steps"]:
            if "set" in step:
                for k, v in step["set"].items():
                    body.append(f"(*{k}) = {v};")
                continue
            if "assert" in step:
                body.append(f"CVK_ASSERT({step['assert']});")
                continue
            if "return" in step and "call" not in step:
                ret = spec["ret"]
                body.append(f"return {step['return']};")
                continue
            fn = step["call"]
            if fn not in self.sigs:
                self.errors.append(f"chain {name}: unknown step {fn}")
                return
            sret, sparams = self.sigs[fn]
            bind, local = step.get("bind", {}), step.get("local", {})
            product = next((stem(p.name) for p in sparams if p.is_out and p.text.split()[0].rstrip("*") in self.reg.handles), None)
            args = []
            for p in sparams:
                ptype = p.text.rsplit(" ", 1)[0]
                if p.name in bind:
                    args.append(bind[p.name])
                elif p.name in local:
                    var = local[p.name]
                    locals_[var] = ptype.rstrip("*").strip()
                    available[stem(p.name)] = var
                    args.append(f"&{var}")
                elif p.is_out:
                    outputs.setdefault(p.name, p)
                    available[stem(p.name)] = f"(*{p.name})"
                    args.append(p.name)
                elif p.name in available:
                    args.append(available[p.name])
                else:
                    pname = p.name
                    if re.fullmatch(r"p(\w+)?Next", p.name) and product:
                        pname = "p" + product[0].upper() + product[1:] + "Next"
                    if pname not in inputs:
                        inputs[pname] = EmittedParam(f"{ptype} {pname}", pname, f"See {fn}", False)
                    args.append(pname)
            call = f"{fn}({', '.join(args)})"
            if step.get("return"):
                ret = sret
                body.append(f"return {call};")
            elif "store" in step:
                body.append(f"(*{step['store']}) = {call};")
            elif "into" in step:
                var = step["into"]
                locals_[var] = sret
                available[var.replace("_", "")] = var          # matched by stem below
                available[stem("p" + "".join(w.capitalize() for w in var.split("_")))] = var
                body.append(f"{var} = {call};")
            else:
                body.append(f"{call};")

        # Declared inputs (used only inside bind expressions) follow the step-derived ones.
        for d_ in declared:
            inputs.setdefault(d_.name, d_)
        ordered = sorted(inputs.values(), key=lambda p: (self.LEVELS.index(p.name) if p.name in self.LEVELS else len(self.LEVELS), 0))
        dispatch = [p for p in ordered if p.name in self.LEVELS]
        declared_names = {d_.name for d_ in declared}
        is_next = lambda p: bool(re.fullmatch(r"p\w*Next", p.name))
        derived = [p for p in inputs.values() if p.name not in self.LEVELS and p.name != "pAllocator" and p.name not in declared_names]
        # Derived inputs keep their order of appearance; declared inputs are inserted before the
        # trailing run of extension-chain pointers so they sit with the inputs they qualify.
        cut = len(derived)
        while cut > 0 and is_next(derived[cut - 1]):
            cut -= 1
        rest = derived[:cut] + [p for p in inputs.values() if p.name in declared_names] + derived[cut:]
        alloc = [p for p in inputs.values() if p.name == "pAllocator"]
        params = dispatch + rest + alloc + list(outputs.values())
        decls = [f"{t} {v};" for v, t in locals_.items()]
        full = decls + ([""] if decls else []) + body
        doc = self.doc_block(spec["brief"], params, ret, None, [])
        sig = self.signature(ret, name, params)
        self.sigs[name] = (ret, params)
        self.place(frozenset(), doc + sig + ";", sig + "\n{\n" + indent("\n".join(align_assignments(full))) + "\n}")

    # ---------------------------------------------------------------- run
    def run(self) -> None:
        self.flat_pairs: Dict[Tuple[str, str], int] = defaultdict(int)
        for cmd in self.cmds:
            ip = self.flip_param(cmd)
            if ip is not None:
                self.flat_pairs[(cmd.verb, self.reg.struct_of(ip.type).name)] += 1
        for cmd in self.cmds:
            self.emit_tier0(cmd)
        for cmd in self.cmds:
            ip = self.flip_param(cmd)
            if ip is not None:
                self.emit_flat(cmd, ip)
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
        # Gap 1: per-element extenders on array inputs (parallel-array form).
        for cmd in self.cmds:
            roles = self.roles(cmd)
            arrays = [p for p, r in zip(cmd.params, roles) if r == "I[]"]
            if len(arrays) != 1:
                continue
            base = self.reg.struct_of(arrays[0].type)
            if any(m.is_pointer or m.array_len for m in base.members if m.name not in ("sType", "pNext")):
                continue                         # base has arrays/pointers: parallel arrays would nest
            for ext in sorted(self.reg.extenders_of(base.name), key=lambda e: e.name):
                if not self.struct_in_profile(ext):
                    continue
                if any(m.is_pointer or m.array_len for m in ext.members if m.name not in ("sType", "pNext")):
                    continue
                self.emit_array_convenience(cmd, arrays[0], ext)
        # Gap 2: extenders on the output sType struct (chained output).
        for cmd in self.cmds:
            roles = self.roles(cmd)
            outs = [p for p, r in zip(cmd.params, roles) if r == "P"]
            if len(outs) != 1:
                continue
            base = self.reg.struct_of(outs[0].type)
            for ext in sorted(self.reg.extenders_of(base.name), key=lambda e: e.name):
                if self.struct_in_profile(ext):
                    self.emit_output_convenience(cmd, outs[0], ext)
        for f in self.FIXED:
            self.emit_fixed(f)
        for chain_name, spec in self.policy.get("chains", {}).items():
            self.emit_chain(chain_name, spec)

    PLATFORM_INCLUDES = {
        "win32": ["<windows.h>", "<vulkan/vulkan_win32.h>"],
        "xlib": ["<X11/Xlib.h>", "<vulkan/vulkan_xlib.h>"],
        "xcb": ["<xcb/xcb.h>", "<vulkan/vulkan_xcb.h>"],
        "wayland": ["<wayland-client.h>", "<vulkan/vulkan_wayland.h>"],
    }

    def platform_header(self, plat: str) -> str:
        guard = f"CVK_{plat.upper()}_H"
        includes = "\n".join(f"#include {i}" for i in self.PLATFORM_INCLUDES[plat])
        decls = "\n".join(self.decls[plat])
        return (f"#ifndef {guard}\n#define {guard}\n\n"
                "/* Generated by scripts/generate.py from the Vulkan registry. Do not edit. */\n\n"
                f"#include \"cvk.h\"\n{includes}\n\n#ifdef __cplusplus\nextern \"C\" {{\n#endif\n\n"
                f"{decls}\n\n#ifdef __cplusplus\n}}\n#endif\n\n#endif /* {guard} */\n")

    def platform_source(self, plat: str) -> str:
        defs = "\n".join(self.defs[plat])
        return ("/* Generated by scripts/generate.py from the Vulkan registry. Do not edit. */\n\n"
                f"#include \"cvk_{plat}.h\"\n#include <string.h>\n\n{defs}\n")

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

/* Sentinel for an unavailable index. */
#define VK_INVALID (UINT32_MAX)

/* Fixed enumeration capacities; override any before including. */
{caps}

{chr(10).join(self.decls[""])}

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

{chr(10).join(self.defs[""])}
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
    outputs = [os.path.join(args.out_dir, "include", "cvk.h"), os.path.join(args.out_dir, "src", "cvk.c")]
    for plat in sorted(k for k in g.decls if k):
        hp, cp = os.path.join(args.out_dir, "include", f"cvk_{plat}.h"), os.path.join(args.out_dir, "src", f"cvk_{plat}.c")
        with open(hp, "w", encoding="utf-8", newline="\n") as f:
            f.write(g.platform_header(plat))
        with open(cp, "w", encoding="utf-8", newline="\n") as f:
            f.write(g.platform_source(plat))
        outputs += [hp, cp]
    # Final formatting is clang-format's (repository .clang-format); the generator's own alignment is a fallback.
    import shutil
    import subprocess
    clang_format = shutil.which("clang-format")
    if clang_format:
        style = os.path.join(REPO_ROOT, ".clang-format")
        subprocess.run([clang_format, "-i", f"--style=file:{style}"] + outputs, check=True, cwd=args.out_dir)
    else:
        print("warning: clang-format not found; output left with generator alignment only")
    t0 = sum(1 for n in g.names.values() if n.startswith("vk") and "+" not in n)
    builders = sum(1 for n in g.names if n.startswith("cvkInit"))
    conv = sum(1 for n in g.names.values() if "+" in n)
    per = ", ".join(f"{k or 'core'}={len(v)}" for k, v in sorted(g.decls.items()))
    print(f"generated {len(outputs)} files ({per}): tier0={t0} builders={builders} conveniences={conv} capacities={len(g.used_capacities)}")


if __name__ == "__main__":
    main()
