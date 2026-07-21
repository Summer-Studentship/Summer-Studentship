# Build Audit Applicability

The prior technical build audit was conducted against transferred files that
are materially equivalent to the files now treated as the starting point for
`Summer-Studentship`. Its technical findings are therefore useful planning
evidence where the corresponding files remain unchanged.

The archived repository's local path, branches, remote configuration, and
commit history do not describe `Summer-Studentship`. Repository identity and
publication decisions must be based on the active `Summer-Studentship`
repository only.

All implementation work now belongs exclusively to `Summer-Studentship`. The
archived repository is a read-only historical reference.

## Applicability Check

| Prior technical finding | Current classification | Current evidence |
| --- | --- | --- |
| Root build uses CMake and C++20 | Confirmed | `CMakeLists.txt` defines the project and sets `CMAKE_CXX_STANDARD 20`. |
| Active GUI target is a Qt Widgets application | Confirmed | `apps/TsunamiGUI` uses `QApplication`, `QMainWindow`, `mainwindow.ui`, AUTOUIC, AUTOMOC, and `Qt6::Widgets`. |
| Shared presets include Windows MinGW and Linux configurations | Confirmed | `cmake --list-presets=all` lists `windows-mingw-vcpkg`, `windows-mingw-vcpkg-all`, `linux-vcpkg-headless`, and `linux-gui-debug`. |
| Scripts and documentation refer to preset names that may not exist | Confirmed | The Windows build script and build notes referenced `qt-kit-*` preset names before reconciliation. |
| Shared Linux presets may omit the vcpkg toolchain | Confirmed | The original shared Linux preset set `VCPKG_TARGET_TRIPLET` but did not set `CMAKE_TOOLCHAIN_FILE`. |
| Docker may bootstrap vcpkg without activating its CMake toolchain | Confirmed | The Dockerfile bootstraps vcpkg and selects `linux-vcpkg-headless`; the original preset did not activate the toolchain. |
| CTest may be enabled without registered tests | Confirmed | `include(CTest)` is present, no `add_test(...)` calls were found, and no `tests/CMakeLists.txt` exists. |
| Installation may exist only at a basic executable-target level | Confirmed | `apps/TsunamiGUI/CMakeLists.txt` contains an `install(TARGETS TsunamiGUI ...)` rule and no broader packaging automation was found. |
| Repository-layout documentation may be ahead of implementation | Confirmed | The layout notes describe `apps/gui`, `cmake/`, `src/io`, `src/models`, `src/visualization`, and `tests/CMakeLists.txt`; those paths are not implemented. |
