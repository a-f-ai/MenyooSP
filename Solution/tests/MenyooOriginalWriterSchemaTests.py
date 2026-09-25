import os
import re
import unittest
from pathlib import Path


SOURCE_ROOT = Path(os.environ.get(
    "MENYOO_SOURCE_ROOT",
    Path(__file__).resolve().parents[1] / "source",
))
HEADER = SOURCE_ROOT / "Submenus" / "Spooner" / "FileManagement.h"
IMPLEMENTATION = SOURCE_ROOT / "Submenus" / "Spooner" / "FileManagement.cpp"


class MenyooOriginalWriterSchemaTests(unittest.TestCase):
    def test_default_writer_uses_original_numeric_ped_slot_ids(self):
        header = HEADER.read_text(encoding="utf-8")
        self.assertRegex(
            header,
            r"AddEntityToXmlNode\([^;]+legacyXMLFormat\s*=\s*true\)",
        )

    def test_writer_uses_only_original_single_animation_fields(self):
        source = IMPLEMENTATION.read_text(encoding="utf-8")
        writer = source[
            source.index("void AddEntityToXmlNode"):
            source.index("void LoadPedCompsFromXml")
        ]
        self.assertNotIn('append_child("Animations")', writer)
        for field in ("AnimActive", "AnimDict", "AnimName"):
            self.assertIn(f'append_child("{field}")', writer)


if __name__ == "__main__":
    unittest.main()
