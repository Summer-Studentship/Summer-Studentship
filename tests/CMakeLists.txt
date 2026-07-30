find_package(Catch2 3 CONFIG REQUIRED)

add_executable(tsunami_tests
    application/application_service_tests.cpp
    core/diagnostic_tests.cpp
    contracts/contract_dummy_consumers.cpp
    contracts/interface_contracts.cpp
    fvm/boundary_tests.cpp
    data/case_configuration_tests.cpp
    fvm/field_tests.cpp
    fvm/mesh_records_tests.cpp
    fvm/numerics_tests.cpp
    fvm/mesh_topology_tests.cpp
    r2d/state_flux_residual_tests.cpp
    r2d/regional_earthquake_initialisation_tests.cpp
    r2d/regional_boundary_relaxation_tests.cpp
    r2d/regional_source_terms_tests.cpp
    r2d/time_integration_tests.cpp
    r2d/well_balanced_wet_dry_tests.cpp
    r2d_benchmarks/benchmark_case_tests.cpp
    r2d_io/csv_output_tests.cpp
    smoke/core_smoke.cpp
)

target_compile_features(tsunami_tests PRIVATE cxx_std_20)

target_link_libraries(tsunami_tests
    PRIVATE
        tsunami_core
        tsunami_application
        tsunami_fvm
        tsunami_data
        tsunami_r2d
        tsunami_r2d_benchmarks
        tsunami_r2d_io
        tsunami_l3d
        tsunami_coupling
        Catch2::Catch2WithMain
)

tsunami_apply_compiler_warnings(tsunami_tests)

include(Catch)
catch_discover_tests(tsunami_tests)
