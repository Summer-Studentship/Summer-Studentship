#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

browser_bin=""
for candidate in chromium chromium-browser google-chrome google-chrome-stable; do
  if command -v "$candidate" >/dev/null 2>&1; then
    browser_bin="$(command -v "$candidate")"
    break
  fi
done

if [[ -z "$browser_bin" ]]; then
  printf '%s\n' \
    "No supported Chromium browser was found." \
    "Install Chromium or Google Chrome, then rerun this script." >&2
  exit 1
fi

render_figure() {
  local source_name="$1"
  local width="$2"
  local height="$3"
  local source_path="$script_dir/$source_name.html"
  local output_path="$script_dir/$source_name.png"

  "$browser_bin" \
    --headless \
    --disable-gpu \
    --hide-scrollbars \
    --force-device-scale-factor=2 \
    --window-size="$width,$height" \
    --screenshot="$output_path" \
    "file://$source_path"
}

render_figure "hybrid-framework-overview" 1800 1080

printf '%s\n' "Rendered editable final-report figure PNGs."
