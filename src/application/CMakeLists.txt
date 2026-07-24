add_library(tsunami_application STATIC
    src/NoSolverApplicationService.cpp
)
add_library(Tsunami::application ALIAS tsunami_application)

target_include_directories(tsunami_application
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(tsunami_application PUBLIC cxx_std_20)

target_link_libraries(tsunami_application
    PUBLIC
        tsunami_core
        tsunami_data
)

tsunami_apply_compiler_warnings(tsunami_application)
