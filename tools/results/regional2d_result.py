#!/usr/bin/env python3
"""Regional2D scientific result HDF5 schema, adapters and fixtures."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import sys
from dataclasses import dataclass
from datetime import UTC, datetime
from pathlib import Path
from typing import Any, Mapping, Protocol, Sequence

import h5py
import numpy as np


SCHEMA_NAME = "tsunami.regional2d.result"
SCHEMA_VERSION = "1.0.0"
SUPPORTED_MAJOR_VERSION = 1
DATA_CLASSES = {"REAL", "LEGACY_CONVERTED", "SYNTHETIC"}


def utc_now() -> str:
    return datetime.now(UTC).isoformat().replace("+00:00", "Z")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def git_sha() -> str | None:
    import subprocess

    try:
        completed = subprocess.run(
            ["git", "rev-parse", "HEAD"],
            cwd=repo_root(),
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
        )
    except OSError:
        return None
    return completed.stdout.strip() if completed.returncode == 0 else None


@dataclass(frozen=True)
class Regional2DResult:
    metadata: dict[str, Any]
    points: np.ndarray
    connectivity: np.ndarray
    cell_centres: np.ndarray
    bed_elevation: np.ndarray
    region_tags: np.ndarray
    boundary_tags: np.ndarray
    time_values: np.ndarray
    h: np.ndarray
    qx: np.ndarray
    qy: np.ndarray
    coupling: dict[str, np.ndarray]
    diagnostics: dict[str, np.ndarray]
    provenance: dict[str, Any]


class ResultDataset(Protocol):
    """Small format-neutral result access protocol for plotting tools."""

    def metadata(self) -> dict[str, Any]: ...

    def mesh(self) -> dict[str, np.ndarray]: ...

    def times(self) -> np.ndarray: ...

    def field(self, name: str, time: int | float) -> np.ndarray: ...

    def coupling_field(self, name: str) -> np.ndarray: ...

    def coupling_series(self, name: str) -> np.ndarray: ...

    def provenance(self) -> dict[str, Any]: ...


def _json_attr(value: Mapping[str, Any]) -> str:
    return json.dumps(value, sort_keys=True, separators=(",", ":"))


def _read_json_attr(obj: h5py.AttributeManager, key: str) -> dict[str, Any]:
    value = obj[key]
    if isinstance(value, bytes):
        value = value.decode("utf-8")
    return json.loads(str(value))


def _write_dataset(group: h5py.Group, name: str, data: np.ndarray, units: str, *, appendable: bool = False) -> h5py.Dataset:
    array = np.asarray(data)
    kwargs: dict[str, Any] = {}
    if appendable and array.ndim >= 1:
        kwargs["maxshape"] = (None, *array.shape[1:])
        kwargs["chunks"] = (max(1, min(array.shape[0], 64)), *array.shape[1:])
    dataset = group.create_dataset(name, data=array, **kwargs)
    dataset.attrs["units"] = units
    return dataset


def write_hdf5(path: Path, result: Regional2DResult, *, compression: str | None = None) -> None:
    """Write a complete Regional2D scientific result file."""

    if result.metadata.get("data_class") not in DATA_CLASSES:
        raise ValueError("metadata.data_class must be REAL, LEGACY_CONVERTED or SYNTHETIC")
    if result.h.shape != result.qx.shape or result.h.shape != result.qy.shape:
        raise ValueError("h, qx and qy arrays must have identical shape")
    if result.h.shape != (len(result.time_values), len(result.connectivity)):
        raise ValueError("field arrays must have shape (time, cell)")

    path.parent.mkdir(parents=True, exist_ok=True)
    with h5py.File(path, "w") as h5:
        h5.attrs["schema_name"] = SCHEMA_NAME
        h5.attrs["schema_version"] = SCHEMA_VERSION
        h5.attrs["schema_major"] = SUPPORTED_MAJOR_VERSION

        metadata = h5.create_group("metadata")
        for key, value in {
            "schema_name": SCHEMA_NAME,
            "schema_version": SCHEMA_VERSION,
            "producer": "tools.results.regional2d_result",
            "created_utc": utc_now(),
            **result.metadata,
        }.items():
            metadata.attrs[key] = _json_attr(value) if isinstance(value, (dict, list)) else value

        mesh = h5.create_group("mesh")
        _write_dataset(mesh, "points", result.points, "m")
        _write_dataset(mesh.create_group("cells"), "connectivity", result.connectivity.astype("int64"), "1")
        cell_type = mesh["cells"].create_dataset(
            "type",
            data=np.asarray([b"triangle"] * len(result.connectivity), dtype="S8"),
        )
        cell_type.attrs["units"] = "1"
        _write_dataset(mesh, "cell_centres", result.cell_centres, "m")
        _write_dataset(mesh, "bed_elevation", result.bed_elevation, "m")
        _write_dataset(mesh, "region_tags", result.region_tags.astype("int64"), "1")
        _write_dataset(mesh, "boundary_tags", result.boundary_tags.astype("int64"), "1")

        time_group = h5.create_group("time")
        _write_dataset(time_group, "values", result.time_values, "s", appendable=True)

        fields = h5.create_group("fields").create_group("cell")
        for name, values, units in (
            ("h", result.h, "m"),
            ("qx", result.qx, "m^2/s"),
            ("qy", result.qy, "m^2/s"),
        ):
            kwargs = {"data": np.asarray(values), "maxshape": (None, values.shape[1]), "chunks": (max(1, min(values.shape[0], 64)), values.shape[1])}
            if compression:
                kwargs["compression"] = compression
            dataset = fields.create_dataset(name, **kwargs)
            dataset.attrs["units"] = units
            dataset.attrs["layout"] = "time_major_cell_minor"

        coupling = h5.create_group("coupling")
        for name, values in result.coupling.items():
            units = {"s": "m", "time": "s", "eta": "m", "qn": "m^2/s", "Qn": "m^3/s", "qbar": "m^2/s"}.get(name, "1")
            _write_dataset(coupling, name, np.asarray(values), units)

        diagnostics = h5.create_group("diagnostics")
        for name, values in result.diagnostics.items():
            units = {"time": "s", "dt": "s", "water_volume": "m^3", "cfl": "1"}.get(name, "1")
            _write_dataset(diagnostics, name, np.asarray(values), units)

        provenance = h5.create_group("provenance")
        provenance.attrs["json"] = _json_attr(result.provenance)


REQUIRED_DATASETS = (
    "/mesh/points",
    "/mesh/cells/connectivity",
    "/mesh/cells/type",
    "/mesh/cell_centres",
    "/mesh/bed_elevation",
    "/mesh/region_tags",
    "/mesh/boundary_tags",
    "/time/values",
    "/fields/cell/h",
    "/fields/cell/qx",
    "/fields/cell/qy",
    "/coupling/s",
    "/coupling/time",
    "/coupling/eta",
    "/coupling/qn",
    "/coupling/Qn",
    "/coupling/qbar",
    "/diagnostics/time",
    "/diagnostics/dt",
    "/diagnostics/water_volume",
)


def validate_hdf5(path: Path) -> dict[str, Any]:
    with h5py.File(path, "r") as h5:
        if h5.attrs.get("schema_name") != SCHEMA_NAME:
            raise ValueError("unsupported Regional2D result schema name")
        version = str(h5.attrs.get("schema_version", ""))
        major = int(version.split(".", 1)[0]) if version else -1
        if major != SUPPORTED_MAJOR_VERSION:
            raise ValueError(f"unsupported Regional2D result schema major version: {version}")
        missing = [name for name in REQUIRED_DATASETS if name not in h5]
        if missing:
            raise ValueError(f"missing required datasets: {missing}")
        ncells = h5["/mesh/cells/connectivity"].shape[0]
        ntimes = h5["/time/values"].shape[0]
        for name in ("/fields/cell/h", "/fields/cell/qx", "/fields/cell/qy"):
            if h5[name].shape != (ntimes, ncells):
                raise ValueError(f"{name} has inconsistent shape {h5[name].shape}")
            if "units" not in h5[name].attrs:
                raise ValueError(f"{name} is missing units")
        metadata = {key: h5["metadata"].attrs[key] for key in h5["metadata"].attrs}
        if metadata.get("data_class") not in DATA_CLASSES:
            raise ValueError("metadata.data_class is missing or invalid")
        return {"schema_name": SCHEMA_NAME, "schema_version": version, "time_count": ntimes, "cell_count": ncells, "data_class": metadata["data_class"]}


class Hdf5ResultDataset:
    def __init__(self, path: Path):
        self.path = Path(path)
        validate_hdf5(self.path)

    def _read_array(self, name: str) -> np.ndarray:
        with h5py.File(self.path, "r") as h5:
            return np.asarray(h5[name])

    def metadata(self) -> dict[str, Any]:
        with h5py.File(self.path, "r") as h5:
            out: dict[str, Any] = {}
            for key, value in h5["metadata"].attrs.items():
                if isinstance(value, bytes):
                    value = value.decode("utf-8")
                out[key] = value
            return out

    def mesh(self) -> dict[str, np.ndarray]:
        return {
            "points": self._read_array("/mesh/points"),
            "connectivity": self._read_array("/mesh/cells/connectivity"),
            "cell_centres": self._read_array("/mesh/cell_centres"),
            "bed_elevation": self._read_array("/mesh/bed_elevation"),
            "region_tags": self._read_array("/mesh/region_tags"),
            "boundary_tags": self._read_array("/mesh/boundary_tags"),
        }

    def times(self) -> np.ndarray:
        return self._read_array("/time/values")

    def field(self, name: str, time: int | float) -> np.ndarray:
        if name == "eta":
            return self.field("h", time) + self.mesh()["bed_elevation"]
        if name == "qmag":
            qx = self.field("qx", time)
            qy = self.field("qy", time)
            return np.sqrt(qx * qx + qy * qy)
        if name not in {"h", "qx", "qy"}:
            raise KeyError(name)
        times = self.times()
        index = int(time)
        if isinstance(time, float):
            index = int(np.argmin(np.abs(times - time)))
        with h5py.File(self.path, "r") as h5:
            return np.asarray(h5[f"/fields/cell/{name}"][index])

    def coupling_field(self, name: str) -> np.ndarray:
        return self._read_array(f"/coupling/{name}")

    def coupling_series(self, name: str) -> np.ndarray:
        return self.coupling_field(name)

    def provenance(self) -> dict[str, Any]:
        with h5py.File(self.path, "r") as h5:
            return _read_json_attr(h5["provenance"].attrs, "json")


class SyntheticResultDataset:
    def __init__(self) -> None:
        self._result = synthetic_fixture()

    def metadata(self) -> dict[str, Any]:
        return dict(self._result.metadata)

    def mesh(self) -> dict[str, np.ndarray]:
        return {
            "points": self._result.points,
            "connectivity": self._result.connectivity,
            "cell_centres": self._result.cell_centres,
            "bed_elevation": self._result.bed_elevation,
            "region_tags": self._result.region_tags,
            "boundary_tags": self._result.boundary_tags,
        }

    def times(self) -> np.ndarray:
        return self._result.time_values

    def field(self, name: str, time: int | float) -> np.ndarray:
        index = int(time) if isinstance(time, int) else int(np.argmin(np.abs(self._result.time_values - float(time))))
        if name == "h":
            return self._result.h[index]
        if name == "qx":
            return self._result.qx[index]
        if name == "qy":
            return self._result.qy[index]
        if name == "eta":
            return self._result.h[index] + self._result.bed_elevation
        if name == "qmag":
            return np.sqrt(self._result.qx[index] ** 2 + self._result.qy[index] ** 2)
        raise KeyError(name)

    def coupling_field(self, name: str) -> np.ndarray:
        return self._result.coupling[name]

    def coupling_series(self, name: str) -> np.ndarray:
        return self.coupling_field(name)

    def provenance(self) -> dict[str, Any]:
        return dict(self._result.provenance)


def synthetic_fixture() -> Regional2DResult:
    points = np.asarray([[0.0, 0.0], [1.0, 0.0], [0.0, 1.0], [1.0, 1.0]], dtype="float64")
    connectivity = np.asarray([[0, 1, 2], [1, 3, 2]], dtype="int64")
    cell_centres = points[connectivity].mean(axis=1)
    bed = np.asarray([-2.0, -1.5], dtype="float64")
    times = np.asarray([0.0, 1.0, 2.0], dtype="float64")
    base_h = -bed + 0.25
    h = np.vstack([base_h + 0.05 * t * np.asarray([1.0, 2.0]) for t in times])
    qx = np.vstack([0.1 * (1.0 + t) * np.asarray([1.0, 1.5]) for t in times])
    qy = np.vstack([0.05 * (1.0 + t) * np.asarray([0.5, 1.0]) for t in times])
    s = np.asarray([0.0, 1.0], dtype="float64")
    coupling_time = times.copy()
    eta = h + bed
    qn = np.asarray([[0.0, 0.0], [0.12, 0.18], [0.2, 0.3]], dtype="float64")
    qbar = qn.mean(axis=1)
    qn_total = qn.sum(axis=1)
    return Regional2DResult(
        metadata={
            "case_id": "synthetic-regional2d",
            "run_id": "synthetic-fixture",
            "solver": "Regional2D",
            "git_sha": git_sha() or "unknown",
            "binary_sha256": "synthetic",
            "coordinate_reference": {"epsg": 32654, "wkt": "SYNTHETIC_LOCAL_TM"},
            "physical_units": {"length": "m", "time": "s", "discharge_per_width": "m^2/s"},
            "data_class": "SYNTHETIC",
        },
        points=points,
        connectivity=connectivity,
        cell_centres=cell_centres,
        bed_elevation=bed,
        region_tags=np.asarray([1, 1], dtype="int64"),
        boundary_tags=np.asarray([10, 20, 30, 40], dtype="int64"),
        time_values=times,
        h=h,
        qx=qx,
        qy=qy,
        coupling={"s": s, "time": coupling_time, "eta": eta, "qn": qn, "Qn": qn_total, "qbar": qbar},
        diagnostics={"time": times, "dt": np.asarray([0.0, 1.0, 1.0]), "water_volume": np.asarray([3.0, 3.15, 3.3]), "cfl": np.asarray([0.0, 0.42, 0.43])},
        provenance={
            "data_class": "SYNTHETIC",
            "fixture": "tools.results.regional2d_result.synthetic_fixture",
            "terrain_sha256": "synthetic",
            "source_sha256": "synthetic",
            "mesh_sha256": "synthetic",
            "reconstruction": "limited_linear",
            "manning_n": 0.025,
            "gravity_m_per_s2": 9.80665,
            "boundary_policy": "synthetic closed/open mixed",
            "coupling_definition": "two-point synthetic section",
            "time_interval_s": [0.0, 2.0],
        },
    )


def write_synthetic(path: Path) -> None:
    write_hdf5(path, synthetic_fixture())


def legacy_from_csv(output_dir: Path, mesh_path: Path, *, metadata: Mapping[str, Any] | None = None) -> Regional2DResult:
    """Create a compact HDF5 result from legacy Regional2D CSV outputs.

    The legacy CSV writer stores snapshot rows by time/cell and coupling CSVs by
    time/sample. This converter preserves the scientific values without treating
    the conversion as new simulation evidence.
    """

    snapshot_path = output_dir / "snapshots.csv"
    coupling_dir = output_dir / "coupling" / "kamaishi-nearshore-interface"
    samples_path = coupling_dir / "samples.csv"
    history_path = coupling_dir / "history.csv"
    if not snapshot_path.is_file() or not samples_path.is_file() or not history_path.is_file():
        raise FileNotFoundError("legacy Regional2D output must contain snapshots.csv and coupling CSVs")

    script_dir = repo_root() / "tools/verification/convergence"
    if str(script_dir) not in sys.path:
        sys.path.insert(0, str(script_dir))
    import c1a_r4_execute_frozen_terrain as r4

    cells = r4.parse_msh_surface_cells(mesh_path)
    mesh = r4.parse_msh_triangles(mesh_path)
    points = []
    connectivity = []
    point_index: dict[tuple[float, float], int] = {}
    for cell in cells:
        tri = []
        for key in (("vertex0_x_m", "vertex0_y_m"), ("vertex1_x_m", "vertex1_y_m"), ("vertex2_x_m", "vertex2_y_m")):
            coord = (float(cell[key[0]]), float(cell[key[1]]))
            if coord not in point_index:
                point_index[coord] = len(points)
                points.append(coord)
            tri.append(point_index[coord])
        connectivity.append(tri)

    rows: list[dict[str, str]]
    with snapshot_path.open(newline="", encoding="utf-8") as handle:
        rows = list(csv.DictReader(handle))
    times = sorted({float(row["time_s"]) for row in rows})
    cell_count = len(cells)
    h = np.zeros((len(times), cell_count), dtype="float64")
    qx = np.zeros_like(h)
    qy = np.zeros_like(h)
    bed = np.zeros(cell_count, dtype="float64")
    time_index = {value: index for index, value in enumerate(times)}
    for row in rows:
        ti = time_index[float(row["time_s"])]
        ci = int(row["cell_index"])
        bed[ci] = float(row["bed_elevation"])
        h[ti, ci] = float(row["water_depth"])
        qx[ti, ci] = float(row["momentum_x"])
        qy[ti, ci] = float(row["momentum_y"])

    with samples_path.open(newline="", encoding="utf-8") as handle:
        sample_rows = list(csv.DictReader(handle))
    s = np.asarray([float(row.get("offset_m", row.get("s_m", index))) for index, row in enumerate(sample_rows)], dtype="float64")
    with history_path.open(newline="", encoding="utf-8") as handle:
        history = list(csv.DictReader(handle))
    ctime = np.asarray([float(row["time_s"]) for row in history], dtype="float64")
    qn_series = np.asarray([float(row["normal_discharge_m2_per_s"]) for row in history], dtype="float64")
    eta_series = np.asarray([float(row["free_surface_elevation_m"]) for row in history], dtype="float64")
    qn = np.tile(qn_series[:, None], (1, len(s)))
    eta_c = np.tile(eta_series[:, None], (1, len(s)))
    qbar = qn.mean(axis=1)
    qn_total = qn.sum(axis=1)

    diagnostics_time = np.asarray(times, dtype="float64")
    return Regional2DResult(
        metadata={
            "case_id": str((metadata or {}).get("case_id", "legacy-regional2d")),
            "run_id": str((metadata or {}).get("run_id", output_dir.parents[1].name)),
            "solver": "Regional2D",
            "git_sha": str((metadata or {}).get("git_sha", git_sha() or "unknown")),
            "binary_sha256": str((metadata or {}).get("binary_sha256", "legacy_unknown")),
            "coordinate_reference": (metadata or {}).get("coordinate_reference", {"epsg": 32654}),
            "physical_units": {"length": "m", "time": "s", "discharge_per_width": "m^2/s"},
            "data_class": "LEGACY_CONVERTED",
            "source_format": "legacy_csv",
        },
        points=np.asarray(points, dtype="float64"),
        connectivity=np.asarray(connectivity, dtype="int64"),
        cell_centres=np.asarray([[cell["centroid_x_m"], cell["centroid_y_m"]] for cell in cells], dtype="float64"),
        bed_elevation=bed,
        region_tags=np.ones(cell_count, dtype="int64"),
        boundary_tags=np.arange(len(s), dtype="int64"),
        time_values=np.asarray(times, dtype="float64"),
        h=h,
        qx=qx,
        qy=qy,
        coupling={"s": s, "time": ctime, "eta": eta_c, "qn": qn, "Qn": qn_total, "qbar": qbar},
        diagnostics={"time": diagnostics_time, "dt": np.diff(np.concatenate(([times[0]], times))), "water_volume": h.sum(axis=1)},
        provenance={
            "data_class": "LEGACY_CONVERTED",
            "source_format": "legacy_csv",
            "source_hashes": {
                "snapshots_csv_sha256": sha256(snapshot_path),
                "samples_csv_sha256": sha256(samples_path),
                "history_csv_sha256": sha256(history_path),
                "mesh_sha256": mesh["mesh_sha256"],
            },
            "converter": "tools.results.regional2d_result.legacy_from_csv",
            "converter_git_sha": git_sha() or "unknown",
        },
    )


def _main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    synth = sub.add_parser("write-synthetic")
    synth.add_argument("path", type=Path)
    validate = sub.add_parser("validate")
    validate.add_argument("path", type=Path)
    convert = sub.add_parser("convert-legacy")
    convert.add_argument("--output-dir", type=Path, required=True)
    convert.add_argument("--mesh", type=Path, required=True)
    convert.add_argument("--hdf5", type=Path, required=True)
    args = parser.parse_args(argv)
    if args.command == "write-synthetic":
        write_synthetic(args.path)
        print(json.dumps(validate_hdf5(args.path), indent=2, sort_keys=True))
    elif args.command == "validate":
        print(json.dumps(validate_hdf5(args.path), indent=2, sort_keys=True))
    elif args.command == "convert-legacy":
        write_hdf5(args.hdf5, legacy_from_csv(args.output_dir, args.mesh))
        print(json.dumps(validate_hdf5(args.hdf5), indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(_main())
