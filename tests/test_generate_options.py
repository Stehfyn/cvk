"""Check loading assumptions and optional API documentation without writing outputs."""

import copy
from pathlib import Path
import re
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from generate import Generator, load_policy
from vkxml import Registry


class GeneratorOptionsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        reg = Registry.load()
        policy = load_policy(str(ROOT / "cvk.policy.json"))
        cls.variants = {}
        for enabled in (False, True):
            settings = copy.deepcopy(policy)
            settings["doc_comments"] = enabled
            gen = Generator(reg, settings)
            gen.PLATFORM_SOURCE_INCLUDES = {}
            gen.run()
            if gen.errors:
                raise AssertionError(gen.errors)
            cls.variants[enabled] = gen

    def test_documentation_switch_preserves_api_and_bodies(self):
        off, on = self.variants[False], self.variants[True]
        self.assertEqual(off.sigs, on.sigs)
        self.assertEqual(off.defs, on.defs)
        for platform, declarations in on.decls.items():
            stripped = [re.sub(r"/\*\*.*?\*/\s*", "", decl, flags=re.S) for decl in declarations]
            self.assertEqual(stripped, off.decls[platform])
        self.assertIn("\\brief", on.header())
        self.assertNotIn("/**", off.header())
        for platform in off.decls:
            if platform:
                self.assertNotIn("/**", off.platform_header(platform))

    def test_child_handles_do_not_gain_lookup_arguments(self):
        gen = self.variants[False]
        self.assertEqual(gen.sigs["cvkCmdDrawMeshTasks"][1][0].name, "commandBuffer")
        self.assertEqual(gen.sigs["cvkAcquireDrmDisplay"][1][0].name, "physicalDevice")

    def test_every_wrapper_calls_its_vk_entry_point(self):
        gen = self.variants[False]
        source = gen.source()
        for platform in gen.defs:
            if platform:
                source += gen.platform_source(platform)
        calls = set(re.findall(r"\b(vk[A-Z]\w*)\(", source))
        self.assertTrue({command.name for command in gen.cmds} <= calls)
        for old_symbol in ("CallCache", "CVK_CALL", "THREAD_LOCAL", "stdatomic"):
            self.assertNotIn(old_symbol, source)
        self.assertIn("#include <vulkan/vulkan_core.h>", gen.header())
        self.assertNotIn("volk.h", gen.header())
        self.assertNotIn("__cplusplus", gen.header())
        self.assertTrue(gen.source().index("#include <volk.h>") < gen.source().index('#include "cvk.h"'))
        for platform in gen.decls:
            if platform:
                self.assertNotIn("volk.h", gen.platform_header(platform))
                self.assertNotIn("__cplusplus", gen.platform_header(platform))
                self.assertIn("#include <volk.h>", gen.platform_source(platform))

    def test_sc_policy_inherits_disabled_docs_and_keeps_native_header(self):
        policy = load_policy(str(ROOT / "scvk.policy.json"))
        self.assertFalse(policy["doc_comments"])
        self.assertEqual(policy["header"], "<vulkan/vulkan_sc_core.h>")


if __name__ == "__main__":
    unittest.main()
