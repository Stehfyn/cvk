# Dependencies keep their upstream sources and are excluded from owned-code diagnostics.
# SYSTEM propagates header classification without changing diagnostics in consumers.
function(cvk_external_target target)
    set_target_properties(${target} PROPERTIES SYSTEM TRUE)
    get_target_property(target_type ${target} TYPE)
    if(NOT target_type STREQUAL "INTERFACE_LIBRARY" AND NOT target_type STREQUAL "UTILITY")
        set_target_properties(${target} PROPERTIES
            C_CLANG_TIDY ""
            VS_GLOBAL_RunCodeAnalysis false
            VS_GLOBAL_EnableMicrosoftCodeAnalysis false
            VS_GLOBAL_EnableClangTidyCodeAnalysis false)
        target_compile_options(${target} PRIVATE
            "$<$<COMPILE_LANG_AND_ID:C,MSVC>:/W0;/external:W0;/external:env:INCLUDE>"
            "$<$<COMPILE_LANG_AND_ID:C,Clang,GNU>:-w>")
    endif()
endfunction()

function(cvk_external_directory directory)
    get_property(targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(target IN LISTS targets)
        cvk_external_target(${target})
    endforeach()
    get_property(children DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
    foreach(child IN LISTS children)
        cvk_external_directory("${child}")
    endforeach()
endfunction()
