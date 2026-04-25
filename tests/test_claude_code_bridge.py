import importlib.util
import json
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
BRIDGE_PATH = REPO_ROOT / "tools" / "claude_code_bridge.py"


def load_bridge_module():
    spec = importlib.util.spec_from_file_location("claude_code_bridge_under_test", BRIDGE_PATH)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


def write_jsonl(path: Path, records: list[dict]) -> None:
    path.write_text(
        "\n".join(json.dumps(record, ensure_ascii=False) for record in records) + "\n",
        encoding="utf-8",
    )


class ClaudeCodeBridgeTests(unittest.TestCase):
    def setUp(self):
        self.bridge = load_bridge_module()
        self.tmpdir = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmpdir.cleanup)

    def transcript_path(self, name: str) -> Path:
        return Path(self.tmpdir.name) / f"{name}.jsonl"

    def test_refresh_transcript_state_keeps_global_latest_reply(self):
        newer = self.transcript_path("newer")
        older = self.transcript_path("older")

        write_jsonl(
            newer,
            [
                {
                    "timestamp": "2026-04-24T10:05:00Z",
                    "message": {
                        "role": "assistant",
                        "content": [{"type": "text", "text": "latest assistant reply"}],
                    },
                }
            ],
        )
        write_jsonl(
            older,
            [
                {
                    "timestamp": "2026-04-24T10:00:00Z",
                    "message": {
                        "role": "assistant",
                        "content": [{"type": "text", "text": "older assistant reply"}],
                    },
                }
            ],
        )

        self.bridge.refresh_transcript_state("newer", str(newer))
        self.bridge.refresh_transcript_state("older", str(older))

        self.assertEqual(self.bridge.ASSISTANT_MSG, "latest assistant reply")

    def test_build_heartbeat_prefers_global_reply_fallback_over_arbitrary_session_cache(self):
        self.bridge.ASSISTANT_MSG = "latest assistant reply"
        self.bridge.SESSION_ASSISTANT["older"] = "older assistant reply"
        self.bridge.SESSION_ASSISTANT["newer"] = "latest assistant reply"

        heartbeat = self.bridge.build_heartbeat()

        self.assertEqual(heartbeat.get("assistant_msg"), "latest assistant reply")

    def test_add_transcript_refreshes_timestamp_for_repeated_latest_activity(self):
        self.bridge.now_hm = lambda: "10:00"
        self.bridge.add_transcript("Bash: make test")

        self.bridge.now_hm = lambda: "10:05"
        self.bridge.add_transcript("Bash: make test")

        self.assertEqual(list(self.bridge.TRANSCRIPT), ["10:05 Bash: make test"])

    def test_bootstrap_recent_state_keeps_latest_timestamp_for_repeated_activity(self):
        transcript = self.transcript_path("activity")
        latest_record = {
            "timestamp": "2026-04-24T10:05:00Z",
            "message": {"role": "user", "content": "repeat me"},
        }
        write_jsonl(
            transcript,
            [
                {
                    "timestamp": "2026-04-24T10:00:00Z",
                    "message": {"role": "user", "content": "repeat me"},
                },
                latest_record,
            ],
        )

        self.bridge.recent_transcript_paths = lambda limit=6: [transcript]
        self.bridge.bootstrap_recent_state()

        expected_hm = self.bridge.parse_event_time(latest_record).strftime("%H:%M")
        self.assertEqual(list(self.bridge.TRANSCRIPT), [f"{expected_hm} > repeat me"])


if __name__ == "__main__":
    unittest.main()
