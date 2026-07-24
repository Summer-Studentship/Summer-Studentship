find_package(
    Qt6
    REQUIRED
    COMPONENTS
        Quick
        QuickControls2
)

qt_standard_project_setup(REQUIRES 6.5)
qt_policy(SET QTP0004 NEW)

qt_add_executable(tsunami_gui
    main.cpp
    ShellController.cpp
    ShellController.hpp
)

qt_add_qml_module(tsunami_gui
    URI Tsunami.Studentship
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/components/ApplicationHeader.qml
        qml/components/DiagnosticPanel.qml
        qml/components/NavigationDelegate.qml
        qml/components/NavigationRail.qml
        qml/components/PlaceholderPage.qml
        qml/components/ServiceStatusBar.qml
        qml/pages/DataPage.qml
        qml/pages/DomainPage.qml
        qml/pages/MeshPage.qml
        qml/pages/PhysicsPage.qml
        qml/pages/SolverPage.qml
        qml/pages/RunPage.qml
        qml/pages/PostProcessingPage.qml
)

target_compile_features(tsunami_gui PRIVATE cxx_std_20)

target_link_libraries(tsunami_gui
    PRIVATE
        tsunami_application
        Qt6::Quick
        Qt6::QuickControls2
)

tsunami_apply_compiler_warnings(tsunami_gui)
