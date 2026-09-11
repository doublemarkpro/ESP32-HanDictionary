import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("capacity_report", ROOT / "tools/capacity_report.py")
capacity = importlib.util.module_from_spec(spec)
spec.loader.exec_module(capacity)


class CapacityTests(unittest.TestCase):
    def fixture(self, root, app_size=100):
        build = root / "build"; build.mkdir()
        (root / "layout.csv").write_text("nvs,data,nvs,0x9000,0x4000,\nota_0,app,ota_0,0x20000,1M,\nassets,data,spiffs,,1M,\n")
        (root / "sdkconfig").write_text('CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="layout.csv"\n')
        (build / "project_description.json").write_text(json.dumps({"config_file":str(root/"sdkconfig"),"project_path":str(root)}))
        (build / "flasher_args.json").write_text(json.dumps({"flash_settings":{"flash_size":"16MB"},"flash_files":{"0x20000":"app.bin","0x120000":"assets.bin"}}))
        (build / "app.bin").write_bytes(b"x"*app_size)
        (build / "assets.bin").write_bytes(b"x"*100)
        return build

    def test_units_and_derived_partition_offset(self):
        self.assertEqual(capacity.number("16M"),16*1024*1024)
        self.assertEqual(capacity.number("512K"),524288)
        with tempfile.TemporaryDirectory() as tmp:
            data=capacity.report(self.fixture(Path(tmp)))
            self.assertEqual(data["partitions"][0]["free"],1048576-100)
            self.assertIsNone(data["runtime_free_ram"])

    def test_overflow_and_safety_floor(self):
        for size in [600000,1048577]:
            with tempfile.TemporaryDirectory() as tmp:
                with self.assertRaises(ValueError): capacity.report(self.fixture(Path(tmp),size))

    def test_overlapping_partitions_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp); build=self.fixture(root)
            (root/"layout.csv").write_text("nvs,data,nvs,0x9000,1M,\nota_0,app,ota_0,0x20000,1M,\n")
            with self.assertRaises(ValueError): capacity.report(build)

    def test_path_escape_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp); build=self.fixture(root)
            data=json.loads((build/"flasher_args.json").read_text())
            data["flash_files"]["0x20000"]="../sdkconfig"
            (build/"flasher_args.json").write_text(json.dumps(data))
            with self.assertRaises(ValueError): capacity.report(build)
