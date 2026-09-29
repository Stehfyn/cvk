"""Verify generated MSVC diagnostics and the owned/external header boundary."""

import argparse
from pathlib import Path
import re
import sys
import subprocess
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
            label = path.relative_to(build) if path.is_relative_to(build) else path.relative_to(root)
            failures.append(f"{label}: {message}")

    # Inspect the effective Clang configuration, including built-in defaults.
    policy = root / "examples/CastDemo/.clang-tidy"
    generated_policy = build / "examples/CastDemo/cvk-generated.clang-tidy"
    generated_checks = (
        "readability-non-const-parameter",
        "bugprone-invalid-enum-default-initialization",
        "clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling",
        "misc-include-cleaner",
        "readability-magic-numbers",
        "misc-no-recursion",
        "performance-no-int-to-ptr",
        "clang-analyzer-optin.core.EnumCastOutOfRange",
        "readability-redundant-casting",
        "readability-function-cognitive-complexity",
        "readability-redundant-parentheses",
        "clang-analyzer-optin.performance.Padding",
        "readability-function-size",
        "readability-identifier-length",
        "readability-avoid-nested-conditional-operator",
        "readability-math-missing-parentheses",
        "clang-analyzer-security.ArrayBound",
    )
    cache = (build / "CMakeCache.txt").read_text(encoding="utf-8")
    tidy = re.search(r"^CAST_CLANG_TIDY_EXECUTABLE:FILEPATH=(.+)$", cache, re.M)
    require(tidy is not None, policy, "configured clang-tidy is missing")
    if tidy is not None:
        result = subprocess.run([tidy[1], f"--config-file={policy}", "--dump-config"],
                                capture_output=True, text=True, check=True)
        config = result.stdout
        extra = re.search(r"^ExtraArgs:\n((?:[ \t]+[^\n]*\n)+)", config, re.M)
        options = re.findall(r"^[ \t]+-[ \t]+['\"]?([^'\"\s]+)", extra[1], re.M) if extra else []
        require(options == ["-Wno-padded", "-Wno-documentation", "-Wno-unsafe-buffer-usage"], policy,
                "only the approved compiler-padding, documentation and raw C buffer exceptions are permitted")
        require("-bugprone-easily-swappable-parameters" in config, policy,
                "the approved adjacent-parameter exception must be effective")
        for option, expected in (("MinimumParameterNameLength", 1),
                                 ("MinimumVariableNameLength", 3),
                                 ("MinimumLoopCounterNameLength", 2)):
            match = re.search(r"readability-identifier-length\." + option + r": +['\"]?(\d+)", config)
            require(match is not None and int(match[1]) == expected, policy,
                    f"identifier-length {option} must be {expected}")
        result = subprocess.run([tidy[1], f"--config-file={generated_policy}", "--dump-config"],
                                capture_output=True, text=True, check=True)
        generated_config = result.stdout
        extra = re.search(r"^ExtraArgs:\n((?:[ \t]+[^\n]*\n)+)", generated_config, re.M)
        options = re.findall(r"^[ \t]+-[ \t]+['\"]?([^'\"\s]+)", extra[1], re.M) if extra else []
        require(options == ["-Wno-padded", "-Wno-documentation", "-Wno-unsafe-buffer-usage",
                            "-Wno-missing-noreturn", "-Wno-implicit-int-enum-cast",
                            "-Wno-bad-function-cast"], generated_policy,
                "generated CVK compiler exceptions exceed the approved set")
        original_checks = re.search(r"^Checks: +([^\n]+)", config, re.M)
        effective_checks = re.search(r"^Checks: +([^\n]+)", generated_config, re.M)
        def check_list(match):
            return [entry.strip() for entry in match[1].strip("'\"").replace("\\n", "").split(",")] if match else []
        require(original_checks is not None and effective_checks is not None and
                check_list(effective_checks) == check_list(original_checks) +
                ["-" + check for check in generated_checks], generated_policy,
                "generated CVK check exceptions exceed the approved set")

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
                is_generated = path == build / "cvk.vcxproj"
                expected_policy = generated_policy if is_generated else policy
                analysis_options = tree.findtext(".//m:ClangTidyToolExeAdditionalOptions", default="", namespaces=NS)
                require(expected_policy.as_posix() in analysis_options.replace("\\", "/"), path,
                        "Clang policy must match this target's approved scope")
                checks = tree.findtext(".//m:ClangTidyChecks", default="", namespaces=NS)
                for check in generated_checks:
                    require(("-" + check in checks) == is_generated, path,
                            "generated-code exceptions may apply only to cvk")
                if is_generated:
                    for source in sources:
                        source_path = Path(source.get("Include", ""))
                        if source_path.suffix.lower() == ".c":
                            require(source_path.is_relative_to(root / "src"), path,
                                    "the generated CVK policy must not cover application sources")
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
