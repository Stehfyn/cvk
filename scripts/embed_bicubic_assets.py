"""Embed CVK's compiled bicubic shaders and pipeline JSON in C include fragments."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import struct


ROOT = Path(__file__).resolve().parent.parent / "assets" / "bicubic"


def spirv_source(path: Path, symbol: str) -> bytes:
    data = path.read_bytes()
    if len(data) % 4 or data[:4] != b"\x03\x02\x23\x07":
        raise ValueError(f"invalid SPIR-V: {path}")
    words = struct.unpack(f"<{len(data) // 4}I", data)
    lines = "\n".join(f"    0x{word:08x}," for word in words)
    return f"static const uint32_t {symbol}[] =\n{{\n{lines}\n}};\n".encode("ascii")


def json_source(path: Path, symbol: str) -> bytes:
    lines = "\n".join(f"    {json.dumps(line + chr(10))}" for line in path.read_text(encoding="ascii").splitlines())
    return f"static const char {symbol}[] =\n{lines};\n".encode("ascii")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    assets = {
        "cvk_fullscreen_vert.inc": spirv_source(ROOT / "fullscreen.vert.spv", "cvk_fullscreen_vert"),
        "cvk_bicubic_frag.inc": spirv_source(ROOT / "bicubic.frag.spv", "cvk_bicubic_frag"),
        "cvk_bicubic_pipeline_json.inc": json_source(ROOT / "bicubic.pipeline.json", "cvk_bicubic_pipeline_json"),
    }
    for name, content in assets.items():
        path = ROOT / name
        if args.check:
            if not path.exists() or path.read_bytes() != content:
                print(f"{path}: differs from source assets")
                return 1
        else:
            path.write_bytes(content)
    if args.check:
        print("bicubic assets match their source files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
