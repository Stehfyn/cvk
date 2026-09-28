"""Build and run the strict Linux diagnostic matrix without running stale binaries."""

import argparse
import datetime
import json
from pathlib import Path
import platform
import subprocess
import sys


def run_logged(command, path):
    """Retain complete diagnostics and the real exit status of each stage."""
    with path.open("w", encoding="utf-8") as output:
        result = subprocess.run(command, stdout=output, stderr=subprocess.STDOUT, check=False)
    return {"command": command, "exit_code": result.returncode, "log": str(path)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mode", choices=["all", "memcheck", "address", "memory", "thread"], default="all")
    parser.add_argument("--jobs", type=int, default=4)
    arguments = parser.parse_args()
    if platform.system() != "Linux":
        parser.error("Run in Linux/WSL: wsl -d Ubuntu -- python3 /mnt/c/dev/cvk/scripts/check_cast_dynamic.py")
    if arguments.jobs < 1:
        parser.error("--jobs must be positive")
    root = Path(__file__).resolve().parents[1]
    output = root / "build" / "cast-diagnostics"
    output.mkdir(parents=True, exist_ok=True)
    modes = ["memcheck", "address", "memory", "thread"] if arguments.mode == "all" else [arguments.mode]
    report = {
        "started_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "scope": "Linux specification sources and portable fixtures; not full Windows application coverage",
        "platform": platform.platform(),
        "modes": {},
    }
    for mode in modes:
        directory = output / mode
        directory.mkdir(exist_ok=True)
        status = {}
        report["modes"][mode] = status
        print(f"{mode}: configure", flush=True)
        status["configure"] = run_logged([
            "cmake", "-S", str(root / "examples" / "CastDemo" / "diagnostics"),
            "-B", str(directory), "-G", "Ninja", "-DCMAKE_C_COMPILER=clang",
            "-DCMAKE_BUILD_TYPE=Release", f"-DCAST_DIAGNOSTIC_MODE={mode}",
        ], directory / "configure.log")
        if status["configure"]["exit_code"] == 0:
            print(f"{mode}: strict build", flush=True)
            status["build"] = run_logged([
                "cmake", "--build", str(directory), "--parallel", str(arguments.jobs), "--", "-k", "0",
            ], directory / "build.log")
            if status["build"]["exit_code"] == 0:
                print(f"{mode}: execute", flush=True)
                status["test"] = run_logged([
                    "ctest", "--test-dir", str(directory), "--output-on-failure", "--no-tests=error",
                ], directory / "test.log")
            else:
                status["test"] = {"not_run": "Strict build failed; no stale or unchecked binary executed"}
        else:
            status["build"] = {"not_run": "Configuration failed"}
            status["test"] = {"not_run": "Configuration failed"}
        status["passed"] = status["test"].get("exit_code") == 0
        print(f"{mode}: {'passed' if status['passed'] else 'failed'}", flush=True)
        (output / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return 0 if all(result["passed"] for result in report["modes"].values()) else 1


if __name__ == "__main__":
    sys.exit(main())
