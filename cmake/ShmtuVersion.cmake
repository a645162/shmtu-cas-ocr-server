function(shmtu_apply_target_version target version)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "shmtu_apply_target_version: target '${target}' does not exist")
    endif()

    set_target_properties(${target} PROPERTIES VERSION "${version}")
endfunction()

function(shmtu_apply_library_version target version soversion)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "shmtu_apply_library_version: target '${target}' does not exist")
    endif()

    set_target_properties(${target} PROPERTIES
        VERSION "${version}"
        SOVERSION "${soversion}"
    )
endfunction()
