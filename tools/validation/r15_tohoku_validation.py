#!/usr/bin/env python3
"""R15 Tohoku historical-validation evidence generator.

The generator deliberately separates immutable raw observations in the external
R15 store from processed tables, figures, and repo-facing handoff documents.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import shutil
import subprocess
from dataclasses import dataclass
from datetime import UTC, datetime, timedelta, timezone
from pathlib import Path
from typing import Any, Iterable, Mapping, Sequence


REPO_DOCS = Path("docs/workstream/SWE - Software/SWE-VER/SWE-VER-CONV/C1A")
FIGURE_ROOT = Path("deliverables/figures/r15_validation/publication")
VIDEO_ROOT = Path("deliverables/video/r15_validation")
STYLE_PATH = Path("tools/results/styles/research.mplstyle")
EXTERNAL_ROOT = Path("/home/helios/SimulationData/Summer-Studentship/validation/tohoku2011/r15")
CORRIDOR_PATH = Path("/home/helios/SimulationData/Summer-Studentship/g6-kamaishi/case/manifests/corridors/kamaishi-delivery-corridor-evidence.json")
H400_HDF5 = Path("/home/helios/SimulationData/Summer-Studentship/results/r11-regional2d-storage-poc/r10-h400-limited-linear/regional2d.h5")
R14_VIDEO = Path("deliverables/video/r14_hybrid/local3d_g6_preview.mp4")
R14_QR = Path("deliverables/video/r14_hybrid/qr_asset_metadata.json")
R14_VIDEO_PROVENANCE = Path("deliverables/video/r14_hybrid/video_provenance.json")
R14_POSTER_SHORTLIST = REPO_DOCS / "regional2d_r14_poster_asset_shortlist.json"
R14_BOUNDEDNESS = REPO_DOCS / "regional2d_r14_local3d_boundedness_audit.json"
EVENT_UTC = datetime(2011, 3, 11, 5, 46, 23, tzinfo=UTC)
JST = timezone(timedelta(hours=9))
CLAIM_LABELS = [
    "UNCALIBRATED HISTORICAL COMPARISON",
    "REGIONAL NUMERICAL UNCERTAINTY NOT FULLY QUALIFIED",
]
ALLOWED_STATE = {"NOT_STARTED", "READY", "RUNNING", "COMPLETE", "BLOCKED", "DEFERRED", "NOT_APPLICABLE"}


@dataclass(frozen=True)
class Point:
    x: float
    y: float


@dataclass(frozen=True)
class DartRow:
    timestamp_utc: datetime
    julian_day: float
    raw_observation_m: float
    fitted_tide_m: float
    residual_m: float
    event_meter_column: float


def utc_now() -> str:
    return datetime.now(UTC).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def read_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path: Path, payload: Mapping[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def git_sha() -> str:
    completed = subprocess.run(["git", "rev-parse", "HEAD"], text=True, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, check=False)
    return completed.stdout.strip() if completed.returncode == 0 else "unknown"


def repo_path(path: Path) -> str:
    return path.as_posix()


def wgs84_to_utm54n(latitude_deg: float, longitude_deg: float) -> Point:
    return wgs84_to_utm(latitude_deg, longitude_deg, zone=54)


def wgs84_to_utm(latitude_deg: float, longitude_deg: float, *, zone: int) -> Point:
    a = 6378137.0
    f = 1 / 298.257223563
    k0 = 0.9996
    e2 = f * (2 - f)
    ep2 = e2 / (1 - e2)
    lat = math.radians(latitude_deg)
    lon = math.radians(longitude_deg)
    lon0 = math.radians((zone - 1) * 6 - 180 + 3)
    n = a / math.sqrt(1 - e2 * math.sin(lat) ** 2)
    t = math.tan(lat) ** 2
    c = ep2 * math.cos(lat) ** 2
    aa = math.cos(lat) * (lon - lon0)
    m = a * (
        (1 - e2 / 4 - 3 * e2**2 / 64 - 5 * e2**3 / 256) * lat
        - (3 * e2 / 8 + 3 * e2**2 / 32 + 45 * e2**3 / 1024) * math.sin(2 * lat)
        + (15 * e2**2 / 256 + 45 * e2**3 / 1024) * math.sin(4 * lat)
        - (35 * e2**3 / 3072) * math.sin(6 * lat)
    )
    easting = k0 * n * (
        aa
        + (1 - t + c) * aa**3 / 6
        + (5 - 18 * t + t**2 + 72 * c - 58 * ep2) * aa**5 / 120
    ) + 500000
    northing = k0 * (
        m
        + n
        * math.tan(lat)
        * (
            aa**2 / 2
            + (5 - t + 9 * c + 4 * c**2) * aa**4 / 24
            + (61 - 58 * t + t**2 + 600 * c - 330 * ep2) * aa**6 / 720
        )
    )
    return Point(easting, northing)


def utm54n_to_wgs84(x: float, y: float) -> tuple[float, float]:
    return utm_to_wgs84(x, y, zone=54)


def utm_to_wgs84(x: float, y: float, *, zone: int) -> tuple[float, float]:
    a = 6378137.0
    f = 1 / 298.257223563
    k0 = 0.9996
    e2 = f * (2 - f)
    ep2 = e2 / (1 - e2)
    e1 = (1 - math.sqrt(1 - e2)) / (1 + math.sqrt(1 - e2))
    x0 = x - 500000
    m = y / k0
    mu = m / (a * (1 - e2 / 4 - 3 * e2**2 / 64 - 5 * e2**3 / 256))
    phi1 = (
        mu
        + (3 * e1 / 2 - 27 * e1**3 / 32) * math.sin(2 * mu)
        + (21 * e1**2 / 16 - 55 * e1**4 / 32) * math.sin(4 * mu)
        + (151 * e1**3 / 96) * math.sin(6 * mu)
        + (1097 * e1**4 / 512) * math.sin(8 * mu)
    )
    n1 = a / math.sqrt(1 - e2 * math.sin(phi1) ** 2)
    t1 = math.tan(phi1) ** 2
    c1 = ep2 * math.cos(phi1) ** 2
    r1 = a * (1 - e2) / (1 - e2 * math.sin(phi1) ** 2) ** 1.5
    d = x0 / (n1 * k0)
    lat = phi1 - (n1 * math.tan(phi1) / r1) * (
        d**2 / 2
        - (5 + 3 * t1 + 10 * c1 - 4 * c1**2 - 9 * ep2) * d**4 / 24
        + (61 + 90 * t1 + 298 * c1 + 45 * t1**2 - 252 * ep2 - 3 * c1**2) * d**6 / 720
    )
    lon0 = math.radians((zone - 1) * 6 - 180 + 3)
    lon = lon0 + (
        d
        - (1 + 2 * t1 + c1) * d**3 / 6
        + (5 - 2 * c1 + 28 * t1 - 3 * c1**2 + 8 * ep2 + 24 * t1**2) * d**5 / 120
    ) / math.cos(phi1)
    return math.degrees(lat), math.degrees(lon)


def point_in_polygon(point: Point, polygon: Sequence[Point]) -> bool:
    inside = False
    j = len(polygon) - 1
    for i, pi in enumerate(polygon):
        pj = polygon[j]
        if (pi.y > point.y) != (pj.y > point.y):
            x_intersection = (pj.x - pi.x) * (point.y - pi.y) / (pj.y - pi.y) + pi.x
            if point.x < x_intersection:
                inside = not inside
        j = i
    return inside


def distance_to_segment(point: Point, a: Point, b: Point) -> float:
    dx = b.x - a.x
    dy = b.y - a.y
    if dx == 0 and dy == 0:
        return math.hypot(point.x - a.x, point.y - a.y)
    t = max(0.0, min(1.0, ((point.x - a.x) * dx + (point.y - a.y) * dy) / (dx * dx + dy * dy)))
    projection = Point(a.x + t * dx, a.y + t * dy)
    return math.hypot(point.x - projection.x, point.y - projection.y)


def distance_to_polygon(point: Point, polygon: Sequence[Point]) -> float:
    ring = list(polygon)
    return min(distance_to_segment(point, ring[i], ring[(i + 1) % len(ring)]) for i in range(len(ring)))


def parse_dart(path: Path) -> list[DartRow]:
    rows: list[DartRow] = []
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        parts = line.split()
        if len(parts) != 11:
            raise ValueError(f"Unexpected DART row with {len(parts)} columns: {line}")
        julian = float(parts[0])
        year, month, day, hour, minute, second = map(int, parts[1:7])
        rows.append(
            DartRow(
                timestamp_utc=datetime(year, month, day, hour, minute, second, tzinfo=UTC),
                julian_day=julian,
                raw_observation_m=clean_dart_value(float(parts[7])),
                fitted_tide_m=clean_dart_value(float(parts[8])),
                residual_m=clean_dart_value(float(parts[9])),
                event_meter_column=clean_dart_value(float(parts[10])),
            )
        )
    return rows


def clean_dart_value(value: float) -> float:
    if abs(value) >= 9999.0:
        return math.nan
    return value


def classify_station(
    *,
    latitude: float,
    longitude: float,
    quantity: str,
    polygon: Sequence[Point],
    model_outputs: Sequence[str],
) -> dict[str, Any]:
    point = wgs84_to_utm54n(latitude, longitude)
    inside = point_in_polygon(point, polygon)
    distance_m = 0.0 if inside else distance_to_polygon(point, polygon)
    quantity_lower = quantity.lower()
    equivalent_eta = "eta_timeseries_at_station" in model_outputs and any(token in quantity_lower for token in ["deep ocean", "tide", "water level", "waveform"])
    if inside and equivalent_eta:
        eligibility = "DIRECT"
        reason = "Station lies in the model corridor and an equivalent eta(t) output is available."
    elif inside:
        eligibility = "PROXY"
        reason = "Station lies in the model corridor, but the observation quantity is not equivalent to an existing model output."
    else:
        eligibility = "TARGET_ONLY"
        reason = "Station lies outside the h400 delivery corridor or requires physics/output not available from the frozen result."
    return {
        "projected_m": {"x": point.x, "y": point.y},
        "inside_h400_corridor": inside,
        "distance_to_corridor_m": distance_m,
        "eligibility": eligibility,
        "eligibility_reason": reason,
    }


def load_corridor(path: Path) -> dict[str, Any]:
    corridor = read_json(path)
    polygon = [Point(float(item["x"]), float(item["y"])) for item in corridor["corridor"]["polygon_projected_m"][:-1]]
    wgs84_polygon = [utm54n_to_wgs84(point.x, point.y) for point in polygon]
    return {**corridor, "polygon_points": polygon, "polygon_wgs84": wgs84_polygon}


def read_noaa_features(path: Path) -> list[dict[str, Any]]:
    return json.loads(path.read_text(encoding="utf-8")).get("features", [])


def build_observation_register(external_root: Path, corridor: Mapping[str, Any]) -> tuple[list[dict[str, Any]], dict[str, Any]]:
    raw = external_root / "raw"
    polygon = corridor["polygon_points"]
    register: list[dict[str, Any]] = []
    model_outputs = ["corridor_eta_field", "coupling_section_eta", "coupling_section_qn"]

    dart = {
        "observation_id": "NOAA_NCEI_DART_21418",
        "name": "D21418 BPR, NE Tokyo, Japan",
        "authority": "NOAA/NCEI and NDBC",
        "source_url": "https://www.ngdc.noaa.gov/hazard/data/DART/20110311_honshu/dart21418_20110301to20110320_meter.txt",
        "raw_file": (raw / "ncei_dart21418_20110301to20110320_meter.txt").as_posix(),
        "latitude": 38.71,
        "longitude": 148.67,
        "quantity": "Deep ocean gauge bottom-pressure residual waveform",
        "data_status": "RAW_OBSERVED_WAVEFORM_AVAILABLE",
        "comparison_role": "open-ocean target outside Kamaishi corridor",
    }
    dart.update(classify_station(latitude=dart["latitude"], longitude=dart["longitude"], quantity=dart["quantity"], polygon=polygon, model_outputs=model_outputs))
    register.append(dart)

    tide_features = read_noaa_features(raw / "noaa_ncei_tide_deep_gauge_observations_event_5413.geojson")
    all_features = read_noaa_features(raw / "noaa_ncei_tsunami_observations_event_5413.geojson")
    wanted_names = {"KAMAISHI", "MIYAKO", "OFUNATO"}
    for feature in tide_features:
        props = feature["properties"]
        name = props.get("LOCATION_NAME") or ""
        if name not in wanted_names:
            continue
        item = {
            "observation_id": f"NOAA_NCEI_TIDE_{int(props['ID'])}",
            "name": name,
            "authority": "NOAA/NCEI hazards event catalogue",
            "source_url": "https://gis.ngdc.noaa.gov/arcgis/rest/services/web_mercator/hazards/MapServer/2",
            "raw_file": (raw / "noaa_ncei_tide_deep_gauge_observations_event_5413.geojson").as_posix(),
            "latitude": props.get("LATITUDE"),
            "longitude": props.get("LONGITUDE"),
            "quantity": props.get("TYPE_MEASUREMENT"),
            "catalogue_water_height_m": props.get("RUNUP_HT"),
            "arrival_utc": catalogue_arrival(props),
            "data_status": "CATALOGUE_POINT_AVAILABLE_NO_RAW_WAVEFORM",
            "comparison_role": "nearshore gauge target; raw waveform unavailable in R15 raw official sources",
        }
        item.update(classify_station(latitude=item["latitude"], longitude=item["longitude"], quantity=str(item["quantity"]), polygon=polygon, model_outputs=model_outputs))
        register.append(item)

    kamaishi_runup = []
    for feature in all_features:
        props = feature["properties"]
        lat = props.get("LATITUDE")
        lon = props.get("LONGITUDE")
        if lat is None or lon is None:
            continue
        if 39.15 <= float(lat) <= 39.38 and 141.82 <= float(lon) <= 141.95 and props.get("RUNUP_HT") is not None:
            kamaishi_runup.append(props)
    kamaishi_runup = sorted(kamaishi_runup, key=lambda p: float(p.get("RUNUP_HT") or 0), reverse=True)[:24]
    for props in kamaishi_runup:
        item = {
            "observation_id": f"NOAA_NCEI_SURVEY_{int(props['ID'])}",
            "name": props.get("LOCATION_NAME") or "Iwate/Kamaishi area",
            "authority": "NOAA/NCEI hazards event catalogue",
            "source_url": "https://gis.ngdc.noaa.gov/arcgis/rest/services/web_mercator/hazards/MapServer/4",
            "raw_file": (raw / "noaa_ncei_tsunami_observations_event_5413.geojson").as_posix(),
            "latitude": props.get("LATITUDE"),
            "longitude": props.get("LONGITUDE"),
            "quantity": props.get("TYPE_MEASUREMENT"),
            "catalogue_water_height_m": props.get("RUNUP_HT"),
            "catalogue_inundation_distance_m": props.get("RUNUP_HORIZ"),
            "data_status": "CATALOGUE_POINT_AVAILABLE",
            "comparison_role": "coastal/run-up target; incompatible with offshore Regional2D h400 output",
        }
        item.update(classify_station(latitude=item["latitude"], longitude=item["longitude"], quantity=str(item["quantity"]), polygon=polygon, model_outputs=model_outputs))
        if item["inside_h400_corridor"]:
            item["eligibility"] = "PROXY"
            item["eligibility_reason"] = "Point is geographically in the delivery corridor but the observed run-up/inundation quantity is not represented by the offshore h400 output."
        register.append(item)

    nowphas = {
        "observation_id": "PARI_NOWPHAS_802G_KAMAISHI_OFFSHORE",
        "name": "NOWPHAS 802G South Iwate/Kamaishi offshore GPS buoy",
        "authority": "PARI/MLIT NOWPHAS",
        "source_url": "https://www.pari.go.jp/en/2011/12/20111213142341.html",
        "raw_file": (raw / "pari_nowphas_tohoku_report.pdf").as_posix(),
        "latitude": 39 + 15 / 60 + 31 / 3600,
        "longitude": 142 + 5 / 60 + 49 / 3600,
        "quantity": "GPS-buoy offshore tsunami waveform and reported crests",
        "water_depth_m": 204,
        "source_detail": "PARI PDF table 3.1 lists station 802G, Iwate South/Kamaishi offshore, 39 deg 15 min 31 sec N, 142 deg 05 min 49 sec E.",
        "data_status": "OFFICIAL_LOCATION_AND_REPORT_PRESERVED_RAW_TIME_SERIES_NOT_OBTAINED",
        "comparison_role": "priority Kamaishi offshore validation target; outside frozen h400 corridor",
    }
    nowphas.update(classify_station(latitude=nowphas["latitude"], longitude=nowphas["longitude"], quantity=nowphas["quantity"], polygon=polygon, model_outputs=model_outputs))
    register.append(nowphas)

    summary = {
        "register_count": len(register),
        "direct_count": sum(1 for item in register if item["eligibility"] == "DIRECT"),
        "proxy_count": sum(1 for item in register if item["eligibility"] == "PROXY"),
        "target_only_count": sum(1 for item in register if item["eligibility"] == "TARGET_ONLY"),
        "raw_sources": raw_source_manifest(raw),
        "earthquake_origin": {
            "utc": EVENT_UTC.isoformat().replace("+00:00", "Z"),
            "jst": EVENT_UTC.astimezone(JST).isoformat(),
            "source": "NOAA/NCEI event summary and project USGS corridor authority",
        },
        "comparison_labels": CLAIM_LABELS,
    }
    return register, summary


def catalogue_arrival(props: Mapping[str, Any]) -> str | None:
    hour = props.get("ARR_HOUR")
    minute = props.get("ARR_MIN")
    day = props.get("ARR_DAY") or 11
    if hour is None or minute is None:
        return None
    return datetime(2011, 3, int(day), int(hour), int(minute), tzinfo=UTC).isoformat().replace("+00:00", "Z")


def raw_source_manifest(raw: Path) -> list[dict[str, Any]]:
    return [
        {
            "path": path.as_posix(),
            "sha256": sha256(path),
            "bytes": path.stat().st_size,
        }
        for path in sorted(raw.glob("*"))
        if path.is_file()
    ]


def write_register_outputs(register: Sequence[Mapping[str, Any]], summary: Mapping[str, Any], external_root: Path, docs_root: Path) -> None:
    metadata = external_root / "metadata"
    write_json(metadata / "observation_register.json", {"summary": summary, "observations": list(register)})
    write_json(docs_root / "regional2d_r15_observation_register.json", {"summary": summary, "observations": list(register)})
    fieldnames = sorted({key for item in register for key in item.keys() if not isinstance(item.get(key), (dict, list))})
    for path in [metadata / "observation_register.csv", docs_root / "regional2d_r15_observation_register.csv"]:
        path.parent.mkdir(parents=True, exist_ok=True)
        with path.open("w", newline="", encoding="utf-8") as handle:
            writer = csv.DictWriter(handle, fieldnames=fieldnames, lineterminator="\n")
            writer.writeheader()
            for item in register:
                writer.writerow({key: item.get(key) for key in fieldnames})
    md = [
        "# R15 Tohoku Observation Register",
        "",
        f"Generated: {utc_now()}",
        "",
        f"Direct comparisons available: **{summary['direct_count']}**.",
        "",
        "| Observation | Authority | Status | Eligibility | Reason |",
        "|---|---|---|---|---|",
    ]
    for item in register:
        md.append(f"| {item['observation_id']} | {item['authority']} | {item['data_status']} | {item['eligibility']} | {item['eligibility_reason']} |")
    text = "\n".join(md) + "\n"
    (metadata / "observation_register.md").write_text(text, encoding="utf-8")
    (docs_root / "regional2d_r15_observation_register.md").write_text(text, encoding="utf-8")


def process_dart(external_root: Path) -> dict[str, Any]:
    path = external_root / "raw/ncei_dart21418_20110301to20110320_meter.txt"
    rows = parse_dart(path)
    window = [row for row in rows if timedelta(hours=-2) <= row.timestamp_utc - EVENT_UTC <= timedelta(hours=12)]
    processed = external_root / "processed/dart21418_event_window.csv"
    processed.parent.mkdir(parents=True, exist_ok=True)
    with processed.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle, lineterminator="\n")
        writer.writerow(["timestamp_utc", "elapsed_hours", "raw_observation_m", "fitted_tide_m", "residual_m", "event_meter_column"])
        for row in window:
            writer.writerow([
                row.timestamp_utc.isoformat().replace("+00:00", "Z"),
                (row.timestamp_utc - EVENT_UTC).total_seconds() / 3600,
                row.raw_observation_m,
                row.fitted_tide_m,
                row.residual_m,
                row.event_meter_column,
            ])
    valid_residual_rows = [row for row in window if math.isfinite(row.residual_m)]
    residuals = [row.residual_m for row in valid_residual_rows]
    peak = max(valid_residual_rows, key=lambda row: abs(row.residual_m))
    return {
        "source": path.as_posix(),
        "source_sha256": sha256(path),
        "processed_csv": processed.as_posix(),
        "processed_sha256": sha256(processed),
        "sample_count_total": len(rows),
        "sample_count_window": len(window),
        "missing_residual_count_window": len(window) - len(valid_residual_rows),
        "window_utc": {
            "start": window[0].timestamp_utc.isoformat().replace("+00:00", "Z"),
            "end": window[-1].timestamp_utc.isoformat().replace("+00:00", "Z"),
        },
        "residual_min_m": min(residuals),
        "residual_max_m": max(residuals),
        "largest_absolute_residual_m": peak.residual_m,
        "largest_absolute_residual_utc": peak.timestamp_utc.isoformat().replace("+00:00", "Z"),
        "largest_absolute_residual_elapsed_hours": (peak.timestamp_utc - EVENT_UTC).total_seconds() / 3600,
    }


def configure_matplotlib():
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    if STYLE_PATH.is_file():
        plt.style.use(STYLE_PATH.as_posix())
    return plt


def save_figure(fig: Any, basename: str, figure_root: Path, external_root: Path, provenance: Mapping[str, Any]) -> list[str]:
    outputs: list[str] = []
    for suffix in [".svg", ".pdf", ".png"]:
        target = figure_root / f"{basename}{suffix}"
        target.parent.mkdir(parents=True, exist_ok=True)
        fig.savefig(target)
        if suffix == ".svg":
            normalize_text_file(target)
        outputs.append(target.as_posix())
    write_json(figure_root / f"{basename}.provenance.json", {**provenance, "outputs": outputs, "generated_at_utc": utc_now(), "git_sha": git_sha()})
    external_plot = external_root / "plots" / f"{basename}.svg"
    external_plot.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(figure_root / f"{basename}.svg", external_plot)
    return outputs


def normalize_text_file(path: Path) -> None:
    lines = path.read_text(encoding="utf-8").splitlines()
    path.write_text("\n".join(line.rstrip() for line in lines) + "\n", encoding="utf-8")


def plot_station_domain_map(register: Sequence[Mapping[str, Any]], corridor: Mapping[str, Any], figure_root: Path, external_root: Path) -> list[str]:
    plt = configure_matplotlib()
    fig, ax = plt.subplots(figsize=(6.7, 5.0))
    ring = corridor["polygon_wgs84"] + [corridor["polygon_wgs84"][0]]
    ax.plot([lon for lat, lon in ring], [lat for lat, lon in ring], color="#2f5d8c", lw=1.8, label="h400 delivery corridor")
    event = corridor["event"]
    ax.scatter(event["epicentre_wgs84"]["longitude"], event["epicentre_wgs84"]["latitude"], marker="*", s=110, color="#c83f35", label="USGS/project epicentre")
    ax.scatter(event["kamaishi_proxy_wgs84"]["longitude"], event["kamaishi_proxy_wgs84"]["latitude"], marker="s", s=42, color="#525252", label="Kamaishi proxy")
    colors = {"DIRECT": "#1b9e77", "PROXY": "#d95f02", "TARGET_ONLY": "#7570b3"}
    markers = {"NOAA_NCEI_DART_21418": "D", "PARI_NOWPHAS_TOHOKU_GPS_BUOYS": "P"}
    labelled: set[str] = set()
    for item in register:
        lat = item.get("latitude")
        lon = item.get("longitude")
        if lat is None or lon is None:
            continue
        eligibility = item["eligibility"]
        label = eligibility if eligibility not in labelled else None
        ax.scatter(lon, lat, s=28, color=colors[eligibility], marker=markers.get(item["observation_id"], "o"), alpha=0.86, label=label)
        labelled.add(eligibility)
    ax.set_xlabel("Longitude (deg E)")
    ax.set_ylabel("Latitude (deg N)")
    ax.set_title("R15 observation geometry gate")
    ax.set_xlim(141.45, 149.05)
    ax.set_ylim(38.05, 39.85)
    ax.legend(loc="upper right", fontsize=7)
    ax.text(141.52, 38.12, "\n".join(CLAIM_LABELS), fontsize=7.2, color="#333333")
    outputs = save_figure(
        fig,
        "validation_station_domain_map",
        figure_root,
        external_root,
        {
            "schema": {"name": "tsunami.r15.figure_provenance", "version": "1.0.0"},
            "data_class": "OBSERVATION_GEOMETRY_GATE",
            "source_files": [CORRIDOR_PATH.as_posix(), (external_root / "metadata/observation_register.json").as_posix()],
        },
    )
    plt.close(fig)
    return outputs


def plot_dart_waveform(external_root: Path, figure_root: Path, dart_summary: Mapping[str, Any]) -> list[str]:
    plt = configure_matplotlib()
    rows = []
    with (external_root / "processed/dart21418_event_window.csv").open(encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        for row in reader:
            residual = float(row["residual_m"])
            if math.isfinite(residual):
                rows.append((float(row["elapsed_hours"]), residual))
    fig, ax = plt.subplots(figsize=(6.7, 3.4))
    ax.plot([x for x, _ in rows], [y for _, y in rows], color="#2c6b8e")
    ax.axvline(0, color="#c83f35", lw=1.0, label="earthquake origin")
    ax.set_xlabel("Elapsed time from 2011-03-11 05:46:23 UTC (hours)")
    ax.set_ylabel("DART residual (m)")
    ax.set_title("NOAA/NCEI DART 21418 observed residual waveform")
    ax.legend(loc="upper right")
    ax.text(0.02, 0.05, "REAL OBSERVATION ONLY\nnot a direct h400 comparison", transform=ax.transAxes, fontsize=7.5)
    outputs = save_figure(
        fig,
        "dart21418_observed_waveform",
        figure_root,
        external_root,
        {
            "schema": {"name": "tsunami.r15.figure_provenance", "version": "1.0.0"},
            "data_class": "REAL_OBSERVATION_NO_DIRECT_COMPARISON",
            "source_files": [dart_summary["source"], dart_summary["processed_csv"]],
            "source_sha256": {dart_summary["source"]: dart_summary["source_sha256"], dart_summary["processed_csv"]: dart_summary["processed_sha256"]},
        },
    )
    plt.close(fig)
    return outputs


def plot_validation_status(summary: Mapping[str, Any], figure_root: Path, external_root: Path) -> list[str]:
    plt = configure_matplotlib()
    labels = ["DIRECT", "PROXY", "TARGET_ONLY"]
    values = [summary["direct_count"], summary["proxy_count"], summary["target_only_count"]]
    colors = ["#1b9e77", "#d95f02", "#7570b3"]
    fig, ax = plt.subplots(figsize=(5.5, 3.4))
    ax.bar(labels, values, color=colors)
    ax.set_ylabel("Registered observations")
    ax.set_title("R15 historical-validation evidence status")
    ax.text(0.02, 0.92, "No direct quantitative validation claimed", transform=ax.transAxes, fontsize=8.2, weight="bold")
    outputs = save_figure(
        fig,
        "validation_framework_status",
        figure_root,
        external_root,
        {
            "schema": {"name": "tsunami.r15.figure_provenance", "version": "1.0.0"},
            "data_class": "VALIDATION_FRAMEWORK_STATUS",
            "source_files": [(external_root / "metadata/observation_register.json").as_posix()],
        },
    )
    plt.close(fig)
    return outputs


def write_validation_docs(summary: Mapping[str, Any], dart_summary: Mapping[str, Any], docs_root: Path, external_root: Path) -> None:
    framework = {
        "schema": {"name": "tsunami.r15.validation_framework", "version": "1.0.0"},
        "status": "VALIDATION_FRAMEWORK_COMPLETE_DIRECT_COMPARISON_NOT_AVAILABLE",
        "comparison_labels": CLAIM_LABELS,
        "scientific_authority_frozen": {
            "regional_authority": "R10 h400 limited_linear; BEST_AVAILABLE_NUMERICALLY_UNCERTAIN",
            "historical_validation": "NOT_CLAIMED",
            "spatial_qualification": "NOT_FULLY_QUALIFIED",
            "terrain_diagnosis": "TERRAIN_SOURCE_FIDELITY_DOMINANT, confidence MODERATE; PROJECTION_FIDELITY_CEILING",
            "formal_order": ["GLOBAL_FIRST_ORDER_VERIFIED", "SECOND_ORDER_VERIFIED"],
        },
        "observation_summary": dict(summary),
        "dart21418_observed_waveform": dict(dart_summary),
    }
    write_json(docs_root / "regional2d_r15_validation_framework.json", framework)
    md = [
        "# R15 Historical-Validation Framework",
        "",
        "Status: **VALIDATION_FRAMEWORK_COMPLETE_DIRECT_COMPARISON_NOT_AVAILABLE**.",
        "",
        "No arbitrary time shift, amplitude scaling or manual alignment was applied.",
        "",
        "The R15 evidence package registers official observations and preserves raw inputs, but it does not make a calibrated or direct historical-validation claim for the frozen R10 h400 result.",
        "",
        "Required labels:",
        "",
        f"- {CLAIM_LABELS[0]}",
        f"- {CLAIM_LABELS[1]}",
    ]
    (docs_root / "regional2d_r15_validation_framework.md").write_text("\n".join(md) + "\n", encoding="utf-8")
    (docs_root / "r15_afternoon_decision.md").write_text(
        "\n".join(
            [
                "# R15 Afternoon Decision",
                "",
                "Decision: **do not claim direct historical validation**.",
                "",
                f"Direct eligible observations found: `{summary['direct_count']}`.",
                "",
                "DART 21418 is outside the h400 Kamaishi delivery corridor and cannot be treated as direct evidence from the frozen output. Kamaishi/Miyako/Ofunato catalogue points and Kamaishi-area run-up observations are real validation targets, but require raw nearshore waveform access, harbour/inundation physics, or additional model outputs not available under R15 constraints.",
                "",
                "The poster should use the R15 station/domain map, the real DART observed waveform, and the validation-framework status figure with the mandatory uncalibrated/numerically uncertain labels.",
            ]
        )
        + "\n",
        encoding="utf-8",
    )


def write_comparison_gate(external_root: Path, docs_root: Path, summary: Mapping[str, Any]) -> None:
    payload = {
        "schema": {"name": "tsunami.r15.comparison_gate", "version": "1.0.0"},
        "status": "NOT_APPLICABLE",
        "direct_observation_count": summary["direct_count"],
        "reason": "No registered observation is both in the frozen h400 corridor and equivalent to an existing station eta(t) output.",
        "metrics_written": False,
        "prohibited_adjustments": ["no_time_shift", "no_amplitude_scaling", "no_manual_alignment"],
    }
    write_json(external_root / "comparisons/comparison_metrics_not_applicable.json", payload)
    write_json(docs_root / "regional2d_r15_comparison_gate.json", payload)
    (docs_root / "regional2d_r15_comparison_gate.md").write_text(
        "# R15 Direct-Comparison Gate\n\n"
        "Status: **NOT_APPLICABLE**.\n\n"
        "No `comparison_metrics.csv` was generated because the DIRECT geometry/equivalence gate did not pass.\n",
        encoding="utf-8",
    )


def write_local3d_closure(docs_root: Path) -> dict[str, Any]:
    bounded = read_json(R14_BOUNDEDNESS)
    payload = {
        "schema": {"name": "tsunami.r15.local3d_closure_inspection", "version": "1.0.0"},
        "status": "COMPLETE",
        "source": R14_BOUNDEDNESS.as_posix(),
        "source_sha256": sha256(R14_BOUNDEDNESS),
        "classification_preserved": bounded["classification"],
        "full_replay_gate": bounded["full_replay_gate"],
        "alpha_acceptance_interval": [-5e-05, 1.00005],
        "inspection_scope": "Existing R14/R14B evidence only; no OpenFOAM retuning, rerun or solver modification.",
        "finding": bounded["root_cause"],
    }
    write_json(docs_root / "regional2d_r15_local3d_closure_inspection.json", payload)
    (docs_root / "regional2d_r15_local3d_closure_inspection.md").write_text(
        "# R15 Local3D Closure Inspection\n\n"
        f"Status: **{payload['status']}**.\n\n"
        f"Classification preserved: `{payload['classification_preserved']}`. Full replay gate remains `{payload['full_replay_gate']}`.\n\n"
        "No OpenFOAM retuning, VOF/MULES change, turbulence change, Local3D solver work or new replay execution was performed.\n",
        encoding="utf-8",
    )
    return payload


def write_video_package(docs_root: Path) -> dict[str, Any]:
    VIDEO_ROOT.mkdir(parents=True, exist_ok=True)
    target = VIDEO_ROOT / "hybrid_g6_poster.mp4"
    shutil.copy2(R14_VIDEO, target)
    qr = read_json(R14_QR)
    provenance = read_json(R14_VIDEO_PROVENANCE)
    payload = {
        "schema": {"name": "tsunami.r15.video_qr_metadata", "version": "1.0.0"},
        "status": "ACCEPTED_G6_VIDEO_REUSED",
        "video_path": target.as_posix(),
        "sha256": sha256(target),
        "source_video": R14_VIDEO.as_posix(),
        "source_sha256": sha256(R14_VIDEO),
        "qr_status": qr["status"],
        "r14_video_provenance": provenance,
        "note": "R15 retains the accepted R14B G6 Local3D preview because current h400 Local3D replay remains unresolved.",
    }
    write_json(VIDEO_ROOT / "video_qr_metadata.json", payload)
    write_json(docs_root / "regional2d_r15_video_qr_metadata.json", payload)
    return payload


def write_ancestry_and_integration(docs_root: Path) -> None:
    rows = [
        ("G6/R14B", "Hybrid architecture", "DEMONSTRATED_THROUGH_G6", "accepted G6 Local3D preview retained"),
        ("R7/R8", "Global first-order verification", "GLOBAL_FIRST_ORDER_VERIFIED", "status frozen"),
        ("R9/R10", "Limited-linear second-order verification", "SECOND_ORDER_VERIFIED", "status frozen"),
        ("R10", "Real-event h400 Regional result", "BEST_AVAILABLE_NUMERICALLY_UNCERTAIN", "not spatially qualified, not calibrated, not historically validated"),
        ("R12/R13", "Regional fidelity limit", "PROJECTION_FIDELITY_CEILING", "terrain/source fidelity dominant, confidence MODERATE"),
        ("R14/R14B", "Current Local3D replay", "REPLAY_VOF_BEHAVIOUR_UNRESOLVED", "full 300 s current-generation replay gate closed"),
        ("R15", "Historical observation evidence", "VALIDATION_FRAMEWORK_COMPLETE_DIRECT_COMPARISON_NOT_AVAILABLE", "official observations registered; no direct claim"),
    ]
    csv_path = docs_root / "regional2d_r15_ancestry_capability_matrix.csv"
    with csv_path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle, lineterminator="\n")
        writer.writerow(["source", "capability", "status", "poster_use"])
        writer.writerows(rows)
    md = ["# R15 Ancestry and Capability Matrix", "", "| Source | Capability | Status | Poster use |", "|---|---|---|---|"]
    for row in rows:
        md.append("| " + " | ".join(row) + " |")
    (docs_root / "regional2d_r15_ancestry_capability_matrix.md").write_text("\n".join(md) + "\n", encoding="utf-8")
    write_json(docs_root / "regional2d_r15_ancestry_capability_matrix.json", {"rows": [dict(zip(["source", "capability", "status", "poster_use"], row)) for row in rows]})
    (docs_root / "r15_integration_plan.md").write_text(
        "# R15 Integration Plan\n\n"
        "Use R15 as the repository freeze layer for poster evidence. Keep R10 h400 as the current Regional authority with explicit uncertainty labels, reuse R14B accepted G6 video evidence, and route future validation work through acquisition of authoritative NOWPHAS/Kamaishi raw waveforms or a model output that can satisfy the DIRECT comparison gate.\n\n"
        "Do not merge historical-validation language into scientific conclusions as a validation result; present it as an observation framework and evidence register.\n",
        encoding="utf-8",
    )


def write_poster_manifest(
    docs_root: Path,
    figure_outputs: Mapping[str, Sequence[str]],
    video_payload: Mapping[str, Any],
    local3d_payload: Mapping[str, Any],
) -> None:
    r14_assets = read_json(R14_POSTER_SHORTLIST)["assets"]
    assets = [
        {
            "path": "deliverables/figures/r15_validation/publication/validation_station_domain_map.svg",
            "caption": "Official observation targets classified against the frozen h400 Kamaishi delivery corridor.",
            "claim_status": "VALIDATION FRAMEWORK",
            "caveat": "No direct historical-validation claim.",
        },
        {
            "path": "deliverables/figures/r15_validation/publication/dart21418_observed_waveform.svg",
            "caption": "NOAA/NCEI DART 21418 observed tsunami residual waveform.",
            "claim_status": "REAL OBSERVATION",
            "caveat": "Outside the h400 corridor; not a direct model comparison.",
        },
        {
            "path": "deliverables/figures/r15_validation/publication/validation_framework_status.svg",
            "caption": "R15 observation-register eligibility status.",
            "claim_status": "VALIDATION FRAMEWORK",
            "caveat": "Direct comparison gate did not pass.",
        },
    ]
    manifest = {
        "schema": {"name": "tsunami.r15.poster_evidence_manifest", "version": "1.0.0"},
        "generated_at_utc": utc_now(),
        "mandatory_labels": CLAIM_LABELS,
        "r15_assets": assets,
        "retained_r14_assets": r14_assets,
        "video": dict(video_payload),
        "local3d_closure": dict(local3d_payload),
        "figure_outputs": {key: list(value) for key, value in figure_outputs.items()},
    }
    write_json(docs_root / "regional2d_r15_poster_evidence_manifest.json", manifest)
    md = ["# R15 Poster Evidence Manifest", "", "Mandatory labels:", "", f"- {CLAIM_LABELS[0]}", f"- {CLAIM_LABELS[1]}", "", "| Asset | Claim status | Caveat |", "|---|---|---|"]
    for asset in assets:
        md.append(f"| {asset['path']} | {asset['claim_status']} | {asset['caveat']} |")
    md.append(f"| {video_payload['video_path']} | DEMONSTRATED G6 HYBRID REPLAY | Current h400 replay remains unresolved. |")
    (docs_root / "regional2d_r15_poster_evidence_manifest.md").write_text("\n".join(md) + "\n", encoding="utf-8")


def update_state(external_root: Path, statuses: Mapping[str, str]) -> None:
    invalid = {status for status in statuses.values() if status not in ALLOWED_STATE}
    if invalid:
        raise ValueError(f"Invalid R15 state values: {sorted(invalid)}")
    payload = {
        "schema": {"name": "tsunami.r15.state", "version": "1.0.0"},
        "updated_utc": utc_now(),
        "starting_git_sha": "d7139c361b0fcee0aca3e1fce5d2cb8c044351c8",
        "current_git_sha": git_sha(),
        "lanes": dict(statuses),
        "last_checkpoint": "r15_evidence_package_generated",
    }
    write_json(external_root / "state/r15_state.json", payload)


def generate(args: argparse.Namespace) -> dict[str, Any]:
    external_root = args.external_root
    docs_root = args.docs_root
    figure_root = args.figure_root
    corridor = load_corridor(CORRIDOR_PATH)
    register, summary = build_observation_register(external_root, corridor)
    write_register_outputs(register, summary, external_root, docs_root)
    dart_summary = process_dart(external_root)
    figure_outputs = {
        "station_domain_map": plot_station_domain_map(register, corridor, figure_root, external_root),
        "dart21418_observed_waveform": plot_dart_waveform(external_root, figure_root, dart_summary),
        "validation_framework_status": plot_validation_status(summary, figure_root, external_root),
    }
    write_validation_docs(summary, dart_summary, docs_root, external_root)
    write_comparison_gate(external_root, docs_root, summary)
    local3d_payload = write_local3d_closure(docs_root)
    video_payload = write_video_package(docs_root)
    write_ancestry_and_integration(docs_root)
    write_poster_manifest(docs_root, figure_outputs, video_payload, local3d_payload)
    handoff = {
        "schema": {"name": "tsunami.r15.handoff_manifest", "version": "1.0.0"},
        "generated_at_utc": utc_now(),
        "status": "COMPLETE",
        "direct_comparison_status": "NOT_APPLICABLE",
        "observation_register": (docs_root / "regional2d_r15_observation_register.json").as_posix(),
        "validation_framework": (docs_root / "regional2d_r15_validation_framework.json").as_posix(),
        "poster_manifest": (docs_root / "regional2d_r15_poster_evidence_manifest.json").as_posix(),
        "external_root": external_root.as_posix(),
        "scientific_statuses_preserved": [
            "MODEL_CONSISTENT_WITH_DOCUMENTATION_FIXES",
            "GLOBAL_FIRST_ORDER_VERIFIED",
            "SECOND_ORDER_VERIFIED",
            "TERRAIN_SOURCE_FIDELITY_DOMINANT",
            "PROJECTION_FIDELITY_CEILING",
            "REPLAY_VOF_BEHAVIOUR_UNRESOLVED",
        ],
        "prohibited_scope_confirmed_absent": [
            "new Regional mesh refinement",
            "600 s solve",
            "h250 production",
            "temporal convergence",
            "native C++ HDF5 writer",
            "OpenFOAM retuning",
        ],
    }
    write_json(docs_root / "regional2d_r15_handoff_manifest.json", handoff)
    update_state(
        external_root,
        {
            "historical_validation": "COMPLETE",
            "research_plots": "COMPLETE",
            "hybrid_video_qr": "COMPLETE",
            "evidence_freeze_handoff": "COMPLETE",
            "integration_ancestry_audit": "COMPLETE",
            "local3d_closure_inspection": "COMPLETE",
            "direct_comparison": "NOT_APPLICABLE",
        },
    )
    return handoff


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--external-root", type=Path, default=EXTERNAL_ROOT)
    parser.add_argument("--docs-root", type=Path, default=REPO_DOCS)
    parser.add_argument("--figure-root", type=Path, default=FIGURE_ROOT)
    args = parser.parse_args(argv)
    payload = generate(args)
    print(json.dumps(payload, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
