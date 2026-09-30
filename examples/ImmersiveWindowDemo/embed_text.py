"""Embed pipeline JSON for the no-CRT demo."""
import pathlib
import sys

source, output, symbol = sys.argv[1:]
data = pathlib.Path(source).read_bytes()
lines = ["    " + ", ".join(f"0x{byte:02X}" for byte in data[index:index + 16]) + ","
         for index in range(0, len(data), 16)]
pathlib.Path(output).write_bytes(
    (f"static const char {symbol}[] = {{\n" + "\n".join(lines) + "\n    0\n};\n").encode("ascii"))
