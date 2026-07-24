# Application Service Contract v0.1

- **Work Package:** `SWE-ARC-SVC-WP1`

## Boundary

`tsunami::application::IApplicationService` is the shared frontend-neutral boundary. It exposes synchronous control-plane operations:

- `describe`
- `validate_case`
- `prepare_run`
- `launch_run`
- `request_cancellation`
- `discover_results`

Long-running numerical execution is not implemented here. Future implementations may submit work, report progress through `tsunami::core::IObserver` and honour `tsunami::core::ICancellationToken`.

## Requests and Responses

Requests use accepted core/data contracts: case IDs, case revision references, preparation IDs, run IDs, checkpoint IDs and artefact references. Responses use `tsunami::core::Result<T>` for expected failures.

The service does not expose Qt futures, threads, signals, CLI11 types, solver instances, HDF5 handles or concrete adapters.
