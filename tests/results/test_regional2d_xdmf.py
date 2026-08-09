import tempfile
import unittest
from pathlib import Path
import xml.etree.ElementTree as ET

import h5py

from tools.results.regional2d_result import synthetic_fixture, write_hdf5
from tools.results.regional2d_xdmf import validate_xdmf, write_xdmf


class Regional2DXdmfTests(unittest.TestCase):
    def test_xdmf_references_hdf5_temporal_triangular_fields(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            hdf5 = Path(directory) / "regional2d.h5"
            write_hdf5(hdf5, synthetic_fixture())
            xdmf = write_xdmf(hdf5)

            summary = validate_xdmf(xdmf, hdf5)
            self.assertEqual(summary["status"], "passed")
            self.assertEqual(summary["cell_count"], 2)
            self.assertEqual(summary["time_count"], 3)

            root = ET.parse(xdmf).getroot()
            self.assertEqual(root.tag, "Xdmf")
            self.assertEqual(len(root.findall(".//Grid[@GridType='Uniform']")), 3)
            self.assertIn("regional2d.h5:/fields/cell/h", xdmf.read_text(encoding="utf-8"))

    def test_xdmf_validation_rejects_missing_referenced_dataset(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            hdf5 = Path(directory) / "regional2d.h5"
            write_hdf5(hdf5, synthetic_fixture())
            xdmf = write_xdmf(hdf5)
            with h5py.File(hdf5, "a") as h5:
                del h5["/fields/cell/qx"]
            with self.assertRaisesRegex(ValueError, "missing required datasets"):
                validate_xdmf(xdmf, hdf5)


if __name__ == "__main__":
    unittest.main()
