# Workstream Documentation

The `docs/workstream` directory contains structured LaTeX documentation for project workstreams. It is intended to hold workstream-level research, specification and handoff material inside the repository's documentation area without changing the existing implementation layout.

Directories use the exact naming convention `<WBS ID> - <WBS Name>`. For Research, Domain directories and Deliverable directories follow the same convention, and generated file names use lowercase WBS components.

The single shared LaTeX style lives at `../Latex/Style/coursework-report-core.sty`. Domain master files load it through the central relative path `../../../Latex/Style/coursework-report-core`, so the same style is available across the workstream tree without copying it into individual Domains or Deliverables.

Each Research Domain master `.tex` file is an independently compilable XeLaTeX document. Deliverable `.tex` files are fragments included by their Domain master and must not define their own document class, bibliography resource or document environment.

Each Research Domain owns one Domain-level `.bib` file. References should remain in the bibliography of the Domain whose research they support.

No Git commit is permitted until the Research workstream is complete and reviewed.
