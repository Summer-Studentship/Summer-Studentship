find_package(CLI11 CONFIG REQUIRED)

add_executable(tsunami_r2d_case
    main.cpp
)

target_compile_features(tsunami_r2d_case PRIVATE cxx_std_20)

target_link_libraries(tsunami_r2d_case
    PRIVATE
        tsunami_r2d_case_runner
        CLI11::CLI11
)

tsunami_apply_compiler_warnings(tsunami_r2d_case)

if(BUILD_TESTING)
    add_test(NAME tsunami_r2d_case_help COMMAND tsunami_r2d_case --help)
endif()
