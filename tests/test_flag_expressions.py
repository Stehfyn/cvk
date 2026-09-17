"""Check registry-directed flag casts and unsigned mask evaluation."""

import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from generate import Generator, load_policy
from vkxml import Registry


def implicit_flag_conversions(reg, ast):
    """Find implicit conversions of flag enumerants/values, including compound expressions.

    C enumerant references have type int, so also inspect EnumConstantDecl names rather
    than relying solely on the expression's type. Explicit casts stop this search.
    """
    masks = set(reg.bitmasks) | {"VkFlags", "VkFlags64", "unsigned int", "unsigned long long"}
    enums = {enum for enum in reg.bitmasks.values() if enum}
    enums.update(alias for alias, target in reg.type_aliases.items() if target in enums)
    bits = {value for enum in enums for value in reg.enums.get(enum, []) + reg.enum_extends.get(enum, [])}
    signatures = {command.name: [param.full_type for param in command.params] for command in reg.commands.values()}

    def uncast_bits(node):
        if node.get("kind") == "CStyleCastExpr":
            return False
        ctype = node.get("type", {}).get("qualType", "").removeprefix("const ").removeprefix("enum ")
        if ctype in enums or node.get("referencedDecl", {}).get("name") in bits:
            return True
        if node.get("kind") == "CallExpr":
            return False  # A flags-valued call may legitimately take bit-enum arguments.
        children = node.get("inner", [])
        if node.get("kind") == "ConditionalOperator":
            children = children[-2:]
        return any(uncast_bits(child) for child in children)

    def referenced_function(node):
        if "referencedDecl" in node:
            return node["referencedDecl"].get("name")
        return next((name for child in node.get("inner", []) if (name := referenced_function(child))), None)

    def diagnostic(node, function, target):
        location = node.get("range", {}).get("begin", {})
        return function, location.get("expansionLoc", location), target

    def visit(node, function):
        if (node.get("kind") == "ImplicitCastExpr" and node.get("castKind") == "IntegralCast"
                and node.get("type", {}).get("qualType") in masks and uncast_bits(node)):
            yield diagnostic(node, function, node["type"]["qualType"])
        # 64-bit bit types are integer typedefs, so Clang may omit IntegralCast entirely.
        children = node.get("inner", [])
        if node.get("kind") == "CallExpr" and children:
            parameters = signatures.get(referenced_function(children[0]), [])
            for target, argument in zip(parameters, children[1:]):
                if target in reg.bitmasks and uncast_bits(argument):
                    yield diagnostic(argument, function, target)
        if node.get("kind") in ("BinaryOperator", "CompoundAssignOperator") and node.get("opcode", "").endswith("=") and len(children) == 2:
            target = children[0].get("type", {}).get("qualType")
            if target in reg.bitmasks and uncast_bits(children[1]):
                yield diagnostic(children[1], function, target)
        for child in node.get("inner", []):
            yield from visit(child, function)

    decoder = json.JSONDecoder()
    offset = 0
    while offset < len(ast):
        if ast[offset].isspace():
            offset += 1
            continue
        node, offset = decoder.raw_decode(ast, offset)
        if node.get("kind") == "FunctionDecl":
            signatures[node["name"]] = [child["type"]["qualType"] for child in node.get("inner", []) if child.get("kind") == "ParmVarDecl"]
        if node.get("kind") == "FunctionDecl" and any(child.get("kind") == "CompoundStmt" for child in node.get("inner", [])):
            yield from visit(node, node["name"])


class FlagExpressionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.gen = Generator(Registry.load(), load_policy(str(ROOT / "cvk.policy.json")))

    @unittest.skipUnless(shutil.which("clang"), "clang is required for implicit-conversion checks")
    def test_generated_code_has_no_implicit_bit_to_mask_conversions(self):
        for profile in ("cvk", "scvk"):
            policy = load_policy(str(ROOT / f"{profile}.policy.json"))
            reg = Registry.load(str(ROOT / policy["registry"]), policy["api"]) if "registry" in policy else Registry.load()
            includes = ["-I", str(ROOT / "include"), "-isystem", str(ROOT / "external" / ("VulkanSC-Headers" if profile == "scvk" else "Vulkan-Headers") / "include")]
            if profile == "cvk":
                includes += ["-DVK_NO_PROTOTYPES", "-isystem", str(ROOT / "external/volk"),
                             "-isystem", str(ROOT / "external/Vulkan-Utility-Libraries/include")]
            for filename in (f"{profile}.c", f"{profile}_json.c"):
                path = ROOT / "src" / filename
                with self.subTest(source=filename):
                    ast = subprocess.check_output(["clang", "-std=c99", "-fsyntax-only", *includes,
                                                   "-Xclang", "-ast-dump=json", "-Xclang", f"-ast-dump-filter={profile}", str(path)], text=True)
                    source = path.read_text()
                    errors = [(function, source.count("\n", 0, location.get("offset", 0)) + 1, target)
                              for function, location, target in implicit_flag_conversions(reg, ast)]
                    self.assertEqual(errors, [])

    def test_destination_and_operand_types(self):
        flags = self.gen.typed_flags
        self.assertEqual(flags("VkColorComponentFlags", "VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT"),
                         "(VkColorComponentFlags)((VkFlags)VK_COLOR_COMPONENT_R_BIT | (VkFlags)VK_COLOR_COMPONENT_G_BIT)")
        self.assertEqual(flags("VkBufferUsageFlags", "usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT"),
                         "(VkBufferUsageFlags)(usage | (VkFlags)VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)")
        self.assertEqual(flags("VkPipelineStageFlags2KHR", "VK_PIPELINE_STAGE_2_COPY_BIT_KHR"),
                         "(VkPipelineStageFlags2KHR)VK_PIPELINE_STAGE_2_COPY_BIT_KHR")
        self.assertEqual(flags("VkPipelineStageFlags", "VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT"),
                         "(VkPipelineStageFlags)VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT")
        for ctype, expression in (("VkBlendFactor", "VK_BLEND_FACTOR_ONE"), ("VkBool32", "VK_FALSE"),
                                  ("VkColorComponentFlags", "colorWriteMask"), ("VkBufferUsageFlags*", "pUsage")):
            self.assertEqual(flags(ctype, expression), expression)

    def test_unsigned_complement_and_64_bit_masks(self):
        clang = shutil.which("clang")
        if not clang:
            self.skipTest("clang is unavailable")
        flags = self.gen.typed_flags
        complement = flags("VkColorComponentFlags", "~VK_COLOR_COMPONENT_R_BIT")
        wide = flags("VkPipelineStageFlags2KHR", "VK_PIPELINE_STAGE_2_COPY_BIT_KHR | VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT_KHR")
        combined = flags("VkColorComponentFlags", "mask & (VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT)")
        source = f"""#include <vulkan/vulkan_core.h>
int main(void)
{{
    VkColorComponentFlags mask = 0xFu;
    VkColorComponentFlags complement = {complement};
    VkPipelineStageFlags2KHR wide = {wide};
    VkColorComponentFlags combined = {combined};
    if (complement != 0xFFFFFFFEu || combined != 3u) return 1;
    if (wide != ((UINT64_C(1) << 32) | (UINT64_C(1) << 16))) return 2;
    return 0;
}}
"""
        with tempfile.TemporaryDirectory() as tmp:
            cfile, exe = Path(tmp) / "flags.c", Path(tmp) / "flags.exe"
            cfile.write_text(source)
            subprocess.run([clang, "-std=c99", "-Wall", "-Wextra", "-Wconversion", "-Werror", "-pedantic-errors",
                            "-isystem", str(ROOT / "external/Vulkan-Headers/include"), str(cfile), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    unittest.main()
