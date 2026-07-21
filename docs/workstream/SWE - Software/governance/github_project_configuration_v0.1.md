# GitHub Software WBS Project Configuration

**Version:** 0.1  
**Repository:** `Summer-Studentship/Summer-Studentship`  
**Source WBS:** `studentship_software_wbs_v0.3.md`

## 1. Project

Create one organisation-level Project:

`Studentship — Software WBS`

## 2. Issue types

- Workstream
- Domain
- Deliverable
- Work Package
- Task
- Bug
- Decision

## 3. Organisation issue fields

- `WBS ID` — text
- `Scope Class` — single select: Active baseline; Continuous baseline; Stretch; Deferred
- `Priority` — single select: Critical; High; Medium; Low
- `Risk` — single select: High; Medium; Low
- `Research Inputs` — text
- `Target Gate` — single select: G0; G1; G2; G3; G4; G5; Continuous; Post-G5
- `Effort` — number or single select
- `Definition Status` — single select: Proposed; Defined; Accepted
- `Verification Required` — single select: Yes; No

## 4. Project-local fields

- `Status` — Backlog; Ready; In Progress; In Review; Verified; Done
- `Iteration`
- `Target start`
- `Target completion`
- `Delivery confidence` — High; Medium; Low

## 5. Views

1. `WBS Structure` — table grouped by Parent issue.
2. `Active Implementation` — board grouped by Status.
3. `Research–Software Handoffs` — table filtered where Research Inputs is non-empty.
4. `Roadmap` — timeline grouped by Domain or Target Gate.
5. `Deferred and Stretch` — Scope Class is Stretch or Deferred.
6. `Blocked Work` — issues with unresolved dependencies.
7. `G0/G1 Execution` — Target Gate is G0 or G1.

## 6. Automation

- New WBS issue -> add to project and set Status to Backlog.
- Task/Work Package with all blockers closed -> Status to Ready.
- Pull request linked -> Status to In Progress or In Review.
- Issue closed -> Status to Done.
- Reopened issue -> Status to In Progress.
- Do not auto-close parent issues solely from checklist progress; verify parent acceptance first.

## 7. Hierarchy

Use native Parent issue/Sub-issue relationships:

`SWE -> Domain -> Deliverable -> Work Package -> Task`

Use native Blocked by/Blocking relationships from the manifest dependencies.

## 8. Milestones / gates

- G0 — Repository and Build Baseline
- G1 — Data and Mesh Vertical Slice
- G2 — Verified Regional 2D Baseline
- G3 — Verified Local 3D Baseline
- G4 — Regional-to-Local Replay
- G5 — Barrier Impact Comparison Baseline

## 9. Import rule

The CSV/JSON manifest is authoritative for issue identity and metadata. Do not generate duplicate issues when a WBS ID already exists.
