"""Run the native workgroup query against arbitrary-precision integer arithmetic."""

import ctypes
from itertools import product
from pathlib import Path
from random import Random
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class DispatchCountTests(unittest.TestCase):
    def test_full_unsigned_domain_boundaries(self):
        clang = shutil.which("clang")
        tidy = shutil.which("clang-tidy")
        self.assertIsNotNone(clang, "Clang is required for this native diagnostic fixture")
        self.assertIsNotNone(tidy, "clang-tidy is required for this native diagnostic fixture")
        policy = ROOT / "examples/CastDemo/.clang-tidy"
        arguments = ["-std=c11", "-Weverything", "-Werror", "-Wno-padded",
                     "-I" + str(ROOT / "include"), "-I" + str(ROOT / "examples/CastDemo"),
                     "-isystem", str(ROOT / "external/Vulkan-Headers/include")]
        # The isolated query has no native calls. A caller-contract failure traps;
        # valid test inputs require no runtime, startup code, allocator or device.
        prefix = '''#include "cvk/cvk.h"
#include "os/os_compiler.h"
/*************************************************************************************************/
/**
 * \\par Details:
 *  Isolated fixture failure transport; no production runtime implementation is linked.
 */
OS_NORETURN void cvkFatal(const char* pExpression, const char* pFile, int line)
{
    (void)pExpression;
    (void)pFile;
    (void)line;
    __builtin_trap();
}
'''
        with tempfile.TemporaryDirectory(prefix="cvk-dispatch-") as directory:
            directory = Path(directory)
            source = directory / "dispatch.c"
            source.write_text(prefix + (ROOT / "scripts/protocols/cvk_protocol_dispatch.c").read_text(),
                              encoding="utf-8")
            subprocess.run([tidy, str(source), f"--config-file={policy}", "--"] + arguments,
                           check=True, capture_output=True, text=True)
            if sys.platform == "win32":
                library = directory / "dispatch.dll"
                linking = ["-fuse-ld=lld", "-Wl,/NOENTRY,/EXPORT:cvkGetWorkgroupCount"]
            else:
                library = directory / "dispatch.so"
                linking = ["-fPIC"]
            subprocess.run([clang] + arguments + ["-shared", "-nostdlib"] + linking +
                           [str(source), "-o", str(library)], check=True, capture_output=True, text=True)
            # A child process releases the DLL before TemporaryDirectory cleans it up on Windows.
            subprocess.run([sys.executable, str(Path(__file__).resolve()), str(library)], check=True)


def check_library(path):
    library = ctypes.CDLL(path)
    query = library.cvkGetWorkgroupCount
    query.argtypes = [ctypes.c_uint32, ctypes.c_uint32, ctypes.c_uint32]
    query.restype = ctypes.c_uint32
    values = [0, 1, 2, 3, 31, 32, 255, 256, 65535, 65536,
              2147483647, 2147483648, 4294967294, 4294967295]
    random = Random(49321)
    cases = list(product(values, values[1:], values[1:]))
    cases += [(random.randrange(4294967296), random.randrange(1, 4294967296),
               random.randrange(1, 4294967296)) for _ in range(10000)]
    for count, per_invocation, local_size in cases:
        per_group = per_invocation * local_size
        expected = (count + per_group - 1) // per_group
        observed = query(count, per_invocation, local_size)
        if observed != expected:
            raise AssertionError((count, per_invocation, local_size, expected, observed))
    print(f"Verified {len(cases)} native workgroup-count cases")


if __name__ == "__main__":
    if len(sys.argv) == 2 and Path(sys.argv[1]).suffix in (".dll", ".so"):
        check_library(sys.argv[1])
    else:
        unittest.main()
