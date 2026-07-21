# SW-BDP-BLD-04-01 - Reconcile cross-platform presets, scripts and build documentation

## WBS ID

`SW-BDP-BLD-04-01`

## Parent WBS ID

`SW-BDP-BLD-04`

## Purpose

Make the cross-platform build entry points consistent across shared CMake
presets, wrapper scripts, Docker, README links, and build documentation.

## Required Inputs

- Current `CMakePresets.json` and `CMakeUserPresets.json.example`.
- Existing Windows and Docker wrapper scripts.
- Existing build documentation under `docs/Markdowns/`.
- Current Dockerfile.
- Prior technical audit findings where the transferred files remain equivalent.

## Required Output

- Shared preset names are used consistently.
- Linux presets that claim vcpkg usage derive the vcpkg toolchain from
  `VCPKG_ROOT`.
- Docker selects a preset that activates the intended vcpkg toolchain.
- README documentation links resolve to existing files.
- Documentation accurately distinguishes configuration, compilation,
  dependency discovery, CTest invocation, registered test execution, GUI
  validation, Windows validation, and packaging validation.

## Exact In-Scope Files

- `CMakePresets.json`
- `CMakeUserPresets.json.example`
- `scripts/build_windows_qt.ps1`
- `scripts/build_docker.ps1`
- `Dockerfile`
- `README.md`
- `docs/Markdowns/build_windows_mingw.md`
- `docs/Markdowns/build_linux_docker.md`
- `docs/Markdowns/dependencies.md`
- `docs/Markdowns/workflow.md`

## Exclusions

- Root CMake cleanup.
- Component migration.
- Dependency-target integration.
- Test-framework integration.
- GUI source, layout, or behavior changes.
- Packaging implementation.
- CI or release workflow creation.
- GitHub Project automation.

## Acceptance Criteria

- [ ] `cmake --list-presets=all` lists every preset referenced by scripts,
      README, build documentation, and Dockerfile.
- [ ] No active command references remain for the deprecated Qt-kit preset
      naming scheme.
- [ ] Windows vcpkg presets derive the toolchain from `VCPKG_ROOT`.
- [ ] Linux vcpkg presets derive the toolchain from `VCPKG_ROOT`.
- [ ] Docker uses `linux-vcpkg-headless` and that preset activates the vcpkg
      toolchain through `VCPKG_ROOT`.
- [ ] README links resolve to existing documentation files.
- [ ] Documentation does not claim non-empty test validation while no tests are
      registered.
- [ ] No solver, GUI, test, packaging, or CI implementation is changed.

## Validation Evidence

- `git status --short`
- `git diff --check`
- `git diff --stat`
- `cmake --list-presets=all`
- stale preset-name search
- vcpkg checkout discovery command
- Linux configure/build attempt when a usable vcpkg checkout exists
- Docker build attempt when Docker is usable
- repository-facing terminology search

## Implementation Checklist

- [ ] Verify repository identity and current state.
- [ ] Verify prior audit applicability against equivalent transferred files.
- [ ] Reconcile shared preset names.
- [ ] Update Windows wrapper script.
- [ ] Update Docker wrapper script if needed.
- [ ] Apply `VCPKG_ROOT`-derived toolchain convention to Linux vcpkg presets.
- [ ] Correct README links.
- [ ] Update Windows build documentation.
- [ ] Update Linux and Docker build documentation.
- [ ] Update dependency strategy documentation.
- [ ] Update workflow documentation.
- [ ] Run validation commands and record blocked validation accurately.

## Unresolved Decisions

- Whether future GUI builds should standardize on Qt installer, vcpkg Qt, system
  Qt, or a documented hybrid route.
- Which test framework should be used when testing integration begins.
- Whether visualization remains based on the current external mathplot route.
- Which packaging format should be implemented after install rules are reviewed.
