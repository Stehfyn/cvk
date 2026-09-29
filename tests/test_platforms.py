"""Check platform selection and C99 syntax without requiring every native SDK."""

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


class PlatformTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.profiles = {}
        for profile in ("cvk", "scvk"):
            policy = load_policy(str(ROOT / f"{profile}.policy.json"))
            reg = Registry.load(str(ROOT / policy["registry"]), policy["api"]) if "registry" in policy else Registry.load()
            gen = Generator(reg, policy)
            gen.run()
            cls.profiles[profile] = gen

    def test_all_supported_platforms_are_emitted(self):
        for profile, gen in self.profiles.items():
            with self.subTest(profile=profile):
                self.assertEqual(gen.errors, [])
                self.assertEqual(gen.platforms, set(gen.reg.platforms))
                supported = {e.platform for e in gen.reg.extensions.values()
                             if gen.api in e.supported and e.platform and (e.commands or e.types)}
                self.assertTrue(supported <= set(gen.decls))
                for plat in supported:
                    self.assertTrue((ROOT / f"include/{profile}/{plat}/{profile}_{plat}.h").is_file())
                    self.assertTrue((ROOT / f"src/{plat}/{profile}_{plat}.c").is_file())
                for plat in gen.json_decls:
                    if plat:
                        self.assertTrue((ROOT / f"src/{plat}/{profile}_{plat}_json.c").is_file())

    def test_explicit_platform_list_still_filters_extensions(self):
        gen = self.profiles["cvk"]
        policy = dict(gen.policy, platforms=["win32", "xlib"])
        limited = Generator(gen.reg, policy)
        self.assertIn("VK_KHR_win32_surface", limited.exts)
        self.assertNotIn("VK_KHR_wayland_surface", limited.exts)
        self.assertNotIn("VK_AMDX_shader_enqueue", limited.exts)
        self.assertTrue(Generator(gen.reg, dict(policy, platforms=["unknown"])).errors)

    @unittest.skipUnless(shutil.which("clang"), "clang is required for platform syntax checks")
    def test_platform_sources_are_c99(self):
        # These declarations check generated C types and includes only, not native ABI
        # compatibility or runtime behavior. Windows is checked by the native build.
        headers = {
            "X11/Xlib.h": "typedef struct _XDisplay Display; typedef unsigned long Window; typedef unsigned long VisualID;",
            "X11/extensions/Xrandr.h": "typedef unsigned long RROutput;",
            "xcb/xcb.h": "typedef struct xcb_connection_t xcb_connection_t; typedef uint32_t xcb_window_t; typedef uint32_t xcb_visualid_t;",
            "wayland-client.h": "struct wl_display; struct wl_surface;",
            "directfb.h": "typedef struct IDirectFB IDirectFB; typedef struct IDirectFBSurface IDirectFBSurface;",
            "zircon/types.h": "typedef uint32_t zx_handle_t;",
            "ggp_c/vulkan_types.h": "typedef int32_t GgpStreamDescriptor; typedef uint64_t GgpFrameToken;",
            "screen/screen.h": "struct _screen_context; struct _screen_window; struct _screen_buffer;",
            "nvscisync.h": "typedef struct NvSciSyncAttrListRec* NvSciSyncAttrList; typedef struct NvSciSyncObjRec* NvSciSyncObj; typedef struct NvSciSyncFence NvSciSyncFence;",
            "nvscibuf.h": "typedef struct NvSciBufAttrListRec* NvSciBufAttrList; typedef struct NvSciBufObjRec* NvSciBufObj;",
            "unistd.h": "int dup(int);",
        }
        with tempfile.TemporaryDirectory() as tmp:
            for name, declarations in headers.items():
                path = Path(tmp) / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("#pragma once\n#include <stdint.h>\n" + declarations + "\n")
            for profile, gen in self.profiles.items():
                includes = ["-I", str(ROOT / "include"), "-isystem", tmp,
                            "-isystem", str(ROOT / "external" / ("Vulkan-Headers" if profile == "cvk" else "VulkanSC-Headers") / "include")]
                if profile == "cvk":
                    includes += ["-isystem", str(ROOT / "external/volk")]
                for plat in sorted(set(gen.decls) | set(gen.json_decls)):
                    if not plat or plat == "win32":
                        continue
                    with self.subTest(profile=profile, platform=plat):
                        sources = sorted((ROOT / "src" / plat).glob(f"{profile}_{plat}*.c"))
                        defines = ["-D" + gen.reg.platforms[plat]] if plat in gen.reg.platforms else []
                        result = subprocess.run(["clang", "-std=c99", "-fsyntax-only", "-Wall", "-Wextra", "-Werror", "-pedantic-errors",
                                                 *includes, *defines, *map(str, sources)], capture_output=True, text=True)
                        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
