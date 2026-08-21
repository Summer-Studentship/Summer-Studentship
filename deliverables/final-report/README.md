# Studentship Final Report

This directory contains the two-column IEEE final-report source, its curated
bibliography and report-specific figures.

## Document structure

- Main text: two figures
  - Kamaishi Regional2D corridor map
  - one-way hybrid-framework overview
- Appendix: one two-column comparison of the original proposal pathway and
  the updated studentship plan

The appendix is additional material. Confirm whether it is excluded from the
formal two-page limit before submission.

## Build the report

From the repository root:

```bash
latexmk -cd -pdf deliverables/final-report/final-report.tex
```

The build requires the `IEEEtran` class and bibliography style.

## Edit and render the figures

The HTML files in `figures/` are the editable figure sources. They use an
IEEE-compatible Times font stack:

```text
Times New Roman, Nimbus Roman No9 L, Liberation Serif, Times, serif
```

After editing either HTML file, regenerate the PNG previews with:

```bash
deliverables/final-report/figures/render-figures.sh
```

The renderer requires Chromium, Chromium Browser, Google Chrome or Google
Chrome Stable. The Kamaishi map is a supplied raster asset and is not modified
by the renderer.
