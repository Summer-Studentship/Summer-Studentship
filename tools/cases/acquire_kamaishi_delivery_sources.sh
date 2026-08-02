#!/usr/bin/env bash
set -euo pipefail

offline=false
overwrite=false
while [[ $# -gt 0 ]]; do
    case "$1" in
        --offline)
            offline=true
            shift
            ;;
        --overwrite)
            overwrite=true
            shift
            ;;
        *)
            echo "unknown argument: $1" >&2
            exit 2
            ;;
    esac
done

terrain_url="https://www.ngdc.noaa.gov/mgg/global/relief/ETOPO2022/data/15s/15s_surface_elev_gtif/ETOPO_2022_v1_15s_N45E135_surface.tif"
quake_url="https://earthquake.usgs.gov/product/finite-fault/usp000hvnu/us/1539808472261/basic_inversion.param"
terrain_path="data/source/terrain/ETOPO_2022_v1_15s_N45E135_surface.tif"
terrain_info="data/source/terrain/ETOPO_2022_v1_15s_N45E135_surface.gdalinfo.json"
quake_path="data/source/earthquake/usgs_usp000hvnu_1539808472261_basic_inversion.param"
sha_path="data/source/SHA256SUMS"
inventory_path="data/source/source_inventory.json"

mkdir -p data/source/terrain data/source/earthquake

download_if_needed() {
    local url=$1
    local path=$2
    if [[ -s "$path" && "$overwrite" == false ]]; then
        return
    fi
    if [[ -e "$path" && "$overwrite" == false ]]; then
        echo "refusing to overwrite unreadable or empty existing file without --overwrite: $path" >&2
        exit 1
    fi
    if [[ "$offline" == true ]]; then
        echo "offline mode: missing required source $path" >&2
        exit 1
    fi
    local tmp="${path}.tmp"
    curl -fL --retry 3 --retry-delay 2 "$url" -o "$tmp"
    test -s "$tmp"
    mv "$tmp" "$path"
}

download_if_needed "$terrain_url" "$terrain_path"
download_if_needed "$quake_url" "$quake_path"

for path in "$terrain_path" "$quake_path"; do
    if [[ ! -r "$path" || ! -s "$path" ]]; then
        echo "required source absent or unreadable: $path" >&2
        exit 1
    fi
done

gdalinfo -json "$terrain_path" > "$terrain_info"
sha256sum "$terrain_path" "$quake_path" > "$sha_path"

terrain_sha=$(sha256sum "$terrain_path" | awk '{print $1}')
quake_sha=$(sha256sum "$quake_path" | awk '{print $1}')
terrain_bytes=$(stat -c '%s' "$terrain_path")
quake_bytes=$(stat -c '%s' "$quake_path")
terrain_access=$(stat -c '%x' "$terrain_path")
quake_access=$(stat -c '%x' "$quake_path")
timestamp=$(date -u +%Y-%m-%dT%H:%M:%SZ)

cat > "$inventory_path" <<JSON
{
  "schema": {"name": "tsunami.kamaishi_delivery_source_inventory", "version": "1.0.0"},
  "generated_at_utc": "$timestamp",
  "offline_mode": $offline,
  "sources": [
    {
      "dataset_id": "etopo-2022-v1-15s-n45e135-surface",
      "provider": "NOAA/NCEI",
      "source_uri": "$terrain_url",
      "path": "$terrain_path",
      "byte_size": $terrain_bytes,
      "sha256": "$terrain_sha",
      "access_timestamp": "$terrain_access",
      "gdalinfo_json": "$terrain_info"
    },
    {
      "dataset_id": "usgs-usp000hvnu-1539808472261-basic-inversion",
      "provider": "USGS",
      "source_uri": "$quake_url",
      "path": "$quake_path",
      "byte_size": $quake_bytes,
      "sha256": "$quake_sha",
      "access_timestamp": "$quake_access"
    }
  ]
}
JSON

echo "$inventory_path"
