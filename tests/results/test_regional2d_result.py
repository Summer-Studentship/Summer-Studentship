import tempfile
import unittest
from pathlib import Path

import h5py
import numpy as np

from tools.results.regional2d_result import (
    Hdf5ResultDataset,
    SCHEMA_NAME,
    SyntheticResultDataset,
    synthetic_fixture,
    validate_hdf5,
    write_hdf5,
)


class Regional2DResultHdf5Tests(unittest.TestCase):
    def test_synthetic_fixture_round_trips_without_scientific_loss(self) -> None:
        source = synthetic_fixture()
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "regional2d.h5"
            write_hdf5(path, source)

            validation = validate_hdf5(path)
            self.assertEqual(validation["schema_name"], SCHEMA_NAME)
            self.assertEqual(validation["cell_count"], 2)
            self.assertEqual(validation["time_count"], 3)
            self.assertEqual(validation["data_class"], "SYNTHETIC")

            dataset = Hdf5ResultDataset(path)
            mesh = dataset.mesh()
            np.testing.assert_allclose(mesh["points"], source.points)
            np.testing.assert_array_equal(mesh["connectivity"], source.connectivity)
            np.testing.assert_allclose(mesh["bed_elevation"], source.bed_elevation)
            np.testing.assert_allclose(dataset.times(), source.time_values)
            np.testing.assert_allclose(dataset.field("h", 1), source.h[1])
            np.testing.assert_allclose(dataset.field("qx", 2.0), source.qx[2])
            np.testing.assert_allclose(dataset.field("eta", 1), source.h[1] + source.bed_elevation)
            np.testing.assert_allclose(dataset.coupling_field("Qn"), source.coupling["Qn"])
            self.assertEqual(dataset.metadata()["data_class"], "SYNTHETIC")
            self.assertEqual(dataset.provenance()["data_class"], "SYNTHETIC")

    def test_synthetic_result_dataset_matches_hdf5_adapter(self) -> None:
        synthetic = SyntheticResultDataset()
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "regional2d.h5"
            write_hdf5(path, synthetic_fixture())
            hdf5 = Hdf5ResultDataset(path)
            np.testing.assert_allclose(synthetic.field("qmag", 2.0), hdf5.field("qmag", 2.0))
            np.testing.assert_allclose(synthetic.coupling_series("qbar"), hdf5.coupling_series("qbar"))

    def test_unsupported_major_version_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "regional2d.h5"
            write_hdf5(path, synthetic_fixture())
            with h5py.File(path, "a") as h5:
                h5.attrs["schema_version"] = "2.0.0"
            with self.assertRaisesRegex(ValueError, "unsupported Regional2D result schema major"):
                validate_hdf5(path)

    def test_missing_required_dataset_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "regional2d.h5"
            write_hdf5(path, synthetic_fixture())
            with h5py.File(path, "a") as h5:
                del h5["/fields/cell/qy"]
            with self.assertRaisesRegex(ValueError, "missing required datasets"):
                validate_hdf5(path)


if __name__ == "__main__":
    unittest.main()
