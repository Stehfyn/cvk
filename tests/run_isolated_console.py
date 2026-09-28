"""Run the native console fixture with a new hidden console, never the caller's."""

import subprocess
import sys


def main():
    if sys.platform != "win32":
        raise RuntimeError("The console fixture requires Windows")
    if len(sys.argv) != 2:
        raise ValueError("Expected one native fixture executable")
    startup = subprocess.STARTUPINFO()
    startup.dwFlags = subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = subprocess.SW_HIDE
    for mode, creation_flags in (
        ("k", subprocess.CREATE_NEW_CONSOLE),
        ("n", subprocess.DETACHED_PROCESS),
    ):
        result = subprocess.run(
            [sys.argv[1], mode],
            creationflags=creation_flags,
            startupinfo=startup,
            timeout=15,
            check=False,
        )
        if result.returncode:
            return result.returncode
    return 0


if __name__ == "__main__":
    sys.exit(main())
