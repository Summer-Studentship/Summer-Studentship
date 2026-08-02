#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -lt 2 ]; then
    echo "usage: $0 <case-dir> <openfoam-command> [args...]" >&2
    exit 2
fi

case_dir=$1
shift

if [ ! -d "$case_dir" ]; then
    echo "OpenFOAM case directory does not exist: $case_dir" >&2
    exit 2
fi

image=${OPENFOAM11_IMAGE:-docker.io/openfoam/openfoam11-paraview510:11}
case_dir=$(realpath "$case_dir")

exec podman run --rm \
    --network none \
    --entrypoint /bin/bash \
    --userns keep-id \
    --user "$(id -u):$(id -g)" \
    --volume "$case_dir:/case:Z" \
    --workdir /case \
    "$image" \
    -lc 'source /opt/openfoam11/etc/bashrc && exec "$@"' \
    openfoam-command "$@"
