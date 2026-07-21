add_library(tsunami_core STATIC
    core/File_System.cpp
    core/Constants.hpp
    core/Error.hpp
    core/Results.hpp
    core/Types.hpp
)

target_include_directories(tsunami_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(tsunami_core PUBLIC cxx_std_20)
