"""Embed pipeline JSON for the no-CRT demo."""
import json
import pathlib
import sys

source, output, symbol = sys.argv[1:]
data = pathlib.Path(source).read_bytes()
lines = [f"    {json.dumps(line.decode('ascii') + chr(10))}"
         for line in data.splitlines()]
pathlib.Path(output).write_bytes(
    (f"static const char {symbol}[] =\n" + "\n".join(lines) + ";\n").encode("ascii"))
