add_library(tsunami_core STATIC
    core/File_System.cpp
)
add_library(Tsunami::core ALIAS tsunami_core)

target_include_directories(tsunami_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(tsunami_core PUBLIC cxx_std_20)

tsunami_apply_compiler_warnings(tsunami_core)
