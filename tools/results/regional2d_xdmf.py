#!/usr/bin/env python3
"""Generate and validate XDMF descriptors for Regional2D HDF5 results."""

from __future__ import annotations

import argparse
import json
import xml.etree.ElementTree as ET
from pathlib import Path
from typing import Any, Sequence

import h5py

from tools.results.regional2d_result import validate_hdf5


def _data_item(parent: ET.Element, *, dimensions: str, number_type: str, precision: str, text: str, item_type: str | None = None, data_format: str = "HDF") -> ET.Element:
    attrs = {"Dimensions": dimensions, "NumberType": number_type, "Precision": precision, "Format": data_format}
    if item_type:
        attrs["ItemType"] = item_type
        attrs["Type"] = item_type
    item = ET.SubElement(parent, "DataItem", attrs)
    item.text = text
    return item


def _field_hyperslab(parent: ET.Element, h5_name: str, dataset_path: str, time_index: int, cell_count: int, time_count: int) -> None:
    slab = ET.SubElement(parent, "DataItem", {"ItemType": "HyperSlab", "Dimensions": str(cell_count), "Type": "HyperSlab"})
    selection = ET.SubElement(slab, "DataItem", {"Dimensions": "3 2", "Format": "XML", "NumberType": "Int"})
    selection.text = f"{time_index} 0 1 1 1 {cell_count}"
    _data_item(
        slab,
        dimensions=f"{time_count} {cell_count}",
        number_type="Float",
        precision="8",
        text=f"{h5_name}:{dataset_path}",
    )


def write_xdmf(hdf5_path: Path, xdmf_path: Path | None = None) -> Path:
    """Write a temporal XDMF descriptor for cell-centred h/qx/qy fields."""

    validate_hdf5(hdf5_path)
    xdmf_path = xdmf_path or hdf5_path.with_suffix(".xdmf")
    with h5py.File(hdf5_path, "r") as h5:
        point_count = h5["/mesh/points"].shape[0]
        cell_count = h5["/mesh/cells/connectivity"].shape[0]
        time_values = h5["/time/values"][:]

    h5_ref = hdf5_path.name
    root = ET.Element("Xdmf", {"Version": "3.0"})
    domain = ET.SubElement(root, "Domain")
    collection = ET.SubElement(domain, "Grid", {"Name": "Regional2D", "GridType": "Collection", "CollectionType": "Temporal"})

    for index, time_value in enumerate(time_values):
        grid = ET.SubElement(collection, "Grid", {"Name": f"Regional2D_t{index}", "GridType": "Uniform"})
        ET.SubElement(grid, "Time", {"Value": f"{float(time_value):.17g}"})
        topology = ET.SubElement(grid, "Topology", {"TopologyType": "Triangle", "NumberOfElements": str(cell_count)})
        _data_item(topology, dimensions=f"{cell_count} 3", number_type="Int", precision="8", text=f"{h5_ref}:/mesh/cells/connectivity")
        geometry = ET.SubElement(grid, "Geometry", {"GeometryType": "XY"})
        _data_item(geometry, dimensions=f"{point_count} 2", number_type="Float", precision="8", text=f"{h5_ref}:/mesh/points")
        for field in ("h", "qx", "qy"):
            attr = ET.SubElement(grid, "Attribute", {"Name": field, "AttributeType": "Scalar", "Center": "Cell"})
            _field_hyperslab(attr, h5_ref, f"/fields/cell/{field}", index, cell_count, len(time_values))

    ET.indent(root, space="  ")
    xdmf_path.parent.mkdir(parents=True, exist_ok=True)
    ET.ElementTree(root).write(xdmf_path, encoding="utf-8", xml_declaration=True)
    return xdmf_path


def validate_xdmf(xdmf_path: Path, hdf5_path: Path | None = None) -> dict[str, Any]:
    """Validate XML, referenced HDF5 paths, topology and temporal metadata."""

    tree = ET.parse(xdmf_path)
    root = tree.getroot()
    if root.tag != "Xdmf":
        raise ValueError("XDMF root element must be Xdmf")
    hdf5_path = hdf5_path or xdmf_path.with_suffix(".h5")
    h5_summary = validate_hdf5(hdf5_path)
    with h5py.File(hdf5_path, "r") as h5:
        point_shape = h5["/mesh/points"].shape
        cell_shape = h5["/mesh/cells/connectivity"].shape
        time_count = h5["/time/values"].shape[0]
        referenced_paths: set[str] = set()
        for item in root.findall(".//DataItem"):
            text = (item.text or "").strip()
            if ":/" not in text:
                continue
            _, dataset_path = text.split(":", 1)
            referenced_paths.add(dataset_path)
            if dataset_path not in h5:
                raise ValueError(f"XDMF references missing HDF5 dataset: {dataset_path}")
        grids = root.findall(".//Grid[@GridType='Uniform']")
        if len(grids) != time_count:
            raise ValueError("XDMF temporal grid count does not match HDF5 time count")
        for grid in grids:
            topology = grid.find("Topology")
            geometry = grid.find("Geometry")
            if topology is None or topology.attrib.get("TopologyType") != "Triangle":
                raise ValueError("XDMF grid is missing triangular topology")
            if int(topology.attrib.get("NumberOfElements", "-1")) != cell_shape[0]:
                raise ValueError("XDMF topology element count does not match HDF5")
            if geometry is None or geometry.attrib.get("GeometryType") != "XY":
                raise ValueError("XDMF grid is missing XY geometry")
            for field in ("h", "qx", "qy"):
                attr = grid.find(f"Attribute[@Name='{field}']")
                if attr is None or attr.attrib.get("Center") != "Cell":
                    raise ValueError(f"XDMF grid is missing cell-centred {field}")
        return {
            "status": "passed",
            "xdmf_path": str(xdmf_path),
            "hdf5_path": str(hdf5_path),
            "time_count": time_count,
            "point_count": point_shape[0],
            "cell_count": cell_shape[0],
            "referenced_dataset_count": len(referenced_paths),
            "hdf5_schema": h5_summary,
        }


def _main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    generate = sub.add_parser("generate")
    generate.add_argument("hdf5", type=Path)
    generate.add_argument("--xdmf", type=Path)
    validate = sub.add_parser("validate")
    validate.add_argument("xdmf", type=Path)
    validate.add_argument("--hdf5", type=Path)
    args = parser.parse_args(argv)
    if args.command == "generate":
        path = write_xdmf(args.hdf5, args.xdmf)
        print(json.dumps(validate_xdmf(path, args.hdf5), indent=2, sort_keys=True))
    elif args.command == "validate":
        print(json.dumps(validate_xdmf(args.xdmf, args.hdf5), indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(_main())
