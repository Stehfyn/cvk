"""cvk generator prototype: registry + policy -> core, JSON, and platform file pairs

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

VENDORS = ("KHR", "EXT", "NV", "NVX", "AMD", "AMDX", "INTEL", "GOOGLE", "ARM", "QCOM", "MESA", "VALVE", "HUAWEI",
           "FUCHSIA", "SEC", "LUNARG", "ANDROID", "IMG", "MSFT", "NN", "MVK", "GGP", "QNX", "OHOS", "KHX")


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


def separate_assignments_from_calls(text: str) -> str:
    """Separate calls, adjacent assignments, assertions and returns across source emitters.

    Scan statement boundaries without treating initializer braces or for-loop semicolons
    as blocks/statements. Mask literals, comments and directives so their punctuation is inert.
    """
    masked = re.sub(r'^\s*#(?:[^\n]*\\\n)*[^\n]*|/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                    lambda m: re.sub(r"[^\n]", " ", m[0]), text, flags=re.M | re.S)
    start = parens = initializer = 0
    assigned = previous_assignment = previous_call = False
    previous_statement = False
    previous_end = 0
    insertions = []
    for token in re.finditer(r"[(){};]|(?<![=!<>])=(?!=)", masked):
        symbol = token[0]
        if symbol == "(":
            parens += 1
        elif symbol == ")":
            parens -= 1
        elif symbol == "=" and not parens:
            assigned = True
        elif symbol == "{" and (initializer or assigned or parens):
            initializer += 1
        elif symbol == "}" and initializer:
            initializer -= 1
        elif symbol in ("{", "}"):
            start = token.end()
            assigned = previous_assignment = previous_call = False
            previous_statement = symbol == "}"
            previous_end = token.end()
        elif symbol == ";" and not parens and not initializer:
            statement = masked[start:token.end()]
            first = start + len(statement) - len(statement.lstrip())
            gap = text[previous_end:first]
            calls = re.findall(r"\b([A-Za-z_]\w*)\s*\(", statement)
            # Alignment arithmetic belongs to the surrounding calculation/assignment run.
            has_call = any(name not in ("if", "for", "while", "switch", "sizeof", "_Alignof", "VK_COUNT_OF",
                                       "cvkAlignDeviceSize", "scvkAlignDeviceSize") for name in calls)
            is_return = re.match(r"\s*return\b", statement) is not None
            is_assert = re.match(r"\s*(?:CVK|SCVK)_ASSERT\s*\(", statement) is not None
            separate = not is_assert and ((previous_assignment and has_call) or (previous_call and assigned) or (previous_statement and is_return))
            if separate and gap.isspace() and gap.count("\n") == 1:
                insertions.append(previous_end)
            if is_assert:
                following = re.compile(r"\s*(\S)").match(masked, token.end())
                if following:
                    next_start = following.start(1)
                    gap = text[token.end():next_start]
                    next_assert = re.compile(r"(?:CVK|SCVK)_ASSERT\b").match(masked, next_start)
                    if masked[next_start] != "}" and not next_assert and gap.isspace() and gap.count("\n") == 1:
                        insertions.append(token.end())
            previous_assignment = assigned
            previous_call = has_call
            previous_statement = True
            previous_end = start = token.end()
            assigned = False
    chunks = []
    start = 0
    for offset in sorted(set(insertions)):
        chunks.append(text[start:offset] + "\n")
        start = offset
    chunks.append(text[start:])
    return "".join(chunks)


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
        # Profile (SPEC.md section 1): the API variant (vulkan, vulkansc), the identifier prefix
        # every emitted name carries, the header the core file includes, and whether a chain or
        # protocol whose step is absent from the profile is dropped or an error.
        self.api: str = policy.get("api", "vulkan")
        self.prefix: str = policy.get("prefix", "cvk")
        self.vk_header: str = policy.get("header", "<vulkan/vulkan_core.h>")
        self.loader_header: Optional[str] = policy.get("loader_header", "<volk.h>" if self.api == "vulkan" else None)
        self.drop_missing: bool = policy.get("missing", "error") == "drop"
        self.dropped: List[str] = []
        self.core_versions: Set[str] = reg.feature_closure(self.core)
        platforms = policy.get("platforms", [])
        self.platforms: Set[str] = set(reg.platforms if platforms == "all" else platforms)
        if policy["extensions"] == "all":
            # Every extension the registry supports for the profile's API that is platform-neutral
            # or belongs to one of the policy's selected platform categories.
            self.exts: Set[str] = {e.name for e in reg.extensions.values()
                                   if self.api in e.supported and (e.platform is None or e.platform in self.platforms)}
        else:
            self.exts = set(policy["extensions"])
        self.cmds: List[Command] = reg.profile(self.core, sorted(self.exts))
        self.capacities: Dict[str, int] = dict(policy.get("capacities", {}))
        self.default_capacity = policy.get("default_capacity", 64)
        self.input_asserts: bool = bool(policy.get("input_asserts", False))
        self.doc_comments: bool = bool(policy.get("doc_comments", True))
        self.single_line_prototypes: bool = bool(policy.get("single_line_prototypes", False))
        self.designated_initializers: bool = bool(policy.get("designated_initializers", True))
        self.used_capacities: Dict[str, int] = {}
        self.names: Dict[str, str] = {}
        self.decls: Dict[str, List[str]] = defaultdict(list)   # "" = core, else platform name
        self.defs: Dict[str, List[str]] = defaultdict(list)
        self.json_decls: Dict[str, List[str]] = defaultdict(list)
        self.json_defs: Dict[str, List[str]] = defaultdict(list)
        self.json_names: Set[str] = set()
        self.json_source_includes: Dict[str, List[str]] = defaultdict(list)
        self.sigs: Dict[str, Tuple[str, List[EmittedParam]]] = {}   # every emitted signature, for chains
        self.used_overrides: Set[str] = set()
        self.keep_suffix: Set[str] = set()
        self.singular_pairs: Dict[Tuple[str, str], int] = defaultdict(int)
        self.recoverable: Set[str] = set(policy.get("recoverable_errors", []))
        # Tier 3 header fragments (scripts/protocols/*.h): types the protocols and the generated
        # JSON parsers share, folded into the header ahead of the declarations.
        self.fragments: List[str] = []
        self.json_fragments: List[str] = []
        if os.path.isdir(self.PROTOCOL_DIR):
            for path in sorted(os.listdir(self.PROTOCOL_DIR)):
                if path.endswith(".h"):
                    with open(os.path.join(self.PROTOCOL_DIR, path), encoding="utf-8") as fh:
                        fragments = self.json_fragments if path.startswith("cvk_protocol_json") else self.fragments
                        fragments.append(fh.read().rstrip("\n"))
        self.errors: List[str] = []
        for platform in sorted(self.platforms - set(reg.platforms)):
            self.errors.append(f"unknown registry platform: {platform}")
        for platform in sorted(self.platforms - set(self.PLATFORM_INCLUDES)):
            self.errors.append(f"missing header mapping for platform: {platform}")

    # ------------------------------------------------------------ profile
    def struct_in_profile(self, s: Struct) -> bool:
        return bool(s.core_version in self.core_versions or any(e in self.exts for e in s.extensions))

    def enum_value_in_profile(self, value: str) -> bool:
        """A result code (or any enum value) is usable when its defining version is in the
        profile's closure or its defining extension is in the profile; core values always."""
        if value in self.reg.removed:
            return False
        origin = self.reg.enum_provenance.get(value)
        return origin is None or origin in self.core_versions or origin in self.exts

    def type_in_profile(self, name: str) -> bool:
        """A struct in the profile; another type (enum, handle) unless an extension outside the
        profile is what defines it; scalars always."""
        s = self.reg.struct_of(name)
        if s is not None:
            return self.struct_in_profile(s)
        if name in self.reg.type_feature:
            return self.reg.type_feature[name] in self.core_versions
        origin = self.reg.type_provenance.get(name)
        return origin is None or origin in self.exts

    def text_in_profile(self, text: str) -> bool:
        """Every VK_* constant and Vk* type a policy expression names is in the profile."""
        return all(self.enum_value_in_profile(t) for t in re.findall(r"\bVK_[A-Z0-9_]+\b", text)) and \
            all(self.type_in_profile(t) for t in re.findall(r"\bVk[A-Za-z0-9]+\b", text))
    def platforms_for(self, exts: List[str], core_version: Optional[str]) -> frozenset:
        """Platforms an entity belongs to: empty when core or provided by any platform-neutral extension."""
        if core_version in self.core_versions:
            return frozenset()
        plats = set()
        for e in exts:
            x = self.reg.extensions.get(e)
            if e in self.exts and x:
                if not x.platform:
                    return frozenset()
                plats.add(x.platform)
        return frozenset(plats)

    # -------------------------------------------------------------- names
    def register(self, name: str, origin: str) -> str:
        """Final name of an emitted function. Declared exceptions in the policy's
        `name_overrides` table are applied here, so every caller uses the overridden name."""
        name = self.hoist_vendor(name)
        override = self.policy.get("name_overrides", {}).get(name)
        if override:
            self.used_overrides.add(name)
            name = override
        if name in self.names and self.names[name] != origin:
            self.errors.append(f"name collision: {name} from {origin} and {self.names[name]}")
        self.names[name] = origin
        return name

    @staticmethod
    def hoist_vendor(name: str) -> str:
        """SPEC.md section 3: a kept vendor suffix trails the whole name. A derived name is
        built from parts that may each carry a suffix (an NV command with a QCOM extender);
        the suffixes move to the end in order of appearance:
        cvkCreateImageWithExternalMemoryNVInfo -> cvkCreateImageWithExternalMemoryInfoNV."""
        t = tokens(name[3:]) if name.startswith("cvk") else tokens(name)
        tags = [x for x in t if x in VENDORS]
        if not tags:
            return name
        rest = [x for x in t if x not in VENDORS]
        return ("cvk" if name.startswith("cvk") else "") + "".join(rest + tags)

    def keeps_suffix(self, name: str) -> bool:
        """SPEC.md section 3: KHR and EXT are the promotable tags and drop; a vendor tag (NV,
        AMD, QCOM, ...) names a vendor's own feature and stays (cvkGetDisplayModeStereoPropertiesNV).
        A promotable tag stays only on a non-canonical entity of a shared stem."""
        return self.vendor_rank(name) >= 3 or name in self.keep_suffix

    def cmd_name(self, cmd: Command) -> str:
        return "cvk" + (cmd.name[2:] if self.keeps_suffix(cmd.name) else strip_vendor(cmd.name[2:]))

    def struct_short(self, s: Struct) -> str:
        return s.name[2:] if self.keeps_suffix(s.name) else strip_vendor(s.name[2:])

    @staticmethod
    def vendor_rank(name: str) -> int:
        """Promotion order: core (no suffix) before KHR before EXT before other vendors."""
        stripped = strip_vendor(name)
        if stripped == name:
            return 0
        return {"KHR": 1, "EXT": 2}.get(name[len(stripped):], 3)

    def compute_keep_suffix(self, structs: List[Struct]) -> None:
        """SPEC.md section 3: when several in-profile entities share a stem after vendor
        stripping, only the canonical one (lowest vendor rank, if unique) drops its suffix.
        An output extender's replacement name (cvkGetPhysicalDeviceExternalImageFormatProperties
        from VkExternalImageFormatProperties) competes in the command group of that name, so a
        legacy command with the same stem (vkGetPhysicalDeviceExternalImageFormatPropertiesNV)
        keeps its suffix. A tie between a command and a struct at the same rank is not settled
        here: the command owns the plain name and the output convenience takes its With form."""
        self.keep_suffix = set()
        groups: Dict[str, List[str]] = defaultdict(list)
        for c in self.cmds:
            groups["cmd:" + strip_vendor(c.name)].append(c.name)
        for st in structs:
            groups["struct:" + strip_vendor(st.name)].append(st.name)
        # A plural command reserves its singular stem (vkCmdBuildAccelerationStructuresKHR ->
        # cvkCmdBuildAccelerationStructure), so a legacy command on that stem keeps its suffix.
        for c in self.cmds:
            stem = strip_vendor(c.name)
            if stem.endswith("s") and any(r == "I[]" for r in self.roles(c)):
                groups["cmd:" + stem[:-1]].append(c.name)
        for cmd, op, ext in self.output_pairs() + self.output_array_pairs():
            spliced = self.splice_output_name(self.with_base(cmd), ext, self.reg.struct_of(op.type), [])
            if spliced:
                groups["cmd:" + strip_vendor(spliced[1:])].append(ext.name)
        for names in groups.values():
            if len(names) < 2:
                continue
            low = min(self.vendor_rank(n) for n in names)
            top = [n for n in names if self.vendor_rank(n) == low]
            if len(top) > 1 and len({n[:2] == "vk" for n in top}) == 1:
                self.keep_suffix.update(names)             # same kind, same rank: no canonical
            else:
                self.keep_suffix.update(n for n in names if self.vendor_rank(n) > low)

    def output_pairs(self) -> List[Tuple[Command, Param, Struct]]:
        """(command, output parameter, extender) for every in-profile extender of a command's
        single output sType struct (SPEC.md section 6, output extenders)."""
        out = []
        for cmd in self.cmds:
            roles = self.roles(cmd)
            outs = [p for p, r in zip(cmd.params, roles) if r == "P"]
            if len(outs) != 1:
                continue
            base = self.reg.struct_of(outs[0].type)
            for ext in sorted(self.reg.extenders_of(base.name), key=lambda e: e.name):
                if self.struct_in_profile(ext):
                    out.append((cmd, outs[0], ext))
        return out

    def extensible_sibling(self, cmd: Command) -> bool:
        """True when the command's version digit distinguishes it from an in-profile command
        that is itself extensible (vkCreateRenderPass2 beside vkCreateRenderPass, whose
        VkRenderPassCreateInfo has its own pNext): the digit is then part of the name."""
        if not re.search(r"\d$", self.cmd_name(cmd)):
            return False
        stem = re.sub(r"\d+$", "", self.cmd_name(cmd))
        for other in self.cmds:
            if other is cmd or self.cmd_name(other) != stem:
                continue
            if any(r in ("I", "I[]") for r in self.roles(other)):
                return True
        return False

    def with_base(self, cmd: Command) -> str:
        """Tier-0 name minus the command's trailing version digit: a chained extender already
        implies the extensible form (cvkBindImageMemory2 -> cvkBindImageMemoryWith...), unless
        the digit-less command is itself extensible (cvkCreateRenderPass2With... beside
        cvkCreateRenderPassWith...)."""
        if self.extensible_sibling(cmd):
            return self.cmd_name(cmd)
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
        kind = ("Info", "Create", "Allocate", "Begin", "Bind", "Submit")

        def norm(t: str) -> str:
            return t[:-1] if len(t) > 3 and t.endswith("s") else t

        e = tokens(self.struct_short(ext))
        b = tokens(self.struct_short(base))
        d = self.distinct_part(ext, base)
        if not d:
            self.errors.append(f"extender {ext.name} has no distinct part against {base.name}")
            return self.cmd_name(cmd) + "With" + self.struct_short(ext)

        cmd_tokens = tokens(self.cmd_name(cmd)[3:])
        verb, rest = cmd_tokens[0], cmd_tokens[1:]
        run = [t for t in b if t not in kind and not t.isdigit()]
        # Operation extenders (Import*/Export*): the extender names the operation, so the
        # function is cvk<Verb><ExtenderStruct minus kind/Info>, the vkImportSemaphoreFdKHR idiom:
        # cvkAllocateImportMemoryFd, cvkCreateExportSemaphoreWin32Handle. Falls back to the
        # With form when one such extender serves several bases with the same verb.
        e_op = tokens(self.struct_short(ext))
        if e_op and e_op[0] in ("Import", "Export") and self.convenience_pairs.get((verb, ext.name), 0) == 1:
            tag = e_op.pop() if e_op[-1] in VENDORS else ""
            while e_op and (e_op[-1] in kind or e_op[-1].isdigit()):
                e_op.pop()
            return "cvk" + verb + "".join(e_op) + tag
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
        # The command names the base run in full (PhysicalDevice Surface in
        # vkGetPhysicalDeviceSurfacePresentModes2EXT) or, when the run starts with the handle
        # the base struct is about and the command is dispatched elsewhere, only its tail
        # (Surface in vkGetDeviceGroupSurfacePresentModes2EXT): the run minus a leading handle
        # name is still the run. Any other qualifier (Compute in VkComputePipelineCreateInfo)
        # must be present, else the With form.
        start, sub = -1, run
        for k in range(len(nrun), 0, -1):
            if k < len(nrun) and "Vk" + "".join(run[:-k]) not in self.reg.handles:
                continue
            i = next((i for i in range(len(nrest) - k + 1) if nrest[i:i + k] == nrun[-k:]), -1)
            if i >= 0:
                start, sub = i, run[-k:]
                break
        if start < 0:
            return self.with_base(cmd) + "With" + "".join(d) + "Info"

        # Splice: the distinct part wraps the base run's unmatched remainder. Its first word
        # follows the last matched base word; the remainder of the run follows; then the rest
        # of the distinct part; then the command's tail.
        matched_norm = {norm(t) for t in e if t not in d}   # extender tokens aligned with the base
        last = max((i for i, t in enumerate(sub) if norm(t) in matched_norm), default=len(sub) - 1)
        head, run_rest = sub[: last + 1], sub[last + 1:]
        spliced = rest[:start] + head + d[:1] + run_rest + d[1:] + rest[start + len(sub):]
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
        """Parameter roles, with the policy's `role_overrides` applied ("vkCmd::param": role)
        for registry quirks such as an input struct passed through a non-const pointer."""
        overrides = self.policy.get("role_overrides", {})
        return [overrides.get(f"{cmd.name}::{p.name}", role_of(self.reg, cmd, p, cmd.params)) for p in cmd.params]

    @staticmethod
    def pnext_type(s: Struct) -> str:
        """The struct's own pNext member type: `const void*` for inputs, `void*` for outputs and
        the registry's few inputs declared without const."""
        return next((m.full_type for m in s.members if m.name == "pNext"), "const void*")

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

    def output_array_name(self, cmd: Command, ap: Param, ext: Struct) -> str:
        """SPEC.md section 6, output-array extenders: the replacement rule of the output
        extenders applied to an enumeration (cvkGetPhysicalDeviceQueueFamilyProperties2 +
        VkQueueFamilyVideoPropertiesKHR -> cvkGetPhysicalDeviceQueueFamilyVideoProperties);
        the With form when the tail is not the run or the name is a command's."""
        base = self.reg.struct_of(ap.type)
        spliced = self.splice_output_name(self.with_base(cmd), ext, base, [])
        if spliced is None or spliced in self.tier0_names:
            d_tokens = self.distinct_part(ext, base, strip_info=False)
            base_tokens = [t for t in tokens(self.struct_short(base)) if not t.isdigit()]
            kind = base_tokens[-1] if base_tokens and base_tokens[-1] in self.KINDS_OUT and base_tokens[-1] not in d_tokens else ""
            spliced = self.cmd_name(cmd) + "With" + "".join(d_tokens) + kind
        return spliced

    def output_array_pairs(self) -> List[Tuple[Command, Param, Struct]]:
        """(command, enumeration array, extender) for every in-profile extender of the element
        struct of a command's single two-call enumeration array of sType structs."""
        out = []
        for cmd in self.cmds:
            roles = self.roles(cmd)
            arrays = [p for p, r in zip(cmd.params, roles) if r == "P[]"]
            if len(arrays) != 1 or not any(r == "n*" for r in roles):
                continue
            base = self.reg.struct_of(arrays[0].type)
            for ext in sorted(self.reg.extenders_of(base.name), key=lambda e: e.name):
                if self.struct_in_profile(ext):
                    out.append((cmd, arrays[0], ext))
        return out

    def emit_tier0(self, cmd: Command, out_ext: Optional[Struct] = None) -> None:
        """Tier 0 (SPEC.md section 4). With `out_ext`, the output-array extender form
        (section 6): the caller supplies the base and extender arrays. This avoids hidden
        fixed-capacity stack storage while retaining the native two-call protocol."""
        reg = self.reg
        roles = self.roles(cmd)
        if out_ext is not None:
            ap = next(p for p, r in zip(cmd.params, roles) if r == "P[]")
            name = self.register(self.output_array_name(cmd, ap, out_ext), f"{cmd.name}+{out_ext.name}:out[]")
        else:
            name = self.register(self.cmd_name(cmd), cmd.name)
        ext_arrays: Dict[str, str] = {}                       # base array parameter -> extender array parameter
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
        form = self.policy.get("command_forms", {}).get(cmd.name, {}) if out_ext is None else {}
        bindings = form.get("bind", {})
        by_value = form.get("by_value", {})
        for unknown in (bindings.keys() | by_value.keys()) - {p.name for p in cmd.params}:
            self.errors.append(f"command form {cmd.name}: unknown parameter {unknown}")
        for duplicate in bindings.keys() & by_value.keys():
            self.errors.append(f"command form {cmd.name}: {duplicate} cannot be both bound and passed by value")

        # Two-call enumeration detection: a count parameter that sizes an output array.
        for p, r in zip(cmd.params, roles):
            if r == "n*":
                arrays = [q for q in cmd.params if q.len and q.len.split(",")[0] == p.name and q.type != "void"]
                if arrays:
                    enum_count = p
                    enum_arrays = [(q, self.capacity_name(q.name)) for q in arrays]

        for p, r in zip(cmd.params, roles):
            if p.name in bindings:
                call_args.append(self.typed_flags(p.full_type, bindings[p.name]))
            elif p.name in by_value:
                if p.pointer_depth != 1 or not p.is_const:
                    self.errors.append(f"command form {cmd.name}: {p.name} must be an input pointer")
                    return
                value = by_value[p.name]
                params.append(EmittedParam(f"{p.type} {value}", value, f"The {p.type} value", False))
                call_args.append(f"&{value}")
            elif r == "I":
                s = reg.struct_of(p.type)
                var = snake(p.name)
                stem = p.name[1:] if p.name.startswith("p") else p.name
                locals_.append(f"{s.name} {var};")
                locals_.append("")
                fields = [("sType", s.stype)]
                arrays = []
                for m in s.members:
                    if m.name == "sType":
                        continue
                    if m.name == "pNext":
                        pn = "pNext" if filled == 1 else f"p{stem}Next"
                        params.append(EmittedParam(f"{self.pnext_type(s)} {pn}", pn, f"Optional extension chain attached to {s.name}, or NULL", False))
                        fields.append(("pNext", pn))
                        continue
                    if m.array_len:
                        params.append(EmittedParam(f"const {m.full_type} {m.name}[{m.array_len}]", m.name, f"{s.name}::{m.name}", False))
                        arrays.append((m.name, m.name))
                    else:
                        params.append(EmittedParam(f"{m.full_type} {m.name}", m.name, f"{s.name}::{m.name}", False))
                        fields.append((m.name, m.name))
                    pre += self.member_assert(m, "", s.members)
                locals_ += self.struct_initialization(s, var, fields, arrays)
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
                locals_ += self.struct_initialization(s, var, [("sType", s.stype), ("pNext", pn)])
                call_args.append(f"&{var}")
                post.append(f"(*{p.name}) = {var};")
            elif p is enum_count:
                params.append(EmittedParam(f"{p.full_type} {p.name}", p.name, "Receives the number of entries written", True))
                pre.append(f"CVK_ASSERT(NULL != {p.name});")
                call_args.append(p.name)
            elif any(p is q for q, _ in enum_arrays):
                cap = dict((id(q), c) for q, c in enum_arrays)[id(p)]
                elem = p.full_type.rstrip("*").strip()
                if out_ext is not None and reg.struct_of(p.type) is not None and out_ext.name in [e.name for e in reg.extenders_of(p.type)]:
                    # Both arrays belong to the caller. The base array provides the native
                    # contiguous workspace, so large enumeration capacities never grow this frame.
                    ext_param = "p" + self.struct_short(out_ext)
                    params.append(EmittedParam(f"{elem} {p.name}[{cap}]", p.name,
                                              f"Caller-owned base workspace of {cap} entries, disjoint from the extender array", True))
                    pre.append(f"CVK_ASSERT(NULL != {p.name});")
                    params.append(EmittedParam(f"{out_ext.name} {ext_param}[{cap}]", ext_param, f"Receives up to {cap} entries of {out_ext.name}, one per element", True))
                    pre.append(f"CVK_ASSERT(NULL != {ext_param});")
                    ext_arrays[p.name] = ext_param
                    call_args.append(p.name)
                else:
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
        call = f"{fn}({', '.join(call_args)})"
        enum_args = {q.name for q, _ in enum_arrays}

        informational = [c for c in cmd.successcodes if c not in ("VK_SUCCESS",) and self.enum_value_in_profile(c)]
        recoverable = [c for c in cmd.errorcodes if c in self.recoverable and self.enum_value_in_profile(c)] if not enum_count else []
        if enum_count:
            informational = [c for c in informational if c != "VK_INCOMPLETE"]
        ret = "void"
        body: List[str] = list(locals_)
        if enum_count:
            # The count is an out-parameter (listed before the array in the signature); the wrapper
            # runs the two-call sequence and writes the entry count through it, returning void.
            # Retain the input extent independently: the native fill overwrites the count pointer.
            # The returned extent must never expose entries outside the initialized prefix.
            body.insert(0, "uint32_t capacity;")
            count_ref = f"(*{enum_count.name})"
            first = f"{fn}({', '.join('NULL' if a in enum_args else a for a in call_args)})"
            body += ["", f"{count_ref} = 0;", ""]
            if cmd.return_type == "VkResult":
                body.append(f"CVK_CHECK({first});")
            else:
                body.append(f"{first};")
            body.append(f"capacity = {count_ref};")
            for q, cap in enum_arrays:
                body.append(f"CVK_ASSERT({cap} >= capacity);")
                s = reg.struct_of(q.type)
                if s and s.stype:
                    arr = q.name
                    if "uint32_t index;" not in body:
                        body.insert(0, "uint32_t index;")
                    body.append("for (index = 0; index < capacity; ++index)")
                    body.append("{")
                    if q.name in ext_arrays:
                        ext_param = ext_arrays[q.name]
                        body += ["    " + line for line in self.struct_initialization(
                            s, f"{arr}[index]", [("sType", s.stype), ("pNext", f"&{ext_param}[index]")])]
                        body.append(f"    {ext_param}[index].sType = {out_ext.stype};")
                        body.append(f"    {ext_param}[index].pNext = NULL;")
                    else:
                        body.append(f"    {arr}[index].sType = {s.stype};")
                        body.append(f"    {arr}[index].pNext = NULL;")
                    body.append("}")
                    body.append("")
            if cmd.return_type == "VkResult":
                body.append(f"CVK_CHECK({call});")
            else:
                body.append(f"{call};")
            body.append(f"CVK_ASSERT(capacity >= {count_ref});")
            body += post
        elif cmd.return_type == "VkResult" and (informational or recoverable):
            # Result policy: an informational success code or a recoverable error (policy list:
            # a support query's NOT_SUPPORTED, a swapchain's OUT_OF_DATE) is the caller's to read.
            ret = "VkResult"
            body.insert(0, "VkResult result;")
            body.append(f"result = {call};")
            body.append("CVK_ASSERT(" + " || ".join(["(0 <= result)"] + [f"({c} == result)" for c in recoverable]) + ");")
            if post and recoverable:
                body += ["if (0 <= result)", "{"] + ["    " + l for l in post] + ["}"]
            else:
                body += post
            body.append("return result;")
        elif cmd.return_type == "VkResult":
            body.append(f"CVK_CHECK({call});")
            body += post
        elif cmd.return_type == "void":
            body.append(f"{call};")
            body += post
        elif cmd.return_type == "PFN_vkVoidFunction":
            # Explicit procedure queries keep their non-null result contract.
            ret = cmd.return_type
            body.insert(0, "PFN_vkVoidFunction function;")
            body.append(f"function = {call};")
            body.append("CVK_ASSERT(NULL != function);")
            body.append("return function;")
        else:
            ret = cmd.return_type
            body.append(f"return {call};")

        # Order: declarations first, then asserts, then statements.
        decl_lines = [l for l in body if re.match(r"^(PFN_vkVoidFunction function;|VkResult result;|uint32_t \w+;|Vk\w+ \w+(\[\w+\])?;)", l)]
        other = [l for l in body if l not in decl_lines]

        # Declarations in order of first access in the statements that follow.
        def first_use(decl: str) -> int:
            var = re.match(r"^\S+\s+(\w+)", decl).group(1)
            return next((i for i, l in enumerate(other) if re.search(rf"\b{var}\b", l)), len(other))
        decl_lines.sort(key=first_use)
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

        brief = form.get("brief", self.brief(cmd))
        plats = self.platforms_for(cmd.extensions, cmd.core_version)
        if out_ext is not None:
            brief = f"{brief[:-1]} with {out_ext.name} chained to every element."
            plats = plats | self.platforms_for(out_ext.extensions, out_ext.core_version)
        doc = self.doc_block(brief, params, ret, cmd, informational + recoverable if ret == "VkResult" else [])
        sig = self.signature(ret, name, params)
        self.sigs[name] = (ret, params)
        self.place(plats, doc + sig + ";", sig + "\n{\n" + indent("\n".join(align_assignments(body))) + "\n}")

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
        if not self.doc_comments:
            return ""
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

    def place(self, plats: frozenset, decl: str, defn: str, *, json: bool = False) -> None:
        """Route an emitted function to the core files or to exactly one platform's files."""
        if len(plats) > 1:
            self.errors.append(f"entity spans several platforms {sorted(plats)}: {decl.splitlines()[-1]}")
            return
        key = next(iter(plats)) if plats else ""
        if self.designated_initializers:
            pattern = (r"(?m)^([ \t]*)(Vk\w+)[ \t]+(\w+);[ \t]*\n(?:[ \t]*\n)*"
                       r"\1\3[ \t]*=[ \t]*\(\2\)[ \t]*\n\1\{")
            defn = re.sub(pattern, r"\1\2 \3 =\n\1{", defn)
        if not self.doc_comments:
            # Protocol metadata is parsed before placement; suppress its copied docs too.
            decl = re.sub(r"/\*\*.*?\*/\s*", "", decl, flags=re.S)
            defn = re.sub(r"/\*\*.*?\*/\s*", "", defn, flags=re.S)
        defn = separate_assignments_from_calls(defn)
        if json:
            self.json_decls[key].append(decl)
            self.json_defs[key].append(defn)
            self.json_names.update(re.findall(r"\b(cvk\w+)\s*\(", decl))
        else:
            self.decls[key].append(decl)
            self.defs[key].append(defn)

    def place_json(self, plats: frozenset, decl: str, defn: str) -> None:
        self.place(plats, decl, defn, json=True)

    # ------------------------------------------------------------ builders
    @staticmethod
    def designated_initializer(start: str, values: List[Tuple[str, str]]) -> List[str]:
        width = max(len(name) for name, _ in values)
        entries = [f"    .{name.ljust(width)} = {value}" for name, value in values]
        return [start, "{"] + ",\n".join(entries).splitlines() + ["};"]

    def struct_initialization(self, s: Struct, target: str, fields: List[Tuple[str, str]],
                              arrays: Optional[List[Tuple[str, str]]] = None) -> List[str]:
        """Initialize wrapper-owned storage, including each element of a local array."""
        arrays = arrays or []
        if self.designated_initializers:
            body = self.designated_initializer(f"{target} = ({s.name})", fields + [(name, "{0}") for name, _ in arrays])
        else:
            body = [f"{target}.{name} = {value};" for name, value in fields]
        return body + [f"(void)memcpy({target}.{name}, {value}, sizeof({target}.{name}));" for name, value in arrays]

    def designated_builder_body(self, s: Struct, fields: List[Tuple[str, str]], arrays: List[Param]) -> List[str]:
        """Initialize the complete value, copying array arguments before writing pInfo."""
        values = fields + [(m.name, "{0}") for m in arrays]
        if not arrays:
            return self.designated_initializer(f"(*pInfo) = ({s.name})", values)
        # A temporary also preserves array arguments that point inside the destination.
        local = "initialized_info"
        while local in {m.name for m in s.members}:
            local += "_"
        return (self.designated_initializer(f"{s.name} {local} =", values)
                + [f"(void)memcpy({local}.{m.name}, {m.name}, sizeof({local}.{m.name}));" for m in arrays]
                + [f"(*pInfo) = {local};"])

    def emit_builder(self, s: Struct) -> None:
        name = self.register("cvkInit" + self.struct_short(s), s.name)
        params = [EmittedParam(f"{s.name}* pInfo", "pInfo", f"Receives the initialized {s.name}", True)]
        body = (["CVK_ASSERT(NULL != pInfo);", ""] if self.input_asserts else [])
        fields: List[Tuple[str, str]] = []
        arrays: List[Param] = []
        if s.stype:
            pnext_type = next((m.full_type for m in s.members if m.name == "pNext"), "const void*")
            params.append(EmittedParam(f"{pnext_type} pNext", "pNext", f"Optional extension chain attached to {s.name}, or NULL", False))
            body += [f"pInfo->sType = {s.stype};", "pInfo->pNext = pNext;"]
            fields += [("sType", s.stype), ("pNext", "pNext")]
        if not s.returnedonly:
            for m in s.members:
                if m.name in ("sType", "pNext"):
                    continue
                if m.array_len:
                    params.append(EmittedParam(f"const {m.full_type} {m.name}[{m.array_len}]", m.name, f"{s.name}::{m.name}", False))
                    body.append(f"(void)memcpy(pInfo->{m.name}, {m.name}, sizeof(pInfo->{m.name}));")
                    arrays.append(m)
                else:
                    params.append(EmittedParam(f"{m.full_type} {m.name}", m.name, f"{s.name}::{m.name}", False))
                    body.append(f"pInfo->{m.name} = {m.name};")
                    fields.append((m.name, m.name))
        # Returned-only builders set chain headers without overwriting output members.
        if self.designated_initializers and not s.returnedonly:
            body = (["CVK_ASSERT(NULL != pInfo);", ""] if self.input_asserts else [])
            body += self.designated_builder_body(s, fields, arrays)
        doc = self.doc_block(f"Initializes a {s.name}.", params, "void", None, [])
        sig = self.signature("void", name, params)
        self.sigs[name] = ("void", params)
        self.place(self.platforms_for(s.extensions, s.core_version), doc + sig + ";",
                   sig + "\n{\n" + indent("\n".join(align_assignments(body))) + "\n}")

    def emit_union_builders(self, s: Struct) -> None:
        """SPEC.md section 6: a union has one builder per member, cvkInit<Union><Member>, each
        setting that member (cvkInitClearValueColor, cvkInitDeviceOrHostAddressDeviceAddress,
        cvkInitAccelerationStructureGeometryDataTriangles)."""
        for m in s.members:
            cap = m.name[0].upper() + m.name[1:]
            name = self.register("cvkInit" + self.struct_short(s) + cap, f"{s.name}::{m.name}")
            params = [EmittedParam(f"{s.name}* pInfo", "pInfo", f"Receives the initialized {s.name}", True)]
            if m.array_len:
                params.append(EmittedParam(f"const {m.full_type} {m.name}[{m.array_len}]", m.name, f"{s.name}::{m.name}", False))
                body = [f"(void)memcpy(pInfo->{m.name}, {m.name}, sizeof(pInfo->{m.name}));"]
            else:
                params.append(EmittedParam(f"{m.full_type} {m.name}", m.name, f"{s.name}::{m.name}", False))
                body = [f"pInfo->{m.name} = {m.name};"]
            if self.designated_initializers:
                body = self.designated_builder_body(s, [] if m.array_len else [(m.name, m.name)], [m] if m.array_len else [])
            doc = self.doc_block(f"Initializes a {s.name} holding its {m.name} member.", params, "void", None, [])
            sig = self.signature("void", name, params)
            self.sigs[name] = ("void", params)
            self.place(self.platforms_for(s.extensions, s.core_version), doc + sig + ";",
                       sig + "\n{\n" + indent("\n".join(align_assignments(body))) + "\n}")

    # ---------------------------------------------------------- JSON parsers
    JSON_UNSIGNED = ("uint32_t", "uint64_t", "uint16_t", "uint8_t", "size_t", "VkDeviceSize", "VkDeviceAddress", "VkFlags", "VkFlags64", "VkSampleMask")
    JSON_SIGNED = ("int32_t", "int64_t", "int16_t", "int8_t", "int")
    JSON_REAL = ("float", "double")

    def enum_short(self, name: str) -> str:
        return name[2:] if self.vendor_rank(name) >= 3 else strip_vendor(name[2:])

    def json_type_required(self, name: str) -> bool:
        """An enumeration or flag type whose headers the profile has: required by a version of
        the closure or by an extension of the profile (an unrequired block, or one of another
        API variant, is not in the headers)."""
        if name in self.reg.removed:
            return False
        if name in self.reg.type_feature:
            return self.reg.type_feature[name] in self.core_versions
        origin = self.reg.type_provenance.get(name)
        return origin is not None and origin in self.exts

    def json_type_platforms(self, name: str) -> frozenset:
        origin = self.reg.type_provenance.get(name)
        return self.platforms_for([origin], None) if origin else frozenset()

    def json_enum_values(self, enum: str) -> List[str]:
        """The enumerants of a type the profile's headers define: the type's own block plus the
        values features and extensions of the profile add; aliases included."""
        seen: List[str] = []
        for v in self.reg.enums.get(enum, []) + self.reg.enum_extends.get(enum, []):
            if v not in seen and self.enum_value_in_profile(v):
                seen.append(v)
        return seen

    def json_scalar(self, ctype: str, src: str) -> Optional[str]:
        """Expression reading a scalar of a C type from a JSON value, or None for a type JSON does not carry."""
        while ctype in self.reg.type_aliases:                  # VkSemaphoreTypeKHR -> VkSemaphoreType
            ctype = self.reg.type_aliases[ctype]
        if ctype == "VkBool32":
            return f"cvkGetJsonBool32({src})"
        if ctype in self.reg.enums:
            return f"cvkParseJson{self.enum_short(ctype)}({src})" if self.json_type_required(ctype) else f"({ctype})cvkGetJsonInt64({src})"
        if ctype in self.reg.bitmasks:
            return f"cvkParseJson{self.enum_short(ctype)}({src})" if self.json_type_required(ctype) else f"({ctype})cvkGetJsonUint64({src})"
        if ctype in self.reg.handles:
            return f"({ctype})(uintptr_t)cvkResolveJsonHandle(pContext, \"{ctype}\", {src})"
        if ctype in self.JSON_UNSIGNED:
            return f"({ctype})cvkParseJsonUint64({src})"
        if ctype in self.JSON_SIGNED:
            return f"({ctype})cvkParseJsonInt64({src})"
        if ctype in self.JSON_REAL:
            return f"({ctype})cvkParseJsonDouble({src})"
        return None

    def json_member_statements(self, m: Param, dst: str) -> Tuple[List[str], bool, bool]:
        """Statements assigning member `dst` (an lvalue expression) from the JSON `value`.
        Returns (statements, needs_index, needs_storage); an empty list means the member is
        not carried by JSON (data pointers, function pointers, platform types)."""
        t = m.type
        s = self.reg.struct_of(t)
        struct_ok = s is not None and self.struct_in_profile(s) and not any(":" in x.full_type for x in s.members)
        if m.name == "pNext":
            return [f"{dst} = cvkParseJsonStructureChain(pContext, value);"], False, False
        if m.array_len:
            dims = [d for d in m.array_len.split("][")]
            if t == "char" and len(dims) == 1:
                return ["if (CVK_JSON_STRING == value->kind)", "{", f"    (void)memcpy({dst}, value->pText, (value->textLength < sizeof({dst})) ? value->textLength : sizeof({dst}) - 1);", "}"], False, False
            elem = self.json_scalar(t, "cvkGetJsonElement(&pContext->document, value, index)")
            if elem is None and struct_ok:
                elem = None
                body = [f"cvkParseJson{self.struct_short(s)}(pContext, cvkGetJsonElement(&pContext->document, value, index), &{dst}[index]);"]
            elif elem is None:
                return [], False, False
            else:
                body = [f"{dst}[index] = {elem};"]
            if len(dims) == 1:
                return ["if (CVK_JSON_ARRAY == value->kind)", "{",
                        f"    for (index = 0; index < value->childCount && index < {dims[0]}; ++index)", "    {"] + ["        " + l for l in body] + ["    }", "}"], True, False
            # a matrix: rows of arrays
            row_elem = self.json_scalar(t, "cvkGetJsonElement(&pContext->document, cvkGetJsonElement(&pContext->document, value, index), inner)")
            return ["if (CVK_JSON_ARRAY == value->kind)", "{",
                    f"    for (index = 0; index < value->childCount && index < {dims[0]}; ++index)", "    {",
                    f"        for (inner = 0; inner < {dims[1]}; ++inner)", "        {",
                    f"            {dst}[index][inner] = {row_elem};", "        }", "    }", "}"], True, False
        if m.pointer_depth == 0:
            if struct_ok:
                return [f"cvkParseJson{self.struct_short(s)}(pContext, value, &{dst});"], False, False
            e = self.json_scalar(t, "value")
            return ([f"{dst} = {e};"], False, False) if e else ([], False, False)
        # pointers
        if t == "void" or t.startswith("PFN_"):
            return [], False, False
        if t == "char" and m.pointer_depth == 1:
            return [f"{dst} = cvkCopyJsonString(pContext->pArena, value);"], False, False
        if t == "char" and m.pointer_depth == 2:
            return ["if (CVK_JSON_ARRAY == value->kind)", "{",
                    "    storage = cvkAllocateArena(pContext->pArena, value->childCount * sizeof(const char*), 16);",
                    "    for (index = 0; index < value->childCount; ++index)", "    {",
                    "        ((const char**)storage)[index] = cvkCopyJsonString(pContext->pArena, cvkGetJsonElement(&pContext->document, value, index));",
                    "    }", f"    {dst} = ({'const ' if m.is_const else ''}char* const*)storage;", "}"], True, True
        if m.pointer_depth == 2 and struct_ok:
            n = s.name
            return ["if (CVK_JSON_ARRAY == value->kind)", "{",
                    f"    storage = cvkAllocateArena(pContext->pArena, value->childCount * sizeof({n}*), 16);",
                    "    for (index = 0; index < value->childCount; ++index)", "    {",
                    f"        (({n}**)storage)[index] = ({n}*)cvkAllocateArena(pContext->pArena, sizeof({n}), 16);",
                    f"        cvkParseJson{self.struct_short(s)}(pContext, cvkGetJsonElement(&pContext->document, value, index), (({n}**)storage)[index]);",
                    "    }", f"    {dst} = ({'const ' if m.is_const else ''}{n}* const*)storage;", "}"], True, True
        if m.pointer_depth != 1:
            return [], False, False
        elem = self.json_scalar(t, "cvkGetJsonElement(&pContext->document, value, index)")
        if struct_ok and not m.len:
            n = s.name
            return [f"storage = cvkAllocateArena(pContext->pArena, sizeof({n}), 16);",
                    f"cvkParseJson{self.struct_short(s)}(pContext, value, ({n}*)storage);",
                    f"{dst} = ({'const ' if m.is_const else ''}{n}*)storage;"], False, True
        if struct_ok:
            n = s.name
            return ["if (CVK_JSON_ARRAY == value->kind)", "{",
                    f"    storage = cvkAllocateArena(pContext->pArena, value->childCount * sizeof({n}), 16);",
                    "    for (index = 0; index < value->childCount; ++index)", "    {",
                    f"        cvkParseJson{self.struct_short(s)}(pContext, cvkGetJsonElement(&pContext->document, value, index), &(({n}*)storage)[index]);",
                    "    }", f"    {dst} = ({'const ' if m.is_const else ''}{n}*)storage;", "}"], True, True
        if elem is not None:
            const = "const " if m.is_const else ""
            return ["if (CVK_JSON_ARRAY == value->kind)", "{",
                    f"    storage = cvkAllocateArena(pContext->pArena, value->childCount * sizeof({t}), 16);",
                    "    for (index = 0; index < value->childCount; ++index)", "    {",
                    f"        (({t}*)storage)[index] = {elem};",
                    "    }", f"    {dst} = ({const}{t}*)storage;", "}"], True, True
        return [], False, False

    def json_name_table(self, values: List[str], dynamic: bool) -> List[str]:
        # Vulkan's 64-bit flag bits are const objects, not C constant expressions.
        # Assign their values after declaring the automatic table.
        if dynamic:
            lines = [f"cvkJsonName table[{len(values)}];", ""]
            for i, value in enumerate(values):
                lines += [f'table[{i}].pName = "{value}";', f"table[{i}].value = (uint64_t){value};"]
            return lines + [""]
        entries = []
        for value in values:
            expression = value
            origin = self.reg.enum_provenance.get(value)
            if value in self.reg.constants and self.platforms_for([origin], None):
                # Numeric readers live in core even when a named constant belongs to a
                # platform header. Preserve its value without requiring that native SDK.
                expression = f"({self.reg.constants[value]})({self.reg.constant_values[value]})"
            entries.append(f'    {{"{value}", (uint64_t){expression}}}')
        return ["static const cvkJsonName table[] = {"] + ",\n".join(entries).splitlines() + ["};", ""]

    def emit_json_parsers(self) -> None:
        """SPEC.md section 8c: cvkParseJson<Type> for every enumeration, flag set, structure, and
        union of the profile, and cvkParseJsonStructureChain dispatching on sType. The JSON
        conventions are the Vulkan SC pipeline JSON's: enumerants and bits by name, flags as
        names joined by "|", pointers as arrays or objects, "NULL" or null for none, handles as
        object ids, VkBool32 as VK_TRUE/VK_FALSE."""
        ctx = "const cvkJsonContext* pContext"
        # numbers that may be written as API constants (VK_SUBPASS_EXTERNAL, VK_WHOLE_SIZE, VK_LOD_CLAMP_NONE)
        # a constant is in the profile when a version or extension of the profile requires it
        ints = [n for n, t in self.reg.constants.items() if t in ("uint32_t", "uint64_t") and n in self.reg.enum_provenance and self.enum_value_in_profile(n)]
        reals = [n for n, t in self.reg.constants.items() if t == "float" and n in self.reg.enum_provenance and self.enum_value_in_profile(n)]
        for fname, ret, reader, table, cast in (("cvkParseJsonUint64", "uint64_t", "cvkGetJsonUint64", ints, "(uint64_t)"),
                                                ("cvkParseJsonInt64", "int64_t", "cvkGetJsonInt64", ints, "(int64_t)(uint64_t)"),
                                                ("cvkParseJsonDouble", "double", "cvkGetJsonDouble", reals, "(double)")):
            name = self.register(fname, "json:" + fname)
            params = [EmittedParam("const cvkJsonValue* pValue", "pValue", "The value: a number, or the name of an API constant", False)]
            body = self.json_name_table(table, False) + ["uint64_t found;", "",
                    "if ((NULL != pValue) && (CVK_JSON_STRING == pValue->kind) && cvkLookupJsonName(table, (uint32_t)(sizeof(table) / sizeof(table[0])), pValue->pText, pValue->textLength, &found))",
                    "{", f"    return {cast}found;" if cast != "(double)" else "    return (double)(float)found;", "}",
                    f"return {reader}(pValue);"]
            if cast == "(double)":
                # float constants are stored as their bit-exact float value: the table holds (uint64_t)(float) which truncates 1000.0F to 1000
                body = self.json_name_table(table, False) + ["uint64_t found;", "",
                        "if ((NULL != pValue) && (CVK_JSON_STRING == pValue->kind) && cvkLookupJsonName(table, (uint32_t)(sizeof(table) / sizeof(table[0])), pValue->pText, pValue->textLength, &found))",
                        "{", "    return (double)found;", "}", f"return {reader}(pValue);"]
            doc = self.doc_block(f"Reads a number from JSON, accepting the name of an API constant.", params, ret, None, [])
            sig = self.signature(ret, name, params)
            self.sigs[name] = (ret, params)
            self.place_json(frozenset(), doc + sig + ";", sig + "\n{\n" + indent("\n".join(body)) + "\n}")
        # enumerations and flag bits
        for enum in sorted(self.reg.enums):
            if not enum.startswith("Vk") or not self.json_type_required(enum) or enum in self.reg.type_aliases:
                continue
            values = self.json_enum_values(enum)
            name = self.register("cvkParseJson" + self.enum_short(enum), f"json:{enum}")
            params = [EmittedParam("const cvkJsonValue* pValue", "pValue", "The value: an enumerant name or a number, or NULL", False)]
            body = []
            if values:
                body += self.json_name_table(values, enum in self.reg.enums64)
                body.append(f"return ({enum})cvkParseJsonEnumerant(pValue, table, (uint32_t)(sizeof(table) / sizeof(table[0])));")
            else:
                body.append(f"return ({enum})cvkGetJsonInt64(pValue);")
            doc = self.doc_block(f"Parses a {enum} from JSON.", params, enum, None, [])
            sig = self.signature(enum, name, params)
            self.sigs[name] = (enum, params)
            self.place_json(self.json_type_platforms(enum), doc + sig + ";", sig + "\n{\n" + indent("\n".join(body)) + "\n}")
        # flag sets
        for flags in sorted(self.reg.bitmasks):
            if not flags.startswith("Vk") or not self.json_type_required(flags) or flags in self.reg.type_aliases:
                continue
            bits = self.reg.bitmasks[flags]
            values = self.json_enum_values(bits) if bits and self.json_type_required(bits) else []
            name = self.register("cvkParseJson" + self.enum_short(flags), f"json:{flags}")
            params = [EmittedParam("const cvkJsonValue* pValue", "pValue", "The value: bit names joined by |, a number, or NULL", False)]
            body = []
            if values:
                body += self.json_name_table(values, bits in self.reg.enums64)
                body.append(f"return ({flags})cvkParseJsonFlags(pValue, table, (uint32_t)(sizeof(table) / sizeof(table[0])));")
            else:
                body.append(f"return ({flags})cvkGetJsonUint64(pValue);")
            doc = self.doc_block(f"Parses a {flags} from JSON.", params, flags, None, [])
            sig = self.signature(flags, name, params)
            self.sigs[name] = (flags, params)
            self.place_json(self.json_type_platforms(flags), doc + sig + ";", sig + "\n{\n" + indent("\n".join(body)) + "\n}")
        # structures and unions
        parsed: List[Struct] = []
        for s in sorted(self.reg.structs.values(), key=lambda x: x.name):
            if s.alias_of or not self.struct_in_profile(s) or s.name in ("VkBaseInStructure", "VkBaseOutStructure"):
                continue
            name = self.register("cvkParseJson" + self.struct_short(s), f"json:{s.name}")
            params = [EmittedParam(ctx, "pContext", "Parsing context: document, arena, handle resolver", False),
                      EmittedParam("const cvkJsonValue* pObject", "pObject", f"The JSON object of the {s.name}", False),
                      EmittedParam(f"{s.name}* pInfo", "pInfo", f"Receives the parsed {s.name}, zeroed first", True)]
            body: List[str] = []
            needs_index = needs_storage = needs_inner = False
            members = []
            for m in s.members:
                if m.name == "sType":
                    continue
                stmts, ni, ns = self.json_member_statements(m, f"pInfo->{m.name}")
                if not stmts:
                    continue
                needs_index |= ni
                needs_storage |= ns
                needs_inner |= any("inner" in l for l in stmts)
                members.append((m, stmts))
            body.append(f"(void)memset(pInfo, 0, sizeof(*pInfo));")
            if s.stype:
                body.append(f"pInfo->sType = {s.stype};")
            for m, stmts in members:
                body.append(f"value = cvkGetJsonMember(&pContext->document, pObject, \"{m.name}\");")
                body.append("if (!cvkIsJsonNull(value))")
                body.append("{")
                body += ["    " + l for l in stmts]
                if s.category == "union":
                    body.append("    return;")
                body.append("}")
            decls = ["const cvkJsonValue* value;"] + (["void* storage;"] if needs_storage else []) + (["uint32_t index;"] if needs_index else []) + (["uint32_t inner;"] if needs_inner else [])
            if not members:
                decls = []
                body = [l for l in body if not l.startswith("value =")] + ["(void)pContext;", "(void)pObject;"]
            doc = self.doc_block(f"Parses a {s.name} from a JSON object; members absent or NULL stay zero.", params, "void", None, [])
            sig = self.signature("void", name, params)
            self.sigs[name] = ("void", params)
            self.place_json(self.platforms_for(s.extensions, s.core_version), doc + sig + ";",
                       sig + "\n{\n" + indent("\n".join(decls + ([""] if decls else []) + body)) + "\n}")
            if s.stype and not self.platforms_for(s.extensions, s.core_version):
                parsed.append(s)
        # the extension chain dispatcher: every sType structure of the core profile
        name = self.register("cvkParseJsonStructureChain", "json:chain")
        params = [EmittedParam(ctx, "pContext", "Parsing context", False),
                  EmittedParam("const cvkJsonValue* pObject", "pObject", "The JSON object; its sType member selects the structure", False)]
        body = ["const cvkJsonValue* s_type;", "void*               storage;", "",
                "s_type = cvkGetJsonMember(&pContext->document, pObject, \"sType\");"]
        for s in parsed:
            body += [f"if (cvkJsonStringEquals(s_type, \"{s.stype}\"))", "{",
                     f"    storage = cvkAllocateArena(pContext->pArena, sizeof({s.name}), 16);",
                     f"    cvkParseJson{self.struct_short(s)}(pContext, pObject, ({s.name}*)storage);",
                     "    return storage;", "}"]
        body += ["cvkFatal(\"unknown sType in a JSON extension chain\", __FILE__, __LINE__);", "return NULL;"]
        doc = self.doc_block("Parses one structure of an extension chain from a JSON object by its sType, into the arena.", params, "void*", None, [])
        sig = self.signature("void*", name, params)
        self.sigs[name] = ("void*", params)
        self.place_json(frozenset(), doc + sig + ";", sig + "\n{\n" + indent("\n".join(body)) + "\n}")

    # ------------------------------------------------------------ singulars
    def singular_name(self, cmd: Command, s: Optional[Struct]) -> str:
        """SPEC.md section 7. A plural command name is depluralized (vkCreateComputePipelines ->
        cvkCreateComputePipeline, vkWaitForFences -> cvkWaitForFence); otherwise the element
        struct names the form, verb removed once (vkQueueSubmit -> cvkQueueSubmitInfo,
        vkBindImageMemory2 -> cvkBindImageMemoryInfo)."""
        base = self.with_base(cmd)
        if base.endswith("s"):
            return base[:-1]
        if s is not None:
            if self.singular_pairs.get((cmd.verb, s.name), 0) > 1:
                return self.cmd_name(cmd) + "Info"          # vkCmdBegin/EndTransformFeedback2EXT share one element struct
            t = tokens(self.struct_short(s))
            if cmd.verb in t:
                t.remove(cmd.verb)
            return "cvk" + cmd.verb + "".join(t)
        return base + "Single"

    def singular_struct(self, cmd: Command) -> Optional[Struct]:
        """The element struct of the command's single input array, when it has a singular form."""
        roles = self.roles(cmd)
        arrays = [(p, r) for p, r in zip(cmd.params, roles) if r in ("I[]", "H[]", "H[]out") and p.len and "," not in p.len and "->" not in p.len]
        others = [p for p, r in zip(cmd.params, roles) if r in ("S[]", "s[]", "P[]", "I", "n*")]
        if not arrays or others:
            return None
        ins = [(p, r) for p, r in arrays if r != "H[]out"]
        if len(ins) != 1 or ins[0][1] != "I[]":
            return None
        return self.reg.struct_of(ins[0][0].type)

    def emit_singular(self, cmd: Command) -> None:
        roles = self.roles(cmd)
        arrays = [(p, r) for p, r in zip(cmd.params, roles) if r in ("I[]", "H[]", "H[]out") and p.len and "," not in p.len and "->" not in p.len]
        others = [p for p, r in zip(cmd.params, roles) if r in ("S[]", "s[]", "P[]", "I", "n*")]
        if not arrays or others:
            return
        ins = [(p, r) for p, r in arrays if r != "H[]out"]
        outs = [(p, r) for p, r in arrays if r == "H[]out"]
        if len(ins) != 1:
            return
        ap, ar = ins[0]
        count = ap.len
        if not any(q.name == count for q in cmd.params):
            return
        if outs and any(o.len != count for o, _ in outs):
            return
        s = self.reg.struct_of(ap.type) if ar == "I[]" else None
        if s is None:
            # A handle singular names its element after the array; skip when that name is taken
            # by another parameter (vkCmdExecuteCommands: commandBuffer is the dispatch handle).
            elem_name = snake(ap.name)
            elem_name = elem_name[:-1] if elem_name.endswith("s") else elem_name
            elem_name = elem_name.split("_")[0] + "".join(w.capitalize() for w in elem_name.split("_")[1:])
            if any(q.name == elem_name for q in cmd.params):
                return
        name = self.register(self.singular_name(cmd, s), f"{cmd.name}:singular")
        t0 = self.cmd_name(cmd)
        ret, t0_params = self.sigs[t0]
        params: List[EmittedParam] = []
        args: List[str] = []
        body: List[str] = []
        var = snake(ap.name)[:-1] if snake(ap.name).endswith("s") else snake(ap.name)
        for p in t0_params:
            if p.name == count:
                args.append("1")
            elif p.name == ap.name and s is not None:
                body += [f"{s.name} {var};", ""]
                fields = [("sType", s.stype)]
                arrays = []
                for m in s.members:
                    if m.name == "sType":
                        continue
                    if m.name == "pNext":
                        params.append(EmittedParam(f"{self.pnext_type(s)} pNext", "pNext", f"Optional extension chain attached to {s.name}, or NULL", False))
                        fields.append(("pNext", "pNext"))
                    elif m.array_len:
                        params.append(EmittedParam(f"const {m.full_type} {m.name}[{m.array_len}]", m.name, f"{s.name}::{m.name}", False))
                        arrays.append((m.name, m.name))
                    else:
                        params.append(EmittedParam(f"{m.full_type} {m.name}", m.name, f"{s.name}::{m.name}", False))
                        fields.append((m.name, m.name))
                body += self.struct_initialization(s, var, fields, arrays)
                args.append(f"&{var}")
            elif p.name == ap.name:
                elem = ap.type
                single = snake(ap.name)
                single = single[:-1] if single.endswith("s") else single
                cname = single.split("_")
                cname = cname[0] + "".join(w.capitalize() for w in cname[1:])
                params.append(EmittedParam(f"{elem} {cname}", cname, f"The single {elem}", False))
                args.append(f"&{cname}")
            elif any(p.name == o.name for o, _ in outs):
                o = next(o for o, _ in outs if o.name == p.name)
                oname = o.name[:-1] if o.name.endswith("s") else o.name
                params.append(EmittedParam(f"{o.type}* {oname}", oname, f"Receives the created {o.type}", True))
                args.append(oname)
            else:
                params.append(p)
                args.append(p.name)
        call = f"{t0}({', '.join(args)})"
        body.append(("return " if ret != "void" else "") + call + ";")
        doc = self.doc_block(f"{self.brief(cmd)[:-1]} for one element.", params, ret, None, [])
        sig = self.signature(ret, name, params)
        self.sigs[name] = (ret, params)
        self.place(self.platforms_for(cmd.extensions, cmd.core_version), doc + sig + ";",
                   sig + "\n{\n" + indent("\n".join(align_assignments(body))) + "\n}")

    # -------------------------------------------------------- conveniences
    def struct_local_name(self, s: Struct, used: Set[str], plural: bool = False) -> str:
        parts = tokens(strip_vendor(s.name[2:]))
        while parts and parts[-1].isdigit():
            parts.pop()
        if parts[-1] == "Info":
            start = max(0, len(parts) - 2)
            while start > 0 and parts[start].isdigit():
                start -= 1
            role = snake("".join(parts[start:]))
        elif parts[-1] in self.KINDS_OUT:
            role = snake(parts[-1])
        else:
            role = snake("".join(parts))
        if plural and not role.endswith("s"):
            role += "s"
        candidate = role
        if candidate in used:
            candidate = "ext_" + role
        suffix = 2
        while candidate in used:
            candidate = f"ext_{role}_{suffix}"
            suffix += 1
        return candidate

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
        body: List[str] = [f"{s.name} {var};", ""]
        fields = [("sType", s.stype)]
        arrays = []
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
                    params.append(EmittedParam(f"{self.pnext_type(s)} {pn}", pn, f"Optional extension chain attached to {s.name}, or NULL", False))
                    fields.append(("pNext", pn))
                elif m.array_len:
                    params.append(EmittedParam(f"const {m.full_type} {m.name}[{m.array_len}]", m.name, f"{s.name}::{m.name}", False))
                    arrays.append((m.name, m.name))
                else:
                    params.append(EmittedParam(f"{m.full_type} {m.name}", m.name, f"{s.name}::{m.name}", False))
                    fields.append((m.name, m.name))
            args.append(f"&{var}")
        body += self.struct_initialization(s, var, fields, arrays)
        call = f"{t0}({', '.join(args)})"
        body.append(("return " if ret != "void" else "") + call + ";")
        doc = self.doc_block(f"{self.brief(cmd)[:-1]} from the fields of {s.name}.", params, ret, None, [])
        sig = self.signature(ret, name, params)
        self.sigs[name] = (ret, params)
        self.place(self.platforms_for(cmd.extensions, cmd.core_version), doc + sig + ";",
                   sig + "\n{\n" + indent("\n".join(align_assignments(body))) + "\n}")

    def emit_convenience(self, cmd: Command, ip: Param, ext: Struct, singular: Optional[str] = None, name_override: Optional[str] = None) -> None:
        """SPEC.md section 6. With `singular`, the extender is chained onto the singular of an
        array-input command (cvkCreateGraphicsPipeline + VkPipelineRenderingCreateInfo ->
        cvkCreateGraphicsPipelineWithRenderingInfo); the name is computed by the caller."""
        reg = self.reg
        base = reg.struct_of(ip.type)
        if singular is not None:
            t0 = singular
            name = self.register(name_override, f"{cmd.name}:singular+{ext.name}")
        else:
            # Chain onto the flattened form: the flat sibling when tier 0 keeps the pointer.
            t0 = self.flat_name(cmd, base) if self.flip_param(cmd) is not None else self.cmd_name(cmd)
            name = self.register(self.convenience_name(cmd, base, ext), f"{cmd.name}+{ext.name}")
        ret, t0_params = self.sigs[t0]
        base_params = [p.text for p in t0_params]
        filled = sum(1 for r in self.roles(cmd) if r in ("I", "P"))
        stem = ip.name[1:] if ip.name.startswith("p") else ip.name
        pn = "pNext" if (singular is not None or filled == 1) else f"p{stem}Next"
        idx = next(i for i, p in enumerate(base_params) if p.split()[-1] == pn)
        ext_params: List[str] = []
        fields = [("sType", ext.stype), ("pNext", pn)]
        arrays = []
        base_names = {p.split()[-1].split("[")[0] for p in base_params}
        # The chain parameter now feeds the extender: its type is the extender's pNext type.
        base_params[idx] = f"{self.pnext_type(ext)} {pn}"
        for em in ext.members:
            if em.name in ("sType", "pNext"):
                continue
            if em.name in self.policy.get("shared_extender_members", {}).get(ext.name, []):
                if em.is_pointer or em.array_len or f"{em.full_type} {em.name}" not in base_params:
                    self.errors.append(f"shared extender member {ext.name}::{em.name} has no matching scalar in {t0}")
                    continue
                fields.append((em.name, em.name))
                continue
            pname = em.name if em.name not in base_names else self.member_alias(em, base, ext)
            if em.array_len:
                ext_params.append(f"const {em.full_type} {pname}[{em.array_len}]")
                arrays.append((em.name, pname))
            else:
                ext_params.append(f"{em.full_type} {pname}")
                fields.append((em.name, pname))
        all_params = base_params[: idx + 1] + ext_params + base_params[idx + 1:]
        ext_var = self.struct_local_name(ext, {p.split()[-1].split("[")[0] for p in all_params})
        body = [f"{ext.name} {ext_var};", ""] + self.struct_initialization(ext, ext_var, fields, arrays)
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

    def member_alias(self, em: Param, base: Struct, ext: Struct) -> str:
        """SPEC.md section 6: an extender member sharing a base member's name. The tokens of
        its type absent from the base member's type distinguish it (VkPipelineCreateFlags2 flags
        beside VkPipelineCreateFlags flags -> flags2); with the same type, the extender's distinct
        part qualifies it (VkImageViewUsageCreateInfo::usage -> imageViewUsage)."""
        bm = next((m for m in base.members if m.name == em.name), None)
        if bm is not None:
            bt = tokens(bm.type)
            extra = [t for t in tokens(em.type) if t not in bt and t not in VENDORS]
            if extra and bm.type != em.type:
                return em.name + "".join(extra)
        d = self.distinct_part(ext, base)
        cap = em.name[0].upper() + em.name[1:]
        if d and d[-1] == cap:
            d = d[:-1]
        head = "".join(d)
        return (head[0].lower() + head[1:] + cap) if head else em.name + "Ext"

    # ------------------------------------------------ array and output extenders
    @staticmethod
    def plural(name: str) -> str:
        """Parallel-array parameter name for a member: image -> pImages, memory -> pMemories,
        imageIndex -> pImageIndices; a name that is already plural keeps it (flags -> pFlags)."""
        cap = name[0].upper() + name[1:]
        if cap.endswith("Index"):
            return "p" + cap[:-5] + "Indices"
        if cap.endswith("y"):
            return "p" + cap[:-1] + "ies"
        if cap.endswith("s"):
            return "p" + cap
        return "p" + cap + "s"

    KINDS_OUT = ("Properties", "Requirements", "Features", "Support", "Capabilities")

    def distinct_part(self, ext: Struct, base: Struct, strip_info: bool = True) -> List[str]:
        """The extender's tokens outside the blocks matching its base (see convenience_name).
        A trailing version digit is a struct suffix only when it follows a kind word
        (Info2, Properties2); a digit inside a name (Vulkan12) is part of that name."""
        import difflib
        e = tokens(self.struct_short(ext))
        tag = e.pop() if e and e[-1] in VENDORS else ""
        if len(e) >= 2 and e[-1].isdigit() and (e[-2] in self.KINDS_OUT or e[-2] == "Info"):
            e.pop()
        b = tokens(strip_vendor(self.struct_short(base)))
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
        return d + ([tag] if tag else [])

    def emit_array_convenience(self, cmd: Command, ap: Param, ext: Struct) -> None:
        """SPEC.md section 6: <tier-0>With<D>Info for an array input, as parallel arrays.
        Each element of the base struct gets the extender chained; the base members and the
        extender members become parallel arrays sized by the command's count parameter."""
        base = self.reg.struct_of(ap.type)
        d = "".join(self.distinct_part(ext, base))
        # Singular first: cvk<Verb><D><noun run> where the run is the command's own noun
        # (vkBindImageMemory2 -> ImageMemory, vkCmdBindDescriptorBuffersEXT -> DescriptorBuffer)
        # depluralized and without its version digit; a Cmd verb carries its action word
        # (CmdBind). cvkBindPlaneImageMemory, cvkBindSwapchainImageMemory,
        # cvkCmdBindPushDescriptorBufferHandleDescriptorBuffer. The plural pluralizes the last word.
        ct = tokens(self.with_base(cmd)[3:])
        ct = [t for t in ct if t not in VENDORS]
        verb = ct[0] + (ct[1] if ct[0] == "Cmd" and len(ct) > 1 else "")
        noun = ct[2:] if ct[0] == "Cmd" else ct[1:]
        if noun and noun[-1].endswith("s") and not noun[-1].endswith("ss"):
            noun[-1] = noun[-1][:-3] + "y" if noun[-1].endswith("ies") else noun[-1][:-1]
        single = self.register("cvk" + verb + d + "".join(noun), f"{cmd.name}+{ext.name}[1]")
        last = tokens(single[3:])[-1]
        name = self.register(single[: -len(last)] + self.plural(last)[1:], f"{cmd.name}+{ext.name}[]")
        t0 = self.cmd_name(cmd)
        ret, t0_params = self.sigs[t0]
        count = ap.len.split(",")[0]
        cap = self.capacity_name(ap.name)
        base_var = snake(ap.name)                               # bind_infos
        params: List[EmittedParam] = []
        args: List[str] = []
        ext_fields = [("sType", ext.stype), ("pNext", "(NULL != ppNexts) ? ppNexts[index] : NULL")]
        base_fields = []
        singles: Dict[str, str] = {}                            # parallel array -> element parameter
        for p in t0_params:
            if p.name != ap.name:
                params.append(p)
                args.append(p.name)
                continue
            params.append(EmittedParam(f"{self.pnext_type(ext)} const* ppNexts", "ppNexts", f"Optional per-element extension chains attached to {ext.name}, or NULL", False))
            base_names = {m.name for m in base.members}
            for m in ext.members:
                if m.name in ("sType", "pNext"):
                    continue
                alias = m.name if m.name not in base_names else self.member_alias(m, base, ext)
                pn = self.plural(alias)
                singles[pn] = alias
                params.append(EmittedParam(f"const {m.full_type}* {pn}", pn, f"{ext.name}::{m.name} per element", False))
                ext_fields.append((m.name, f"{pn}[index]"))
            for m in base.members:
                if m.name in ("sType", "pNext"):
                    continue
                pn = self.plural(m.name)
                singles[pn] = m.name
                params.append(EmittedParam(f"const {m.full_type}* {pn}", pn, f"{base.name}::{m.name} per element", False))
                base_fields.append((m.name, f"{pn}[index]"))
            args.append(base_var)
        ext_var = self.struct_local_name(ext, {p.name for p in params} | {base_var, "index"}, plural=True)
        base_fields = [("sType", base.stype), ("pNext", f"&{ext_var}[index]")] + base_fields
        body = [f"{base.name} {base_var}[{cap}];", f"{ext.name} {ext_var}[{cap}];", "uint32_t index;", "",
                f"CVK_ASSERT({cap} >= {count});",
                f"for (index = 0; index < {count}; ++index)", "{"]
        body += ["    " + line for line in self.struct_initialization(ext, f"{ext_var}[index]", ext_fields)]
        body += ["    " + line for line in self.struct_initialization(base, f"{base_var}[index]", base_fields)]
        body += ["}", ("return " if ret != "void" else "") + f"{t0}({', '.join(args)});"]
        doc = self.doc_block(f"{self.brief(cmd)[:-1]} with {ext.name} chained to every element.", params, ret, None, [])
        sig = self.signature(ret, name, params)
        self.sigs[name] = (ret, params)
        plats = self.platforms_for(cmd.extensions, cmd.core_version) | self.platforms_for(ext.extensions, ext.core_version)
        self.place(plats, doc + sig + ";", sig + "\n{\n" + indent("\n".join(align_assignments(body))) + "\n}")

        # Singular (count = 1): one element flattened; forwards to the plural.
        s_params: List[EmittedParam] = []
        s_args: List[str] = []
        for p in params:
            if p.name == count:
                s_args.append("1")
            elif p.name == "ppNexts":
                s_params.append(EmittedParam(f"{self.pnext_type(ext)} pNext", "pNext", f"Optional extension chain attached to {ext.name}, or NULL", False))
                s_args.append("&pNext")
            elif p.name in singles:
                elem_type = p.text.rsplit(" ", 1)[0][len("const "):].rstrip("*").strip()
                member = singles[p.name]
                s_params.append(EmittedParam(f"{elem_type} {member}", member, p.doc[: -len(" per element")], False))
                s_args.append(f"&{member}")
            else:
                s_params.append(p)
                s_args.append(p.name)
        s_body = [("return " if ret != "void" else "") + f"{name}({', '.join(s_args)});"]
        s_doc = self.doc_block(f"{self.brief(cmd)[:-1]} for one element with {ext.name} chained.", s_params, ret, None, [])
        s_sig = self.signature(ret, single, s_params)
        self.sigs[single] = (ret, s_params)
        self.place(plats, s_doc + s_sig + ";", s_sig + "\n{\n" + indent("\n".join(s_body)) + "\n}")

    def splice_output_name(self, prefix: str, ext: Struct, base: Struct, d_tokens: List[str]) -> Optional[str]:
        """When the target name ends with the base struct's noun run, the extender's full name
        replaces that run: cvkGetPhysicalDeviceFeatures + VkPhysicalDeviceHostQueryResetFeatures
        -> cvkGetPhysicalDeviceHostQueryResetFeatures; cvkGetPhysicalDeviceFormatProperties +
        VkDrmFormatModifierPropertiesList2EXT -> cvkGetPhysicalDeviceDrmFormatModifierPropertiesList2.
        None when the target's tail is not the run (a flattened sibling ending in Info2)."""
        t = tokens(prefix[3:])
        run = [x for x in tokens(self.struct_short(base)) if not x.isdigit()]
        # Exact match only: an enumeration naming its run in the plural (SurfaceFormats for
        # VkSurfaceFormat2KHR) takes the With form, since replacing the run would drop the subject.
        if len(t) <= len(run) or t[-len(run):] != run:
            return None
        return "cvk" + "".join(t[: -len(run)] + tokens(self.struct_short(ext)))

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
        # The name is derived from the command's name (its tail is the output run); the call
        # goes to the flattened sibling when one exists so the input stays flattened.
        prefix = self.with_base(cmd)
        spliced = self.splice_output_name(prefix, ext, base, d_tokens)
        if spliced in self.tier0_names:
            spliced = None                                   # the plain name is a command's (vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR)
        ext_tokens = [t for t in tokens(self.struct_short(ext)) if not t.isdigit() and t not in VENDORS]
        if not any(t in self.KINDS_OUT for t in ext_tokens):
            # An output extender without a kind word (VkImageUsageFlags2CreateInfoKHR,
            # VkSurfacePresentModeCompatibilityEXT on VkSurfaceCapabilities2KHR) cannot replace
            # the run: <base>With<D>, keeping a trailing Info.
            spliced = prefix + "With" + "".join(self.distinct_part(ext, base)) + ("Info" if ext_tokens[-1] == "Info" else "")
        name = self.register(spliced if spliced else target + "With" + d + kind, f"{cmd.name}+{ext.name}:out")
        ret, t_params = self.sigs[target]
        out_stem = op.name[1:] if op.name.startswith("p") else op.name
        chain_param = next((p for p in t_params if p.name in (f"p{out_stem}Next", "pNext") and p.text.startswith("void*")), None)
        if chain_param is None:
            self.errors.append(f"output convenience {name}: no output chain parameter on {target}")
            return
        # Qualify the output chain only when another chain parameter needs distinguishing.
        ext_out = "p" + self.struct_short(ext)
        other_next = any(p is not chain_param and re.fullmatch(r"p\w*Next", p.name) for p in t_params)
        ext_next = f"p{self.struct_short(ext)}Next" if other_next else "pNext"
        params: List[EmittedParam] = []
        args: List[str] = []
        # The base output struct is only the head of the chain Vulkan requires: it stays a
        # local, and the function returns exactly the struct it is named after.
        base_var = snake(op.name)
        var = self.struct_local_name(ext, {p.name for p in t_params} | {base_var, "result", ext_out, ext_next})
        for p in t_params:
            if p is chain_param:
                params.append(EmittedParam(f"void* {ext_next}", ext_next, f"Optional output extension chain attached to {ext.name}, or NULL", False))
                args.append(f"&{var}")
            elif p.name == op.name:
                args.append(f"&{base_var}")
            else:
                params.append(p)
                args.append(p.name)
        # Caller-provided storage is input, even on a returned-only extender. Flatten those
        # fields like other wrapper inputs; never read them from the output destination.
        inout = any(m.is_pointer for m in ext.members if m.name not in ("sType", "pNext"))
        fields = [("sType", ext.stype), ("pNext", ext_next)]
        arrays = []
        if inout:
            used = {p.name for p in params} | {ext_out}
            storage_params = []
            for m in ext.members:
                if m.name in ("sType", "pNext"):
                    continue
                pname = m.name if m.name not in used else self.member_alias(m, base, ext)
                used.add(pname)
                if m.array_len:
                    text = f"const {m.full_type} {pname}[{m.array_len}]"
                    arrays.append((m.name, pname))
                else:
                    text = f"{m.full_type} {pname}"
                    fields.append((m.name, pname))
                storage_params.append(EmittedParam(text, pname, f"{ext.name}::{m.name}", False))
            index = next(i for i, p in enumerate(params) if p.name == ext_next) + 1
            params[index:index] = storage_params
            var = self.struct_local_name(ext, used | {base_var, "result", ext_next})
        params.append(EmittedParam(f"{ext.name}* {ext_out}", ext_out,
                                   f"Receives the {ext.name}", True))
        args = [f"&{var}" if p is chain_param else arg for p, arg in zip(t_params, args)]
        body = [f"{base.name} {base_var};", f"{ext.name} {var};", ""]
        body += self.struct_initialization(ext, var, fields, arrays)
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

    FIXED += [
        {
            "name": "cvkGetExtensionSupport",
            "ret": "VkBool32",
            "brief": "Reports whether an enumerated extension array contains one extension name.",
            "params": [("uint32_t", "propertyCount"), ("const VkExtensionProperties*", "pProperties"), ("const char*", "pRequiredExtensionName")],
            "body": [
                "uint32_t property_index;",
                "",
                "for (property_index = 0; property_index < propertyCount; ++property_index)",
                "{",
                "    if (0 == strncmp(pProperties[property_index].extensionName, pRequiredExtensionName, VK_MAX_EXTENSION_NAME_SIZE))",
                "    {",
                "        return VK_TRUE;",
                "    }",
                "}",
                "return VK_FALSE;",
            ],
        },
        {
            "name": "cvkGetExtensionsSupport",
            "ret": "VkBool32",
            "brief": "Reports whether an enumerated extension array contains every required extension name.",
            "params": [("uint32_t", "propertyCount"), ("const VkExtensionProperties*", "pProperties"),
                       ("uint32_t", "requiredExtensionCount"), ("const char* const*", "ppRequiredExtensionNames")],
            "body": [
                "uint32_t required_index;",
                "",
                "for (required_index = 0; required_index < requiredExtensionCount; ++required_index)",
                "{",
                "    if (!cvkGetExtensionSupport(propertyCount, pProperties, ppRequiredExtensionNames[required_index]))",
                "    {",
                "        return VK_FALSE;",
                "    }",
                "}",
                "return VK_TRUE;",
            ],
        },
        {
            "name": "cvkGetQueueFamilyIndex",
            "ret": "uint32_t",
            "brief": "Returns the first queue family whose flags cover the required flags, or VK_INVALID.",
            "params": [("uint32_t", "propertyCount"), ("const VkQueueFamilyProperties*", "pProperties"), ("VkQueueFlags", "requiredFlags")],
            "body": [
                "uint32_t property_index;",
                "",
                "for (property_index = 0; property_index < propertyCount; ++property_index)",
                "{",
                "    if ((0 != pProperties[property_index].queueCount) && ((pProperties[property_index].queueFlags & requiredFlags) == requiredFlags))",
                "    {",
                "        return property_index;",
                "    }",
                "}",
                "return VK_INVALID;",
            ],
        },
    ]

    FIXED.append({
        "name": "cvkGetVideoQueueFamilyIndex",
        "requires": ["VkQueueFamilyVideoPropertiesKHR"],
        "ret": "uint32_t",
        "brief": "Returns the first queue family whose video codec operations cover the required operations, or VK_INVALID.",
        "params": [("uint32_t", "propertyCount"), ("const VkQueueFamilyVideoPropertiesKHR*", "pVideoProperties"),
                   ("VkVideoCodecOperationFlagsKHR", "requiredOperations")],
        "body": [
            "uint32_t property_index;",
            "",
            "for (property_index = 0; property_index < propertyCount; ++property_index)",
            "{",
            "    if ((pVideoProperties[property_index].videoCodecOperations & requiredOperations) == requiredOperations)",
            "    {",
            "        return property_index;",
            "    }",
            "}",
            "return VK_INVALID;",
        ],
    })

    FIXED.append({
        "name": "cvkAlignDeviceSize",
        "ret": "VkDeviceSize",
        "brief": "Rounds a size or offset up to a power-of-two alignment.",
        "params": [("VkDeviceSize", "value"), ("VkDeviceSize", "alignment")],
        "body": [
            "CVK_ASSERT((0 != alignment) && (0 == (alignment & (alignment - 1))));",
            "return (value + alignment - 1) & ~(alignment - 1);",
        ],
    })
    FIXED.append({
        "name": "cvkGetInactiveVideoReferenceSlotIndex",
        "ret": "int32_t",
        "brief": "Returns the first DPB slot that is not active, or -1 when every slot is active.",
        "params": [("uint32_t", "slotCount"), ("const VkBool32*", "pSlotActive")],
        "body": [
            "uint32_t slot_index;",
            "",
            "for (slot_index = 0; slot_index < slotCount; ++slot_index)",
            "{",
            "    if (!pSlotActive[slot_index])",
            "    {",
            "        return (int32_t)slot_index;",
            "    }",
            "}",
            "return -1;",
        ],
    })

    def emit_fixed(self, f: dict) -> None:
        if any(self.reg.struct_of(t) is None or not self.struct_in_profile(self.reg.struct_of(t)) for t in f.get("requires", [])):
            self.dropped.append(f["name"])                 # its types are not in this profile
            return
        name = self.register(f["name"], "policy:" + f["name"])
        params = [EmittedParam(f"{t} {n}", n, f"{n} value", False) for t, n in f["params"]]
        doc = self.doc_block(f["brief"], params, f["ret"], None, [])
        sig = self.signature(f["ret"], name, params)
        self.sigs[name] = (f["ret"], params)
        self.place(frozenset(), doc + sig + ";", sig + "\n{\n" + indent("\n".join(f["body"])) + "\n}")

    # ------------------------------------------------------------ protocols
    PROTOCOL_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "protocols")

    def emit_protocol_file(self, path: str) -> None:
        """SPEC.md section 8b: a tier-3 C file kept with the generator (scripts/protocols/
        cvk_protocol_<platform>.c) is folded into that platform's generated pair: each
        Doxygen block plus signature goes to the header, each definition to the source, and
        its system includes to the source. Composed from the generated surface only: every
        cvk* it calls must exist, and it may not call vk* directly."""
        m_plat = re.search(r"cvk_protocol(?:_(\w+))?\.c$", os.path.basename(path))
        plat = m_plat.group(1) if m_plat.group(1) in self.PLATFORM_INCLUDES else ""   # else the core pair
        is_json = os.path.basename(path).startswith("cvk_protocol_json")
        with open(path, encoding="utf-8") as fh:
            text = fh.read()
        # A file may name the API variant it belongs to in its leading comment (Profile: vulkan):
        # its includes and functions are then absent from the other variants.
        m_prof = re.search(r"Profile:\s*(\w+)", text[:800])
        if m_prof and m_prof.group(1) != self.api:
            self.dropped.append(os.path.basename(path))
            return
        stripped = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), text, flags=re.S)
        if re.search(r"\bvk[A-Z]\w*\s*\(", stripped):
            self.errors.append(f"protocol {os.path.basename(path)} calls a vk* command directly")
        for inc in re.findall(r"^#include\s+<[^>]+>", text, flags=re.M):
            inc = inc[len("#include "):]
            includes = self.json_source_includes if is_json else self.PLATFORM_SOURCE_INCLUDES
            if inc not in includes.setdefault(plat, []):
                includes[plat].append(inc)
        for m in re.finditer(r"(/\*\*(?=\s).*?\*/\s*)?^(\w[\w\s\*]*?\s+(cvk\w+)\s*\((.*?)\))\s*\{", text, flags=re.S | re.M):
            doc, sig, name, plist = m.group(1) or "", m.group(2), m.group(3), m.group(4)
            depth, i, quote = 1, m.end(), ""
            while i < len(text) and depth:
                c = text[i]
                if quote:
                    if c == "\\":
                        i += 1
                    elif c == quote:
                        quote = ""
                elif text.startswith("/*", i):
                    i = text.index("*/", i) + 1                # comments hold no blocks (and stray apostrophes)
                elif c in "\"'":
                    quote = c                                  # braces inside literals ('{') are not blocks
                else:
                    depth += {"{": 1, "}": -1}.get(c, 0)
                i += 1
            body = text[m.end() - 1:i]
            # A function may name the API variant it belongs to (\par Profile: vulkansc).
            prof = re.search(r"\\par Profile:\s*(\w+)", doc)
            if prof and prof.group(1) != self.api:
                continue
            unknown = [c for c in sorted(set(re.findall(r"\b(cvk\w+)\s*\(", body))) if c not in self.sigs and c != name]
            if unknown:
                if self.drop_missing:
                    self.dropped.append(name)                # composes over something outside this profile
                    continue
                for callee in unknown:
                    self.errors.append(f"protocol {name} calls {callee}, which the generator does not produce")
            name = self.register(name, "protocol:" + os.path.basename(path))
            ret = sig.split(name)[0].strip()
            params: List[EmittedParam] = []
            for piece in plist.split(","):
                piece = re.sub(r"\s+", " ", piece.strip())
                pm = re.match(r"(.*?)\b(\w+)\s*(\[[^\]]*\])?$", piece)
                params.append(EmittedParam(piece, pm.group(2) if pm else piece, "", not piece.startswith("const") and "*" in piece))
            self.sigs[name] = (ret, params)
            self.place(frozenset([plat]) if plat else frozenset(), doc + sig + ";", sig + "\n" + body, json=is_json)

    # ---------------------------------------------------------------- chains
    LEVELS = ["instance", "physicalDevice", "device", "queue", "commandBuffer"]

    def typed_flags(self, ctype: str, expression: str) -> str:
        """Evaluate flag bits in their unsigned storage type, then convert to the destination mask."""
        canonical = ctype
        while canonical in self.reg.type_aliases:
            canonical = self.reg.type_aliases[canonical]
        base = self.reg.bitmask_bases.get(canonical)
        if base is None:
            return expression
        if expression.strip() == "0":
            return f"({ctype})0"
        enum = self.reg.bitmasks[canonical]
        while enum in self.reg.type_aliases:
            enum = self.reg.type_aliases[enum]
        bits = set(self.reg.enums.get(enum, []) + self.reg.enum_extends.get(enum, []))
        if expression.strip() in bits:
            return f"({ctype}){expression.strip()}"
        typed = re.sub(r"\bVK_\w+\b", lambda m: f"({base}){m[0]}" if m[0] in bits else m[0], expression)
        if typed != expression or re.search(r"(?<![|&])[|&](?![|&])|[~^]", expression):
            return f"({ctype})({typed})"
        return expression

    def emit_chain(self, name: str, spec: dict) -> None:
        """SPEC.md section 8: compose a straight-line chain from a step table.

        The chain's parameters are computed from the steps' signatures: an input is satisfied
        by an earlier output whose stem matches (image <- pImage, allocationSize <-
        pAllocationSize), a step's pNext is renamed after that step's product (pImageNext,
        pMemoryNext), every unsatisfied input becomes a chain parameter, and every produced
        handle becomes a chain out-parameter. Dispatch handles come first by level, then inputs
        in order of appearance, then pAllocator, then outputs."""
        missing = [st["call"] for st in spec["steps"] if "call" in st and st["call"] not in self.sigs]
        mentions = json.dumps(spec)                           # every constant and type the row names
        if any(e not in self.exts for e in spec.get("extensions", [])) or not self.text_in_profile(mentions):
            self.dropped.append(name)                         # names a constant or type outside the profile
            return
        if missing:
            if self.drop_missing:
                self.dropped.append(name)
            else:
                self.errors.append(f"chain {name}: unknown step {missing[0]}")
            return
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
        # Declared outputs join the parameter list where a step first binds, stores, or sets them.
        declared_outputs: Dict[str, EmittedParam] = {}
        for out in spec.get("outputs", []):
            declared_outputs[out["name"]] = EmittedParam(f"{out['type']}* {out['name']}", out["name"], f"Receives the {stem(out['name'])}", True)
            available[stem(out["name"])] = f"(*{out['name']})"
        raw_decls: List[str] = []
        local_arrays: Dict[str, Tuple[str, int]] = {}
        for step in spec["steps"]:
            if "declare" in step:
                raw_decls.append(step["declare"] + ";")          # a local with an initializer
                continue
            if "assign" in step:
                for k, v in step["assign"].items():           # a statement on a local (an array element)
                    body.append(f"{k} = {v};")
                continue
            if "set" in step:
                for k, v in step["set"].items():
                    if k in declared_outputs:
                        outputs.setdefault(k, declared_outputs[k])
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
            # The step's product names its pNext (pImageNext): the first handle it produces, under
            # the chain's own name for it when the step's out-parameter is bound to a chain output
            # (pCommandBuffers bound to pCommandBuffer -> pCommandBufferNext).
            product = next((stem(bind.get(p.name, p.name)) for p in sparams if p.is_out and p.text.split()[0].rstrip("*") in self.reg.handles), None)
            args = []
            enum_into = None  # set when a void two-call enumerate's count is captured into 'into'
            for p in sparams:
                ptype = p.text.rsplit(" ", 1)[0]
                if p.name in bind:
                    if bind[p.name] in declared_outputs:
                        outputs.setdefault(bind[p.name], declared_outputs[bind[p.name]])
                    args.append(self.typed_flags(ptype, bind[p.name]))
                elif p.name in local:
                    var = local[p.name]
                    elem = re.fullmatch(r"(\w+)\[(\d+)\]", var)
                    if elem:
                        # An element of a chain-local array (shader_stages[1]): declared once, sized by the highest index.
                        arr, idx = elem.group(1), int(elem.group(2))
                        local_arrays[arr] = (ptype.rstrip("*").strip(), max(idx + 1, local_arrays.get(arr, ("", 0))[1]))
                        args.append(f"&{var}")
                    elif "[" in p.text:
                        # Fixed-capacity enumeration array: declared as an array, passed by name.
                        cap = p.text[p.text.index("[") + 1: p.text.index("]")]
                        locals_[f"{var}[{cap}]"] = p.text.split(" ", 1)[0]
                        available[stem(p.name)] = var
                        args.append(var)
                    else:
                        locals_[var] = ptype.rstrip("*").strip()
                        available[stem(p.name)] = var
                        args.append(f"&{var}")
                elif p.is_out and sret == "void" and "into" in step and ptype.rstrip("*").strip() == "uint32_t":
                    # A two-call enumerate now returns void and writes its entry count through this
                    # out-parameter; keep the count internal under the step's 'into' name.
                    enum_into = step["into"]
                    locals_[enum_into] = "uint32_t"
                    available[stem(p.name)] = enum_into
                    args.append(f"&{enum_into}")
                elif p.is_out:
                    outputs.setdefault(p.name, p)
                    available[stem(p.name)] = f"(*{p.name})"
                    args.append(p.name)
                elif p.name in available:
                    args.append(available[p.name])
                else:
                    pname = p.name
                    if p.name == "pNext" and product:          # an already qualified p<X>Next keeps its name
                        pname = "p" + product[0].upper() + product[1:] + "Next"
                    if pname not in inputs:
                        inputs[pname] = EmittedParam(f"{ptype} {pname}", pname, f"See {fn}", False)
                    elif inputs[pname].text != f"{ptype} {pname}":
                        # Two steps want the same name for different things (a shader module's flags
                        # and a pipeline's flags): the policy must bind one of them explicitly.
                        self.errors.append(f"chain {name}: input {pname} is {inputs[pname].text} and {ptype} {pname} for {fn}; bind one of them")
                    args.append(pname)
            call = f"{fn}({', '.join(args)})"
            if enum_into is not None:
                body.append(f"{call};") # the count was captured through the out-parameter above
            elif step.get("check"):
                if sret != "VkResult" or any(key in step for key in ("return", "store", "into")):
                    self.errors.append(f"chain {name}: check requires an otherwise unconsumed VkResult from {fn}")
                body.append(f"CVK_CHECK({call});")
            elif step.get("return"):
                ret = sret
                body.append(f"return {call};")
            elif "store" in step:
                if step["store"] in declared_outputs:
                    outputs.setdefault(step["store"], declared_outputs[step["store"]])
                body.append(f"(*{step['store']}) = {call};")
            elif "into" in step:
                var = step["into"]
                locals_[var] = sret
                available[var.replace("_", "")] = var          # matched by stem below
                available[stem("p" + "".join(w.capitalize() for w in var.split("_")))] = var
                body.append(f"{var} = {call};")
            elif sret == "VkResult":
                self.errors.append(f"chain {name}: step {fn} returns VkResult; route it with check, return, into, or store")
            else:
                body.append(f"{call};")

        # Declared inputs (used only inside bind expressions) follow the step-derived ones.
        for d_ in declared:
            inputs.setdefault(d_.name, d_)
        declared_names = {d_.name for d_ in declared}
        ordered = sorted(inputs.values(), key=lambda p: (self.LEVELS.index(p.name) if p.name in self.LEVELS else len(self.LEVELS), 0))
        # A declared input that happens to share a dispatch handle's name is data, not dispatch.
        dispatch = [p for p in ordered if p.name in self.LEVELS and p.name not in declared_names]
        is_next = lambda p: bool(re.fullmatch(r"p\w*Next", p.name))
        derived = [p for p in inputs.values() if p.name not in self.LEVELS and p.name != "pAllocator" and p.name not in declared_names]
        # Derived inputs keep their order of appearance; declared inputs are inserted before the
        # trailing run of extension-chain pointers so they sit with the inputs they qualify.
        cut = len(derived)
        while cut > 0 and is_next(derived[cut - 1]):
            cut -= 1
        rest = derived[:cut] + [p for p in inputs.values() if p.name in declared_names] + derived[cut:]
        alloc = [p for p in inputs.values() if p.name == "pAllocator"]
        for oname, oparam in declared_outputs.items():
            outputs.setdefault(oname, oparam)
        # A chain over a builder is a builder: its pInfo leads like every cvkInit.
        info_out = [p for p in outputs.values() if p.name == "pInfo"]
        if info_out:
            rest = [p for p in rest if p.name == "pNext"] + [p for p in rest if p.name != "pNext"]   # pInfo, pNext, members
        params = info_out + dispatch + rest + alloc + [p for p in outputs.values() if p.name != "pInfo"]
        decls = [f"{t} {v};" for v, t in locals_.items()] + [f"{t} {v}[{n}];" for v, (t, n) in local_arrays.items()] + raw_decls
        full = decls + ([""] if decls else []) + body
        doc = self.doc_block(spec["brief"], params, ret, None, [])
        sig = self.signature(ret, name, params)
        self.sigs[name] = (ret, params)
        is_json = any(step.get("call") in self.json_names for step in spec["steps"])
        self.place(frozenset(), doc + sig + ";", sig + "\n{\n" + indent("\n".join(align_assignments(full))) + "\n}", json=is_json)

    # ---------------------------------------------------------------- run
    def run(self) -> None:
        self.compute_keep_suffix([st for st in self.reg.structs.values() if not st.alias_of and self.struct_in_profile(st)])
        self.tier0_names = {self.cmd_name(c) for c in self.cmds}
        self.flat_pairs: Dict[Tuple[str, str], int] = defaultdict(int)
        for cmd in self.cmds:
            ip = self.flip_param(cmd)
            if ip is not None:
                self.flat_pairs[(cmd.verb, self.reg.struct_of(ip.type).name)] += 1
        # Builders first: every struct initializer precedes the wrappers that use them.
        # A builder exists for every struct the caller fills: sType structs (at least sType/pNext)
        # and plain input structs. Returned-only plain structs have nothing to initialize.
        structs = [s for s in self.reg.structs.values() if not s.alias_of and s.category == "struct" and self.struct_in_profile(s)
                   and (s.stype or not s.returnedonly)
                   and s.name not in ("VkBaseInStructure", "VkBaseOutStructure", "VkAllocationCallbacks")
                   and not any(":" in m.full_type for m in s.members)]
        for s in sorted(structs, key=lambda s: s.name):
            self.emit_builder(s)
        for s in sorted((u for u in self.reg.structs.values() if not u.alias_of and u.category == "union" and self.struct_in_profile(u)), key=lambda u: u.name):
            self.emit_union_builders(s)
        if self.policy.get("json", True):
            self.emit_json_parsers()
        for cmd in self.cmds:
            self.emit_tier0(cmd)
        for cmd in self.cmds:
            ip = self.flip_param(cmd)
            if ip is not None:
                self.emit_flat(cmd, ip)
        self.singular_pairs: Dict[Tuple[str, str], int] = defaultdict(int)
        for cmd in self.cmds:
            s = self.singular_struct(cmd)
            if s is not None:
                self.singular_pairs[(cmd.verb, s.name)] += 1
        for cmd in self.cmds:
            self.emit_singular(cmd)
        pairs: List[Tuple[Command, Param, Struct]] = []
        for cmd in self.cmds:
            roles = self.roles(cmd)
            for ip in [p for p, r in zip(cmd.params, roles) if r == "I"]:
                base = self.reg.struct_of(ip.type)
                for ext in sorted(self.reg.extenders_of(base.name), key=lambda e: e.name):
                    if not self.struct_in_profile(ext) or re.match(r"VkPhysicalDevice\w*Features", ext.name):
                        continue
                    pairs.append((cmd, ip, ext))
        self.convenience_pairs = defaultdict(int)
        for cmd, _, ext in pairs:
            self.convenience_pairs[(cmd.verb, ext.name)] += 1
        for cmd, ip, ext in pairs:
            self.emit_convenience(cmd, ip, ext)
        # Gap 4: extenders on the singular of an array-input command (SPEC.md section 6):
        # <singular>With<D>Info. Two extenders of one base with the same distinct part
        # (VkPipelineLibraryCreateInfoKHR and VkGraphicsPipelineLibraryCreateInfoEXT both
        # leave `Library`) each take their whole struct name instead.
        singular_ext: List[Tuple[Command, Param, Struct, str]] = []
        for cmd in self.cmds:
            s_elem = self.singular_struct(cmd)
            if s_elem is None:
                continue
            sname = self.singular_name(cmd, s_elem)
            if sname not in self.sigs:
                continue
            ap = next(p for p, r in zip(cmd.params, self.roles(cmd)) if r == "I[]")
            for ext in sorted(self.reg.extenders_of(s_elem.name), key=lambda e: e.name):
                if not self.struct_in_profile(ext) or re.match(r"VkPhysicalDevice\w*Features", ext.name):
                    continue
                d = self.distinct_part(ext, s_elem)
                singular_ext.append((cmd, ap, ext, sname + "With" + "".join(d) + "Info"))
        by_name: Dict[str, int] = defaultdict(int)
        for _, _, _, nm in singular_ext:
            by_name[nm] += 1
        for cmd, ap, ext, nm in singular_ext:
            if by_name[nm] > 1:
                whole = tokens(self.struct_short(ext))
                tag = whole.pop() if whole and whole[-1] in VENDORS else ""
                while whole and (whole[-1] in self.KIND or whole[-1] == "Info" or whole[-1].isdigit()):
                    whole.pop()
                nm = self.singular_name(cmd, self.reg.struct_of(ap.type)) + "With" + "".join(whole) + "Info" + tag
            self.emit_convenience(cmd, ap, ext, singular=self.singular_name(cmd, self.reg.struct_of(ap.type)), name_override=nm)
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
        for cmd, op, ext in self.output_pairs():
            self.emit_output_convenience(cmd, op, ext)
        # Gap 3: extenders on the element struct of a two-call enumeration (chained output array).
        for cmd, ap, ext in self.output_array_pairs():
            self.emit_tier0(cmd, out_ext=ext)
        for f in self.FIXED:
            self.emit_fixed(f)
        # Chains and protocols compose each other: emit every chain whose steps are known,
        # then the protocol files, then the chains that were waiting on a protocol.
        pending = list(self.policy.get("chains", {}).items())

        def ready(spec: dict) -> bool:
            return all(st.get("call", "") in self.sigs or "call" not in st for st in spec["steps"])

        progressed = True
        while pending and progressed:
            progressed = False
            for chain_name, spec in list(pending):
                if ready(spec):
                    self.emit_chain(chain_name, spec)
                    pending.remove((chain_name, spec))
                    progressed = True
        for path in sorted(os.listdir(self.PROTOCOL_DIR)) if os.path.isdir(self.PROTOCOL_DIR) else []:
            if re.fullmatch(r"cvk_protocol(_\w+)?\.c", path):
                self.emit_protocol_file(os.path.join(self.PROTOCOL_DIR, path))
        progressed = True
        while pending and progressed:
            progressed = False
            for chain_name, spec in list(pending):
                if ready(spec):
                    self.emit_chain(chain_name, spec)
                    pending.remove((chain_name, spec))
                    progressed = True
        for chain_name, spec in pending:
            self.emit_chain(chain_name, spec)          # reports the unknown step

    # Header includes per platform pair. "posix" is a home the registry does not name:
    # VK_KHR_external_memory_fd is platform-neutral, but descriptor ownership needs dup(2).
    PLATFORM_INCLUDES = {
        "android": ["<vulkan/vulkan_android.h>"],
        "directfb": ["<directfb.h>", "<vulkan/vulkan_directfb.h>"],
        "fuchsia": ["<zircon/types.h>", "<vulkan/vulkan_fuchsia.h>"],
        "ggp": ["<ggp_c/vulkan_types.h>", "<vulkan/vulkan_ggp.h>"],
        "ios": ["<vulkan/vulkan_ios.h>"],
        "macos": ["<vulkan/vulkan_macos.h>"],
        "metal": ["<vulkan/vulkan_metal.h>"],
        "ohos": ["<vulkan/vulkan_ohos.h>"],
        "provisional": ["<vulkan/vulkan_beta.h>"],
        "sci": ["<nvscisync.h>", "<nvscibuf.h>", "<vulkan/vulkan_sci.h>"],
        "screen": ["<screen/screen.h>", "<vulkan/vulkan_screen.h>"],
        "ubm": ["<vulkan/vulkan_ubm.h>"],
        "vi": ["<vulkan/vulkan_vi.h>"],
        "win32": ["<Windows.h>", "<vulkan/vulkan_win32.h>"],
        "xlib": ["<X11/Xlib.h>", "<vulkan/vulkan_xlib.h>"],
        "xlib_xrandr": ["<X11/Xlib.h>", "<X11/extensions/Xrandr.h>", "<vulkan/vulkan_xlib_xrandr.h>"],
        "xcb": ["<xcb/xcb.h>", "<vulkan/vulkan_xcb.h>"],
        "wayland": ["<wayland-client.h>", "<vulkan/vulkan_wayland.h>"],
        "posix": [],
    }
    PLATFORM_SOURCE_INCLUDES: Dict[str, List[str]] = {}

    def format_flag_expressions(self, text: str) -> str:
        """Keep each operand of a compound mask on its own line after clang-format."""
        masked = re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                        lambda m: re.sub(r"[^\n]", " ", m[0]), text, flags=re.S)
        edits = []
        for match in re.finditer(r"\((Vk\w+)\)\s*\(", masked):
            if match[1] not in self.reg.bitmasks:
                continue
            depth, start = 1, match.end()
            parts = []
            for i in range(start, len(masked)):
                char = masked[i]
                if char == "(":
                    depth += 1
                elif char == ")":
                    depth -= 1
                    if depth == 0:
                        break
                elif depth == 1 and char in "|&^" and masked[i - 1] != char and masked[i + 1] not in (char, "="):
                    parts.append(text[start:i].strip() + " " + char)
                    start = i + 1
            else:
                continue
            if not parts or masked[match.start():i] != text[match.start():i]:
                continue
            parts.append(text[start:i].strip())
            column = match.start() - text.rfind("\n", 0, match.start()) - 1
            padding = " " * (column + 4)
            expression = f"({match[1]})(\n" + "\n".join(padding + re.sub(r"\s+", " ", part) for part in parts) + ")"
            edits.append((match.start(), i + 1, expression))
        for start, end, expression in reversed(edits):
            text = text[:start] + expression + text[end:]
        return text

    def finish_source(self, text: str) -> str:
        """Separate assignments from calls and restore Allman initializers after clang-format."""
        text = separate_assignments_from_calls(text)
        if not self.designated_initializers:
            return self.format_flag_expressions(text)
        pattern = (r"^([ \t]*)((?:\(\*pInfo\)|\w+(?:\[\w+\])?)[ \t]*=\s*\(Vk\w+\)|"
                   r"Vk\w+[ \t]+\w+[ \t]*=)\s*\{(?=\s*\.)(.*?)\};")

        def restore(match: re.Match) -> str:
            start = re.sub(r"\s+", " ", match[2])
            fields = [field.strip().split("=", 1) for field in re.split(r",\s*(?=\.)", match[3].strip())]
            values = [(name.strip()[1:], value.strip()) for name, value in fields]
            return "\n".join(match[1] + line for line in self.designated_initializer(start, values))

        return self.format_flag_expressions(re.sub(pattern, restore, text, flags=re.M | re.S))

    def finish_header(self, text: str) -> str:
        """Apply the configured layout to function declarations only."""
        if not self.single_line_prototypes:
            return text
        pattern = r"(?m)^[ \t]*(?:[A-Za-z_]\w*[ \t*]+)+(?:cvk|" + re.escape(self.prefix) + r")[A-Z]\w*[ \t]*\([^;{}]*\)[ \t]*;"
        return re.sub(pattern, lambda m: re.sub(r"\s+", " ", m.group()).strip(), text)

    def prototype_block(self, declarations: str) -> str:
        if not self.single_line_prototypes:
            return declarations
        # Keep subsequent standalone clang-format runs consistent with the policy.
        return "/* clang-format off */\n" + self.finish_header(declarations) + "\n/* clang-format on */"

    def platform_header(self, plat: str) -> str:
        guard = f"CVK_{plat.upper()}_H"
        includes = "\n".join(f"#include {i}" for i in self.PLATFORM_INCLUDES[plat])
        decls = "\n".join(self.decls[plat])
        return self.finish_header(f"#ifndef {guard}\n#define {guard}\n\n"
                "/* Generated by scripts/generate.py from the Vulkan registry. Do not edit. */\n\n"
                f"#include \"cvk/cvk.h\"\n{includes}\n\n"
                f"{self.prototype_block(decls)}\n\n#endif /* {guard} */\n")

    def platform_source(self, plat: str) -> str:
        defs = "\n".join(self.defs[plat])
        # The public platform header supplies its native SDK prerequisites. Keep explicit
        # protocol dependencies and directly used Vulkan declarations in the implementation.
        required = set(self.PLATFORM_SOURCE_INCLUDES.get(plat, []))
        required.update(self.direct_type_headers(defs) - set(self.PLATFORM_INCLUDES[plat]))
        required.update(header for header in self.PLATFORM_INCLUDES[plat] if header.startswith("<vulkan/"))
        required.add(self.vk_header)
        if re.search(r"\b(?:memcpy|memmove|memcmp|memset|str\w*)\s*\(", defs):
            required.add("<string.h>")
        includes = "".join(f"#include {i}\n" for i in sorted(required))
        # Expose this platform's extensions before Volk declares their dispatch pointers.
        # In particular, the pinned Volk does not include vulkan_ubm.h itself.
        preamble = "#ifndef VK_NO_PROTOTYPES\n#define VK_NO_PROTOTYPES\n#endif\n" if self.loader_header else ""
        return ("/* Generated by scripts/generate.py from the Vulkan registry. Do not edit. */\n\n"
                f"{preamble}#include \"cvk/cvk.h\"\n#include \"cvk/{plat}/cvk_{plat}.h\"\n{self.loader_include()}"
                f"{includes}\n{defs}\n")

    def loader_include(self) -> str:
        # Volk must suppress Vulkan prototypes before the public core header is read.
        return f"#include {self.loader_header}\n" if self.loader_header else ""

    def direct_type_headers(self, source: str) -> Set[str]:
        """Include the providers of used imported types, instead of relying on transitive headers."""
        code = re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', " ", source, flags=re.S)
        tokens = set(re.findall(r"\b[A-Za-z_]\w*\b", code))
        headers = {f"<{self.reg.type_headers[name]}>" for name in tokens & self.reg.type_headers.keys()}
        if any(re.fullmatch(r"u?int(?:8|16|32|64|ptr)_t|U?INT(?:8|16|32|64|PTR)_(?:MAX|MIN|C)", name) for name in tokens):
            headers.add("<stdint.h>")
        if tokens & {"VKAPI_ATTR", "VKAPI_CALL", "VKAPI_PTR"}:
            headers.add("<vulkan/vk_platform.h>")
        return headers

    def json_header(self, plat: str = "") -> str:
        guard = f"CVK_{plat.upper() + '_' if plat else ''}JSON_H"
        includes = '#include "cvk/cvk_json.h"\n' if plat else '#include "cvk/cvk.h"\n'
        if plat:
            includes += "".join(f"#include {i}\n" for i in self.PLATFORM_INCLUDES[plat])
        fragments = "\n\n".join(self.json_fragments) if not plat else ""
        declarations = self.prototype_block("\n".join(self.json_decls[plat]))
        return self.finish_header(f"#ifndef {guard}\n#define {guard}\n\n"
                "/* Generated by scripts/generate.py from the Vulkan registry. Do not edit. */\n\n"
                f"{includes}\n{fragments}\n\n{declarations}\n\n#endif /* {guard} */\n")

    def json_source(self, plat: str = "") -> str:
        header = f"cvk/{plat}/cvk_{plat}_json.h" if plat else "cvk/cvk_json.h"
        definitions = "\n".join(self.json_defs[plat])
        required = set(self.json_source_includes.get(plat, [])) | self.direct_type_headers(definitions)
        includes = "".join(f"#include {i}\n" for i in sorted(required))
        return ("/* Generated by scripts/generate.py from the Vulkan registry. Do not edit. */\n\n"
                f'#include "{header}"\n#include <string.h>\n{includes}\n{definitions}\n')

    def header(self) -> str:
        # Every policy capacity is emitted (protocols use some that no enumeration does), plus any derived.
        all_caps = dict(self.capacities)
        all_caps.update(self.used_capacities)
        caps = "\n".join(f"#define {k} ({v})" for k, v in sorted(all_caps.items()))
        return self.finish_header(f"""#ifndef CVK_H
#define CVK_H

/* Generated by scripts/generate.py from the Vulkan registry. Do not edit. */

#include {self.vk_header}
#include <stddef.h>
#include <stdint.h>

/* Fatal diagnostics. Override CVK_ASSERT before including to change the policy. */
void cvkFatal(const char* pExpression, const char* pFile, int line);
void cvkCheckResult(VkResult result, const char* pExpression, const char* pFile, int line);

#ifndef CVK_ASSERT
#define CVK_ASSERT(expression) ((expression) ? (void)0 : cvkFatal(#expression, __FILE__, __LINE__))
#endif
#define CVK_CHECK(expression) cvkCheckResult((expression), #expression, __FILE__, __LINE__)

/* Sentinel for an unavailable index. */
#define VK_INVALID (UINT32_MAX)

#define VK_NO_OFFSET ((VkDeviceSize)0)
#define VK_COUNT_OF(array) ((uint32_t)(sizeof(array) / sizeof((array)[0])))

/* Fixed capacities, from cvk.policy.json. */
{caps}

{(chr(10) * 2).join(self.fragments)}

{self.prototype_block(chr(10).join(self.decls[""]))}

#endif /* CVK_H */
""")

    def source(self) -> str:
        definitions = "\n".join(self.defs[""])
        required = set(self.PLATFORM_SOURCE_INCLUDES.get("", [])) | self.direct_type_headers(definitions)
        required -= {"<stdio.h>", "<stdlib.h>", "<string.h>", self.vk_header}
        return f"""/* Generated by scripts/generate.py from the Vulkan registry. Do not edit. */

{self.loader_include()}#include "cvk/cvk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include {self.vk_header}
{"".join(f"#include {i}{chr(10)}" for i in sorted(required))}
void cvkFatal(const char* pExpression, const char* pFile, int line)
{{
    (void)fprintf(stderr, "%s:%d: cvk fatal: %s\\n", pFile, line, pExpression);
    abort();
}}

void cvkCheckResult(VkResult result, const char* pExpression, const char* pFile, int line)
{{
    if (VK_SUCCESS != result)
    {{
        cvkFatal(pExpression, pFile, line);
    }}
}}

{definitions}
"""

def load_policy(path: str) -> dict:
    """A policy may extend another (scvk.policy.json extends cvk.policy.json): the base is
    loaded first and the extending file's keys replace the base's."""
    with open(path, encoding="utf-8") as f:
        policy = json.load(f)
    if "extends" in policy:
        base = load_policy(os.path.join(os.path.dirname(os.path.abspath(path)), policy.pop("extends")))
        base.update(policy)
        policy = base
    return policy


def brand(text: str, prefix: str) -> str:
    """SPEC.md section 3: every emitted identifier carries the profile's prefix. The generator
    composes with the cvk spelling; the prefix is applied to the whole output, so a profile is
    a standalone library (scvk.h/scvk.c for Vulkan SC)."""
    if prefix == "cvk":
        return text
    text = re.sub(r"(?<![A-Za-z0-9_])cvk(?=[A-Z_./\s\"])", prefix, text)
    text = re.sub(r"(?<![A-Za-z0-9_])CVK_", prefix.upper() + "_", text)
    text = text.replace("PFN_cvk", "PFN_" + prefix)
    return text


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--policy", default=os.path.join(REPO_ROOT, "cvk.policy.json"))
    ap.add_argument("--out-dir", default=REPO_ROOT)
    args = ap.parse_args()
    args.out_dir = os.path.abspath(args.out_dir)
    policy = load_policy(args.policy)
    reg = Registry.load(os.path.join(REPO_ROOT, policy["registry"]) if "registry" in policy else Registry.load.__defaults__[0], policy.get("api", "vulkan"))
    g = Generator(reg, policy)
    g.run()
    for stale in sorted(set(policy.get("name_overrides", {})) - g.used_overrides):
        g.errors.append(f"name_overrides entry never matched a generated name: {stale}")
    if g.errors:
        for e in g.errors:
            print("error:", e)
        sys.exit(1)
    px = g.prefix
    include_dir = os.path.join(args.out_dir, "include", px)
    os.makedirs(include_dir, exist_ok=True)
    os.makedirs(os.path.join(args.out_dir, "src"), exist_ok=True)
    with open(os.path.join(include_dir, f"{px}.h"), "w", encoding="utf-8", newline="\n") as f:
        f.write(brand(g.header(), px))
    with open(os.path.join(args.out_dir, "src", f"{px}.c"), "w", encoding="utf-8", newline="\n") as f:
        f.write(brand(g.source(), px))
    outputs = [os.path.join(include_dir, f"{px}.h"), os.path.join(args.out_dir, "src", f"{px}.c")]
    for plat in sorted(k for k in g.decls if k):
        hp = os.path.join(include_dir, plat, f"{px}_{plat}.h")
        cp = os.path.join(args.out_dir, "src", plat, f"{px}_{plat}.c")
        os.makedirs(os.path.dirname(hp), exist_ok=True)
        os.makedirs(os.path.dirname(cp), exist_ok=True)
        with open(hp, "w", encoding="utf-8", newline="\n") as f:
            f.write(brand(g.platform_header(plat), px))
        with open(cp, "w", encoding="utf-8", newline="\n") as f:
            f.write(brand(g.platform_source(plat), px))
        outputs += [hp, cp]
    for plat in sorted(g.json_decls):
        stem = f"{px}_{plat}_json" if plat else f"{px}_json"
        hp = os.path.join(include_dir, plat, stem + ".h")
        cp = os.path.join(args.out_dir, "src", plat, stem + ".c")
        os.makedirs(os.path.dirname(hp), exist_ok=True)
        os.makedirs(os.path.dirname(cp), exist_ok=True)
        with open(hp, "w", encoding="utf-8", newline="\n") as f:
            f.write(brand(g.json_header(plat), px))
        with open(cp, "w", encoding="utf-8", newline="\n") as f:
            f.write(brand(g.json_source(plat), px))
        outputs += [hp, cp]
    if g.dropped:
        print(f"dropped {len(g.dropped)} compositions outside the {g.api} profile")
    # Use clang-format's layout, then restore the requested initializer braces.
    import shutil
    import subprocess
    # clang-format is part of the generator's toolchain: without it the output is not the
    # output, and reproducibility (SPEC.md rule zero) would silently depend on the machine.
    clang_format = shutil.which("clang-format")
    if not clang_format:
        print("error: clang-format is required to generate (it defines the output's formatting)")
        sys.exit(1)
    style = os.path.join(REPO_ROOT, ".clang-format")
    subprocess.run([clang_format, "-i", f"--style=file:{style}"] + outputs, check=True, cwd=args.out_dir)
    for path in outputs:
        if path.endswith(".c"):
            with open(path, encoding="utf-8") as f:
                text = g.finish_source(f.read())
            with open(path, "w", encoding="utf-8", newline="\n") as f:
                f.write(text)
    t0 = sum(1 for n in g.names.values() if n.startswith("vk") and "+" not in n)
    builders = sum(1 for n in g.names if n.startswith("cvkInit"))
    conv = sum(1 for n in g.names.values() if "+" in n)
    per = ", ".join(f"{k or 'core'}={len(v)}" for k, v in sorted(g.decls.items()))
    per += ", " + ", ".join(f"{k + '/' if k else ''}json={len(v)}" for k, v in sorted(g.json_decls.items()))
    print(f"generated {len(outputs)} files ({per}): tier0={t0} builders={builders} conveniences={conv} capacities={len(g.used_capacities)}")


if __name__ == "__main__":
    main()
