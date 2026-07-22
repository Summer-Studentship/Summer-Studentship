find_package(CLI11 CONFIG REQUIRED)

add_executable(tsunami_cli
    main.cpp
)

target_compile_features(tsunami_cli PRIVATE cxx_std_20)

target_link_libraries(tsunami_cli
    PRIVATE
        tsunami_core
        CLI11::CLI11
)

tsunami_apply_compiler_warnings(tsunami_cli)
