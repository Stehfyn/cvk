# Required owned-code policy. External libraries/headers are excluded by user instruction;
# MSVC permits only compiler padding. The four later approved Clang exceptions
# are specified in .clang-tidy; the VS command line retains the same check selection.
find_program(CAST_CLANG_TIDY_EXECUTABLE NAMES clang-tidy REQUIRED)
get_filename_component(cast_clang_tidy_directory "${CAST_CLANG_TIDY_EXECUTABLE}" DIRECTORY)
set(cast_clang_tidy_config "${CMAKE_CURRENT_LIST_DIR}/.clang-tidy")
set(cast_analysis_targets "${CMAKE_CURRENT_LIST_DIR}/BuildPolicy.targets")
set(cast_clang_tidy_checks "clang-analyzer-*,bugprone-*,cert-*,concurrency-*,misc-*,performance-*,portability-*,readability-*,-bugprone-easily-swappable-parameters")

# The user requires generated CVK code to remain unchanged and approved only the
# diagnostic categories reported in that code. Derive its Clang policy from
# the owned policy so future changes to the common checks remain effective.
set(cast_generated_clang_tidy_config "${CMAKE_CURRENT_BINARY_DIR}/cvk-generated.clang-tidy")
string(JOIN "," cast_generated_clang_tidy_exceptions
    "-readability-non-const-parameter"
    "-bugprone-invalid-enum-default-initialization"
    "-clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling"
    "-misc-include-cleaner"
    "-readability-magic-numbers"
    "-misc-no-recursion"
    "-performance-no-int-to-ptr"
    "-clang-analyzer-optin.core.EnumCastOutOfRange"
    "-readability-redundant-casting"
    "-readability-function-cognitive-complexity"
    "-readability-redundant-parentheses"
    "-clang-analyzer-optin.performance.Padding"
    "-readability-function-size")
file(READ "${cast_clang_tidy_config}" cast_generated_clang_tidy_text)
string(REPLACE "  -bugprone-easily-swappable-parameters"
    "  -bugprone-easily-swappable-parameters,\n  ${cast_generated_clang_tidy_exceptions}"
    cast_generated_clang_tidy_text "${cast_generated_clang_tidy_text}")
string(REPLACE "'-Wno-unsafe-buffer-usage']"
    "'-Wno-unsafe-buffer-usage', '-Wno-missing-noreturn', '-Wno-implicit-int-enum-cast', '-Wno-bad-function-cast']"
    cast_generated_clang_tidy_text "${cast_generated_clang_tidy_text}")
file(WRITE "${cast_generated_clang_tidy_config}" "${cast_generated_clang_tidy_text}")

# An installed executable is insufficient if it cannot recognize the policy.
# Recheck when the configuration changes; retain every diagnostic on failure.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${cast_clang_tidy_config}")
execute_process(
    COMMAND "${CAST_CLANG_TIDY_EXECUTABLE}" "--config-file=${cast_clang_tidy_config}" --verify-config
    COMMAND_ERROR_IS_FATAL ANY)
execute_process(
    COMMAND "${CAST_CLANG_TIDY_EXECUTABLE}" "--config-file=${cast_generated_clang_tidy_config}" --verify-config
    COMMAND_ERROR_IS_FATAL ANY)

function(cast_apply_build_policy target)
    get_target_property(target_type ${target} TYPE)
    if(target_type STREQUAL "INTERFACE_LIBRARY" OR target_type STREQUAL "UTILITY")
        return()
    endif()

    set(target_clang_tidy_config "${cast_clang_tidy_config}")
    set(target_clang_tidy_checks "${cast_clang_tidy_checks}")
    if(target STREQUAL "cvk")
        set(target_clang_tidy_config "${cast_generated_clang_tidy_config}")
        string(APPEND target_clang_tidy_checks ",${cast_generated_clang_tidy_exceptions}")
    endif()

    target_compile_options(${target} PRIVATE
        "$<$<COMPILE_LANG_AND_ID:C,MSVC>:/Wall;/WX;/wd4820;/external:W0;/external:env:INCLUDE;/analyze:external->")
    set_target_properties(${target} PROPERTIES
        C_CLANG_TIDY "${CAST_CLANG_TIDY_EXECUTABLE};--config-file=${target_clang_tidy_config};--warnings-as-errors=*"
        NO_SYSTEM_FROM_IMPORTED FALSE
        VS_GLOBAL_RunCodeAnalysis true
        VS_GLOBAL_CodeAnalysisRuleSet "AllRules.ruleset"
        VS_GLOBAL_CodeAnalysisTreatWarningsAsErrors true
        VS_GLOBAL_ForceImportAfterCppTargets "${cast_analysis_targets}"
        VS_GLOBAL_EnableMicrosoftCodeAnalysis true
        VS_GLOBAL_EnableClangTidyCodeAnalysis true
        VS_GLOBAL_ClangTidyToolPath "${cast_clang_tidy_directory}"
        VS_GLOBAL_ClangTidyChecks "${target_clang_tidy_checks}"
        VS_GLOBAL_ClangTidyHeaderFilter ".*"
        VS_GLOBAL_ClangTidyWarningsInSystemHeaders false
        VS_GLOBAL_ClangTidyToolExeAdditionalOptions "--warnings-as-errors=* --config-file=\"${target_clang_tidy_config}\"")
endfunction()

function(cast_apply_directory_policy directory)
    get_property(targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(target IN LISTS targets)
        cast_apply_build_policy(${target})
    endforeach()
    get_property(children DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
    foreach(child IN LISTS children)
        cast_apply_directory_policy("${child}")
    endforeach()
endfunction()
