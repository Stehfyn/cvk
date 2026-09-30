# Dependencies keep their upstream sources. SYSTEM propagates header classification.
function(cvk_external_target target)
    set_target_properties(${target} PROPERTIES SYSTEM TRUE CVK_EXTERNAL TRUE)
    get_target_property(target_type ${target} TYPE)
    if(NOT target_type STREQUAL "INTERFACE_LIBRARY" AND NOT target_type STREQUAL "UTILITY")
        set_target_properties(${target} PROPERTIES
            C_CLANG_TIDY ""
            VS_GLOBAL_RunCodeAnalysis false
            VS_GLOBAL_EnableMicrosoftCodeAnalysis false
            VS_GLOBAL_EnableClangTidyCodeAnalysis false)
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
