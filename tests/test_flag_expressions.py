"""Check registry-directed flag casts and unsigned mask evaluation."""

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


class FlagExpressionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.gen = Generator(Registry.load(), load_policy(str(ROOT / "cvk.policy.json")))

    def test_destination_and_operand_types(self):
        flags = self.gen.typed_flags
        self.assertEqual(flags("VkColorComponentFlags", "VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT"),
                         "(VkColorComponentFlags)((VkFlags)VK_COLOR_COMPONENT_R_BIT | (VkFlags)VK_COLOR_COMPONENT_G_BIT)")
        self.assertEqual(flags("VkBufferUsageFlags", "usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT"),
                         "(VkBufferUsageFlags)(usage | (VkFlags)VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)")
        self.assertEqual(flags("VkPipelineStageFlags2KHR", "VK_PIPELINE_STAGE_2_COPY_BIT_KHR"),
                         "(VkPipelineStageFlags2KHR)((VkFlags64)VK_PIPELINE_STAGE_2_COPY_BIT_KHR)")
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
