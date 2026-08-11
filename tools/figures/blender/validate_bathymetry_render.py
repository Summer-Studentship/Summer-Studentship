#!/usr/bin/env python3
"""Validate and package the R17 Blender bathymetry render outputs."""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from datetime import UTC, datetime
from pathlib import Path
from typing import Any, Sequence

from PIL import Image, ImageDraw, ImageFont


REPO_ROOT = Path(__file__).resolve().parents[3]
R17_ROOT = REPO_ROOT / "deliverables/figures/r17_closure"
PREVIEW_ROOT = R17_ROOT / "previews"
PUBLICATION_ROOT = R17_ROOT / "publication"
PROVENANCE_ROOT = R17_ROOT / "provenance"
SOURCE_ROOT = R17_ROOT / "sources/blender"
TERRAIN_MANIFEST = SOURCE_ROOT / "terrain/terrain_manifest.json"
CONTACT_SHEET = PREVIEW_ROOT / "figure_C_vertical_exaggeration_contact_sheet.png"
FINAL_PNG = PUBLICATION_ROOT / "figure_C_corridor_bathymetry_3d.png"
FINAL_PDF = PUBLICATION_ROOT / "figure_C_corridor_bathymetry_3d.pdf"
PROVENANCE = PROVENANCE_ROOT / "figure_C_corridor_bathymetry_3d.provenance.json"


CAPTION = (
    "Oblique Blender terrain visualisation of the Kamaishi Regional2D corridor using the authoritative "
    "ETOPO 2022 WGS84 + EGM2008 bathymetry/topography lineage from R16. The translucent blue plane marks "
    "z = 0 EGM2008 sea level, and the muted warm outline shows the actual computational corridor. "
    "Vertical relief is exaggerated for interpretation; this is not a 3D fluid simulation or Local3D result."
)


def utc_now() -> str:
    return datetime.now(UTC).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def file_record(path: Path) -> dict[str, Any]:
    record: dict[str, Any] = {"path": path.relative_to(REPO_ROOT).as_posix() if path.is_relative_to(REPO_ROOT) else path.as_posix(), "exists": path.exists()}
    if path.is_file():
        record.update({"bytes": path.stat().st_size, "sha256": sha256(path)})
    return record


def read_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def candidate_png(exaggeration: int) -> Path:
    return PREVIEW_ROOT / f"figure_C_corridor_bathymetry_3d_x{exaggeration}_preview.png"


def candidate_record(exaggeration: int) -> Path:
    return PROVENANCE_ROOT / f"figure_C_corridor_bathymetry_3d_x{exaggeration}_render.json"


def image_size(path: Path) -> tuple[int, int]:
    with Image.open(path) as image:
        return image.size


def make_contact_sheet() -> dict[str, Any]:
    PREVIEW_ROOT.mkdir(parents=True, exist_ok=True)
    try:
        label_font = ImageFont.truetype("/usr/share/fonts/liberation/LiberationSans-Regular.ttf", 46)
    except OSError:
        label_font = ImageFont.load_default()
    panels = []
    for exaggeration in [2, 4, 6]:
        path = candidate_png(exaggeration)
        if not path.is_file():
            raise RuntimeError(f"Missing candidate preview: {path}")
        image = Image.open(path).convert("RGB")
        image.thumbnail((1600, 950), Image.Resampling.LANCZOS)
        canvas = Image.new("RGB", (1600, 1040), "white")
        canvas.paste(image, ((1600 - image.width) // 2, 70))
        draw = ImageDraw.Draw(canvas)
        draw.text((44, 20), f"{exaggeration}x vertical exaggeration", fill=(20, 20, 20), font=label_font)
        panels.append(canvas)
    sheet = Image.new("RGB", (4800, 1040), "white")
    for index, panel in enumerate(panels):
        sheet.paste(panel, (1600 * index, 0))
    CONTACT_SHEET.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(CONTACT_SHEET)
    return {"path": file_record(CONTACT_SHEET), "dimensions": {"width": sheet.width, "height": sheet.height}}


def create_pdf() -> dict[str, Any]:
    if not FINAL_PNG.is_file():
        raise RuntimeError(f"Missing final PNG: {FINAL_PNG}")
    with Image.open(FINAL_PNG) as image:
        rgb = image.convert("RGB")
        rgb.save(FINAL_PDF, "PDF", resolution=300.0)
    return file_record(FINAL_PDF)


def validate_images(selected: int) -> dict[str, Any]:
    results: dict[str, Any] = {}
    for exaggeration in [2, 4, 6]:
        path = candidate_png(exaggeration)
        width, height = image_size(path)
        if width < 1800 or height < 1000:
            raise RuntimeError(f"Preview too small: {path}: {width}x{height}")
        results[f"{exaggeration}x_preview"] = {"path": file_record(path), "width": width, "height": height}
    width, height = image_size(FINAL_PNG)
    if width < 6000:
        raise RuntimeError(f"Final render width below 6000 px: {width}")
    if height < 3000:
        raise RuntimeError(f"Final render height unexpectedly small: {height}")
    results["final_png"] = {"path": file_record(FINAL_PNG), "width": width, "height": height}
    results["selected_preview"] = selected
    return results


def package(selected: int, selection_rationale: str, visual_qc: Sequence[str]) -> dict[str, Any]:
    if selected not in {2, 4, 6}:
        raise RuntimeError("--selected-exaggeration must be one of 2, 4 or 6")
    terrain_manifest = read_json(TERRAIN_MANIFEST)
    render_records = {str(ex): read_json(candidate_record(ex)) for ex in [2, 4, 6]}
    selected_record = read_json(PROVENANCE_ROOT / f"figure_C_corridor_bathymetry_3d_final_x{selected}_render.json")
    contact = make_contact_sheet()
    pdf = create_pdf()
    images = validate_images(selected)
    payload = {
        "schema": {"name": "tsunami.r17.figure_c_3d_bathymetry", "version": "1.0.0"},
        "status": "COMPLETE",
        "generated_at_utc": utc_now(),
        "figure_id": "C",
        "basename": "figure_C_corridor_bathymetry_3d",
        "replaces_r16_figure_c": True,
        "caption": CAPTION.replace("Vertical relief is exaggerated", f"Vertical relief is exaggerated {selected}x"),
        "selected_vertical_exaggeration": selected,
        "selection_rationale": selection_rationale,
        "visual_qc": list(visual_qc),
        "terrain": terrain_manifest,
        "render_candidates": render_records,
        "selected_render_record": selected_record,
        "outputs": {
            "publication_png": images["final_png"],
            "publication_pdf": pdf,
            "contact_sheet": contact,
            "previews": {key: value for key, value in images.items() if key.endswith("_preview")},
        },
        "scientific_caveats": [
            "Terrain visualisation only; not a 3D fluid simulation.",
            "Uses ETOPO 2022/R16 GIS terrain lineage; no new DEM was downloaded.",
            "The R10 h400 limited_linear event result remains BEST_AVAILABLE_NUMERICALLY_UNCERTAIN.",
            "No historical validation, calibration or real-event mesh convergence is implied.",
        ],
        "regeneration": {
            "prepare": "python3 tools/figures/blender/prepare_corridor_terrain.py",
            "render_preview": "ALSOFT_DRIVERS=null blender --factory-startup --background --python tools/figures/blender/render_corridor_bathymetry.py -- --vertical-exaggeration <2|4|6> --width 2400 --height 1400 --output <preview.png> --record <record.json>",
            "render_final": "ALSOFT_DRIVERS=null blender --factory-startup --background --python tools/figures/blender/render_corridor_bathymetry.py -- --vertical-exaggeration <selected> --width 6200 --height 3600 --output deliverables/figures/r17_closure/publication/figure_C_corridor_bathymetry_3d.png --record <final_record.json>",
            "package": "python3 tools/figures/blender/validate_bathymetry_render.py --selected-exaggeration <selected> --selection-rationale '<text>'",
        },
    }
    write_json(PROVENANCE, payload)
    return payload


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--selected-exaggeration", type=int, required=True)
    parser.add_argument("--selection-rationale", required=True)
    parser.add_argument("--visual-qc", action="append", default=[])
    args = parser.parse_args(argv)
    payload = package(args.selected_exaggeration, args.selection_rationale, args.visual_qc)
    print(json.dumps(payload, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
