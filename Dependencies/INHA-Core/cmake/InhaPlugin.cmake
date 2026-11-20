include_guard(GLOBAL)

function(inha_attach_core target)
    if (NOT TARGET ${target})
        message(FATAL_ERROR "inha_attach_core: '${target}' is not a CMake target")
    endif()
    if (NOT TARGET INHA::Core)
        message(FATAL_ERROR
            "INHA Core has not been added. Call add_subdirectory(Dependencies/INHA-Core) first.")
    endif()

    target_link_libraries(${target} PRIVATE INHA::Core)
    if (WIN32)
        if (NOT TARGET INHA::License)
            message(FATAL_ERROR "INHA::License is unavailable on this Windows build")
        endif()
        target_link_libraries(${target} PRIVATE INHA::License)
    endif()
endfunction()
