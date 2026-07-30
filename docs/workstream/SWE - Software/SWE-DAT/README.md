# SWE-DAT - Data Domain

The Data domain owns persistence-neutral configuration and data contracts used
by the solver, application service and adapters. It does not own numerical
methods, GUI presentation or concrete scientific file adapters.

## Deliverables

- [SWE-DAT-CFG - Case configuration](SWE-DAT-CFG/g1_case_schema_v1.0.0.md)

## Baseline Documents

- [G1 case schema v1.0.0](SWE-DAT-CFG/g1_case_schema_v1.0.0.md)
- [Case schema verification v0.1](SWE-DAT-CFG/case_schema_verification_v0.1.md)

## Handoffs

- `SWE-DAT-CFG` owns `case.json`, schema compatibility, parsing,
  semantic validation and canonical serialisation.
- `SWE-DAT-MAN` remains responsible for dataset/provenance manifest content.
- `SWE-DAT-SCH` and `SWE-DAT-CHK` remain responsible for scientific outputs
  and checkpoint schemas.
