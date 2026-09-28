"""Verify production assertions remain fatal in the optimized CRT-free executable."""

import pathlib
import subprocess
import sys


def main():
    binary = pathlib.Path(sys.argv[1]).resolve(strict=True)
    success = subprocess.run([str(binary), "t"], timeout=10, capture_output=True)
    if success.returncode:
        raise SystemExit(f"Valid construction failed: {success.returncode:#x}")
    quoted = subprocess.run(f'"{binary}" "t"   ', timeout=10, capture_output=True)
    if quoted.returncode:
        raise SystemExit(f"Quoted selector with trailing spaces failed: {quoted.returncode:#x}")
    for argument in ([], [""], ["tt"], ["unknown"]):
        invalid = subprocess.run([str(binary), *argument], timeout=10, capture_output=True)
        if invalid.returncode != 2:
            raise SystemExit(f"Invalid selector {argument!r}: expected 2, got {invalid.returncode:#x}")
    cases = "hskrbpmdoecwjvxnqiuazRNC0123456789ABDEFGHIJKLMOPQ"
    cases += "!#$%&()*+,-./:;<=>"
    cases += "STUVW"
    for case in cases:
        result = subprocess.run([str(binary), case], timeout=10, capture_output=True)
        code = result.returncode & 0xFFFFFFFF
        if code != 0xC0000409:
            raise SystemExit(f"Case {case}: expected fail-fast 0xc0000409, got {code:#x}")
    print(f"{len(cases)} caller-misuse assertions verified in Release; valid construction and selectors passed.")


if __name__ == "__main__":
    main()
