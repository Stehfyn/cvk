"""Verify generated MSVC diagnostics and the owned/external header boundary."""

import argparse
from pathlib import Path
import re
import sys
import xml.etree.ElementTree as ET


NS = {"m": "http://schemas.microsoft.com/developer/msbuild/2003"}


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=root / "build/windows-msvc-2026")
    args = parser.parse_args()
    build = args.build.resolve()
    owned = sorted((build / "examples/CastDemo").rglob("*.vcxproj"))
    owned.append(build / "cvk.vcxproj")
    external = sorted((build / "external/cdx").rglob("*.vcxproj"))
    external.append(build / "external/volk/volk.vcxproj")
    external.append(build / "cva.vcxproj")
    failures = []
    counts = {"owned": 0, "external": 0}

    def require(condition, path, message):
        if not condition:
            failures.append(f"{path.relative_to(build)}: {message}")

    for category, paths in (("owned", owned), ("external", external)):
        for path in paths:
            if not path.is_file():
                require(False, path, "project is missing; configure the preset first")
                continue
            tree = ET.parse(path)
            sources = tree.findall(".//m:ClCompile[@Include]", NS)
            if not any(item.get("Include", "").lower().endswith(".c") for item in sources):
                continue
            counts[category] += 1
            is_owned = category == "owned"
            enabled = "true" if is_owned else "false"
            for setting in ("RunCodeAnalysis", "EnableMicrosoftCodeAnalysis", "EnableClangTidyCodeAnalysis"):
                require(tree.findtext(f".//m:{setting}", namespaces=NS) == enabled,
                        path, f"{setting} must be {enabled}")
            if is_owned:
                for setting, expected in (("CodeAnalysisRuleSet", "AllRules.ruleset"),
                                          ("CodeAnalysisTreatWarningsAsErrors", "true"),
                                          ("ClangTidyWarningsInSystemHeaders", "false")):
                    require(tree.findtext(f".//m:{setting}", namespaces=NS) == expected,
                            path, f"{setting} must be {expected}")
            for group in tree.findall("m:ItemDefinitionGroup", NS):
                compile_node = group.find("m:ClCompile", NS)
                if compile_node is None:
                    continue
                values = {node.tag.rsplit("}", 1)[-1]: node.text or "" for node in compile_node}
                expected = "EnableAllWarnings" if is_owned else "TurnOffAllWarnings"
                require(values.get("WarningLevel") == expected, path, f"WarningLevel must be {expected}")
                require(values.get("ExternalWarningLevel") == "TurnOffAllWarnings",
                        path, "external headers must have warnings disabled")
                if is_owned:
                    require(values.get("TreatWarningAsError") == "true", path, "owned warnings must be errors")
                    require(values.get("DisableSpecificWarnings") == "4820", path, "only padding may be suppressed")
                    require(values.get("DisableAnalyzeExternal") == "true", path, "exclude external header analysis")
                    options = values.get("AdditionalOptions", "")
                    require(not re.search(r"[/\-](?:QSpectre|wd\d+|W[0-4]|WX-)(?:\s|$)", options),
                            path, "additional options override owned diagnostics")
                include_paths = values.get("AdditionalIncludeDirectories", "").replace("\\", "/")
                if is_owned:
                    require("/external/" not in include_paths, path, "dependency header is a normal include directory")
                options = values.get("AdditionalOptions", "")
                for directory in re.findall(r'/external:I\s+"([^"]+)"', options):
                    require("/external/" in directory.replace("\\", "/"), path,
                            f"owned header classified as external: {directory}")

    # SC is a separate profile; its external headers must receive the same treatment.
    sc_project = build / "scvk.vcxproj"
    if sc_project.is_file():
        for node in ET.parse(sc_project).findall("m:ItemDefinitionGroup/m:ClCompile", NS):
            options = node.findtext("m:AdditionalOptions", default="", namespaces=NS)
            require('/external:I "' + root.as_posix() + '/external/VulkanSC-Headers/include"' in options,
                    sc_project, "SC headers must be external")
            require(node.findtext("m:ExternalWarningLevel", namespaces=NS) == "TurnOffAllWarnings",
                    sc_project, "SC header warnings must be disabled")
    else:
        require(False, sc_project, "SC project is missing")
    if failures:
        print("\n".join(failures))
    else:
        print(f"Verified {counts['owned']} owned and {counts['external']} external C targets; SC headers external")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
