# Required owned-code policy. MSVC permits only compiler padding.
find_program(CAST_CLANG_TIDY_EXECUTABLE NAMES clang-tidy REQUIRED)
get_filename_component(cast_clang_tidy_directory "${CAST_CLANG_TIDY_EXECUTABLE}" DIRECTORY)
set(cast_clang_tidy_config "${CMAKE_CURRENT_LIST_DIR}/.clang-tidy")
set(cast_clang_tidy_checks "clang-analyzer-*,bugprone-*,-bugprone-easily-swappable-parameters,cert-*,concurrency-*,misc-*,performance-*,portability-*,readability-*,-clang-analyzer-optin.performance.Padding")

# An installed executable is insufficient if it cannot recognize the policy.
# Recheck when the configuration changes; retain every diagnostic on failure.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${cast_clang_tidy_config}")
execute_process(
    COMMAND "${CAST_CLANG_TIDY_EXECUTABLE}" "--config-file=${cast_clang_tidy_config}" --verify-config
    COMMAND_ERROR_IS_FATAL ANY)
function(cast_apply_build_policy target)
    get_target_property(target_type ${target} TYPE)
    get_target_property(target_external ${target} CVK_EXTERNAL)
    if(target_external OR target_type STREQUAL "INTERFACE_LIBRARY" OR target_type STREQUAL "UTILITY")
        return()
    endif()

    target_compile_options(${target} PRIVATE
        "$<$<COMPILE_LANG_AND_ID:C,MSVC>:/Wall;/WX;/wd4820;/external:env:INCLUDE;/analyze:external->")
    # Native source metadata keeps analysis diagnostics visible and disables the
    # obsolete minimal-rebuild mode without a target that rewrites build items.
    get_target_property(target_sources ${target} SOURCES)
    get_target_property(target_source_directory ${target} SOURCE_DIR)
    foreach(source IN LISTS target_sources)
        if(source MATCHES "\\.[cC]$")
            cmake_path(ABSOLUTE_PATH source BASE_DIRECTORY "${target_source_directory}")
            # Keep the property reference until MSBuild evaluates the item.
            # CMake drops a literal empty value instead of emitting metadata.
            set_property(SOURCE "${source}" TARGET_DIRECTORY ${target} APPEND PROPERTY
                VS_SETTINGS "MinimalRebuild=false"
                "PREfastAdditionalOptions=$(PREfastAdditionalOptions)")
        endif()
    endforeach()
    set_target_properties(${target} PROPERTIES
        C_CLANG_TIDY "${CAST_CLANG_TIDY_EXECUTABLE};--config-file=${cast_clang_tidy_config};--warnings-as-errors=*"
        NO_SYSTEM_FROM_IMPORTED FALSE
        VS_GLOBAL_RunCodeAnalysis true
        VS_GLOBAL_CodeAnalysisRuleSet "AllRules.ruleset"
        VS_GLOBAL_CodeAnalysisTreatWarningsAsErrors true
        VS_GLOBAL_EnableMicrosoftCodeAnalysis true
        VS_GLOBAL_EnableClangTidyCodeAnalysis true
        VS_GLOBAL_ClangTidyToolPath "${cast_clang_tidy_directory}"
        VS_GLOBAL_ClangTidyChecks "${cast_clang_tidy_checks}"
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
        set(external_directory "${PROJECT_SOURCE_DIR}/external")
        cmake_path(IS_PREFIX external_directory "${child}" NORMALIZE child_external)
        if(NOT child_external)
            cast_apply_directory_policy("${child}")
        endif()
    endforeach()
endfunction()
