# Studentship Final Report

This directory contains the two-column IEEE final-report source, its curated
bibliography and report-specific figures.

## Document structure

- Main text: two figures
  - Kamaishi Regional2D corridor map
  - one-way hybrid-framework overview
- Appendix: the original project-planning and architecture workflow PNG from
  the studentship poster

The appendix is additional material. Confirm whether it is excluded from the
formal two-page limit before submission.

## Build the report

From the repository root:

```bash
latexmk -cd -pdf deliverables/final-report/final-report.tex
```

The build requires the `IEEEtran` class and bibliography style.

## Edit and render the figures

`hybrid-framework-overview.html` is the editable source for the report-specific
hybrid architecture figure. Its roadmap, status markers and numbered cards
follow the visual language of the original poster workflow while using an
IEEE-compatible Times font stack:

```text
Times New Roman, Nimbus Roman No9 L, Liberation Serif, Times, serif
```

After editing the HTML file, regenerate its PNG preview with:

```bash
deliverables/final-report/figures/render-figures.sh
```

The renderer requires Chromium, Chromium Browser, Google Chrome or Google
Chrome Stable. `planning-workflow-original.png` is the unchanged poster asset;
the planning workflow and Kamaishi map are not modified by the renderer.

## Hyperlinks

`hyperref` is loaded with `hidelinks` so citations, figure/appendix references,
DOIs and URLs remain clickable without adding coloured boxes or link text that
would conflict with IEEE's print appearance.
