# GitHub Project Update

## Discovery Result

- Repository: `Summer-Studentship/Summer-Studentship`
- Classic repository Projects endpoint: `404 Not Found`.
- Local `gh auth status`: token for `Helios-MEOW` has `repo` scope and was sufficient for issue, milestone and PR mutation through the GitHub CLI.
- Current GitHub connector exposes issues, branches, commits and PRs, but returned `403 Resource not accessible by integration` for issue and PR writes in this session.
- `gh project` is installed, but the current token lacks the required Project scope. `gh project list --owner Summer-Studentship --format json` returned a missing-scope error for `read:project`.
- Repository convention in `docs/project-management/manual-wbs-workflow.md` says Project fields are completed manually.

Project owner, Project number and Project URL were not discoverable through the available authenticated tools. Do not claim a saved view was created from this prompt.

## Fields To Create Or Reuse

- `Implementation Route`: Native, OpenFOAM adopted, OpenFOAM adapted, External library, Deferred
- `Evidence State`: No evidence, Implementation linked, Acceptance passed, Validation passed
- `WBS Disposition`: Complete, Partial, Superseded, Deferred, Not started
- `Gate Relevance`: G6 required, Post-G6, Excluded
- `Current Gate`: text or single select; use `G6 — Theoretical Hybrid Model Complete` for G6-required items

## Saved View Manual Instructions

Create or update a Project view named `G6 — Theoretical Model Gate` with:

- Filter: `Gate Relevance = G6 required`
- Group: parent WBS or workstream field
- Sort: `WBS Disposition`, then `WBS ID`
- Visible fields: Status, WBS ID, WBS Disposition, Implementation Route, Evidence State, Gate Relevance

## Item Field Values

| Issue | WBS ID | Implementation Route | Evidence State | WBS Disposition | Gate Relevance | Current Gate |
| --- | --- | --- | --- | --- | --- | --- |
| #1 | `SWE` | Deferred | Implementation linked | Partial | Excluded |  |
| #2 | `SWE-ENV` | Deferred | Implementation linked | Partial | Excluded |  |
| #3 | `SWE-ARC` | Deferred | Implementation linked | Partial | Excluded |  |
| #4 | `SWE-FVM` | Deferred | Implementation linked | Partial | Excluded |  |
| #5 | `SWE-DAT` | External library | Implementation linked | Partial | Excluded |  |
| #6 | `SWE-GEO` | External library | Implementation linked | Partial | G6 required | G6 — Theoretical Hybrid Model Complete |
| #7 | `SWE-R2D` | Native | Acceptance passed | Complete | G6 required | G6 — Theoretical Hybrid Model Complete |
| #8 | `SWE-L3D` | OpenFOAM adopted | Implementation linked | Partial | G6 required | G6 — Theoretical Hybrid Model Complete |
| #9 | `SWE-CPL` | Native | Implementation linked | Partial | G6 required | G6 — Theoretical Hybrid Model Complete |
| #10 | `SWE-GUI` | Deferred | No evidence | Deferred | Excluded |  |
| #11 | `SWE-VER` | Deferred | Implementation linked | Partial | Excluded |  |
| #12 | `SWE-HPC` | Deferred | No evidence | Deferred | Post-G6 |  |
| #13 | `SWE-STR` | Deferred | No evidence | Deferred | Post-G6 |  |
| #14 | `SWE-REL` | Deferred | Implementation linked | Partial | Excluded |  |
| #15 | `SWE-ENV-STACK` | Native | Acceptance passed | Complete | Excluded |  |
| #16 | `SWE-ENV-BLD` | Native | Acceptance passed | Complete | Excluded |  |
| #17 | `SWE-ENV-DEP` | Native | Acceptance passed | Complete | Excluded |  |
| #18 | `SWE-ENV-PRS` | Native | Acceptance passed | Complete | Excluded |  |
| #19 | `SWE-ENV-LIC` | Native | Acceptance passed | Complete | Excluded |  |
| #20 | `SWE-ENV-SMK` | Deferred | Implementation linked | Partial | Excluded |  |
| #21 | `SWE-ARC-TGT` | Native | Acceptance passed | Complete | Excluded |  |
| #22 | `SWE-ARC-LAY` | Native | Acceptance passed | Complete | Excluded |  |
| #23 | `SWE-ARC-CASE` | Native | Acceptance passed | Complete | Excluded |  |
| #24 | `SWE-ARC-API` | Native | Acceptance passed | Complete | Excluded |  |
| #25 | `SWE-ARC-SVC` | Native | Acceptance passed | Complete | Excluded |  |
| #26 | `SWE-ARC-ERR` | Native | Acceptance passed | Complete | Excluded |  |
| #27 | `SWE-FVM-MSH` | Deferred | Implementation linked | Partial | Excluded |  |
| #28 | `SWE-FVM-FLD` | Deferred | Implementation linked | Partial | Excluded |  |
| #29 | `SWE-FVM-BC` | Deferred | Implementation linked | Partial | Excluded |  |
| #30 | `SWE-FVM-NUM` | Deferred | Implementation linked | Partial | Excluded |  |
| #31 | `SWE-FVM-LIN` | Deferred | Implementation linked | Partial | Excluded |  |
| #32 | `SWE-FVM-CTL` | Deferred | Implementation linked | Partial | Excluded |  |
| #33 | `SWE-DAT-CFG` | External library | Acceptance passed | Complete | G6 required | G6 — Theoretical Hybrid Model Complete |
| #34 | `SWE-DAT-MAN` | External library | Acceptance passed | Complete | G6 required | G6 — Theoretical Hybrid Model Complete |
| #35 | `SWE-DAT-ING` | External library | Implementation linked | Partial | G6 required | G6 — Theoretical Hybrid Model Complete |
| #36 | `SWE-DAT-SCH` | Deferred | No evidence | Not started | Post-G6 |  |
| #37 | `SWE-DAT-XDMF` | Deferred | No evidence | Not started | Post-G6 |  |
| #38 | `SWE-DAT-CHK` | External library | Implementation linked | Partial | Post-G6 |  |
| #39 | `SWE-GEO-IMP` | External library | Acceptance passed | Complete | G6 required | G6 — Theoretical Hybrid Model Complete |
| #40 | `SWE-GEO-CRS` | External library | Acceptance passed | Complete | G6 required | G6 — Theoretical Hybrid Model Complete |
| #41 | `SWE-GEO-COR` | External library | Acceptance passed | Complete | G6 required | G6 — Theoretical Hybrid Model Complete |
| #42 | `SWE-GEO-TER` | External library | Acceptance passed | Complete | G6 required | G6 — Theoretical Hybrid Model Complete |
| #43 | `SWE-GEO-MSH` | External library | Implementation linked | Partial | G6 required | G6 — Theoretical Hybrid Model Complete |
| #44 | `SWE-GEO-TAG` | External library | Implementation linked | Partial | G6 required | G6 — Theoretical Hybrid Model Complete |
| #45 | `SWE-GEO-BAR` | External library | Implementation linked | Partial | G6 required | G6 — Theoretical Hybrid Model Complete |
| #46 | `SWE-GEO-CHK` | External library | Implementation linked | Partial | G6 required | G6 — Theoretical Hybrid Model Complete |
| #47 | `SWE-R2D-STA` | Native | Acceptance passed | Complete | G6 required | G6 — Theoretical Hybrid Model Complete |
| #48 | `SWE-R2D-FLX` | Native | Acceptance passed | Complete | G6 required | G6 — Theoretical Hybrid Model Complete |
| #49 | `SWE-R2D-WB` | Native | Acceptance passed | Complete | G6 required | G6 — Theoretical Hybrid Model Complete |
| #50 | `SWE-R2D-WD` | Native | Acceptance passed | Complete | G6 required | G6 — Theoretical Hybrid Model Complete |
| #51 | `SWE-R2D-SRC` | Native | Acceptance passed | Complete | G6 required | G6 — Theoretical Hybrid Model Complete |
| #52 | `SWE-R2D-BC` | Native | Acceptance passed | Complete | G6 required | G6 — Theoretical Hybrid Model Complete |
| #53 | `SWE-R2D-TIM` | Native | Acceptance passed | Complete | G6 required | G6 — Theoretical Hybrid Model Complete |
| #54 | `SWE-R2D-EQK` | Native | Acceptance passed | Complete | G6 required | G6 — Theoretical Hybrid Model Complete |
| #55 | `SWE-R2D-SOL` | Native | Acceptance passed | Complete | G6 required | G6 — Theoretical Hybrid Model Complete |
| #56 | `SWE-L3D-VOF` | OpenFOAM adopted | Acceptance passed | Complete | G6 required | G6 — Theoretical Hybrid Model Complete |
| #57 | `SWE-L3D-MOM` | OpenFOAM adopted | Acceptance passed | Complete | G6 required | G6 — Theoretical Hybrid Model Complete |
| #58 | `SWE-L3D-PRS` | OpenFOAM adopted | Acceptance passed | Complete | G6 required | G6 — Theoretical Hybrid Model Complete |
| #59 | `SWE-L3D-SST` | OpenFOAM adopted | Acceptance passed | Complete | G6 required | G6 — Theoretical Hybrid Model Complete |
| #60 | `SWE-L3D-WLF` | OpenFOAM adopted | Implementation linked | Partial | G6 required | G6 — Theoretical Hybrid Model Complete |
| #61 | `SWE-L3D-BC` | OpenFOAM adopted | Implementation linked | Partial | G6 required | G6 — Theoretical Hybrid Model Complete |
| #62 | `SWE-L3D-TIM` | OpenFOAM adopted | Implementation linked | Partial | G6 required | G6 — Theoretical Hybrid Model Complete |
| #63 | `SWE-L3D-SOL` | OpenFOAM adopted | Acceptance passed | Complete | G6 required | G6 — Theoretical Hybrid Model Complete |
| #64 | `SWE-L3D-FRC` | OpenFOAM adopted | Implementation linked | Partial | G6 required | G6 — Theoretical Hybrid Model Complete |
| #65 | `SWE-CPL-IFC` | Native | Acceptance passed | Complete | G6 required | G6 — Theoretical Hybrid Model Complete |
| #66 | `SWE-CPL-RPL` | Native | Acceptance passed | Complete | G6 required | G6 — Theoretical Hybrid Model Complete |
| #67 | `SWE-CPL-MAP` | Native | Acceptance passed | Complete | G6 required | G6 — Theoretical Hybrid Model Complete |
| #68 | `SWE-CPL-BC` | Native | Acceptance passed | Complete | G6 required | G6 — Theoretical Hybrid Model Complete |
| #69 | `SWE-CPL-MET` | Native | Implementation linked | Partial | Post-G6 |  |
| #70 | `SWE-CPL-CMP` | Native | Implementation linked | Partial | Post-G6 |  |
| #71 | `SWE-GUI-SHL` | Native | Acceptance passed | Complete | Excluded |  |
| #72 | `SWE-GUI-CAS` | Deferred | No evidence | Deferred | Excluded |  |
| #73 | `SWE-GUI-RUN` | Deferred | No evidence | Deferred | Excluded |  |
| #74 | `SWE-GUI-LOG` | Deferred | No evidence | Deferred | Excluded |  |
| #75 | `SWE-GUI-VIS` | Deferred | No evidence | Deferred | Excluded |  |
| #76 | `SWE-GUI-POST` | Deferred | No evidence | Deferred | Excluded |  |
| #77 | `SWE-VER-UNIT` | Deferred | Implementation linked | Partial | G6 required | G6 — Theoretical Hybrid Model Complete |
| #78 | `SWE-VER-ACC` | Deferred | Implementation linked | Partial | G6 required | G6 — Theoretical Hybrid Model Complete |
| #79 | `SWE-VER-BMK` | Deferred | Implementation linked | Partial | Excluded |  |
| #80 | `SWE-VER-REG` | Deferred | Implementation linked | Partial | G6 required | G6 — Theoretical Hybrid Model Complete |
| #81 | `SWE-VER-CONV` | Deferred | No evidence | Not started | Post-G6 |  |
| #82 | `SWE-VER-VAL` | Deferred | No evidence | Not started | Post-G6 |  |
| #83 | `SWE-HPC-PROF` | Deferred | No evidence | Deferred | Post-G6 |  |
| #84 | `SWE-HPC-SIMD` | Deferred | No evidence | Deferred | Post-G6 |  |
| #85 | `SWE-HPC-OMP` | Deferred | No evidence | Deferred | Post-G6 |  |
| #86 | `SWE-HPC-MPI` | Deferred | No evidence | Deferred | Post-G6 |  |
| #87 | `SWE-HPC-GPU` | Deferred | No evidence | Deferred | Post-G6 |  |
| #88 | `SWE-HPC-AIO` | Deferred | No evidence | Deferred | Post-G6 |  |
| #89 | `SWE-STR-LOD` | Deferred | No evidence | Deferred | Post-G6 |  |
| #90 | `SWE-STR-MAT` | Deferred | No evidence | Deferred | Post-G6 |  |
| #91 | `SWE-STR-FEM` | Deferred | No evidence | Deferred | Post-G6 |  |
| #92 | `SWE-STR-DMG` | Deferred | No evidence | Deferred | Post-G6 |  |
| #93 | `SWE-STR-FSI` | Deferred | No evidence | Deferred | Post-G6 |  |
| #94 | `SWE-REL-CI` | Deferred | Implementation linked | Partial | Excluded |  |
| #95 | `SWE-REL-PKG` | Deferred | Implementation linked | Partial | Excluded |  |
| #96 | `SWE-REL-DOC` | Deferred | Implementation linked | Partial | Excluded |  |
| #97 | `SWE-REL-REP` | Deferred | Implementation linked | Partial | Excluded |  |
| #98 | `SWE-ENV-STACK-WP1` | Native | Acceptance passed | Complete | Excluded |  |
| #99 | `SWE-ENV-STACK-WP1-T1` | Native | Acceptance passed | Complete | Excluded |  |
| #100 | `SWE-ENV-STACK-WP1-T2` | Native | Acceptance passed | Complete | Excluded |  |
| #101 | `SWE-ENV-STACK-WP1-T3` | Native | Acceptance passed | Complete | Excluded |  |
| #102 | `SWE-ENV-BLD-WP1` | Native | Acceptance passed | Complete | Excluded |  |
| #103 | `SWE-ENV-BLD-WP1-T1` | Native | Acceptance passed | Complete | Excluded |  |
| #104 | `SWE-ENV-BLD-WP1-T2` | Native | Acceptance passed | Complete | Excluded |  |
| #105 | `SWE-ENV-BLD-WP1-T3` | Native | Acceptance passed | Complete | Excluded |  |
| #106 | `SWE-ENV-DEP-WP1` | Native | Acceptance passed | Complete | Excluded |  |
| #107 | `SWE-ENV-DEP-WP1-T1` | Native | Acceptance passed | Complete | Excluded |  |
| #108 | `SWE-ENV-DEP-WP1-T2` | Native | Acceptance passed | Complete | Excluded |  |
| #109 | `SWE-ENV-DEP-WP1-T3` | Native | Acceptance passed | Complete | Excluded |  |
| #110 | `SWE-ENV-PRS-WP1` | Native | Acceptance passed | Complete | Excluded |  |
| #111 | `SWE-ENV-PRS-WP1-T1` | Native | Acceptance passed | Complete | Excluded |  |
| #112 | `SWE-ENV-PRS-WP1-T2` | Native | Acceptance passed | Complete | Excluded |  |
| #113 | `SWE-ENV-PRS-WP1-T3` | Native | Acceptance passed | Complete | Excluded |  |
| #114 | `SWE-ENV-LIC-WP1` | Native | Acceptance passed | Complete | Excluded |  |
| #115 | `SWE-ENV-LIC-WP1-T1` | Native | Acceptance passed | Complete | Excluded |  |
| #116 | `SWE-ENV-LIC-WP1-T2` | Native | Acceptance passed | Complete | Excluded |  |
| #117 | `SWE-ENV-LIC-WP1-T3` | Native | Acceptance passed | Complete | Excluded |  |
| #118 | `SWE-ENV-SMK-WP1` | Deferred | Implementation linked | Partial | Excluded |  |
| #119 | `SWE-ENV-SMK-WP1-T1` | Deferred | Implementation linked | Partial | Excluded |  |
| #120 | `SWE-ENV-SMK-WP1-T2` | Deferred | Implementation linked | Partial | Excluded |  |
| #121 | `SWE-ENV-SMK-WP1-T3` | Deferred | Implementation linked | Partial | Excluded |  |
| #122 | `SWE-ARC-TGT-WP1` | Native | Acceptance passed | Complete | Excluded |  |
| #123 | `SWE-ARC-TGT-WP1-T1` | Native | Acceptance passed | Complete | Excluded |  |
| #124 | `SWE-ARC-TGT-WP1-T2` | Native | Acceptance passed | Complete | Excluded |  |
| #125 | `SWE-ARC-TGT-WP1-T3` | Native | Acceptance passed | Complete | Excluded |  |
| #126 | `SWE-ARC-LAY-WP1` | Native | Acceptance passed | Complete | Excluded |  |
| #127 | `SWE-ARC-LAY-WP1-T1` | Native | Acceptance passed | Complete | Excluded |  |
| #128 | `SWE-ARC-LAY-WP1-T2` | Native | Acceptance passed | Complete | Excluded |  |
| #129 | `SWE-ARC-LAY-WP1-T3` | Native | Acceptance passed | Complete | Excluded |  |
| #130 | `SWE-ARC-CASE-WP1` | Native | Acceptance passed | Complete | Excluded |  |
| #131 | `SWE-ARC-CASE-WP1-T1` | Deferred | Implementation linked | Partial | Excluded |  |
| #132 | `SWE-ARC-CASE-WP1-T2` | Deferred | Implementation linked | Partial | Excluded |  |
| #133 | `SWE-ARC-CASE-WP1-T3` | Deferred | Implementation linked | Partial | Excluded |  |
| #134 | `SWE-ARC-API-WP1` | Native | Acceptance passed | Complete | Excluded |  |
| #135 | `SWE-ARC-API-WP1-T1` | Native | Acceptance passed | Complete | Excluded |  |
| #136 | `SWE-ARC-API-WP1-T2` | Native | Acceptance passed | Complete | Excluded |  |
| #137 | `SWE-ARC-API-WP1-T3` | Native | Acceptance passed | Complete | Excluded |  |
| #138 | `SWE-ARC-SVC-WP1` | Native | Acceptance passed | Complete | Excluded |  |
| #139 | `SWE-ARC-SVC-WP1-T1` | Native | Acceptance passed | Complete | Excluded |  |
| #140 | `SWE-ARC-SVC-WP1-T2` | Native | Acceptance passed | Complete | Excluded |  |
| #141 | `SWE-ARC-SVC-WP1-T3` | Native | Acceptance passed | Complete | Excluded |  |
| #142 | `SWE-ARC-ERR-WP1` | Native | Acceptance passed | Complete | Excluded |  |
| #143 | `SWE-ARC-ERR-WP1-T1` | Native | Acceptance passed | Complete | Excluded |  |
| #144 | `SWE-ARC-ERR-WP1-T2` | Native | Acceptance passed | Complete | Excluded |  |
| #145 | `SWE-ARC-ERR-WP1-T3` | Native | Acceptance passed | Complete | Excluded |  |
| #146 | `SWE-GUI-SHL-WP1` | Native | Acceptance passed | Complete | Excluded |  |
| #147 | `SWE-GUI-SHL-WP1-T1` | Native | Acceptance passed | Complete | Excluded |  |
| #148 | `SWE-GUI-SHL-WP1-T2` | Native | Acceptance passed | Complete | Excluded |  |
| #149 | `SWE-GUI-SHL-WP1-T3` | Native | Acceptance passed | Complete | Excluded |  |
| #150 | `SWE-VER-UNIT-WP1` | Deferred | Implementation linked | Partial | G6 required | G6 — Theoretical Hybrid Model Complete |
| #151 | `SWE-VER-UNIT-WP1-T1` | Deferred | Implementation linked | Partial | G6 required | G6 — Theoretical Hybrid Model Complete |
| #152 | `SWE-VER-UNIT-WP1-T2` | Deferred | Implementation linked | Partial | G6 required | G6 — Theoretical Hybrid Model Complete |
| #153 | `SWE-VER-UNIT-WP1-T3` | Deferred | Implementation linked | Partial | G6 required | G6 — Theoretical Hybrid Model Complete |
| #154 | `SWE-REL-CI-WP1` | Deferred | Implementation linked | Partial | Excluded |  |
| #155 | `SWE-REL-CI-WP1-T1` | Deferred | Implementation linked | Partial | Excluded |  |
| #156 | `SWE-REL-CI-WP1-T2` | Deferred | Implementation linked | Partial | Excluded |  |
| #157 | `SWE-REL-CI-WP1-T3` | Deferred | Implementation linked | Partial | Excluded |  |
| #158 | `SWE-DAT-CFG-WP1` | External library | Acceptance passed | Complete | G6 required | G6 — Theoretical Hybrid Model Complete |
| #159 | `SWE-DAT-CFG-WP1-T1` | External library | Acceptance passed | Complete | G6 required | G6 — Theoretical Hybrid Model Complete |
| #160 | `SWE-DAT-CFG-WP1-T2` | External library | Acceptance passed | Complete | G6 required | G6 — Theoretical Hybrid Model Complete |
| #161 | `SWE-DAT-CFG-WP1-T3` | External library | Acceptance passed | Complete | G6 required | G6 — Theoretical Hybrid Model Complete |
| #162 | `SWE-DAT-MAN-WP1` | External library | Acceptance passed | Complete | G6 required | G6 — Theoretical Hybrid Model Complete |
| #163 | `SWE-DAT-MAN-WP1-T1` | External library | Acceptance passed | Complete | G6 required | G6 — Theoretical Hybrid Model Complete |
| #164 | `SWE-DAT-MAN-WP1-T2` | External library | Acceptance passed | Complete | G6 required | G6 — Theoretical Hybrid Model Complete |
| #165 | `SWE-DAT-MAN-WP1-T3` | External library | Acceptance passed | Complete | G6 required | G6 — Theoretical Hybrid Model Complete |
| #166 | `SWE-DAT-SCH-WP1` | Deferred | No evidence | Not started | Post-G6 |  |
| #167 | `SWE-DAT-SCH-WP1-T1` | Deferred | No evidence | Not started | Post-G6 |  |
| #168 | `SWE-DAT-SCH-WP1-T2` | Deferred | No evidence | Not started | Post-G6 |  |
| #169 | `SWE-DAT-SCH-WP1-T3` | Deferred | No evidence | Not started | Post-G6 |  |
| #170 | `SWE-DAT-XDMF-WP1` | Deferred | No evidence | Not started | Post-G6 |  |
| #171 | `SWE-DAT-XDMF-WP1-T1` | Deferred | No evidence | Not started | Post-G6 |  |
| #172 | `SWE-DAT-XDMF-WP1-T2` | Deferred | No evidence | Not started | Post-G6 |  |
| #173 | `SWE-DAT-XDMF-WP1-T3` | Deferred | No evidence | Not started | Post-G6 |  |
| #174 | `SWE-FVM-MSH-WP1` | Native | Acceptance passed | Complete | Excluded |  |
| #175 | `SWE-FVM-MSH-WP1-T1` | Native | Acceptance passed | Complete | Excluded |  |
| #176 | `SWE-FVM-MSH-WP1-T2` | Native | Acceptance passed | Complete | Excluded |  |
| #177 | `SWE-FVM-MSH-WP1-T3` | Native | Acceptance passed | Complete | Excluded |  |
| #178 | `SWE-FVM-FLD-WP1` | Native | Acceptance passed | Complete | Excluded |  |
| #179 | `SWE-FVM-FLD-WP1-T1` | Native | Acceptance passed | Complete | Excluded |  |
| #180 | `SWE-FVM-FLD-WP1-T2` | Native | Acceptance passed | Complete | Excluded |  |
| #181 | `SWE-FVM-FLD-WP1-T3` | Native | Acceptance passed | Complete | Excluded |  |
| #182 | `SWE-FVM-BC-WP1` | Native | Acceptance passed | Complete | Excluded |  |
| #183 | `SWE-FVM-BC-WP1-T1` | Native | Acceptance passed | Complete | Excluded |  |
| #184 | `SWE-FVM-BC-WP1-T2` | Native | Acceptance passed | Complete | Excluded |  |
| #185 | `SWE-FVM-BC-WP1-T3` | Native | Acceptance passed | Complete | Excluded |  |
| #186 | `SWE-FVM-NUM-WP1` | Native | Acceptance passed | Complete | Excluded |  |
| #187 | `SWE-FVM-NUM-WP1-T1` | Native | Acceptance passed | Complete | Excluded |  |
| #188 | `SWE-FVM-NUM-WP1-T2` | Native | Acceptance passed | Complete | Excluded |  |
| #189 | `SWE-FVM-NUM-WP1-T3` | Native | Acceptance passed | Complete | Excluded |  |
| #190 | `SWE-GEO-IMP-WP1` | External library | Acceptance passed | Complete | G6 required | G6 — Theoretical Hybrid Model Complete |
| #191 | `SWE-GEO-IMP-WP1-T1` | External library | Acceptance passed | Complete | G6 required | G6 — Theoretical Hybrid Model Complete |
| #192 | `SWE-GEO-IMP-WP1-T2` | External library | Acceptance passed | Complete | G6 required | G6 — Theoretical Hybrid Model Complete |
| #193 | `SWE-GEO-IMP-WP1-T3` | External library | Acceptance passed | Complete | G6 required | G6 — Theoretical Hybrid Model Complete |
| #194 | `SWE-GEO-CRS-WP1` | External library | Acceptance passed | Complete | G6 required | G6 — Theoretical Hybrid Model Complete |
| #195 | `SWE-GEO-CRS-WP1-T1` | External library | Acceptance passed | Complete | G6 required | G6 — Theoretical Hybrid Model Complete |
| #196 | `SWE-GEO-CRS-WP1-T2` | External library | Acceptance passed | Complete | G6 required | G6 — Theoretical Hybrid Model Complete |
| #197 | `SWE-GEO-CRS-WP1-T3` | External library | Acceptance passed | Complete | G6 required | G6 — Theoretical Hybrid Model Complete |
| #198 | `SWE-GEO-COR-WP1` | External library | Acceptance passed | Complete | G6 required | G6 — Theoretical Hybrid Model Complete |
| #199 | `SWE-GEO-COR-WP1-T1` | External library | Acceptance passed | Complete | G6 required | G6 — Theoretical Hybrid Model Complete |
| #200 | `SWE-GEO-COR-WP1-T2` | External library | Acceptance passed | Complete | G6 required | G6 — Theoretical Hybrid Model Complete |
| #201 | `SWE-GEO-COR-WP1-T3` | External library | Acceptance passed | Complete | G6 required | G6 — Theoretical Hybrid Model Complete |
| #202 | `SWE-GEO-TER-WP1` | External library | Acceptance passed | Complete | G6 required | G6 — Theoretical Hybrid Model Complete |
| #203 | `SWE-GEO-TER-WP1-T1` | External library | Acceptance passed | Complete | G6 required | G6 — Theoretical Hybrid Model Complete |
| #204 | `SWE-GEO-TER-WP1-T2` | External library | Acceptance passed | Complete | G6 required | G6 — Theoretical Hybrid Model Complete |
| #205 | `SWE-GEO-TER-WP1-T3` | External library | Acceptance passed | Complete | G6 required | G6 — Theoretical Hybrid Model Complete |
| #206 | `SWE-GEO-MSH-WP1` | External library | Acceptance passed | Complete | G6 required | G6 — Theoretical Hybrid Model Complete |
| #207 | `SWE-GEO-MSH-WP1-T1` | External library | Acceptance passed | Complete | G6 required | G6 — Theoretical Hybrid Model Complete |
| #208 | `SWE-GEO-MSH-WP1-T2` | External library | Acceptance passed | Complete | G6 required | G6 — Theoretical Hybrid Model Complete |
| #209 | `SWE-GEO-MSH-WP1-T3` | External library | Acceptance passed | Complete | G6 required | G6 — Theoretical Hybrid Model Complete |
| #210 | `SWE-GEO-TAG-WP1` | External library | Implementation linked | Partial | G6 required | G6 — Theoretical Hybrid Model Complete |
| #211 | `SWE-GEO-TAG-WP1-T1` | External library | Implementation linked | Partial | G6 required | G6 — Theoretical Hybrid Model Complete |
| #212 | `SWE-GEO-TAG-WP1-T2` | External library | Implementation linked | Partial | G6 required | G6 — Theoretical Hybrid Model Complete |
| #213 | `SWE-GEO-TAG-WP1-T3` | External library | Implementation linked | Partial | G6 required | G6 — Theoretical Hybrid Model Complete |
| #214 | `SWE-GEO-BAR-WP1` | External library | Implementation linked | Partial | G6 required | G6 — Theoretical Hybrid Model Complete |
| #215 | `SWE-GEO-BAR-WP1-T1` | External library | Implementation linked | Partial | G6 required | G6 — Theoretical Hybrid Model Complete |
| #216 | `SWE-GEO-BAR-WP1-T2` | External library | Implementation linked | Partial | G6 required | G6 — Theoretical Hybrid Model Complete |
| #217 | `SWE-GEO-BAR-WP1-T3` | External library | Implementation linked | Partial | G6 required | G6 — Theoretical Hybrid Model Complete |
| #218 | `SWE-GEO-CHK-WP1` | External library | Acceptance passed | Complete | G6 required | G6 — Theoretical Hybrid Model Complete |
| #219 | `SWE-GEO-CHK-WP1-T1` | External library | Acceptance passed | Complete | G6 required | G6 — Theoretical Hybrid Model Complete |
| #220 | `SWE-GEO-CHK-WP1-T2` | External library | Acceptance passed | Complete | G6 required | G6 — Theoretical Hybrid Model Complete |
| #221 | `SWE-GEO-CHK-WP1-T3` | External library | Acceptance passed | Complete | G6 required | G6 — Theoretical Hybrid Model Complete |
| #222 | `SWE-GUI-CAS-WP1` | Deferred | No evidence | Deferred | Excluded |  |
| #223 | `SWE-GUI-CAS-WP1-T1` | Deferred | No evidence | Deferred | Excluded |  |
| #224 | `SWE-GUI-CAS-WP1-T2` | Deferred | No evidence | Deferred | Excluded |  |
| #225 | `SWE-GUI-CAS-WP1-T3` | Deferred | No evidence | Deferred | Excluded |  |
| #226 | `SWE-GUI-VIS-WP1` | Deferred | No evidence | Deferred | Excluded |  |
| #227 | `SWE-GUI-VIS-WP1-T1` | Deferred | No evidence | Deferred | Excluded |  |
| #228 | `SWE-GUI-VIS-WP1-T2` | Deferred | No evidence | Deferred | Excluded |  |
| #229 | `SWE-GUI-VIS-WP1-T3` | Deferred | No evidence | Deferred | Excluded |  |
| #230 | `SWE-VER-REG-WP1` | Deferred | Implementation linked | Partial | G6 required | G6 — Theoretical Hybrid Model Complete |
| #231 | `SWE-VER-REG-WP1-T1` | Deferred | Implementation linked | Partial | G6 required | G6 — Theoretical Hybrid Model Complete |
| #232 | `SWE-VER-REG-WP1-T2` | Deferred | Implementation linked | Partial | G6 required | G6 — Theoretical Hybrid Model Complete |
| #233 | `SWE-VER-REG-WP1-T3` | Native | Acceptance passed | Complete | G6 required | G6 — Theoretical Hybrid Model Complete |
