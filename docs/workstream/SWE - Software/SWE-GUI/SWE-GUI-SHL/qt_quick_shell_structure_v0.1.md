# Qt Quick Shell Structure v0.1

The retained GUI target is `tsunami_gui`. The retained QML module URI is `Tsunami.Studentship`.

The shell uses `ApplicationWindow`, a persistent `ApplicationHeader`, `NavigationRail`, central `StackLayout` content surface, `DiagnosticPanel` and `ServiceStatusBar`.

QML resources are declared through `qt_add_qml_module`. Component resources live under `qml/components/`; placeholder pages live under `qml/pages/`. `ShellController` is private to `tsunami_gui`, owns the accepted application-service instance and converts neutral service/diagnostic values into Qt properties.

Qt modules used by the GUI target are `Qt6::Quick` and `Qt6::QuickControls2`. Core, data and application targets remain Qt-free.
