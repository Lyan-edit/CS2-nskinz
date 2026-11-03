"""Regression checks for preserving explicitly user-locked radar standards."""
from pathlib import Path
import csv
import hashlib
import json
import tempfile
import unittest
from unittest.mock import patch

from PIL import Image
import build_radar_reference_catalog as catalog


class LockedRadarTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='nskinz-radar-lock-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.assets = self.root/'assets'
        self.candidates = self.root/'candidates'
        self.assets.mkdir()
        self.candidates.mkdir()
        image = Image.new('RGBA', (64, 64), (0, 60, 100, 0))
        for y in range(16, 48):
            for x in range(16, 48): image.putpixel((x, y), (0, 60, 100, 255))
        image.save(self.assets/'fixed.png')
        image.putpixel((20, 20), (0, 60, 101, 255))
        image.save(self.candidates/'sample.png')
        self.entry = dict(key='fixed', file='fixed.png', virtual_path='panorama/images/overheadmaps/de_inferno_water_radar_tga.vtex_c',
                          source_fnv1a64=f'{catalog.fnv(b"original"):x}', source_size=8,
                          map_names=['de_inferno_water'], workshop_ids=['3318429218'],
                          locked=True, review='user-approved', **catalog.inspect_png(self.assets/'fixed.png'))
        self.manifest = self.assets/'catalog.json'
        self.manifest.write_text(json.dumps(dict(version=1, references=[self.entry])), encoding='utf-8')
        self.original_manifest = self.manifest.read_bytes()
        self.inventory = self.root/'inventory.tsv'
        self.write_inventory(b'original')
        self.overrides = patch.multiple(catalog, ASSETS=self.assets, MANIFEST=self.manifest,
                                       HEADER=self.root/'generated.hpp', RESOURCE=self.root/'generated.rc2')
        self.overrides.start()
        self.addCleanup(self.overrides.stop)

    def write_inventory(self, source):
        (self.root/'sample.vtex_c').write_bytes(source)
        with self.inventory.open('w', newline='', encoding='utf-8') as stream:
            writer = csv.DictWriter(stream, fieldnames=['map','workshop','virtual_path','file','source_fnv1a64','source_size'], delimiter='\t')
            writer.writeheader()
            writer.writerow(dict(map='de_inferno_water', workshop='3318429218', virtual_path=self.entry['virtual_path'],
                                 file='sample', source_fnv1a64=f'{catalog.fnv(source):x}', source_size=len(source)))

    def assert_fixed(self):
        self.assertEqual(hashlib.sha256((self.assets/'fixed.png').read_bytes()).hexdigest(), self.entry['sha256'])
        self.assertEqual(json.loads(self.manifest.read_text())['references'][0], self.entry)

    def test_reimport_cannot_replace_same_version(self):
        with self.assertRaisesRegex(ValueError, 'cannot be replaced'):
            catalog.import_reviewed(self.inventory, self.candidates)
        self.assertEqual(self.manifest.read_bytes(), self.original_manifest)
        self.assert_fixed()

    def test_new_source_cannot_replace_locked_standard(self):
        self.write_inventory(b'updated source')
        catalog.import_reviewed(self.inventory, self.candidates)
        self.assert_fixed()

    def test_rescan_cannot_drop_locked_standard(self):
        self.inventory.write_text('map\tworkshop\tvirtual_path\tfile\tsource_fnv1a64\tsource_size\n')
        catalog.import_reviewed(self.inventory, self.candidates)
        self.assert_fixed()

    def test_modified_asset_is_rejected(self):
        (self.assets/'fixed.png').write_bytes((self.candidates/'sample.png').read_bytes())
        with self.assertRaisesRegex(ValueError, 'image changed'):
            catalog.generate()

    def test_unsafe_asset_path_is_rejected(self):
        invalid = dict(self.entry, file='../outside.png')
        self.manifest.write_text(json.dumps(dict(version=1, references=[invalid])))
        with self.assertRaisesRegex(ValueError, 'simple PNG filename'):
            catalog.generate()


if __name__ == '__main__':
    unittest.main()
