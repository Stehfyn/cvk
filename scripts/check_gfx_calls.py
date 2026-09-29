"""Check owned C clients against the pinned CVK, cva and cdx declarations."""

import argparse
from pathlib import Path
import re
import sys


def mask(text):
    """Keep source offsets while ignoring comments and quoted tokens."""
    tokens = r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\''
    def replace(match):
        blank = re.sub(r"[^\n]", " ", match[0])
        return blank if match[0].startswith(("/*", "//")) else "X" + blank[1:]
    return re.sub(tokens, replace, text, flags=re.S)


def arguments(code, opening):
    """Return the top-level argument count and the offset following the call."""
    depth = 1
    count = 1
    for offset in range(opening + 1, len(code)):
        token = code[offset]
        if token in "([{":
            depth += 1
        elif token in ")]}":
            depth -= 1
        elif token == "," and depth == 1:
            count += 1
        if depth == 0:
            if code[opening + 1:offset].strip() in ("", "void"):
                count = 0
            return count, offset + 1
    raise ValueError("Unterminated parameter/argument list")


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=root / "build/windows-msvc-2026")
    args = parser.parse_args()
    configured_cva = args.build / "external/cva/generated/include"
    if not (configured_cva / "cva.h").is_file():
        print(f"Configure cva before checking calls: missing {configured_cva / 'cva.h'}")
        return 1
    # The pinned headers describe both platform source trees; the configured headers
    # additionally check the selected backend's actual generated declaration surface.
    header_roots = (root / "include/cvk", root / "external/cva/include", configured_cva, root / "external/cdx/include")
    pattern = re.compile(r"\b((?:cvk|cva|cdx12|cdxgi)[A-Z]\w*)\s*\(")
    declarations = {}
    failures = []
    for directory in header_roots:
        for header in directory.rglob("*.h"):
            code = mask(header.read_text(encoding="utf-8"))
            for match in pattern.finditer(code):
                opening = code.index("(", match.start())
                count, end = arguments(code, opening)
                if code[end:].lstrip().startswith(";"):
                    name = match[1]
                    if name in declarations and declarations[name] != count:
                        failures.append(f"{header}: incompatible declarations for {name}")
                    declarations[name] = count
    checked = 0
    sources = sorted(source for directory in (root / "examples", root / "tests")
                     for source in directory.rglob("*.c"))
    for source in sources:
        code = mask(source.read_text(encoding="utf-8"))
        for match in pattern.finditer(code):
            count, _ = arguments(code, code.index("(", match.start()))
            name = match[1]
            line = code[:match.start()].count("\n") + 1
            if declarations.get(name) != count:
                failures.append(f"{source.relative_to(root)}:{line}: {name} receives {count} arguments; "
                                f"declaration expects {declarations.get(name, 'missing')}")
            checked += 1
    if failures:
        print("\n".join(failures))
    else:
        print(f"Verified declaration presence and argument counts for {checked} owned-client dependency calls")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
