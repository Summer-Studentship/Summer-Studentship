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

add_library(tsunami_fvm STATIC
    fvm/src/FiniteVolumeMesh.cpp
    fvm/src/GreenGaussGradient.cpp
    fvm/src/LinearInterpolationStencil.cpp
    fvm/src/MeshBinding.cpp
    fvm/src/MeshGeometry.cpp
    fvm/src/MeshTopology.cpp
)
add_library(Tsunami::fvm ALIAS tsunami_fvm)
target_include_directories(tsunami_fvm
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/fvm/include>
        $<INSTALL_INTERFACE:include>
)
target_compile_features(tsunami_fvm PUBLIC cxx_std_20)
target_link_libraries(tsunami_fvm PUBLIC tsunami_core)
tsunami_apply_compiler_warnings(tsunami_fvm)

add_library(tsunami_data INTERFACE)
add_library(Tsunami::data ALIAS tsunami_data)
target_include_directories(tsunami_data
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/data/include>
        $<INSTALL_INTERFACE:include>
)
target_compile_features(tsunami_data INTERFACE cxx_std_20)
target_link_libraries(tsunami_data INTERFACE tsunami_core)

add_subdirectory(application)

add_library(tsunami_r2d STATIC
    r2d/src/CflTimestep.cpp
    r2d/src/ForwardEulerUpdate.cpp
    r2d/src/RegionalConservedState.cpp
    r2d/src/RegionalResidual.cpp
    r2d/src/RegionalResidualEvaluation.cpp
    r2d/src/RusanovFlux.cpp
    r2d/src/ShallowWaterFlux.cpp
    r2d/src/ShallowWaterState.cpp
)
add_library(Tsunami::r2d ALIAS tsunami_r2d)
target_include_directories(tsunami_r2d
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/r2d/include>
        $<INSTALL_INTERFACE:include>
)
target_compile_features(tsunami_r2d PUBLIC cxx_std_20)
target_link_libraries(tsunami_r2d PUBLIC tsunami_core tsunami_fvm tsunami_data)
tsunami_apply_compiler_warnings(tsunami_r2d)

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
