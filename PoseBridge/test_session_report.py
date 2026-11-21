import io
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import session_report as report


class SessionReportTest(unittest.TestCase):
    def test_saved_report_and_failure_recovery(self):
        summary = dict(session="private-session-id", version=3, side="right", planned=3, completed=1,
                       skipped=0, interruptions=1, dodges=1, finish_reason="Finished early",
                       reps=[dict(exercise="arm_lift", estimated_peak_arm_elevation_deg=52.3)])
        facts = report.report_facts(summary)
        recap = "You completed one arm lift and one dodge."

        def reply(request, timeout):
            self.assertEqual(request.get_header("X-goog-api-key"), "test-only")
            self.assertNotIn("private-session-id", request.data.decode())
            packet = json.loads(request.data)
            self.assertEqual(json.loads(packet["contents"][0]["parts"][0]["text"]), facts)
            return io.BytesIO(json.dumps({"candidates": [{"finishReason": "STOP", "content": {"parts": [
                {"thought": True, "text": "internal"}, {"text": recap}]}}]}).encode())

        self.assertEqual(report.generate_report(facts, "test-only", "fixture", reply), recap)
        with tempfile.TemporaryDirectory() as folder:
            source = Path(folder) / "demo.json"
            original = json.dumps(summary)
            source.write_text(original)
            result = source.with_suffix(".report.txt")
            with patch("sys.argv", ["session_report.py", str(source)]), patch.dict(os.environ, {}, clear=True):
                self.assertEqual(report.main(), 2)
                self.assertFalse(result.exists())
                with patch.dict(os.environ, {"GEMINI_API_KEY": "test-only"}):
                    with patch.object(report, "generate_report", return_value=recap):
                        self.assertEqual(report.main(), 0)
                    saved = result.read_text(encoding="utf8")
                    self.assertTrue(saved.startswith(recap))
                    self.assertIn("estimated peak arm elevation: 52.3", saved)
                    with patch.object(report, "generate_report", side_effect=ValueError("Incomplete response")):
                        self.assertEqual(report.main(), 1)
                    self.assertEqual(result.read_text(encoding="utf8"), saved)
            self.assertEqual(source.read_text(), original)

    def test_incomplete_and_empty_responses(self):
        for candidate in ([], [{"finishReason": "MAX_TOKENS"}], [{"finishReason": "STOP", "content": {}}]):
            with self.subTest(candidate=candidate), self.assertRaises(ValueError):
                report.generate_report({}, "test-only", "fixture", lambda *a, **k:
                                       io.BytesIO(json.dumps({"candidates": candidate}).encode()))


if __name__ == "__main__":
    unittest.main()
