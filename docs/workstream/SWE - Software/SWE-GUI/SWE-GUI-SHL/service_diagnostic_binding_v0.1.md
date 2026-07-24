# Service and Diagnostic Binding v0.1

`ShellController` queries the shared no-solver application service and exposes backend, boundary and capability properties to QML. QML formats the values but does not invent capability state.

`runDiagnosticSmoke()` calls the accepted shared validation boundary with an empty case location. The diagnostic code, category, severity, message and context originate in core/application contracts and are exposed through read-only Qt properties.

The diagnostic panel displays severity, category, code, message, operation, rule ID and state-changed context. It is inactive during ordinary startup and is not a final log viewer.
