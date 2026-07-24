find_package(Catch2 3 CONFIG REQUIRED)

add_executable(tsunami_tests
    application/application_service_tests.cpp
    contracts/contract_dummy_consumers.cpp
    contracts/interface_contracts.cpp
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
        tsunami_l3d
        tsunami_coupling
        Catch2::Catch2WithMain
)

tsunami_apply_compiler_warnings(tsunami_tests)

include(Catch)
catch_discover_tests(tsunami_tests)
