import os
import unittest
from pathlib import Path


SOURCE_ROOT = Path(os.environ.get(
    "MENYOO_SOURCE_ROOT",
    Path(__file__).resolve().parents[1] / "source",
))
IMPLEMENTATION = SOURCE_ROOT / "Submenus" / "Spooner" / "FileManagement.cpp"


class MenyooOriginalWriterSchemaTests(unittest.TestCase):
    def test_default_writer_uses_original_numeric_ped_slot_ids(self):
        source = IMPLEMENTATION.read_text(encoding="utf-8")
        writer = source[
            source.index("void AddEntityToXmlNode"):
            source.index("SpoonerEntityWithInitHandle SpawnEntityFromXmlNode")
        ]
        numeric_slot = '.append_child(("_" + std::to_string(i)).c_str())'
        self.assertIn("nodePedProps" + numeric_slot, writer)
        self.assertIn("nodePedComps" + numeric_slot, writer)

    def test_writer_uses_only_original_single_animation_fields(self):
        source = IMPLEMENTATION.read_text(encoding="utf-8")
        writer = source[
            source.index("void AddEntityToXmlNode"):
            source.index("SpoonerEntityWithInitHandle SpawnEntityFromXmlNode")
        ]
        self.assertNotIn('append_child("Animations")', writer)
        for field in ("AnimActive", "AnimDict", "AnimName"):
            self.assertIn(f'append_child("{field}")', writer)


if __name__ == "__main__":
    unittest.main()
