# Application Service Audit v0.1

- **Work Package:** `SWE-ARC-SVC-WP1`
- **Issues:** `#138`, `#139`, `#140`, `#141`
- **Policy:** `architecture/application_service_policy_v0.1.json`
- **Document date:** 2026-07-24

## Current State

The CLI owned command parsing only and linked directly to `tsunami_core`. The Qt shell owned startup and QML display only and also linked directly to `tsunami_core`. Neither frontend had a shared application-service boundary.

The accepted API contracts provide reusable identity, `Result`, `Error`, cancellation, observer, case revision, preparation, run and artefact reference types. This work reuses those types and does not duplicate them.

## Service Gap

Before this WP there was no target that represented case validation, preparation, launch, cancellation or result discovery as frontend-neutral control-plane operations. CLI and GUI therefore had no common boundary to call.

## Decision

Create `tsunami_application` as the application-service owner. CLI and GUI now consume the same no-solver service factory and service-description contract. Transitional direct frontend-to-core links are removed.

Downstream diagnostics, final GUI workflow, case parsing, solver composition and persistence remain with later WBS owners.
