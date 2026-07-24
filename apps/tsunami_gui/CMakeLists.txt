find_package(
    Qt6
    REQUIRED
    COMPONENTS
        Quick
)

qt_standard_project_setup(REQUIRES 6.5)
qt_policy(SET QTP0004 NEW)

qt_add_executable(tsunami_gui
    main.cpp
)

qt_add_qml_module(tsunami_gui
    URI Tsunami.Studentship
    VERSION 1.0
    QML_FILES
        qml/Main.qml
)

target_compile_features(tsunami_gui PRIVATE cxx_std_20)

target_link_libraries(tsunami_gui
    PRIVATE
        tsunami_application
        Qt6::Quick
)

tsunami_apply_compiler_warnings(tsunami_gui)
