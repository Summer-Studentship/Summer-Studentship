# Studentship LaTeX System Audit

Date: 2026-08-07

Reference template inspected: `/home/helios/Downloads/Coursework_Template.tex`.
Authoritative style implementation found: `docs/Latex/Style/coursework-report-core.sty`.

## Shared Presentation Authority

`docs/Latex/Style/coursework-report-core.sty` owns the shared coursework/report
presentation layer: XeLaTeX font setup, default A4 geometry where no document
has already selected a functional page size, paragraph rhythm, captions, table
defaults, headers/footers, hyperlink and cross-reference styling, bibliography
presentation, mathematical packages, section-based equation numbering and
display-equation spacing.

`docs/Latex/Style/studentship-math-conventions.sty` provides the minimal shared
mathematics convention used by the consolidated model without importing the full
coursework report layout.

`docs/Latex/Style/research-workstream.sty` is a thin Research extension over the
coursework core. It adds Research-wide technical math support (`bm`) and forwards
style options to the core.

The shared title helper suppresses optional blank metadata rows, so documents do
not need placeholder values for unavailable student-number or word-count fields.

Final display-equation spacing:

- `\abovedisplayskip`: `12pt plus 3pt minus 2pt`
- `\belowdisplayskip`: `12pt plus 3pt minus 2pt`
- `\abovedisplayshortskip`: `8pt plus 2pt minus 2pt`
- `\belowdisplayshortskip`: `8pt plus 2pt minus 2pt`

## Standalone Entrypoints

| Path | Class | Loaded shared style | Geometry | Fonts | Headings/paragraphs | Math/numbering | Captions | Headers/footers | Bibliography | Engine | Shared-style candidate | Compile status |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `deliverables/abstract/abstract.tex` | `article[12pt,a4paper]` | coursework core | core default | core Times New Roman | core | core section equations | core | core | none | XeLaTeX | already core | not changed |
| `deliverables/poster/poster-legacy.tex` | `article` | coursework core | intentional A0 landscape local geometry | core Times New Roman | core | core section equations | core | core | no citations | XeLaTeX | legacy scaffold; final poster authority is `deliverables/poster/poster.html` | not changed |
| `deliverables/report/report.tex` | `article[11pt]` | coursework core | core default | core Times New Roman | core + metadata title | core section equations | core | core, report header text | biblatex/biber, `project-citation-bank.bib` | XeLaTeX + biber | core | PASS |
| `docs/Latex/Notes/Doc.tex` | `article[11pt]` | coursework core | local legacy geometry | core Times New Roman | local legacy + core | core section equations | core | core | biblatex/biber | XeLaTeX | future cleanup candidate | not changed |
| `docs/Latex/Proposed Model/studentship_consolidated_mathematical_model_v1.0.tex` | `article[11pt,a4paper]` | math conventions only | intentional local geometry | local Latin Modern | intentional local layout | section equations via math conventions | local caption package | intentional local fancyhdr | local `thebibliography` | XeLaTeX | math conventions only | PASS |
| `docs/Latex/Studentship Proposal/Studentship_Proposal.tex` | `article[11pt]` | none | local legacy geometry | local fontspec | local legacy | local section equations | local | local fancyhdr | biblatex/biber | XeLaTeX | future cleanup candidate | not changed |
| `docs/Latex/Studentship Proposal/Updated_Proposal.tex` | `article[11pt]` | coursework core | local legacy geometry | core Times New Roman | local legacy + core | core section equations | core | local header | biblatex/biber | XeLaTeX | future cleanup candidate | not changed |
| `docs/Research Papers/Excess/fsi_high_level_progress_v0.1.tex` | standalone | no shared style | not normalised | local | local | local | local | local | biblatex/biber | XeLaTeX likely | future cleanup candidate outside RES workstream | not changed |
| `docs/workstream/RES - Research/RES-DAT - Environmental Data and Case-Study Definition/res-dat.tex` | `article[12pt,a4paper]` | research extension | core default | core Times New Roman | core | core section equations | core | core | biblatex/biber, `res-dat.bib` | XeLaTeX + biber | research extension | PASS |
| `docs/workstream/RES - Research/RES-GEO - Barrier Geometry and Spatial Representation/res-geo.tex` | `article[12pt,a4paper]` | research extension | core default | core Times New Roman | core | core section equations | core | core | biblatex/biber, `res-geo.bib` | XeLaTeX + biber | research extension | PASS |
| `docs/workstream/RES - Research/RES-ML - Machine-Learning Methodology/res-ml.tex` | `article[12pt,a4paper]` | research extension | core default | core Times New Roman | core | core section equations | core | core | biblatex/biber, `res-ml.bib` | XeLaTeX + biber | research extension | PASS |
| `docs/workstream/RES - Research/RES-MOD - Mathematical Model and Coupling/res-mod.tex` | `article[12pt,a4paper]` | research extension | core default | core Times New Roman | core | core section equations | core | core | biblatex/biber, `res-mod.bib` | XeLaTeX + biber | research extension | PASS |
| `docs/workstream/RES - Research/RES-NUM - Numerical Methods, Accuracy and Stability/res-num.tex` | `article[12pt,a4paper]` | research extension | core default | core Times New Roman | core | core section equations | core | core | biblatex/biber, `res-num.bib` | XeLaTeX + biber | research extension | PASS |
| `docs/workstream/RES - Research/RES-OPT - Design Optimisation/res-opt.tex` | `article[12pt,a4paper]` | research extension | core default | core Times New Roman | core | core section equations | core | core | biblatex/biber, `res-opt.bib` | XeLaTeX + biber | research extension | PASS |
| `docs/workstream/RES - Research/RES-PHY - Tsunami and Coastal Fluid Dynamics/res-phy.tex` | `article[12pt,a4paper]` | research extension | core default | core Times New Roman | core | core section equations | core | core | biblatex/biber, `res-phy.bib` | XeLaTeX + biber | research extension | PASS |
| `docs/workstream/RES - Research/RES-STR - Materials, Structural Response and Damage/res-str.tex` | `article[12pt,a4paper]` | research extension | core default | core Times New Roman | core | core section equations | core | core | biblatex/biber, `res-str.bib` | XeLaTeX + biber | research extension | PASS |
| `docs/workstream/RES - Research/RES-SUS - Sustainability and Environmental Impact/res-sus.tex` | `article[12pt,a4paper]` | research extension | core default | core Times New Roman | core | core section equations | core | core | biblatex/biber, `res-sus.bib` | XeLaTeX + biber | research extension | PASS |
| `docs/workstream/RES - Research/RES-VER - Verification, Testing and Uncertainty Methodology/res-ver.tex` | `article[12pt,a4paper]` | research extension | core default | core Times New Roman | core | core section equations | core | core | biblatex/biber, `res-ver.bib` | XeLaTeX + biber | research extension | PASS |

## Include/Input Fragments

Fragments do not load document-level shared styles.

- `deliverables/abstract/abstract-body.tex`
- `deliverables/report/sections/01-introduction.tex`
- `deliverables/report/sections/02-project-aims-and-scope.tex`
- `deliverables/report/sections/03-tohoku-case-study-and-data.tex`
- `deliverables/report/sections/04-hybrid-system-architecture.tex`
- `deliverables/report/sections/05-earthquake-tsunami-generation.tex`
- `deliverables/report/sections/06-regional2d-governing-model.tex`
- `deliverables/report/sections/07-regional2d-numerical-formulation.tex`
- `deliverables/report/sections/08-kamaishi-corridor-and-terrain.tex`
- `deliverables/report/sections/09-regional-to-local-coupling.tex`
- `deliverables/report/sections/10-local3d-urans-vof-model.tex`
- `deliverables/report/sections/11-openfoam-implementation.tex`
- `deliverables/report/sections/12-boundary-wall-and-timestep-treatment.tex`
- `deliverables/report/sections/13-g6-verification-and-model-closure.tex`
- `deliverables/report/sections/14-validation-methodology.tex`
- `deliverables/report/sections/15-calibration-and-sensitivity-methodology.tex`
- `deliverables/report/sections/16-complete-computational-algorithm.tex`
- `deliverables/report/sections/17-current-results-and-evidence.tex`
- `deliverables/report/sections/18-reproducibility-and-traceability.tex`
- `deliverables/report/sections/19-limitations.tex`
- `deliverables/report/sections/20-future-work.tex`
- `deliverables/report/sections/21-conclusions.tex`
- `docs/Research Papers/Excess/res-str_fsi_insertions_v0.2.tex`
- all `RES-*/*/res-*.tex` files included by the ten Research domain entrypoints

## Intentional Exceptions

- The poster keeps A0 landscape geometry by loading `geometry` before the core.
- The consolidated model keeps its local geometry, title page, colour palette,
  headers and `thebibliography`; it imports only shared mathematics conventions.
- Legacy Notes and Studentship Proposal documents remain future cleanup
  candidates and were not part of this refactor.
- Research empty-bibliography warnings in `RES-ML`, `RES-OPT` and `RES-SUS`
  reflect no current citations in those documents, not unresolved citations.
- Large Research overfull-vbox warnings were present on the untouched
  `origin/main` baseline and are recorded as inherited layout debt.

## Verification

Build outputs and captured logs were written to `/tmp/latex-build-publish-final`.

- `latexmk -cd -xelatex -interaction=nonstopmode -halt-on-error`: PASS for the
  deliverables report, consolidated model and ten Research standalone entrypoints.
- Final `.log` scan: no undefined control sequences, missing style/input files,
  option clashes, unresolved citations or unresolved references.
- Remaining final-log bibliography warnings are the expected empty-bibliography
  warnings for `RES-ML`, `RES-OPT` and `RES-SUS`.
- Visual spot checks rendered the deliverables report title/body pages, the
  consolidated model title/body pages and first-page contact sheet for all ten
  Research standalone PDFs.
- Consolidated display-equation spacing was probed at document end and remained
  `12pt plus 3pt minus 2pt` above/below displays and
  `8pt plus 2pt minus 2pt` above/below short displays.

## Tracked PDF Synchronisation

| Standalone source | Source SHA-256 | Tracked PDF path | PDF SHA-256 | Build status | Page count |
| --- | --- | --- | --- | --- | --- |
| `deliverables/report/report.tex` | `3b8c651f06e4011bb3b26205724dff0b87ff96bfacb0d4fb9ce4560f7c7d6e57` | `deliverables/report/report.pdf` | `11f157374eb500797a86c76231dc515c934fecdf2b07bf7ca864fdbf886e933f` | PASS | 17 |
| `docs/workstream/RES - Research/RES-DAT - Environmental Data and Case-Study Definition/res-dat.tex` | `22bb90cddf08df1d121c6ecb9f25bfd4ce6fce3a7cc3c166c20381c034225885` | `docs/workstream/RES - Research/RES-DAT - Environmental Data and Case-Study Definition/res-dat.pdf` | `92cbcb190d8367e0cefc94af55b93978386c8c60f95d9e7db14ab84d620066f3` | PASS | 37 |
| `docs/workstream/RES - Research/RES-GEO - Barrier Geometry and Spatial Representation/res-geo.tex` | `d00348bca6796c2cd0de60c95f2d8924d61c547181f0ad4020714dd715be7ad7` | `docs/workstream/RES - Research/RES-GEO - Barrier Geometry and Spatial Representation/res-geo.pdf` | `54f1ef61ccb088778784a76d1f7e1f08bcb7393857020e88de6ca4ad6dad8025` | PASS | 24 |
| `docs/workstream/RES - Research/RES-ML - Machine-Learning Methodology/res-ml.tex` | `0c33510d2f072929e7986fe424ca2267de5a8067cbc58d56aa93036505f74516` | `docs/workstream/RES - Research/RES-ML - Machine-Learning Methodology/res-ml.pdf` | `a480341a6857611056419c7b2df780505edcb530f163ad98d869691af11d0939` | PASS | 32 |
| `docs/workstream/RES - Research/RES-MOD - Mathematical Model and Coupling/res-mod.tex` | `42caf1656433b3f4bf9765ce77a21e45d6e8d48407b3e15e3d90acb4837a07ce` | `docs/workstream/RES - Research/RES-MOD - Mathematical Model and Coupling/res-mod.pdf` | `b2943bcbcd38502de26318231aab3aff726ccad9bc31d635b65254620fc6825e` | PASS | 52 |
| `docs/workstream/RES - Research/RES-NUM - Numerical Methods, Accuracy and Stability/res-num.tex` | `8f9dea99718c720df0301c7a48a08e092c23ba5c6d4c5a7c7e79fec4057cccc7` | `docs/workstream/RES - Research/RES-NUM - Numerical Methods, Accuracy and Stability/res-num.pdf` | `bf1eb29b0be5a35b2193a5ff0b25f325d0ffc468abce5a7085ea10139069f369` | PASS | 59 |
| `docs/workstream/RES - Research/RES-OPT - Design Optimisation/res-opt.tex` | `7d9e94b6576e9a6d8f3c067c5c2d72e81300a35fa934a56fc7ee56d5307eca05` | `docs/workstream/RES - Research/RES-OPT - Design Optimisation/res-opt.pdf` | `1505e1b0ce32f0f44ccb870f028a045a780bbbdb33bef97cad6afa19a73c3216` | PASS | 18 |
| `docs/workstream/RES - Research/RES-PHY - Tsunami and Coastal Fluid Dynamics/res-phy.tex` | `0ef40389a12bd4f0d8888560498c4de6c39501f435de324caf2d80c3abac4f15` | `docs/workstream/RES - Research/RES-PHY - Tsunami and Coastal Fluid Dynamics/res-phy.pdf` | `111627f91d72954795d90803290660294a5e3b85ac44a794724b5851c095f628` | PASS | 24 |
| `docs/workstream/RES - Research/RES-STR - Materials, Structural Response and Damage/res-str.tex` | `b1419353e0aaba10515a33853ceb982a8fb18cc66ea20942bd2b82e7e156de42` | `docs/workstream/RES - Research/RES-STR - Materials, Structural Response and Damage/res-str.pdf` | `d87d2141f5e46637c8a5163c09e25daff21a1493002142eddf9e766184b6fd6c` | PASS | 28 |
| `docs/workstream/RES - Research/RES-VER - Verification, Testing and Uncertainty Methodology/res-ver.tex` | `a67ed3a7d7d169e0cf2cc498349a426d0274e7f25d440402ac642b2f062c722e` | `docs/workstream/RES - Research/RES-VER - Verification, Testing and Uncertainty Methodology/res-ver.pdf` | `5390b7d7ba9277ede21f5e25d951cc54c7f1b2489f06a1e7349a6ab7d73a7fec` | PASS | 32 |

The consolidated model PDF and `RES-SUS` PDF were rebuilt for QA but not copied
or staged because those PDF paths are not tracked by Git.
