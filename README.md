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

- [Dependency acquisition](docs/Markdowns/dependencies.md)
- [Repository layout](docs/Markdowns/repository_layout.md)
- [Windows MinGW build notes](docs/Markdowns/build_windows_mingw.md)
- [Linux/Docker build notes](docs/Markdowns/build_linux_docker.md)
- [Final development workflow](docs/Markdowns/workflow.md)
- [Regional2D state, flux and residual method](docs/workstream/SWE%20-%20Software/SWE-R2D/regional_state_flux_residual_v0.1.md)
- [Regional2D well-balancing and wet-dry method](docs/workstream/SWE%20-%20Software/SWE-R2D/regional_well_balancing_and_wet_dry_v0.1.md)
- [Regional2D radiation and relaxation boundaries](docs/workstream/SWE%20-%20Software/SWE-R2D/regional_radiation_and_relaxation_boundaries_v0.1.md)
- [Regional2D boundary reflection and absorption verification](docs/workstream/SWE%20-%20Software/SWE-R2D/boundary_reflection_absorption_verification_v0.1.md)
- [Regional2D Manning and Coriolis sources](docs/workstream/SWE%20-%20Software/SWE-R2D/regional_manning_coriolis_sources_v0.1.md)
- [Regional2D source-term verification](docs/workstream/SWE%20-%20Software/SWE-R2D/source_term_verification_v0.1.md)

The current C++ scaffold is CMake-based and uses C++20. Third-party C++ packages
are declared in `vcpkg.json` and resolved from the exact registry commit in
`vcpkg-configuration.json`. Qt is acquired only through the Qt
installer/Maintenance Tool, not vcpkg. Dependency acquisition and licensing are
accepted G0 baselines; target-based CMake integration, shared presets and the
clean-clone smoke test remain owned by their later Software Work Packages.
