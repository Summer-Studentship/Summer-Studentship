# Frontend Service Boundary v0.1

- **Work Package:** `SWE-ARC-SVC-WP1`

Both frontends create the same factory product:

```cpp
tsunami::application::make_no_solver_application_service()
```

Both consume the same service-description value and operation definitions. The CLI formats deterministic text for `--service-status`; the Qt shell converts the same neutral standard-library values into Qt/QML values during startup.

Neither frontend implements lifecycle rules, fabricates run/case state, or links directly to solver, coupling, FVM, persistence or geospatial targets. Formatting differs; service semantics do not.
