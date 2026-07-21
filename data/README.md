# Data Directory

Bulk data is intentionally ignored by Git. Use this directory for local
organisation and commit only small curated examples that are useful for tests or
documentation.

Recommended stages:

- `raw/`: immutable source data
- `interim/`: converted, cleaned, or partially processed data
- `processed/`: model-ready datasets
- `results/`: simulation and ML outputs
- `figures/`: generated plots, screenshots, and visual outputs
- `examples/`: tiny tracked samples only

Use a consistent taxonomy inside each stage:

```text
data/<stage>/<model>/<subcategory>/<run_id>/
```

Prefer HDF5 for large structured arrays and pair each run with lightweight JSON
metadata for parameters, provenance, and units.
