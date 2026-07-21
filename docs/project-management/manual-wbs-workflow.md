# Manual WBS Workflow

This repository uses a work-breakdown structure to keep planning, issues, and
implementation evidence aligned.

## WBS Hierarchy

| Level | Meaning | GitHub mapping |
| --- | --- | --- |
| Level 0 | Studentship | GitHub Project |
| Level 1 | Workstream | Workstream field |
| Level 2 | Domain or capability | Domain field and/or high-level issue |
| Level 3 | Sub-deliverable | Parent issue |
| Level 4 | Work package | Work-package issue |
| Level 5 | Executable task | Executable sub-issue |
| Level 6 | Checklist | Checklist inside an issue |

Parent issues normally define scope, sequencing, and review boundaries. They do
not normally require implementation branches. Independently implementable
Level-5 tasks normally receive a branch and pull request.

## When To Create An Issue

Create an issue when the item has a named output, requires review, blocks other
work, or needs evidence before it can close. Use a checklist item when the work
is a small verification step inside an existing executable task.

Level-2 and Level-3 issues can be used to collect decisions and group child
work. Level-4 issues define work packages. Level-5 issues should be small enough
to implement and review in one pull request.

## Branch Naming

Use lowercase branch names with the WBS ID and a short description:

```text
sw-bdp-bld-04-01-preset-reconciliation
```

For documentation-only tasks, use the same convention and keep the pull request
scope explicit.

## Pull-Request Conventions

Pull requests should state the WBS ID, list the issue being closed or advanced,
summarize changed files, and include validation evidence. Avoid mixing unrelated
work packages in one pull request.

Recommended pull-request sections:

```text
Summary
Scope
Validation
Open decisions
```

## Closure Evidence

Before closing an issue, attach or quote the relevant evidence:

- command outputs for build, test, lint, formatting, or documentation checks;
- screenshots only when the deliverable is visual;
- links to review notes or decisions;
- a list of files changed;
- an explanation for blocked validation.

An issue should not close on intent alone. It should close when the required
output exists and the acceptance criteria have evidence.

## Manual Project-Field Procedure

The project owner completes Project fields manually after each issue is created.
Do not automate Project-field updates from repository files.

Manual steps:

1. Add the issue to the Project.
2. Set `WBS ID`.
3. Set `WBS Level`.
4. Set `Workstream`.
5. Set `Domain`.
6. Set `Status`.
7. Set `Week`.
8. Set `Priority`.
9. Set `Effort`.
10. Set `Decision Gate`.
11. Set `Risk`.
12. Add parent or sub-issue relationships where applicable.
13. Confirm the issue appears in the relevant Project views.

## Research Source-of-Truth Rule

During active research, GitHub is the authoritative source for evolving
research content, evidence, decisions and proposed WBS changes.

Jira remains the authoritative project-control system for approved WBS
definitions and delivery status.

Changes discovered during research are recorded in the GitHub pending Jira
change manifest and are applied to Jira only after the associated research
deliverables have been completed and reviewed.

This distinction prevents GitHub research notes and Jira delivery definitions
from silently diverging.

## Deferred Jira Update Procedure

Use `docs/project-management/jira/pending_research_wbs_changes.csv` as a
deferred-change register, not as a Jira import file.

The final Jira update procedure is:

1. Conduct and complete the affected GitHub research deliverables.
2. Resolve citations, assumptions and open decisions.
3. Complete or approve the integrated specifications.
4. Review the pending Jira change manifest.
5. Mark accepted rows `APPROVED FOR JIRA SYNC`.
6. Generate a final Jira update file from approved rows only.
7. Apply the changes to Jira.
8. Record the Jira update date and issue links in GitHub.
9. Mark the corresponding rows `APPLIED TO JIRA`.

Do not generate a final Jira update or import file from rows that are still
`NOT READY`, `RESEARCH IN PROGRESS` or `READY FOR REVIEW`.
