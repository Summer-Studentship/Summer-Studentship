import shutil
import tempfile
import unittest
from pathlib import Path

import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/openfoam"))

import r14_local3d_paraview as r14pv


class R14Local3DParaviewTests(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="tsunami-r14-pv-"))

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def test_discover_case_reports_times_fields_and_vtk(self):
        (self.tmp / "0").mkdir()
        (self.tmp / "0/alpha.water").write_text("field\n", encoding="utf-8")
        (self.tmp / "1").mkdir()
        (self.tmp / "1/U").write_text("field\n", encoding="utf-8")
        (self.tmp / "VTK").mkdir()
        (self.tmp / "VTK/case_1.vtk").write_text("# vtk\n", encoding="utf-8")
        (self.tmp / "case.foam").write_text("case\n", encoding="utf-8")
        discovered = r14pv.discover_case(self.tmp)
        self.assertEqual(discovered["time_directories"], [0.0, 1.0])
        self.assertIn("alpha.water", discovered["fields_by_time"]["0"])
        self.assertTrue(discovered["has_case_foam"])
        self.assertEqual(len(discovered["vtk_files"]), 1)


if __name__ == "__main__":
    unittest.main()
