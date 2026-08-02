#!/usr/bin/env bash
set -euo pipefail

output_root=${1:-/tmp/tsunami-openfoam-replay-smoke}

python3 tools/openfoam/run_synthetic_replay_smoke.py \
    --output-root "$output_root" \
    --wrapper tools/openfoam/run_openfoam11.sh \
    --fixture-root tests/fixtures/openfoam/synthetic_replay \
    --clean
