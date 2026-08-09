# SWE-DAT - Data Domain

The Data domain owns persistence-neutral configuration and data contracts used
by the solver, application service and adapters. It does not own numerical
methods, GUI presentation or concrete scientific file adapters.

## Deliverables

- [SWE-DAT-CFG - Case configuration](SWE-DAT-CFG/g1_case_schema_v1.0.0.md)
- [SWE-DAT-MAN - Dataset manifest](SWE-DAT-MAN/g1_dataset_manifest_v1.0.0.md)
- [SWE-DAT-SCH - Regional2D result layout](SWE-DAT-SCH/regional2d_result_layout_v0.1.md)
- [SWE-DAT-SCH - Regional2D HDF5 schema](SWE-DAT-SCH/regional2d_hdf5_schema_v1.0.0.md)
- [SWE-DAT-XDMF - Regional2D XDMF handoff](SWE-DAT-XDMF/regional2d_xdmf_handoff_v0.1.md)
- [Regional result-system workflow](result_system_workflow_v0.1.md)
- [Result storage PoC validation 2026-08-09](result_storage_poc_validation_2026-08-09.md)

## Baseline Documents

- [G1 case schema v1.0.0](SWE-DAT-CFG/g1_case_schema_v1.0.0.md)
- [Case schema verification v0.1](SWE-DAT-CFG/case_schema_verification_v0.1.md)
- [G1 dataset manifest v1.0.0](SWE-DAT-MAN/g1_dataset_manifest_v1.0.0.md)
- [Dataset manifest verification v0.1](SWE-DAT-MAN/dataset_manifest_verification_v0.1.md)
- [Regional2D result layout v0.1](SWE-DAT-SCH/regional2d_result_layout_v0.1.md)
- [Regional2D HDF5 result schema v1.0.0](SWE-DAT-SCH/regional2d_hdf5_schema_v1.0.0.md)
- [Regional2D XDMF handoff v0.1](SWE-DAT-XDMF/regional2d_xdmf_handoff_v0.1.md)
- [Regional result-system workflow v0.1](result_system_workflow_v0.1.md)
- [Result storage PoC validation 2026-08-09](result_storage_poc_validation_2026-08-09.md)

## Handoffs

- `SWE-DAT-CFG` owns `case.json`, schema compatibility, parsing,
  semantic validation and canonical serialisation.
- `SWE-DAT-MAN` owns `manifests/datasets.json`, provider and licence records,
  dataset provenance, generated lineage and case-binding verification.
- `SWE-DAT-SCH` and `SWE-DAT-CHK` remain responsible for scientific outputs
  and checkpoint schemas.
