# Target Graph Validation v0.1

- **Work Package:** `SWE-ARC-TGT-WP1`
- **Issues:** `#122`, `#125`
- **Policy:** `architecture/target_dependency_policy_v0.1.json`
- **Policy version:** `0.1`
- **Document date:** 2026-07-22

## Tool Versions

| Tool | Version |
|---|---|
| Python | `3.14.5` |
| CMake | `4.3.3` |
| Ninja | `1.13.2` |
| GCC | `16.1.1 20260430` |
| vcpkg | `2026-05-27-d5b6777d666efc1a7f491babfcdab37794c1ae3e` |
| Qt | `6.11.1` from `/usr` |

## Validator Commands

```sh
python3 -m json.tool architecture/target_dependency_policy_v0.1.json
python3 -m py_compile tools/architecture/validate_target_graph.py
python3 tools/architecture/validate_target_graph.py \
  --policy architecture/target_dependency_policy_v0.1.json
```

Result:

```text
policy: 0.1
targets: 16 (4 active, 12 planned)
policy edges: 58
policy graph: acyclic
policy rules: passed
```

## Active Graph Validation

The accepted shared presets were used with:

```text
VCPKG_ROOT=/tmp/tsunami-g0-vcpkg
QT_ROOT=/usr
```

### `linux-gcc-dev`

Commands:

```sh
env VCPKG_ROOT=/tmp/tsunami-g0-vcpkg QT_ROOT=/usr cmake --preset linux-gcc-dev
cmake --build --preset linux-gcc-dev-build
cmake --graphviz=build/linux-gcc-dev/target-graph.dot build/linux-gcc-dev
python3 tools/architecture/validate_target_graph.py \
  --policy architecture/target_dependency_policy_v0.1.json \
  --cmake-graph build/linux-gcc-dev/target-graph.dot
```

Observed active project targets:

```text
tsunami_cli, tsunami_core
```

Observed direct project edge:

```text
tsunami_cli -> tsunami_core
```

Imported ownership:

```text
tsunami_cli -> CLI11::CLI11
```

Result: passed. `tsunami_gui` and `tsunami_tests` were absent because this
configuration disables GUI and tests.

### `linux-gcc-test`

Commands:

```sh
env VCPKG_ROOT=/tmp/tsunami-g0-vcpkg QT_ROOT=/usr cmake --preset linux-gcc-test
cmake --build --preset linux-gcc-test-build
cmake --graphviz=build/linux-gcc-test/target-graph.dot build/linux-gcc-test
python3 tools/architecture/validate_target_graph.py \
  --policy architecture/target_dependency_policy_v0.1.json \
  --cmake-graph build/linux-gcc-test/target-graph.dot
```

Observed active project targets:

```text
tsunami_cli, tsunami_core, tsunami_tests
```

Observed direct project edges:

```text
tsunami_cli -> tsunami_core
tsunami_tests -> tsunami_core
```

Imported ownership:

```text
tsunami_cli -> CLI11::CLI11
tsunami_tests -> Catch2::Catch2WithMain
```

Result: passed. The validator ignores CMake's generated self-edge for
`tsunami_tests` and still checks project-target cycles. No production target
depends on `tsunami_tests`.

### `linux-gcc-gui-dev`

Commands:

```sh
env VCPKG_ROOT=/tmp/tsunami-g0-vcpkg QT_ROOT=/usr cmake --preset linux-gcc-gui-dev
cmake --build --preset linux-gcc-gui-dev-build
cmake --graphviz=build/linux-gcc-gui-dev/target-graph.dot build/linux-gcc-gui-dev
python3 tools/architecture/validate_target_graph.py \
  --policy architecture/target_dependency_policy_v0.1.json \
  --cmake-graph build/linux-gcc-gui-dev/target-graph.dot
```

Observed active project targets:

```text
tsunami_cli, tsunami_core, tsunami_gui
```

Observed direct project edges:

```text
tsunami_cli -> tsunami_core
tsunami_gui -> tsunami_core
```

Imported ownership:

```text
tsunami_cli -> CLI11::CLI11
tsunami_gui -> Qt6::Core, Qt6::Qml, Qt6::Quick and Qt-generated tools
```

Result: passed. Qt reported missing optional Vulkan headers during configure,
but generation and build succeeded. The validator ignores Qt's generated
`tsunami_gui_tooling` helper target because it is not a project architecture
target.

## Review Results

| Check | Result |
|---|---|
| Policy graph acyclic | Passed |
| Active CMake project graph acyclic | Passed |
| Core has no Qt, CLI11 or Catch2 edge | Passed |
| No UI-to-core ownership inversion | Passed |
| No adapter-to-domain or domain-to-adapter inversion in active graph | Passed |
| No production target depends on tests | Passed |
| Third-party framework ownership explicit | Passed |
| Planned targets distinguished from active targets | Passed |
| Public/private boundary review recorded | Passed |

## Invalid Fixture Check

A temporary policy fixture in `/tmp` deliberately added the prohibited edge
`tsunami_core -> tsunami_gui`. The validator returned non-zero and reported the
prohibited-edge failure:

```text
ERROR: tsunami_core: dependency is both allowed and prohibited: ['tsunami_gui']
```

The fixture was not committed.

## Limitations and Handoff

- Planned G1/later targets are policy entries only; no implementation
  directories or placeholder libraries were created.
- Detailed layer placement remains with `SWE-ARC-LAY-WP1`.
- Detailed C++ interface contracts remain with `SWE-ARC-API-WP1`.
- Windows MSVC graph validation remains a Windows-host activity; this work
  validates the active Linux GCC developer, test and GUI graphs requested by
  the work package.
