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
