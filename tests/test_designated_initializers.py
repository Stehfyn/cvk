"""Check builder semantics and API stability with either initialization style."""

import copy
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


class DesignatedInitializerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.variants = {}
        for profile in ("cvk", "scvk"):
            policy = load_policy(str(ROOT / f"{profile}.policy.json"))
            reg = Registry.load(str(ROOT / policy["registry"]), policy["api"]) if "registry" in policy else Registry.load()
            for enabled in (False, True):
                settings = copy.deepcopy(policy)
                settings["designated_initializers"] = enabled
                gen = Generator(reg, settings)
                gen.PLATFORM_SOURCE_INCLUDES = {}
                gen.run()
                if gen.errors:
                    raise AssertionError(gen.errors)
                cls.variants[profile, enabled] = gen

    def test_switch_preserves_api_and_json_parsers(self):
        for profile in ("cvk", "scvk"):
            off, on = self.variants[profile, False], self.variants[profile, True]
            self.assertEqual(off.sigs, on.sigs)
            self.assertEqual(off.decls, on.decls)
            self.assertEqual(off.json_decls, on.json_decls)
            self.assertEqual(off.json_defs, on.json_defs)
            self.assertEqual(off.header(), on.header())
        policy = load_policy(str(ROOT / "cvk.policy.json"))
        policy.pop("designated_initializers")
        self.assertTrue(Generator(self.variants["cvk", False].reg, policy).designated_initializers)

    @unittest.skipUnless(shutil.which("clang"), "clang is required for C99 runtime checks")
    def test_struct_union_array_and_output_builders(self):
        for (profile, enabled), gen in self.variants.items():
            with self.subTest(profile=profile, enabled=enabled), tempfile.TemporaryDirectory() as tmp:
                names = ["cvkInitExtent3D", "cvkInitApplicationInfo", "cvkInitClearColorValueFloat32",
                         "cvkInitClearValueColor", "cvkInitPhysicalDeviceProperties2"]
                if profile == "cvk":
                    names.append("cvkInitTransformMatrix")
                definitions = []
                for name in names:
                    definitions.append(next(d for d in gen.defs[""] if d.startswith("void " + name + "(")))
                body = r'''
    VkExtent3D extent;
    cvkInitExtent3D(&extent, 5, 6, 7);
    CHECK(extent.width == 5 && extent.height == 6 && extent.depth == 7);
    const char app_name[] = "app";
    const char engine_name[] = "engine";
    int next;
    VkApplicationInfo app;
    cvkInitApplicationInfo(&app, &next, app_name, 11, engine_name, 12, 13);
    CHECK(app.sType == VK_STRUCTURE_TYPE_APPLICATION_INFO && app.pNext == &next);
    CHECK(app.pApplicationName == app_name && app.applicationVersion == 11);
    CHECK(app.pEngineName == engine_name && app.engineVersion == 12 && app.apiVersion == 13);
    const float values[4] = {1, 2, 3, 4};
    VkClearColorValue color;
    cvkInitClearColorValueFloat32(&color, values);
    CHECK(memcmp(color.float32, values, sizeof(values)) == 0);
    VkClearValue clear;
    cvkInitClearValueColor(&clear, color);
    CHECK(memcmp(clear.color.float32, values, sizeof(values)) == 0);
    VkPhysicalDeviceProperties2 properties;
    memset(&properties, 0, sizeof(properties));
    properties.properties.vendorID = 123;
    properties.properties.deviceID = 456;
    cvkInitPhysicalDeviceProperties2(&properties, &next);
    CHECK(properties.sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 && properties.pNext == &next);
    CHECK(properties.properties.vendorID == 123 && properties.properties.deviceID == 456);
'''
                if enabled:
                    body += '''
    cvkInitClearColorValueFloat32(&color, color.float32);
    CHECK(memcmp(color.float32, values, sizeof(values)) == 0);
'''
                if profile == "cvk":
                    body += '''
    const float matrix[3][4] = {{1, 2, 3, 4}, {5, 6, 7, 8}, {9, 10, 11, 12}};
    VkTransformMatrixKHR transform;
    cvkInitTransformMatrix(&transform, matrix);
    CHECK(memcmp(transform.matrix, matrix, sizeof(matrix)) == 0);
'''
                source = Path(tmp) / "builders.c"
                source.write_text(f"#include {gen.vk_header}\n#include <string.h>\n#include <stdio.h>\n"
                                  '#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\\n", __LINE__, #x); return 1; } } while (0)\n'
                                  + "\n".join(definitions) + "\nint main(void) {\n" + body + "return 0;\n}\n", encoding="utf-8")
                exe = Path(tmp) / "builders.exe"
                headers = ROOT / "external" / ("Vulkan-Headers" if profile == "cvk" else "VulkanSC-Headers") / "include"
                subprocess.run(["clang", "-std=c99", "-Wall", "-Wextra", "-pedantic-errors", "-isystem", str(headers),
                                str(source), "-o", str(exe)], check=True, capture_output=True, text=True)
                subprocess.run([str(exe)], check=True, capture_output=True, text=True)


if __name__ == "__main__":
    unittest.main()
