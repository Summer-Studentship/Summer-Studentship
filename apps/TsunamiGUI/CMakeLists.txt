set(PROJECT_SOURCES
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
)

qt_add_executable(TsunamiGUI ${PROJECT_SOURCES})

set_target_properties(TsunamiGUI PROPERTIES
    AUTOMOC ON
    AUTOUIC ON
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

target_link_libraries(TsunamiGUI
    PRIVATE
        tsunami_core
        Qt6::Widgets
)

include(GNUInstallDirs)
install(TARGETS TsunamiGUI
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
