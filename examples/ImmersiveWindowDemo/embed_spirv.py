"""Embed glslc output as aligned uint32_t words; no runtime shader files."""
import pathlib
import struct
import sys

source, output, symbol = sys.argv[1:]
data = pathlib.Path(source).read_bytes()
words = struct.unpack(f"<{len(data) // 4}I", data)
lines = [", ".join(f"0x{word:08x}" for word in words[i:i + 8])
         for i in range(0, len(words), 8)]
pathlib.Path(output).write_bytes(
    (f"static const uint32_t {symbol}[] =\n{{\n    "
     + ",\n    ".join(lines) + "\n};\n").encode("ascii"))
