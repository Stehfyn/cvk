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

    def test_nominal_dynamic_state_forms(self):
        for gen in self.variants.values():
            for noun, ctype in (("Scissor", "VkRect2D"), ("Viewport", "VkViewport")):
                name, value = "cvkCmdSet" + noun, noun.lower()
                ret, params = gen.sigs[name]
                self.assertEqual(ret, "void")
                self.assertEqual([p.text for p in params], ["VkCommandBuffer commandBuffer", f"{ctype} {value}"])
                source = next(d for d in gen.defs[""] if d.startswith("void " + name + "("))
                self.assertIn(f"vkCmdSet{noun}(commandBuffer, 0, 1, &{value});", source)
                self.assertNotIn("cvkCmdSetFirst" + noun, gen.sigs)
                self.assertIn(name + "2D", gen.sigs)
            self.assertIn("cvkCmdSetViewportAndScissor2D", gen.sigs)
            self.assertNotIn("CmdSetFirst", gen.source())

    def test_allman_initializers_survive_formatter_line_wrapping(self):
        formatter = ["clang-format", f"--style=file:{ROOT / '.clang-format'}"]
        for profile in ("cvk", "scvk"):
            gen = self.variants[profile, True]
            sources = [gen.source()] + [gen.platform_source(p) for p in gen.defs if p]
            for source in sources:
                formatted = subprocess.check_output(formatter, input=source, text=True)
                finished = gen.finish_source(formatted)
                self.assertNotRegex(finished, r"\(Vk\w+\)[ \t]*\{")
                self.assertNotRegex(finished, r"=\s*\n\s*\(Vk\w+\)\s*\{")
                self.assertNotRegex(finished, r",\s*}")
                self.assertEqual(finished, gen.finish_source(subprocess.check_output(formatter, input=finished, text=True)))
        gen = self.variants["cvk", True]
        name = "cvkGetPhysicalDeviceNestedCommandBufferProperties"
        source = next(d for d in gen.defs[""] if d.startswith("void " + name + "("))
        # Keep exercising the long-name wrapping case even though locals now use short role names.
        source = source.replace("VkPhysicalDeviceNestedCommandBufferPropertiesEXT ext_properties =",
                                "VkPhysicalDeviceNestedCommandBufferPropertiesEXT ext_properties;\n\n"
                                "    ext_properties = (VkPhysicalDeviceNestedCommandBufferPropertiesEXT)")
        source = source.replace("ext_properties", "physical_device_nested_command_buffer_properties")
        formatted = gen.finish_source(subprocess.check_output(formatter, input=source, text=True))
        self.assertIn("physical_device_nested_command_buffer_properties = (VkPhysicalDeviceNestedCommandBufferPropertiesEXT)\n    {\n", formatted)

    @unittest.skipUnless(shutil.which("clang"), "clang is required for C99 runtime checks")
    def test_wrappers_pass_initialized_inputs_and_array_chains(self):
        for (profile, enabled), gen in self.variants.items():
            with self.subTest(profile=profile, enabled=enabled), tempfile.TemporaryDirectory() as tmp:
                names = ("cvkCreateBuffer", "cvkBindImageMemory2", "cvkBindPlaneImageMemories")
                definitions = [next(d for d in gen.defs[""] if d.startswith("void " + name + "(")) for name in names]
                if enabled:
                    self.assertIn("VkBufferCreateInfo create_info =", definitions[0])
                    self.assertIn("bind_infos[index] = (VkBindImageMemoryInfo)", definitions[2])
                body = r'''
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); abort(); } } while (0)
#define CVK_ASSERT(x) CHECK(x)
#define CVK_CHECK(x) CHECK((x) == VK_SUCCESS)
#define CVK_MAX_BIND_INFOS 4
static int calls;
static int next;
VKAPI_ATTR VkResult VKAPI_CALL vkCreateBuffer(VkDevice device, const VkBufferCreateInfo* info,
                                              const VkAllocationCallbacks* allocator, VkBuffer* buffer)
{
    CHECK(device == (VkDevice)(uintptr_t)1 && allocator == NULL);
    CHECK(info->sType == VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO && info->pNext == &next);
    CHECK(info->flags == VK_BUFFER_CREATE_SPARSE_BINDING_BIT && info->size == 4096);
    CHECK(info->usage == VK_BUFFER_USAGE_TRANSFER_DST_BIT && info->sharingMode == VK_SHARING_MODE_CONCURRENT);
    CHECK(info->queueFamilyIndexCount == 2 && info->pQueueFamilyIndices[0] == 3 && info->pQueueFamilyIndices[1] == 5);
    *buffer = (VkBuffer)(uintptr_t)2;
    ++calls;
    return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkBindImageMemory2(VkDevice device, uint32_t count, const VkBindImageMemoryInfo* infos)
{
    CHECK(device == (VkDevice)(uintptr_t)1 && count == 2);
    for (uint32_t i = 0; i < count; ++i) {
        const VkBindImagePlaneMemoryInfo* plane = infos[i].pNext;
        CHECK(infos[i].sType == VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO);
        CHECK(infos[i].image == (VkImage)(uintptr_t)(3 + i));
        CHECK(infos[i].memory == (VkDeviceMemory)(uintptr_t)(5 + i) && infos[i].memoryOffset == 256 * (i + 1));
        CHECK(plane->sType == VK_STRUCTURE_TYPE_BIND_IMAGE_PLANE_MEMORY_INFO);
        CHECK(plane->pNext == (i ? NULL : &next));
        CHECK(plane->planeAspect == (i ? VK_IMAGE_ASPECT_PLANE_1_BIT : VK_IMAGE_ASPECT_PLANE_0_BIT));
    }
    ++calls;
    return VK_SUCCESS;
}
'''
                main = r'''
int main(void)
{
    uint32_t families[2] = {3, 5};
    VkBuffer buffer;
    cvkCreateBuffer((VkDevice)(uintptr_t)1, &next, VK_BUFFER_CREATE_SPARSE_BINDING_BIT, 4096,
                    VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_SHARING_MODE_CONCURRENT, 2, families, NULL, &buffer);
    CHECK(buffer == (VkBuffer)(uintptr_t)2);
    const void* nexts[2] = {&next, NULL};
    VkImageAspectFlagBits aspects[2] = {VK_IMAGE_ASPECT_PLANE_0_BIT, VK_IMAGE_ASPECT_PLANE_1_BIT};
    VkImage images[2] = {(VkImage)(uintptr_t)3, (VkImage)(uintptr_t)4};
    VkDeviceMemory memories[2] = {(VkDeviceMemory)(uintptr_t)5, (VkDeviceMemory)(uintptr_t)6};
    VkDeviceSize offsets[2] = {256, 512};
    cvkBindPlaneImageMemories((VkDevice)(uintptr_t)1, 2, nexts, aspects, images, memories, offsets);
    CHECK(calls == 2);
    return 0;
}
'''
                source = Path(tmp) / "wrappers.c"
                source.write_text(f"#include {gen.vk_header}\n#include <stdio.h>\n#include <stdlib.h>\n"
                                  + body + "\n".join(definitions) + main, encoding="utf-8")
                exe = Path(tmp) / "wrappers.exe"
                headers = ROOT / "external" / ("Vulkan-Headers" if profile == "cvk" else "VulkanSC-Headers") / "include"
                subprocess.run(["clang", "-std=c99", "-Wall", "-Wextra", "-pedantic-errors", "-isystem", str(headers),
                                str(source), "-o", str(exe)], check=True, capture_output=True, text=True)
                subprocess.run([str(exe)], check=True, capture_output=True, text=True)

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
    (void)memset(&properties, 0, sizeof(properties));
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
