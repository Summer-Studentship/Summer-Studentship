# GitHub Project Setup

This guide describes the recommended manual Project setup for the
`Summer-Studentship` repository. Create and maintain Project fields manually.

## Recommended Fields

| Field | Purpose | Recommended type | Example options | Completed by |
| --- | --- | --- | --- | --- |
| WBS ID | Stores the canonical work-breakdown identifier | Text | `SW-BDP-BLD-04-01` | Project owner after issue creation |
| WBS Level | Shows planning depth | Single select | `Level 1`, `Level 2`, `Level 3`, `Level 4`, `Level 5`, `Level 6` | Project owner after issue creation |
| Workstream | Groups work by major stream | Single select | `Software Platform`, `Research`, `Documentation` | Project owner after issue creation |
| Domain | Groups work within a workstream | Single select or text | `Build, Dependencies and Packaging` | Project owner after issue creation |
| Status | Tracks delivery state | Single select | `Backlog`, `Ready`, `In progress`, `Blocked`, `Review`, `Done` | Project owner during planning and review |
| Week | Places work into a delivery window | Single select or iteration | `Week 1`, `Week 2`, `Week 3` | Project owner during planning |
| Priority | Orders work within the backlog | Single select | `High`, `Medium`, `Low` | Project owner during planning |
| Effort | Estimates implementation size | Single select | `Small`, `Medium`, `Large` | Project owner during planning |
| Decision Gate | Marks whether owner approval is required | Single select | `Not required`, `Required`, `Approved`, `Deferred` | Project owner when decisions are known |
| Risk | Flags delivery or technical risk | Single select | `Low`, `Medium`, `High` | Project owner during planning and review |

## Useful Views

| View | Purpose | Suggested grouping or filter |
| --- | --- | --- |
| Workstream overview | Show all work grouped by major stream | Group by `Workstream` |
| Current week | Focus on the active delivery window | Filter by current `Week` |
| Build workstream | Track Build, Dependencies and Packaging work | Filter by `Domain` |
| Blocked work | Highlight items needing decisions or external input | Filter `Status` is `Blocked` |
| Decision gates | Track work requiring owner approval | Filter `Decision Gate` is `Required` |
| Completed work | Review closed or delivered items | Filter `Status` is `Done` |

## Manual Setup Checklist

1. Create the Project.
2. Add the recommended fields.
3. Create the useful views.
4. Add repository issues manually.
5. Complete Project fields for each issue.
6. Add parent and sub-issue relationships where applicable.
7. Review views at the end of each work session.
