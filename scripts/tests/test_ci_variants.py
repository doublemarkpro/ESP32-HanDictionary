import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("ci_selection", ROOT / "scripts/select_ci_variants.py")
ci = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ci)


class CiSelectionTests(unittest.TestCase):
    def setUp(self):
        self.products = [{"board": "m5stack/tab5", "name": name, "full_name": name}
                         for name in ci.PRODUCT_NAMES]
        self.variants = self.products + [{"board": "m5stack/tab5", "name": "m5stack-tab5",
                                          "full_name": "m5stack-tab5"}]

    def test_automatic_events_only_select_two_product_variants(self):
        for event in ("push", "pull_request"):
            self.assertEqual(ci.select(self.variants, event), self.products)

    def test_manual_default_remains_product_only(self):
        self.assertEqual(ci.select(self.variants, "workflow_dispatch"), self.products)

    def test_full_matrix_requires_explicit_manual_request(self):
        self.assertEqual(ci.select(self.variants, "workflow_dispatch", "all"), self.variants)
        for event in ("push", "pull_request"):
            with self.assertRaises(ValueError):
                ci.select(self.variants, event, "all")

    def test_missing_and_duplicate_products_fail_closed(self):
        for rows in ([], self.products[:1], self.products + self.products):
            with self.assertRaises(ValueError):
                ci.select(rows)

    def test_same_name_on_other_board_does_not_replace_product(self):
        rows = [dict(v, board="other") for v in self.products]
        with self.assertRaises(ValueError):
            ci.select(rows)

    def test_unknown_scope_or_event_fails_closed(self):
        for event, scope in (("schedule", "all"), ("push", "other")):
            with self.assertRaises(ValueError):
                ci.select(self.variants, event, scope)
