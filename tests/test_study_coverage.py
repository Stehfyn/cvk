"""Coverage-map checks: python -m unittest discover -s tests -p test_study_coverage.py"""

import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from study_coverage import build_coverage, scan_file


class CoverageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.report = build_coverage()
        cls.commands = {c["command"]: c for c in cls.report["commands"]}

    def test_checked_in_map_matches_current_sources(self):
        recorded = json.loads((ROOT / "study/coverage.json").read_text(encoding="utf-8"))
        self.assertEqual(recorded, self.report)
        self.assertFalse(self.report["summary"]["missing_generated_functions"])

    def test_core_and_extension_command_evidence(self):
        for command, wrapper in (("vkCreateBuffer", "cvkCreateBuffer"),
                                 ("vkCmdTraceRaysKHR", "cvkCmdTraceRays")):
            self.assertEqual(self.commands[command]["status"], "covered")
            self.assertIn(wrapper, self.commands[command]["wrappers"])

    def test_aliases_do_not_inflate_denominator(self):
        command = self.commands["vkGetPhysicalDeviceFeatures2"]
        self.assertIn("vkGetPhysicalDeviceFeatures2KHR", command["names"])
        self.assertNotIn("vkGetPhysicalDeviceFeatures2KHR", self.commands)

    def test_platform_and_provisional_exclusions(self):
        for name, category in (("vkCreateWaylandSurfaceKHR", "wayland"),
                               ("vkCmdDispatchGraphAMDX", "provisional"),
                               ("vkAcquireXlibDisplayEXT", "xlib_xrandr")):
            command = self.commands[name]
            self.assertEqual(command["status"], "outside profile")
            self.assertIn(category, command["reason"])
        self.assertEqual(self.commands["vkCreateWin32SurfaceKHR"]["status"], "covered")

    def test_query_helpers_and_builders_are_distinct(self):
        feature = "VkPhysicalDeviceVulkan12Features"
        self.assertIn("cvkInitPhysicalDeviceVulkan12Features", self.report["structures"][feature]["builders"])
        self.assertIn("cvkGetPhysicalDeviceVulkan12Features",
                      self.commands["vkGetPhysicalDeviceFeatures2"]["extender_helpers"][feature])
        self.assertNotIn(feature, self.commands["vkCreateDevice"]["extender_helpers"])
        self.assertTrue(any(r["structure"] == feature and r["relation"] == "pNext"
                            for r in self.commands["vkCreateDevice"]["structures"]))

    def test_output_array_reset_is_reported(self):
        parameters = self.commands["vkGetPhysicalDeviceQueueFamilyProperties2"]["parameters"]
        self.assertTrue(next(p for p in parameters if p["name"] == "pQueueFamilyProperties")["wrapper_resets_array_pnext"])
        self.assertFalse(any(p["wrapper_resets_array_pnext"]
                             for p in self.commands["vkGetPhysicalDeviceFeatures2"]["parameters"]))

    def test_source_locations_point_to_function_signatures(self):
        texts = {}
        for name, function in self.report["functions"].items():
            for location in (function["header"], function["source"]):
                if not location:
                    continue
                path = location["path"]
                if path not in texts:
                    texts[path] = (ROOT / path).read_text(encoding="utf-8").splitlines()
                self.assertIn(name + "(", texts[path][location["line"] - 1])

    def test_scanner_ignores_comment_and_literal_functions(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as directory:
            path = Path(directory) / "cvk_fixture.c"
            path.write_text('/* void cvkFake(void) { } */\n'
                            'void cvkReal(void)\n{\n'
                            '    const char* text = "void cvkFakeString(void) { }";\n'
                            '    if (text) { vkDeviceWaitIdle(device); }\n}\n', encoding="utf-8")
            found = scan_file(path)
            self.assertEqual(set(found), {"cvkReal"})
            self.assertEqual(found["cvkReal"]["line"], 2)
            self.assertNotIn("cvkFakeString", found["cvkReal"]["body"])
            self.assertIn("vkDeviceWaitIdle", found["cvkReal"]["body"])


if __name__ == "__main__":
    unittest.main()
