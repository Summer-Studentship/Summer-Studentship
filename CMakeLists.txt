cmake_minimum_required(VERSION 3.21)

project(TsunamiBarrierSimulation LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(
    TSUNAMI_BUILD_GUI
    "Build the Qt Widgets graphical application"
    ON
)

option(
    TSUNAMI_BUILD_TESTS
    "Build the test suite"
    OFF
)

option(
    TSUNAMI_BUILD_MATPLOT_SMOKE
    "Build the Matplot++ smoke-test executable"
    OFF
)
add_subdirectory(src)

if(TSUNAMI_BUILD_GUI)
    find_package(
        Qt6
        REQUIRED
        COMPONENTS
            Widgets
    )

    add_subdirectory(apps/TsunamiGUI)
endif()

if(TSUNAMI_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(TSUNAMI_BUILD_MATPLOT_SMOKE)
    add_subdirectory(tools/matplot_smoke)
endif()

include(CTest)
