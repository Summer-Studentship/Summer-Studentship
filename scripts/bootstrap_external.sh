#!/usr/bin/env sh
set -eu

repo_root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
external_dir="${repo_root}/external"
mathplot_dir="${external_dir}/mathplot"
mathplot_repo="https://github.com/sebsjames/mathplot.git"
mathplot_commit="c2e17f21b8112c5fbbbfa5811ca92c21cbf3cd6a"

if ! command -v git >/dev/null 2>&1; then
    echo "git was not found on PATH. Install Git first." >&2
    exit 1
fi

mkdir -p "${external_dir}"

if [ ! -d "${mathplot_dir}" ]; then
    echo "Cloning mathplot into external/mathplot..."
    git clone --recursive "${mathplot_repo}" "${mathplot_dir}"
elif [ ! -d "${mathplot_dir}/.git" ]; then
    echo "external/mathplot exists but is not a git checkout." >&2
    exit 1
else
    echo "Updating existing external/mathplot checkout and submodules..."
    git -C "${mathplot_dir}" fetch --tags origin
    git -C "${mathplot_dir}" submodule update --init --recursive
fi

git -C "${mathplot_dir}" checkout "${mathplot_commit}"
git -C "${mathplot_dir}" submodule update --init --recursive

for relative_path in \
    "mplot/Visual.h" \
    "mplot/GraphVisual.h" \
    "maths/sm/vvec" \
    "mplot/fonts/verafonts.h"
do
    if [ ! -e "${mathplot_dir}/${relative_path}" ]; then
        echo "Mathplot checkout is incomplete. Missing: external/mathplot/${relative_path}" >&2
        exit 1
    fi
done

visual_face_base="${mathplot_dir}/mplot/VisualFaceBase.h"
sed -i.bak \
    -e 's/#elif defined _MSC_VER/#elif defined _WIN32/g' \
    -e 's/#ifndef _MSC_VER/#ifndef _WIN32/g' \
    -e 's/#ifdef _MSC_VER/#ifdef _WIN32/g' \
    "${visual_face_base}"
rm -f "${visual_face_base}.bak"

echo "External dependencies are ready."
