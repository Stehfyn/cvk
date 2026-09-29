"""Check loading assumptions and optional API documentation without writing outputs."""

import copy
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
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
        self.assertEqual(off.json_defs, on.json_defs)
        for off_decls, on_decls in ((off.decls, on.decls), (off.json_decls, on.json_decls)):
            for platform, declarations in on_decls.items():
                stripped = [re.sub(r"/\*\*.*?\*/\s*", "", decl, flags=re.S) for decl in declarations]
                self.assertEqual(stripped, off_decls[platform])
        self.assertIn("\\brief", on.header())
        self.assertNotIn("/**", off.header())
        for platform in off.decls:
            if platform:
                self.assertNotIn("/**", off.platform_header(platform))
        for platform in off.json_decls:
            self.assertNotIn("/**", off.json_header(platform))

    @unittest.skipUnless(shutil.which("clang"), "clang is required for diagnostic boundary checks")
    def test_diagnostic_line_signed_boundaries(self):
        source = r'''
#include "cvk/cvk.h"
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
'''
        source += self.variants[True].diagnostic_source()
        source += r'''
/*************************************************************************************************/
/**
 * \par Details:
 *  Compare signed extremes and decimal transitions against the host formatter.
 */
int main(void)
{
    int32_t lines[9];
    char_t  actual[CVK_DIAGNOSTIC_CAPACITY + 2];
    char_t  expected[CVK_DIAGNOSTIC_CAPACITY];
    char_t* text;
    uint32_t index;
    int32_t length;
    int32_t result;
    bool_t  fits;
    bool_t  matches;
    bool_t  guards_match;
    bool_t  is_valid;

    lines[0] = INT_MIN;
    lines[1] = INT_MAX;
    lines[2] = 0;
    lines[3] = -1;
    lines[4] = 1;
    lines[5] = 9;
    lines[6] = 10;
    lines[7] = 99;
    lines[8] = 100;
    result   = 0;
    for (index = 0; index < VK_COUNT_OF(lines); ++index)
    {
        actual[0] = CVK_DIAGNOSTIC_CHAR_MINUS;
        actual[CVK_DIAGNOSTIC_CAPACITY + 1] = CVK_DIAGNOSTIC_CHAR_MINUS;
        text         = cvkFormatDiagnosticLine(actual + 1, lines[index]);
        length       = snprintf(expected, sizeof(expected), "%d", lines[index]);
        fits         = (0 < length);
        matches      = (0 == strcmp(text, expected));
        guards_match = (actual[0] == actual[CVK_DIAGNOSTIC_CAPACITY + 1]);
        is_valid     = fits && matches && guards_match;
        if (!is_valid)
        {
            result = 1;
            break;
        }
    }
    return result;
}
'''
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            fixture = directory / "diagnostic.c"
            executable = directory / "diagnostic.exe"
            fixture.write_text(source, encoding="utf-8")
            subprocess.run([shutil.which("clang"), "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                            "-I", str(ROOT / "include"), "-I", str(ROOT / "external/Vulkan-Headers/include"),
                            str(fixture), "-o", str(executable)], check=True)
            subprocess.run([str(executable)], check=True)

    def test_json_api_is_separate_from_core_and_platform_wrappers(self):
        gen = self.variants[False]
        for header, source in [(gen.header(), gen.source())] + [
                (gen.platform_header(p), gen.platform_source(p)) for p in gen.decls if p]:
            self.assertNotIn("cvkJson", header)
            self.assertNotIn("cvkParseJson", source)
            self.assertNotIn("cvkParsePipelineJson", source)
        for platform in gen.json_decls:
            header, source = gen.json_header(platform), gen.json_source(platform)
            self.assertIn("cvkParseJson", header)
            self.assertIn("cvkParseJson", source)
            self.assertNotIn("volk.h", source)
            self.assertNotRegex(source, r"\bvk[A-Z]\w*\s*\(")
            self.assertIn('#include "cvk/cvk_json.h"' if platform else '#include "cvk/cvk.h"', header)
        for function in ("cvkParseJsonDocument", "cvkParsePipelineJson", "cvkCreatePipelineFromJson"):
            self.assertIn(function, gen.json_header())
            self.assertIn(function, gen.json_source())
        self.assertIn("#include <ctype.h>", gen.json_source())
        self.assertNotIn("#include <ctype.h>", gen.source())

    def test_child_handles_do_not_gain_lookup_arguments(self):
        gen = self.variants[False]
        self.assertEqual(gen.sigs["cvkCmdDrawMeshTasks"][1][0].name, "commandBuffer")
        self.assertEqual(gen.sigs["cvkAcquireDrmDisplay"][1][0].name, "physicalDevice")

    def test_output_array_conveniences_use_caller_workspace(self):
        gen = self.variants[False]
        for command, array, extension in gen.output_array_pairs():
            name = gen.output_array_name(command, array, extension)
            with self.subTest(function=name):
                parameters = gen.sigs[name][1]
                names = [parameter.name for parameter in parameters]
                workspace = next(parameter for parameter in parameters if parameter.name == array.name)
                capacity = gen.capacity_name(array.name)
                self.assertEqual(workspace.text, f"{array.type} {array.name}[{capacity}]")
                extender = "p" + gen.struct_short(extension)
                self.assertEqual(names.index(extender), names.index(array.name) + 1)
                body = next(body for bodies in gen.defs.values() for body in bodies
                            if body.startswith(f"void {name}("))
                statements = body[body.index("{"):]
                self.assertNotRegex(statements, rf"\b{array.type}\s+\w+\s*\[")
                self.assertIn(f"{array.name}[index]", statements)
                self.assertIn(f"&{extender}[index]", statements)
        stereo = gen.sigs["cvkGetDisplayModeStereoPropertiesNV"][1]
        self.assertEqual([parameter.name for parameter in stereo],
                         ["physicalDevice", "display", "pPropertyCount", "pProperties", "pDisplayModeStereoPropertiesNV"])

    def test_extension_queries_do_not_hide_large_workspaces(self):
        gen = self.variants[False]
        for name in ("cvkGetInstanceExtensionSupport", "cvkGetPhysicalDeviceExtensionSupport",
                     "cvkCreateDebugInstance", "cvkGetPhysicalDeviceByPciBusInfo", "cvkSetupDeviceQueue"):
            with self.subTest(function=name):
                parameters = gen.sigs[name][1]
                workspace = next(parameter for parameter in parameters if parameter.name == "pProperties")
                self.assertEqual(workspace.text, "VkExtensionProperties pProperties[CVK_MAX_PROPERTIES]")
        self.assertNotRegex(gen.source(), r"(?m)^\s+VkExtensionProperties\s+\w+\[CVK_MAX_PROPERTIES\];")

    def test_enumeration_results_stay_within_the_prepared_extent(self):
        gen = self.variants[False]
        checked = set()
        for bodies in gen.defs.values():
            for body in bodies:
                capture = re.search(r"capacity\s*=\s*\(\*(p\w+)\);", body)
                if capture is None:
                    continue
                count = capture.group(1)
                statements = body[body.index("{"):]
                calls = list(re.finditer(r"\bvk[A-Z]\w*\([^;]+;", statements))
                self.assertEqual(len(calls), 2, body[:body.index("{")])
                self.assertIn(", NULL)", calls[0].group())
                self.assertIn(count, calls[1].group())
                self.assertLess(statements.index("capacity ="), calls[1].start())
                self.assertIn(f"CVK_ASSERT(capacity >= (*{count}));", statements[calls[1].end():])
                self.assertNotRegex(statements, rf"index < \(\*{count}\)")
                checked.add(re.search(r"\b(cvk\w+)\(", body).group(1))
        self.assertTrue({"cvkGetPhysicalDeviceQueueFamilyVideoProperties",
                         "cvkGetPhysicalDeviceQueueFamilyProperties2",
                         "cvkEnumerateInstanceExtensionProperties",
                         "cvkGetDisplayModeStereoPropertiesNV"} <= checked)

    def test_bound_color_mask_has_explicit_flag_types(self):
        gen = self.variants[False]
        source = next(d for d in gen.defs[""] if d.startswith("void cvkInitOpaquePipelineColorBlendAttachmentState("))
        self.assertIn("(VkColorComponentFlags)((VkFlags)VK_COLOR_COMPONENT_R_BIT | (VkFlags)VK_COLOR_COMPONENT_G_BIT | "
                      "(VkFlags)VK_COLOR_COMPONENT_B_BIT | (VkFlags)VK_COLOR_COMPONENT_A_BIT)", source)

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
        self.assertTrue(gen.source().index("#include <volk.h>") < gen.source().index('#include "cvk/cvk.h"'))
        for platform in gen.decls:
            if platform:
                self.assertNotIn("volk.h", gen.platform_header(platform))
                self.assertNotIn("__cplusplus", gen.platform_header(platform))
                self.assertIn("#include <volk.h>", gen.platform_source(platform))

    def test_sc_policy_inherits_disabled_docs_and_keeps_native_header(self):
        policy = load_policy(str(ROOT / "scvk.policy.json"))
        self.assertFalse(policy["doc_comments"])
        self.assertEqual(policy["header"], "<vulkan/vulkan_sc_core.h>")
        self.assertTrue(policy["single_line_prototypes"])

    def test_one_physical_line_per_prototype_for_both_doc_modes(self):
        pattern = r"(?m)^[ \t]*(?:[A-Za-z_]\w*[ \t*]+)+(cvk[A-Z]\w*)[ \t]*\([^;{}]*\)[ \t]*;"
        for gen in self.variants.values():
            for header in ([gen.header()] + [gen.platform_header(p) for p in gen.decls if p]
                           + [gen.json_header(p) for p in gen.json_decls]):
                declarations = list(re.finditer(pattern, header))
                self.assertTrue(declarations)
                self.assertTrue(all("\n" not in m.group() for m in declarations))
            one_line = gen.header()
            gen.single_line_prototypes = False
            try:
                multiline = gen.header()
                self.assertTrue(any("\n" in m.group() for m in re.finditer(pattern, multiline)))
                self.assertEqual(re.findall(pattern, one_line), re.findall(pattern, multiline))
            finally:
                gen.single_line_prototypes = True

    def test_clang_format_respects_each_layout_configuration(self):
        pattern = r"(?m)^[ \t]*(?:[A-Za-z_]\w*[ \t*]+)+cvk[A-Z]\w*[ \t]*\([^;{}]*\)[ \t]*;"
        formatter = ["clang-format", f"--style=file:{ROOT / '.clang-format'}"]
        for gen in self.variants.values():
            for single_line in (False, True):
                gen.single_line_prototypes = single_line
                try:
                    formatted = subprocess.check_output(formatter, input=gen.header(), text=True)
                    self.assertEqual(formatted, subprocess.check_output(formatter, input=formatted, text=True))
                    declarations = list(re.finditer(pattern, formatted))
                    self.assertTrue(declarations)
                    if single_line:
                        self.assertTrue(all("\n" not in m.group() for m in declarations))
                    else:
                        self.assertTrue(any("\n" in m.group() for m in declarations))
                finally:
                    gen.single_line_prototypes = True


if __name__ == "__main__":
    unittest.main()
