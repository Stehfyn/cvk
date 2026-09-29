# Required owned-code policy. External libraries/headers are excluded by user instruction;
# MSVC permits only compiler padding. The four later approved Clang exceptions
# are specified in .clang-tidy; the VS command line retains the same check selection.
find_program(CAST_CLANG_TIDY_EXECUTABLE NAMES clang-tidy REQUIRED)
get_filename_component(cast_clang_tidy_directory "${CAST_CLANG_TIDY_EXECUTABLE}" DIRECTORY)
set(cast_clang_tidy_config "${CMAKE_CURRENT_LIST_DIR}/.clang-tidy")
set(cast_analysis_targets "${CMAKE_CURRENT_LIST_DIR}/BuildPolicy.targets")

# An installed executable is insufficient if it cannot recognize the policy.
# Recheck when the configuration changes; retain every diagnostic on failure.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${cast_clang_tidy_config}")
execute_process(
    COMMAND "${CAST_CLANG_TIDY_EXECUTABLE}" "--config-file=${cast_clang_tidy_config}" --verify-config
    COMMAND_ERROR_IS_FATAL ANY)

function(cast_apply_build_policy target)
    get_target_property(target_type ${target} TYPE)
    if(target_type STREQUAL "INTERFACE_LIBRARY" OR target_type STREQUAL "UTILITY")
        return()
    endif()

    target_compile_options(${target} PRIVATE
        "$<$<COMPILE_LANG_AND_ID:C,MSVC>:/Wall;/WX;/wd4820;/external:W0;/external:env:INCLUDE;/analyze:external->")
    set_target_properties(${target} PROPERTIES
        C_CLANG_TIDY "${CAST_CLANG_TIDY_EXECUTABLE};--config-file=${cast_clang_tidy_config};--warnings-as-errors=*"
        NO_SYSTEM_FROM_IMPORTED FALSE
        VS_GLOBAL_RunCodeAnalysis true
        VS_GLOBAL_CodeAnalysisRuleSet "AllRules.ruleset"
        VS_GLOBAL_CodeAnalysisTreatWarningsAsErrors true
        VS_GLOBAL_ForceImportAfterCppTargets "${cast_analysis_targets}"
        VS_GLOBAL_EnableMicrosoftCodeAnalysis true
        VS_GLOBAL_EnableClangTidyCodeAnalysis true
        VS_GLOBAL_ClangTidyToolPath "${cast_clang_tidy_directory}"
        VS_GLOBAL_ClangTidyChecks "clang-analyzer-*,bugprone-*,cert-*,concurrency-*,misc-*,performance-*,portability-*,readability-*,-bugprone-easily-swappable-parameters"
        VS_GLOBAL_ClangTidyHeaderFilter ".*"
        VS_GLOBAL_ClangTidyWarningsInSystemHeaders false
        VS_GLOBAL_ClangTidyToolExeAdditionalOptions "--warnings-as-errors=* --config-file=\"${cast_clang_tidy_config}\"")
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
