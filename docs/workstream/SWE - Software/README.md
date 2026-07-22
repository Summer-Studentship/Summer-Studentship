# SWE — Software Workstream

The [Software WBS v0.3](governance/studentship_software_wbs_v0.3.md) is the authoritative breakdown for the Software workstream. Its hierarchy is:

`Workstream → Domain → Deliverable → Work Package → Task`

All Domains and Deliverables are defined. G0 and G1 are currently decomposed to Work Package and Task level.

## Ownership boundary

Research remains authoritative for:

- governing equations;
- physics;
- numerical-method selection;
- source data;
- corridor geometry; and
- validation criteria.

Software implements accepted Research decisions. It does not replace or independently revise those decisions.

Existing pre-WBS code and documents may provide historical, prototype, partial or reusable evidence. Their presence does not automatically complete a Software WBS Task, Work Package or Deliverable. The [pre-WBS evidence matrix](governance/pre_wbs_wbs_evidence_matrix_v0.1.md) records the initial assessment.

## Operating model

- Tasks normally close through pull requests that merge their accepted evidence.
- Work Packages and Deliverables close only after manual acceptance against their stated conditions.
- Parent issues are not closed solely because child checklists or related Tasks are complete.

## Baseline documents

- [Authoritative Software WBS v0.3](governance/studentship_software_wbs_v0.3.md)
- [GitHub Project configuration v0.1](governance/github_project_configuration_v0.1.md)
- [Software stack decision register v0.1](SWE-ENV/SWE-ENV-STACK/studentship_software_stack_decision_register_v0.1.md)
- [Target catalogue v0.1](SWE-ARC/SWE-ARC-TGT/target_catalogue_v0.1.md)
- [Target dependency policy v0.1](../../../architecture/target_dependency_policy_v0.1.json)
- [Architectural layers v0.1](SWE-ARC/SWE-ARC-LAY/architectural_layers_v0.1.md)
- [Layer ownership policy v0.1](../../../architecture/layer_ownership_policy_v0.1.json)
- [Case lifecycle audit v0.1](SWE-ARC/SWE-ARC-CASE/case_lifecycle_audit_v0.1.md)
- [Case state machine v0.1](SWE-ARC/SWE-ARC-CASE/case_state_machine_v0.1.md)
- [Run state machine v0.1](SWE-ARC/SWE-ARC-CASE/run_state_machine_v0.1.md)
- [Canonical case directory v0.1](SWE-ARC/SWE-ARC-CASE/canonical_case_directory_v0.1.md)
- [Case artefact ownership matrix v0.1](SWE-ARC/SWE-ARC-CASE/case_artefact_ownership_matrix_v0.1.md)
- [Case lifecycle policy v0.1](../../../architecture/case_lifecycle_policy_v0.1.json)

## Architecture handoff notes

- `SWE-ARC-CASE-WP1` defines lifecycle transitions and deterministic artefact placement only.
- `SWE-DAT-CFG` owns the actual user configuration schema.
- `SWE-DAT-MAN` owns dataset and provenance manifest content.
- `SWE-DAT-SCH` and `SWE-DAT-CHK` own persistent scientific output and checkpoint schemas.
- `SWE-ARC-API-WP1` owns concrete C++ contracts.
- `SWE-ARC-SVC-WP1` owns orchestration semantics that execute the lifecycle transitions.
