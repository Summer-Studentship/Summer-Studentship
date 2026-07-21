# Summer Studentship

Research and prototype software for tsunami-barrier simulation, data handling
and visualisation. The repository has transitioned from exploratory work to
formal, Software WBS-governed development and is entering the G0 governance and
environment baseline.

## Current status

- The Research workstream contains LaTeX sources, reports, literature and
  architecture-relevant modelling decisions.
- The Qt GUI and C++ recreation/reference implementations are prototypes in
  progress; their presence does not mean the related Software WBS Deliverables
  are accepted or complete.
- The active simulation target couples a regional two-dimensional nonlinear
  shallow-water-equation (2D NLSWE) model with a local three-dimensional
  URANS–VOF model for barrier interaction.
- Machine-learning and generative-optimisation implementation remains deferred
  until the required simulation, verification and data foundations exist.

## Start Here

- [Dependency strategy](docs/Markdowns/dependencies.md)
- [Repository layout](docs/Markdowns/repository_layout.md)
- [Windows MinGW build notes](docs/Markdowns/build_windows_mingw.md)
- [Linux/Docker build notes](docs/Markdowns/build_linux_docker.md)
- [Final development workflow](docs/Markdowns/workflow.md)

The current C++ scaffold is CMake-based and uses C++20. Third-party C++ packages
are declared in `vcpkg.json`; Qt can be provided either by the Qt installer or
by the optional vcpkg `gui` feature. These files record the pre-WBS prototype
baseline and will be assessed through the formal Software Work Packages before
being treated as accepted project infrastructure.
