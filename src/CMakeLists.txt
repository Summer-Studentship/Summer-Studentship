add_library(tsunami_core STATIC
    core/File_System.cpp
)
add_library(Tsunami::core ALIAS tsunami_core)

target_include_directories(tsunami_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/core/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(tsunami_core PUBLIC cxx_std_20)

tsunami_apply_compiler_warnings(tsunami_core)

add_library(tsunami_fvm INTERFACE)
add_library(Tsunami::fvm ALIAS tsunami_fvm)
target_include_directories(tsunami_fvm
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/fvm/include>
        $<INSTALL_INTERFACE:include>
)
target_compile_features(tsunami_fvm INTERFACE cxx_std_20)
target_link_libraries(tsunami_fvm INTERFACE tsunami_core)

add_library(tsunami_data INTERFACE)
add_library(Tsunami::data ALIAS tsunami_data)
target_include_directories(tsunami_data
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/data/include>
        $<INSTALL_INTERFACE:include>
)
target_compile_features(tsunami_data INTERFACE cxx_std_20)
target_link_libraries(tsunami_data INTERFACE tsunami_core)

add_library(tsunami_r2d INTERFACE)
add_library(Tsunami::r2d ALIAS tsunami_r2d)
target_include_directories(tsunami_r2d
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/r2d/include>
        $<INSTALL_INTERFACE:include>
)
target_compile_features(tsunami_r2d INTERFACE cxx_std_20)
target_link_libraries(tsunami_r2d INTERFACE tsunami_core tsunami_data)

add_library(tsunami_l3d INTERFACE)
add_library(Tsunami::l3d ALIAS tsunami_l3d)
target_include_directories(tsunami_l3d
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/l3d/include>
        $<INSTALL_INTERFACE:include>
)
target_compile_features(tsunami_l3d INTERFACE cxx_std_20)
target_link_libraries(tsunami_l3d INTERFACE tsunami_core tsunami_data)

add_library(tsunami_coupling INTERFACE)
add_library(Tsunami::coupling ALIAS tsunami_coupling)
target_include_directories(tsunami_coupling
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/coupling/include>
        $<INSTALL_INTERFACE:include>
)
target_compile_features(tsunami_coupling INTERFACE cxx_std_20)
target_link_libraries(tsunami_coupling
    INTERFACE
        tsunami_core
        tsunami_data
        tsunami_r2d
        tsunami_l3d
)
