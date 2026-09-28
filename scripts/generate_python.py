"""Generate native-ABI ctypes bindings from CVK's actual C headers.

Requires libclang only at build time. Never guesses a C type or silently drops a
function. Record layouts are checked by Python at import time. By-value records
go through generated C adapters so unions/bitfields never cross libffi by value.
"""

from __future__ import annotations

import argparse
import ast
from pathlib import Path
import re
import pprint

from clang import cindex as ci

ROOT = Path(__file__).resolve().parents[1]


def python_name(name):
    name = re.sub(r"^p+(?=[A-Z])", "", name)
    return re.sub(r"(?<=[a-z0-9])(?=[A-Z])|(?<=[A-Z])(?=[A-Z][a-z])", "_", name).lower()


def singular(name):
    name = python_name(name).removeprefix("p_")
    if name.endswith("indices"):
        return name[:-7] + "index"
    if name.endswith("properties"):
        return name[:-10] + "property"
    return name.removesuffix("s")


class Bindings:
    def __init__(self, headers, clang_args):
        from generate import Generator, load_policy
        from vkxml import Registry

        self.registry = Registry.load()
        self.roles = Generator(
            self.registry, load_policy(str(ROOT / "cvk.policy.json"))
        )
        self.roles.run()
        if self.roles.errors:
            raise RuntimeError(self.roles.errors)
        self.headers = headers
        source = "\n".join(f'#include "{h}"' for h in headers)
        self.tu = ci.Index.create().parse(
            "cvk_python_input.c",
            args=[
                "-x",
                "c",
                "-DVK_NO_PROTOTYPES",
                f"-I{ROOT / 'include'}",
                f"-I{ROOT / 'external/Vulkan-Headers/include'}",
                *clang_args,
            ],
            unsaved_files=[("cvk_python_input.c", source)],
            options=ci.TranslationUnit.PARSE_DETAILED_PROCESSING_RECORD,
        )
        errors = [
            str(d) for d in self.tu.diagnostics if d.severity >= ci.Diagnostic.Error
        ]
        if errors:
            raise RuntimeError("\n".join(errors))
        self.nodes = list(self.tu.cursor.get_children())
        self.records = {}
        self.definitions = []
        self.layouts = {}
        self.aliases = {}

    def record_name(self, cursor):
        return cursor.spelling or (
            "_Anonymous_"
            + Path(cursor.location.file.name).stem
            + f"_{cursor.location.line}_{cursor.location.column}"
        )

    def type(self, typ):
        k = typ.kind
        primitives = {
            ci.TypeKind.VOID: "None",
            ci.TypeKind.BOOL: "ct.c_bool",
            ci.TypeKind.CHAR_S: "ct.c_char",
            ci.TypeKind.CHAR_U: "ct.c_char",
            ci.TypeKind.SCHAR: "ct.c_int8",
            ci.TypeKind.UCHAR: "ct.c_uint8",
            ci.TypeKind.SHORT: "ct.c_short",
            ci.TypeKind.USHORT: "ct.c_ushort",
            ci.TypeKind.INT: "ct.c_int",
            ci.TypeKind.UINT: "ct.c_uint",
            ci.TypeKind.LONG: "ct.c_long",
            ci.TypeKind.ULONG: "ct.c_ulong",
            ci.TypeKind.LONGLONG: "ct.c_longlong",
            ci.TypeKind.ULONGLONG: "ct.c_ulonglong",
            ci.TypeKind.FLOAT: "ct.c_float",
            ci.TypeKind.DOUBLE: "ct.c_double",
        }
        if k in primitives:
            return primitives[k]
        if k in (ci.TypeKind.TYPEDEF, ci.TypeKind.ELABORATED):
            return self.type(typ.get_canonical())
        if k == ci.TypeKind.ENUM:
            return self.type(typ.get_declaration().enum_type)
        if k == ci.TypeKind.POINTER:
            pointee = typ.get_pointee().get_canonical()
            if pointee.kind == ci.TypeKind.VOID:
                return "ct.c_void_p"
            if pointee.kind in (ci.TypeKind.CHAR_S, ci.TypeKind.CHAR_U):
                return "ct.c_char_p"
            if (
                pointee.kind == ci.TypeKind.RECORD
                and not pointee.get_declaration().is_definition()
            ):
                return "ct.c_void_p"  # Vulkan handles and opaque native objects.
            if pointee.kind == ci.TypeKind.FUNCTIONPROTO:
                args = [
                    self.type(pointee.get_result()),
                    *(self.type(a) for a in pointee.argument_types()),
                ]
                # Vulkan callbacks use VKAPI_CALL, which is stdcall on 32-bit Windows.
                convention = (
                    "_VKCALL" if "__stdcall" in pointee.spelling else "ct.CFUNCTYPE"
                )
                return f"{convention}({', '.join(args)})"
            return f"ct.POINTER({self.type(pointee)})"
        if k == ci.TypeKind.CONSTANTARRAY:
            return f"({self.type(typ.element_type)} * {typ.element_count})"
        if k == ci.TypeKind.RECORD:
            cursor = typ.get_declaration()
            name = self.record_name(cursor)
            if name not in self.records:
                self.records[name] = (
                    "ct.Union"
                    if cursor.kind == ci.CursorKind.UNION_DECL
                    else "ct.Structure"
                )
                fields = []
                offsets = {}
                for field in cursor.get_children():
                    if field.kind != ci.CursorKind.FIELD_DECL:
                        continue
                    expr = self.type(field.type)
                    bits = (
                        f", {field.get_bitfield_width()}" if field.is_bitfield() else ""
                    )
                    fields.append(f"({field.spelling!r}, {expr}{bits})")
                    if not field.is_bitfield():
                        offsets[field.spelling] = field.get_field_offsetof() // 8
                if cursor.is_definition():
                    self.definitions.append(
                        f"{name}._fields_ = [\n    " + ",\n    ".join(fields) + ",\n]"
                    )
                    self.layouts[name] = (typ.get_size(), typ.get_align(), offsets)
            return name
        raise TypeError(f"Unsupported C type: {typ.spelling} ({k})")

    def generate(self):
        functions = []
        python_functions = {}
        exports = [
            "cvkPythonInitialize",
            "cvkPythonLoadInstance",
            "cvkPythonLoadDevice",
        ]
        adapters = [
            "/* Generated by scripts/generate_python.py. Do not edit. */",
            *(f'#include "{h}"' for h in self.headers),
        ]
        constants = {}
        for node in self.nodes:
            if node.kind == ci.CursorKind.ENUM_DECL:
                for child in node.get_children():
                    if child.kind == ci.CursorKind.ENUM_CONSTANT_DECL:
                        constants[child.spelling] = child.enum_value
            if node.kind == ci.CursorKind.TYPEDEF_DECL and node.spelling.startswith(
                ("Vk", "StdVideo", "cvk", "PFN_vk", "PFN_cvk")
            ):
                self.aliases[node.spelling] = self.type(node.underlying_typedef_type)
            if (
                node.kind != ci.CursorKind.FUNCTION_DECL
                or not node.spelling.startswith("cvk")
            ):
                continue
            result = self.type(node.result_type)
            args = list(node.get_arguments())
            names = [a.spelling for a in args]
            types = []
            indirect = []
            declarations = []
            forwarding = []
            for i, arg in enumerate(args):
                typ = arg.type.get_canonical()
                expr = self.type(typ)
                if typ.kind == ci.TypeKind.RECORD:
                    indirect.append(i)
                    types.append(f"ct.POINTER({expr})")
                    declarations.append(f"const {arg.type.spelling}* {arg.spelling}")
                    forwarding.append(f"*{arg.spelling}")
                elif typ.kind == ci.TypeKind.CONSTANTARRAY:
                    types.append(f"ct.POINTER({self.type(typ.element_type)})")
                    declarations.append(f"{typ.element_type.spelling}* {arg.spelling}")
                    forwarding.append(arg.spelling)
                else:
                    types.append(expr)
                    declarations.append(f"{arg.type.spelling} {arg.spelling}")
                    forwarding.append(arg.spelling)
            symbol = node.spelling
            exports.append(symbol)
            if indirect:
                symbol = "cvkPython_" + symbol
                exports.append(symbol)
                ret = "" if node.result_type.kind == ci.TypeKind.VOID else "return "
                adapters.append(
                    f"{node.result_type.spelling} {symbol}({', '.join(declarations)})\n"
                    f"{{\n    {ret}{node.spelling}({', '.join(forwarding)});\n}}"
                )
            functions.append(
                f"    {node.spelling!r}: ({symbol!r}, {result}, [{', '.join(types)}], {names!r}, {indirect!r}),"
            )
            python_function = python_name(node.spelling[3:])
            if python_function in python_functions:
                raise RuntimeError(f"Python function name collision: {python_function}")
            python_functions[python_function] = self.python_metadata(node, args)
        # Scalar flag bits in current headers are static const variables, not enums.
        # Include simple object macros (capacities, API versions, limits, strings).
        pending = {}
        for node in self.nodes:
            if not node.spelling.startswith(("VK_", "CVK_", "STD_VIDEO_")):
                continue
            tokens = [t.spelling for t in node.get_tokens()]
            if node.kind == ci.CursorKind.MACRO_DEFINITION:
                if len(tokens) > 1:
                    pending[node.spelling] = " ".join(tokens[1:])
            elif node.kind == ci.CursorKind.VAR_DECL and "=" in tokens:
                pending[node.spelling] = " ".join(
                    tokens[tokens.index("=") + 1 :]
                ).rstrip(" ;")
        constants.update(VK_NULL_HANDLE=None, VK_NO_OFFSET=0, VK_INVALID=0xFFFFFFFF)
        for _ in range(10):
            progress = False
            for name, value in list(pending.items()):
                try:
                    constants[name] = constant(
                        value,
                        {
                            "UINT32_MAX": 0xFFFFFFFF,
                            "UINT64_MAX": 0xFFFFFFFFFFFFFFFF,
                            **constants,
                        },
                    )
                except (ValueError, SyntaxError, KeyError, TypeError):
                    continue  # Function macros and diagnostic expressions are not values.
                del pending[name]
                progress = True
            if not progress:
                break
        output = [
            '"""Generated by scripts/generate_python.py. Do not edit."""',
            "import ctypes as ct",
            "import sys",
            "_VKCALL = getattr(ct, 'WINFUNCTYPE', ct.CFUNCTYPE)",
            "if ct.sizeof(ct.c_void_p) != 8:",
            "    raise ImportError('CVK Python bindings require a 64-bit Python interpreter')",
            *[f"{name} = {value!r}" for name, value in sorted(constants.items())],
            *[
                f"class {name}({base}):\n    pass"
                for name, base in self.records.items()
            ],
            *self.definitions,
            *[
                f"{name} = {expr}"
                for name, expr in sorted(self.aliases.items())
                if name != expr
            ],
            f"_LAYOUTS = {self.layouts!r}",
            "for _name, (_size, _alignment, _offsets) in _LAYOUTS.items():",
            "    _type = globals()[_name]",
            "    if ct.sizeof(_type) != _size or ct.alignment(_type) != _alignment:",
            "        raise ImportError(f'CVK native layout mismatch for {_name}')",
            "    for _field, _offset in _offsets.items():",
            "        if getattr(_type, _field).offset != _offset:",
            "            raise ImportError(f'CVK native layout mismatch for {_name}.{_field}')",
            "FUNCTIONS = {",
            *functions,
            "}",
            "",
            "PYTHON_FUNCTIONS = "
            + pprint.pformat(python_functions, width=88, sort_dicts=False),
        ]
        return (
            "\n".join(output),
            "\n\n".join(adapters) + "\n",
            "EXPORTS\n" + "\n".join(sorted(exports)) + "\n",
            len(functions),
        )

    def python_metadata(self, node, args):
        names = [arg.spelling for arg in args]
        roles = self.roles.sigs.get(node.spelling)
        roles = roles[1] if roles and [p.name for p in roles[1]] == names else None
        source = self.roles.names.get(node.spelling)
        source = self.registry.commands.get(source) or self.registry.structs.get(source)
        original = getattr(source, "params", getattr(source, "members", []))
        lengths = {param.name: param.len for param in original if param.len}
        outputs = {}
        counts = {}
        defaults = {}
        unsafe_output = False
        for index, arg in enumerate(args):
            typ = arg.type.get_canonical()
            role = roles[index] if roles else None
            count_index = None
            length = lengths.get(arg.spelling, "")
            if not length and role:
                length = self.parameter_length(node.spelling, role)
            length = length.split(",")[0].split("::")[-1]
            if length in names:
                count_index = names.index(length)
            else:
                for other, name in enumerate(names):
                    stem = re.sub(r"Count$", "", re.sub(r"^p(?=[A-Z])", "", name))
                    if name.endswith("Count") and singular(stem) == singular(
                        arg.spelling
                    ).removesuffix("_name"):
                        count_index = other
                        break
            if role and role.is_out:
                if typ.kind == ci.TypeKind.CONSTANTARRAY:
                    outputs[index] = (typ.element_count, count_index)
                elif typ.kind == ci.TypeKind.POINTER:
                    pointee = typ.get_pointee().get_canonical()
                    if pointee.kind in (
                        ci.TypeKind.VOID,
                        ci.TypeKind.CHAR_S,
                        ci.TypeKind.CHAR_U,
                    ):
                        unsafe_output = True
                    elif count_index is not None:
                        outputs[index] = (None, count_index)
                    elif length and length != "null-terminated":
                        unsafe_output = True
                    elif arg.spelling.endswith("s") and not arg.spelling.endswith(
                        (
                            "Properties",
                            "Requirements",
                            "Features",
                            "Capabilities",
                            "Status",
                            "Address",
                            "Limits",
                        )
                    ):
                        unsafe_output = True
                    else:
                        outputs[index] = (None, None)
                continue
            if count_index is not None and typ.kind == ci.TypeKind.POINTER:
                count_type = args[count_index].type.get_canonical()
                if count_type.kind in (
                    ci.TypeKind.UINT,
                    ci.TypeKind.INT,
                    ci.TypeKind.ULONG,
                    ci.TypeKind.ULONGLONG,
                ):
                    counts.setdefault(count_index, []).append(index)
                    defaults[count_index] = None
                    defaults[index] = None
            name = python_name(arg.spelling)
            if name == "next" or name.endswith("_next") or name == "allocator":
                defaults[index] = None
            elif name == "flags" or re.search(
                r"(?:Create|Allocate|Reset|Map|Dependency|Submit)Flags",
                arg.type.spelling,
            ):
                defaults[index] = 0
            elif name in (
                "application_info",
                "enabled_features",
                "specialization_info",
                "inheritance_info",
            ):
                defaults[index] = None
            elif name == "queue_priority":
                defaults[index] = 1.0
            elif name in ("sharing_mode", "offset", "dst_offset", "src_offset"):
                defaults[index] = 0
            elif name == "command_buffer_count":
                defaults[index] = 1
            elif name == "fence" and node.spelling.startswith("cvkQueueSubmit"):
                defaults[index] = None
        if unsafe_output:
            outputs = {}
        for index in outputs:
            defaults.pop(index, None)
        python_parameters = [python_name(name) for name in names]
        for index, name in enumerate(names):
            if (
                name.startswith("pp")
                and python_parameters.count(python_parameters[index]) > 1
            ):
                python_parameters[index] = singular(name) + "_pointers"
        inputs = [
            name for index, name in enumerate(python_parameters) if index not in outputs
        ]
        if len(set(inputs)) != len(inputs):
            raise RuntimeError(f"Python parameter name collision: {node.spelling}")
        return (node.spelling, python_parameters, outputs, counts, defaults)

    def parameter_length(self, function, parameter, visited=None):
        visited = set() if visited is None else visited
        if (function, parameter.name) in visited:
            return ""
        visited.add((function, parameter.name))
        source = self.roles.names.get(function)
        source = self.registry.commands.get(source) or self.registry.structs.get(source)
        for original in getattr(source, "params", getattr(source, "members", [])):
            if original.name == parameter.name and original.len:
                return original.len
        member = re.search(r"(Vk\w+)::(\w+)", parameter.doc)
        if member and member[1] in self.registry.structs:
            for original in self.registry.structs[member[1]].members:
                if original.name == member[2] and original.len:
                    return original.len
        array = re.search(r"Array of (\w+) entries", parameter.doc)
        if array:
            return array[1]
        parent = re.search(r"See (cvk\w+)", parameter.doc)
        if parent and parent[1] in self.roles.sigs:
            for original in self.roles.sigs[parent[1]][1]:
                if original.name == parameter.name:
                    return self.parameter_length(parent[1], original, visited)
        return ""


def constant(expression, symbols):
    """Evaluate the small integer/string expression language used by C constants."""
    width = (
        64
        if re.search(r"\b(?:0x[0-9a-fA-F]+|[0-9]+)[uUlL]*[lL]{2}", expression)
        else 32
    )
    expression = re.sub(
        r"\b(0x[0-9a-fA-F]+)[uUlL]+\b|\b([0-9]+(?:\.[0-9]*)?(?:[eE][+-]?[0-9]+)?)[uUlLfF]+\b",
        lambda match: match[1] or match[2],
        expression,
    )
    expression = re.sub(r"\(\s*(?:Vk\w+|u?int\d+_t)\s*\)", "", expression)
    node = ast.parse(expression, mode="eval").body

    def visit(n):
        if isinstance(n, ast.Constant) and isinstance(n.value, (str, int, float)):
            return n.value
        if isinstance(n, ast.Name):
            return symbols[n.id]
        if isinstance(n, ast.UnaryOp):
            value = visit(n.operand)
            if isinstance(n.op, ast.Invert):
                return ~value & ((1 << width) - 1)
            if isinstance(n.op, ast.USub):
                return -value
            if isinstance(n.op, ast.UAdd):
                return value
        if isinstance(n, ast.BinOp):
            left, right = visit(n.left), visit(n.right)
            operations = {
                ast.BitOr: lambda: left | right,
                ast.LShift: lambda: left << right,
                ast.Add: lambda: left + right,
                ast.Sub: lambda: left - right,
            }
            if type(n.op) in operations:
                return operations[type(n.op)]()
        if isinstance(n, ast.Call) and isinstance(n.func, ast.Name):
            args = [visit(a) for a in n.args]
            if n.func.id == "VK_MAKE_API_VERSION" and len(args) == 4:
                return args[0] << 29 | args[1] << 22 | args[2] << 12 | args[3]
            if n.func.id == "VK_MAKE_VERSION" and len(args) == 3:
                return args[0] << 22 | args[1] << 12 | args[2]
        raise ValueError(expression)

    return visit(node)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out-dir", type=Path, required=True)
    parser.add_argument("--platform", action="append", default=[])
    parser.add_argument("--clang-arg", action="append", default=[])
    args = parser.parse_args()
    import struct

    if struct.calcsize("P") != 8:
        parser.error("CVK Python currently requires a 64-bit build")
    headers = ["cvk.h", "cvk_json.h"]
    for platform in args.platform:
        headers.append(f"{platform}/cvk_{platform}.h")
        if (ROOT / "include" / platform / f"cvk_{platform}_json.h").exists():
            headers.append(f"{platform}/cvk_{platform}_json.h")
    generated, adapters, exports, count = Bindings(headers, args.clang_arg).generate()
    args.out_dir.mkdir(parents=True, exist_ok=True)
    (args.out_dir / "_generated.py").write_text(
        generated, encoding="utf-8", newline="\n"
    )
    (args.out_dir / "_adapters.c").write_text(adapters, encoding="utf-8", newline="\n")
    (args.out_dir / "_exports.def").write_text(exports, encoding="utf-8", newline="\n")
    print(f"Generated {count} CVK function bindings into {args.out_dir}")


if __name__ == "__main__":
    main()
