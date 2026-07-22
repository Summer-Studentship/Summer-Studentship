function(tsunami_apply_compiler_warnings target_name)
    if(NOT TARGET "${target_name}")
        message(FATAL_ERROR "Cannot apply warnings to unknown target: ${target_name}")
    endif()

    set(msvc_warnings
        /W4
        /permissive-
    )

    set(gcc_warnings
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wsign-conversion
    )

    set(clang_warnings
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wsign-conversion
    )

    if(MSVC)
        target_compile_options("${target_name}" PRIVATE ${msvc_warnings})
        if(TSUNAMI_WARNINGS_AS_ERRORS)
            target_compile_options("${target_name}" PRIVATE /WX)
        endif()
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options("${target_name}" PRIVATE ${clang_warnings})
        if(TSUNAMI_WARNINGS_AS_ERRORS)
            target_compile_options("${target_name}" PRIVATE -Werror)
        endif()
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options("${target_name}" PRIVATE ${gcc_warnings})
        if(TSUNAMI_WARNINGS_AS_ERRORS)
            target_compile_options("${target_name}" PRIVATE -Werror)
        endif()
    endif()
endfunction()
