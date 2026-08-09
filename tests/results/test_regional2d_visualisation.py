import json
import tempfile
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

from tools.results.regional2d_visualisation import generate_synthetic_poc


class Regional2DVisualisationTests(unittest.TestCase):
    def test_synthetic_poc_generates_labelled_svg_figures_and_provenance(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            run_root = Path(directory) / "runs/synthetic-regional2d/synthetic-fixture"
            manifest = generate_synthetic_poc(run_root)
            self.assertEqual(manifest["data_classification"], "SYNTHETIC")
            self.assertGreaterEqual(len(manifest["figures"]), 6)
            index = json.loads((run_root / "figures/index.json").read_text(encoding="utf-8"))
            self.assertEqual(index["data_classification"], "SYNTHETIC")
            categories = {Path(item["figure"]).parent.name for item in index["figures"]}
            self.assertTrue({"mesh", "fields", "coupling", "convergence", "comparisons"}.issubset(categories))
            for item in index["figures"]:
                figure = Path(item["figure"])
                provenance = Path(item["provenance"])
                self.assertTrue(figure.name.startswith("synthetic_"))
                self.assertGreater(figure.stat().st_size, 100)
                ET.parse(figure)
                payload = json.loads(provenance.read_text(encoding="utf-8"))
                self.assertEqual(payload["data_classification"], "SYNTHETIC")
                self.assertIn("SYNTHETIC", figure.read_text(encoding="utf-8"))
                self.assertIn("fields_used", payload)
                self.assertIn("units", payload)


if __name__ == "__main__":
    unittest.main()
