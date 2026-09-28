"""Check the imported gfx public function signatures against the preserved API."""

import json
from pathlib import Path
import re
import sys


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
    failures = []
    for name in sorted(actual.keys() | expected.keys()):
        before = expected.get(name, {})
        after = actual.get(name, {})
        for function in sorted(before.keys() | after.keys()):
            if before.get(function) != after.get(function):
                failures.append(f"{name}: public signature changed: {function}")
    if failures:
        print("\n".join(failures))
    else:
        print(f"Preserved {sum(len(items) for items in actual.values())} public Gfx_ signatures across {len(actual)} headers")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
