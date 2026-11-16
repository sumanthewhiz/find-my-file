import unittest
from evaluate_context import metrics, score


class MetricsTests(unittest.TestCase):
    def test_known_rank_and_no_unseen_penalty(self):
        result = metrics(["irrelevant", "unseen"], {"irrelevant": 0, "unseen": 3})
        self.assertAlmostEqual(result["ndcg10"], 1 / 1.584962500721156)
        self.assertEqual(result["mrr10"], 0.5)
        self.assertEqual(result["recall10"], 1)

    def test_absence_is_not_a_perfect_result(self):
        with self.assertRaises(ValueError):
            metrics(["x"], {"x": 0})

    def test_consumer_failure_cannot_pass_as_a_silent_fallback(self):
        case = {"id": "test", "synthetic": True, "split": "heldout", "cohorts": ["unseen_relevant"],
                "candidates": [{"id": "x"}, {"id": "y"}], "features": {"ok": True},
                "expected_baseline": ["x", "y"], "expected_contextual": ["y", "x"],
                "labels": {"relevance": {"x": 0, "y": 3}, "fallback_order_unchanged": False}}
        result = score([case], [{"id": "test", "baseline": ["x", "y"], "contextual": ["x", "y"],
                                "available": False, "status": "invalid_response"}])
        self.assertFalse(result["passed"])
        self.assertTrue(any("availability mismatch" in failure for failure in result["failures"]))


if __name__ == "__main__":
    unittest.main()
