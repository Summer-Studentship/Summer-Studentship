# Interface Catalogue v0.1

- **Work Package:** `SWE-ARC-API-WP1`
- **Policy:** `architecture/interface_contract_policy_v0.1.json`

| Contract family | Namespace | Target | Consumers | Inputs | Outputs | Ownership and lifetime | Failure model | Downstream owner |
|---|---|---|---|---|---|---|---|---|
| Identity, version, error, result | `tsunami::core` | `tsunami_core` | all runtime targets | owned text, version numbers, error records | strong IDs, `SemanticVersion`, `Error`, `Result<T>` | values own data | `Result` for expected failures | `SWE-ARC-ERR-WP1` |
| Cancellation and observer | `tsunami::core` | `tsunami_core` | solvers, coupling, application, tests | borrowed token, observation records | `stop_requested()`, neutral observations | borrowed or shared immutable | no automatic logging | `SWE-ARC-SVC-WP1`, `SWE-ARC-ERR-WP1` |
| Mesh and field views | `tsunami::fvm` | `tsunami_fvm` | solvers, adapters, tests | mesh/field owner lifetime | summaries/descriptors | read-only borrowed views | no storage mutation | `SWE-FVM-MSH-WP1`, `SWE-FVM-FLD-WP1` |
| Data and I/O ports | `tsunami::data` | `tsunami_data` | solvers, coupling, adapters, tests | case/run/artefact refs, read/write requests | metadata, transactions | values own refs; transactions transfer `unique_ptr` | `Result`, abort on failed write | `SWE-DAT-*` |
| Regional solver | `tsunami::r2d` | `tsunami_r2d` | application, coupling, tests | case revision, preparation, run, artefacts, cancellation, observer | run summary and artefact refs | borrowed call inputs | `Result<RegionalRunSummary>` | `SWE-R2D-*` |
| Local solver | `tsunami::l3d` | `tsunami_l3d` | application, coupling, tests | case revision, preparation, run, artefacts, cancellation, observer | run summary and artefact refs | borrowed call inputs | `Result<LocalRunSummary>` | `SWE-L3D-*` |
| Replay and coupling | `tsunami::coupling` | `tsunami_coupling` | application, tests | regional output, local preparation, mapping/replay requests | replay artefacts, validation result | borrowed call inputs | `Result` | `SWE-CPL-*` |

Public headers prohibit Qt, CLI11, Catch2, HDF5, GDAL, Gmsh, Matplot++ and Eigen public interchange types in inward contracts.
