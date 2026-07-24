# GUI Shell Audit v0.1

The existing active GUI target was `tsunami_gui`, with QML module URI `Tsunami.Studentship`. It linked `tsunami_application` and `Qt6::Quick`, exposed service status and the accepted representative diagnostic through `main.cpp`, and contained one QML resource: `qml/Main.qml`.

Current smoke modes were `--smoke-startup` and `--diagnostic-smoke`. The existing QML had no navigation capability and used a bare `Window` rather than an application shell. Reusable elements were limited to accepted service and diagnostic conversion logic, which needed to move into a GUI-owned controller.

Task `#147` was partially satisfied by the G0 CMake scaffold, but closure required an accepted resource hierarchy and maintainable shell composition. Downstream GUI WPs own case editing, run controls, logs and visualisation.
