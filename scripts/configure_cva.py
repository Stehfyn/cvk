"""Generate the pinned cva library with its supported optional-diagnostic policy.

Only vaQuerySurfaceError changes its native result handling. All documented query
failures return to the image owner, which retains the primary synchronization
result. This does not change synchronization, construction or fatal-handler policy.
The upstream checkout remains untouched; outputs belong to the selected build.
"""

import argparse
import copy
import json
from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[1]
CVA = ROOT / "external/cva"
sys.path.insert(0, str(CVA / "scripts"))
from generate import Generator
from vaheaders import Registry


def configure(platform):
    """Apply the reviewed query-status policy through the upstream generator."""
    policy = json.loads((CVA / "cva.policy.json").read_text(encoding="utf-8"))
    policy["registry"] = str(ROOT / "external/libva")
    policy["platforms"] = [platform]
    registry = Registry(ROOT / "external/libva")
    overrides = json.loads((ROOT / "cmake/cva-status-policy.json").read_text(encoding="utf-8"))
    if set(overrides) != {"vaQuerySurfaceError"}:
        raise ValueError("Only optional surface diagnostics may change their result policy")
    statuses = overrides["vaQuerySurfaceError"]
    if len(statuses) != len(set(statuses)):
        raise ValueError("Duplicate diagnostic status in cva policy")
    if set(statuses) != registry.statuses - {"VA_STATUS_SUCCESS"}:
        raise ValueError("Diagnostic status policy must explicitly cover the pinned libva error domain")

    baseline = Generator(registry, copy.deepcopy(policy))
    baseline.run()
    policy["recoverable_errors"].update(overrides)
    configured = Generator(registry, policy)
    configured.run()

    if configured.functions.keys() != baseline.functions.keys():
        raise ValueError("Configuration must not add or remove cva functions")
    changed = [name for name in baseline.functions
               if baseline.functions[name] != configured.functions[name]]
    if changed != ["cvaQuerySurfaceError"]:
        raise ValueError(f"Unexpected cva function changes: {changed}")
    query = configured.functions["cvaQuerySurfaceError"]
    if query.ret != "VAStatus" or query.params != baseline.functions[query.name].params:
        raise ValueError("Configured query must preserve parameters and return its native status")
    return configured


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--platform", choices=("win32", "drm"), required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--check", action="store_true", help="Verify existing output without writing files")
    args = parser.parse_args()
    try:
        generator = configure(args.platform)
        files = generator.files()
        files["cva.policy.json"] = json.dumps(generator.policy, indent=2) + "\n"
        failures = []
        for name, content in files.items():
            path = args.output / name
            expected = content.encode("utf-8")
            unchanged = path.is_file() and path.read_bytes() == expected
            if args.check:
                if not unchanged:
                    failures.append(name)
            elif not unchanged:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(expected)
        if failures:
            raise ValueError("Configured cva output differs: " + ", ".join(failures))
    except (KeyError, OSError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    action = "Verified" if args.check else "Configured"
    print(f"{action} {len(files)} cva files; only optional diagnostic result handling changes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
