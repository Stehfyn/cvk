"""Fail if include/ or src/ differ from a fresh generation (SPEC.md rule zero).

    python scripts/check_generated.py [--policy cvk.policy.json]

Regenerates into a temporary directory with the same policy and compares every
generated file byte for byte against the checked-in one. Any difference means a
generated file was edited by hand, or the generator changed without regenerating.
"""

from __future__ import annotations

import argparse
import filecmp
import os
import subprocess
import sys
import tempfile

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), os.pardir))


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--policy", default=os.path.join(REPO_ROOT, "cvk.policy.json"))
    args = ap.parse_args()
    with tempfile.TemporaryDirectory() as tmp:
        subprocess.run([sys.executable, os.path.join(REPO_ROOT, "scripts", "generate.py"),
                        "--policy", args.policy, "--out-dir", tmp], check=True, cwd=REPO_ROOT,
                       stdout=subprocess.DEVNULL)
        bad = []
        for sub in ("include", "src"):
            fresh_dir = os.path.join(tmp, sub)
            repo_dir = os.path.join(REPO_ROOT, sub)
            fresh = sorted(os.listdir(fresh_dir))
            repo = sorted(f for f in os.listdir(repo_dir) if f.endswith((".h", ".c")))
            for f in sorted(set(fresh) | set(repo)):
                a, b = os.path.join(repo_dir, f), os.path.join(fresh_dir, f)
                if not os.path.exists(a):
                    bad.append(f"{sub}/{f}: missing from repository")
                elif not os.path.exists(b):
                    bad.append(f"{sub}/{f}: not produced by the generator")
                elif not filecmp.cmp(a, b, shallow=False):
                    bad.append(f"{sub}/{f}: differs from fresh generation")
    if bad:
        print("generated files are not reproducible:")
        for b in bad:
            print("  " + b)
        return 1
    print("generated files match a fresh generation")
    return 0


if __name__ == "__main__":
    sys.exit(main())
