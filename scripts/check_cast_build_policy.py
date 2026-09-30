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
    target_directories = build / "CMakeFiles/TargetDirectories.txt"
    active_projects = {Path(directory).parent.parent / (Path(directory).name.removesuffix(".dir") + ".vcxproj")
                       for directory in target_directories.read_text(encoding="utf-8").splitlines()}
    owned = []
    external = []
    for path in sorted(build.rglob("*.vcxproj")):
        if path.is_relative_to(build / "CMakeFiles") or path not in active_projects:
            continue
        sources = [Path(item.get("Include"))
                   for item in ET.parse(path).findall(".//m:ClCompile[@Include]", NS)
                   if item.get("Include", "").lower().endswith(".c")]
        if sources:
            category = external if all(source.is_relative_to(root / "external") or
                                       source.is_relative_to(build / "external")
                                       for source in sources) else owned
            category.append(path)
    failures = []
    counts = {"owned": 0, "external": 0}

    def require(condition, path, message):
        if not condition:
            label = path.relative_to(build) if path.is_relative_to(build) else path.relative_to(root)
            failures.append(f"{label}: {message}")

    for removed in (root / "scripts/run_clang_tidy.py",
                    root / "examples/CastDemo/BuildPolicy.targets"):
        require(not removed.exists(), removed, "custom analysis runners and build hooks must remain absent")

    core_project = build / "cvk.vcxproj"
    platform_project = build / "cvk-platform.vcxproj"
    if core_project.is_file() and platform_project.is_file():
        core_tree = ET.parse(core_project)
        platform_tree = ET.parse(platform_project)
        def compiled_paths(tree):
            return {Path(item.get("Include")) for item in tree.findall(".//m:ClCompile[@Include]", NS)}
        core_sources = compiled_paths(core_tree)
        platform_sources = compiled_paths(platform_tree)
        references = {Path(item.get("Include"))
                      for item in core_tree.findall(".//m:ProjectReference", NS)}
        require(platform_project in references, core_project,
                "platform compilation and analysis must precede core compilation and analysis")
        require(core_sources == {root / "src/cvk.c", root / "src/cvk_json.c",
                                 root / "src/cvk_json_enums.c", root / "src/cvk_json_structs.c"}, core_project,
                "the core project must compile exactly the generated core pair")
        require(bool(platform_sources) and not (core_sources & platform_sources), platform_project,
                "platform sources must compile once, separately from the core pair")
        objects = core_tree.findall(".//m:Object[@Include]", NS)
        require(len(objects) == len(platform_sources), core_project,
                "the cvk archive must retain every generated platform object")

    # Inspect the effective Clang configuration, including built-in defaults.
    policy = root / "examples/CastDemo/.clang-tidy"
    generated_policy = policy
    generated_checks = ()
    cache = (build / "CMakeCache.txt").read_text(encoding="utf-8")
    tidy = re.search(r"^CAST_CLANG_TIDY_EXECUTABLE:FILEPATH=(.+)$", cache, re.M)
    require(tidy is not None, policy, "configured clang-tidy is missing")
    if tidy is not None:
        result = subprocess.run([tidy[1], f"--config-file={policy}", "--dump-config"],
                                capture_output=True, text=True, check=True)
        config = result.stdout
        extra = re.search(r"^ExtraArgs:\n((?:[ \t]+[^\n]*\n)+)", config, re.M)
        options = re.findall(r"^[ \t]+-[ \t]+['\"]?([^'\"\s]+)", extra[1], re.M) if extra else []
        require(options == ["-Wno-padded", "-Wno-unsafe-buffer-usage"], policy,
                "only padding and counted C buffer-index diagnostics may be disabled")
        require("-bugprone-easily-swappable-parameters" in config, policy,
                "the approved adjacent-parameter exception must remain enabled")
        require("-clang-analyzer-optin.performance.Padding" in config, policy,
                "the approved compiler-padding exception must cover the analyzer diagnostic")
        for option, expected in (("MinimumParameterNameLength", 1),
                                 ("MinimumVariableNameLength", 1),
                                 ("MinimumLoopCounterNameLength", 2)):
            match = re.search(r"readability-identifier-length\." + option + r": +['\"]?(\d+)", config)
            require(match is not None and int(match[1]) == expected, policy,
                    f"identifier-length {option} must be {expected}")

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
            for setting in ("UseMultiToolTask", "UseMsbuildResourceManager", "EnforceProcessCountAcrossBuilds",
                            "MultiProcSchedulerName", "MultiProcMaxCount"):
                require(tree.find(f".//m:{setting}", namespaces=NS) is None, path,
                        f"{setting} must not add a scheduler override")
            require(tree.findtext(".//m:MaxNumberOfProcesses", namespaces=NS) ==
                    "0", path,
                    "clang-tidy must use the host processor count")
            require(tree.find(".//m:ForceImportAfterCppTargets", namespaces=NS) is None, path,
                    "analysis must not import custom build hooks")
            is_owned = category == "owned"
            enabled = "true" if is_owned else "false"
            for setting in ("RunCodeAnalysis", "EnableMicrosoftCodeAnalysis", "EnableClangTidyCodeAnalysis"):
                require(tree.findtext(f".//m:{setting}", namespaces=NS) == enabled,
                        path, f"{setting} must be {enabled}")
            if is_owned:
                for source in sources:
                    if source.get("Include", "").lower().endswith(".c"):
                        require(source.findtext("m:MinimalRebuild", namespaces=NS) == "false", path,
                                "owned sources must disable obsolete minimal rebuild")
                        options = source.find("m:PREfastAdditionalOptions", NS)
                        require(options is not None and options.text == "$(PREfastAdditionalOptions)", path,
                                "native analysis diagnostics must remain visible without a rewrite target")
                is_generated = path in (build / "cvk.vcxproj", build / "cvk-platform.vcxproj",
                                        build / "scvk.vcxproj")
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
                if is_owned:
                    require(values.get("WarningLevel") == "EnableAllWarnings", path,
                            "owned warnings must remain enabled")
                else:
                    require(values.get("WarningLevel") != "TurnOffAllWarnings", path,
                            "external projects must not disable compiler warnings")
                if is_owned:
                    require(values.get("TreatWarningAsError") == "true", path, "owned warnings must be errors")
                    link_codegen_target = path.name in ("ImmersiveWindowDemo.vcxproj", "immersive-video.vcxproj")
                    expected_disabled = "4711;4820" if link_codegen_target else "4820"
                    require(values.get("DisableSpecificWarnings") == expected_disabled, path,
                            "compiler warning exceptions must match the target policy")
                    require(values.get("DisableAnalyzeExternal") == "true", path, "exclude external header analysis")
                    options = values.get("AdditionalOptions", "")
                    checked_options = options.replace("/wd4711", "") if link_codegen_target else options
                    require(not re.search(r"[/\-](?:QSpectre|wd\d+|W[0-4]|WX-)(?:\s|$)", checked_options),
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
    else:
        require(False, sc_project, "SC project is missing")
    if failures:
        print("\n".join(failures))
    else:
        print(f"Verified {counts['owned']} owned and {counts['external']} external C targets; SC headers external")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
