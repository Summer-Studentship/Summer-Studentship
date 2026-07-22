cmake_minimum_required(VERSION 3.28)

project(
    TsunamiBarrierSimulation
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Export compile_commands.json")

option(
    TSUNAMI_BUILD_CLI
    "Build the command-line application"
    ON
)

option(
    TSUNAMI_BUILD_GUI
    "Build the Qt Quick application"
    OFF
)

option(
    TSUNAMI_WARNINGS_AS_ERRORS
    "Treat project warnings as errors"
    OFF
)

include(CTest)
include(cmake/TsunamiCompilerWarnings.cmake)

add_subdirectory(src)

if(TSUNAMI_BUILD_CLI)
    add_subdirectory(apps/tsunami_cli)
endif()

if(TSUNAMI_BUILD_GUI)
    add_subdirectory(apps/tsunami_gui)
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
