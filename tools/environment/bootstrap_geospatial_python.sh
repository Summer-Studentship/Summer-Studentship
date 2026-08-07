#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
env_dir=${TSUNAMI_C1A_GEOSPATIAL_ENV:-/home/helios/SimulationData/Summer-Studentship/envs/c1a-geospatial}
python_bin=${PYTHON:-python3}
requirements=${TSUNAMI_C1A_GEOSPATIAL_REQUIREMENTS:-"$repo_root/tools/earthquake/requirements-tohoku-artifact.txt"}

if [ ! -d "$env_dir" ]; then
    "$python_bin" -m venv "$env_dir"
fi

"$env_dir/bin/python" -m pip install --upgrade pip setuptools wheel
"$env_dir/bin/python" -m pip install -r "$requirements"
"$env_dir/bin/python" - <<'PY'
import sys
import rasterio
import pyproj
import clawpack
from clawpack.geoclaw import dtopotools

print("python", sys.version)
print("python_executable", sys.executable)
print("rasterio", rasterio.__version__, rasterio.__file__)
print("pyproj", pyproj.__version__, pyproj.__file__)
print("clawpack", getattr(clawpack, "__version__", "version-not-exposed"))
print("dtopotools", dtopotools.__file__)
PY
