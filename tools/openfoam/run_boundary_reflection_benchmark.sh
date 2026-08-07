#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
python3 "$repo_root/tools/openfoam/boundary_reflection_benchmark.py" \
  --config "$repo_root/tests/fixtures/openfoam/boundary_reflection/benchmark_config.json" \
  --output-root "${1:-$repo_root/docs/workstream/SWE - Software/SWE-L3D/g6_boundary_reflection}" \
  --overwrite
