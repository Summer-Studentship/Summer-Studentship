add_executable(tsunami_r2d_benchmark
    main.cpp
)

target_compile_features(tsunami_r2d_benchmark PRIVATE cxx_std_20)

target_link_libraries(tsunami_r2d_benchmark
    PRIVATE
        tsunami_r2d
        tsunami_r2d_io
        tsunami_r2d_benchmarks
)

tsunami_apply_compiler_warnings(tsunami_r2d_benchmark)
