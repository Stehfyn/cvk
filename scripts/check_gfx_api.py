"""Preserve gfxlib's public signatures and enforce native wrapper retirement."""

import json
from pathlib import Path
import re
import sys

from check_gfx_calls import mask


def signatures(text):
    """Extract public C prototypes, retaining parameter names, qualifiers and widths."""
    text = re.sub(r"/\*.*?\*/|//[^\n]*", "", text, flags=re.DOTALL)
    pattern = re.compile(r"^[ \t]*([A-Za-z_][\w \t*]*\b(Gfx_\w+)\s*\([^;{}]*\)\s*;)", re.MULTILINE)
    result = {}
    for match in pattern.finditer(text):
        name = match.group(2)
        if name in result:
            raise ValueError(f"Duplicate public declaration: {name}")
        result[name] = " ".join(re.findall(r"[A-Za-z_]\w*|\d+|[^\s]", match.group(1)))
    return result


def main():
    root = Path(__file__).resolve().parents[1] / "examples" / "CastDemo" / "gfxlib"
    reference = json.loads((root / "UPSTREAM.json").read_text(encoding="utf-8"))
    actual = {}
    for header in sorted((root / "include" / "gfx").rglob("*.h")):
        declarations = signatures(header.read_text(encoding="utf-8"))
        actual[header.relative_to(root).as_posix()] = declarations
    expected = reference["public_signatures"]
    # The user authorized replacing these two low-level families with CVK/cva.
    retired = {
        "include/gfx/gfx_vulkan.h",
        "include/gfx/gfx_vulkan_platform.h",
        "include/gfx/win32/gfx_vulkan_win32.h",
        "include/gfx/linux/gfx_vulkan_linux.h",
        "include/gfx/gfx_vaapi.h",
        "include/gfx/gfx_vaapi_platform.h",
    }
    failures = []
    for name in sorted(actual.keys() | expected.keys()):
        if name in retired:
            if name in actual:
                failures.append(f"{name}: retired wrapper header reintroduced")
            continue
        before = expected.get(name, {})
        after = actual.get(name, {})
        for function in sorted(before.keys() | after.keys()):
            if before.get(function) != after.get(function):
                failures.append(f"{name}: public signature changed: {function}")
    implementation = [path for directory in ("src", "include") for path in (root / directory).rglob("*")]
    for path in sorted(implementation):
        if path.suffix not in {".c", ".h"}:
            continue
        if path.stem.startswith(("gfx_vulkan", "gfx_vaapi")):
            failures.append(f"{path.relative_to(root)}: retired wrapper module reintroduced")
        code = mask(path.read_text(encoding="utf-8"))
        for match in re.finditer(r"\b(?:Gfx_(?:Vulkan|Vaapi)_\w+|(?:vk|va)[A-Z]\w*\s*\()", code):
            line = code[:match.start()].count("\n") + 1
            failures.append(f"{path.relative_to(root)}:{line}: retired wrapper or raw native call: {match[0]}")
    if failures:
        print("\n".join(failures))
    else:
        kept = {name: items for name, items in actual.items() if name not in retired}
        print(f"Preserved {sum(len(items) for items in kept.values())} higher-level Gfx_ signatures across {len(kept)} headers")
        count = sum(len(items) for name, items in expected.items() if name in retired)
        print(f"Retired all {count} low-level wrapper declarations; no direct Vulkan/libva calls in gfxlib")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
