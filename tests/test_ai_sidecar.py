import importlib.util
import json
import pathlib
import sys
import tempfile
import unittest
from unittest.mock import patch


TOOLS = pathlib.Path(__file__).resolve().parents[1] / "tools"
sys.path.insert(0, str(TOOLS))
SPEC = importlib.util.spec_from_file_location("ai_sidecar", TOOLS / "ai_sidecar.py")
sidecar = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(sidecar)


class AiSidecarTests(unittest.TestCase):
    def setUp(self):
        self.event = sidecar.parse_event(
            "2026-09-28 (INFO): [YaPB ai] chat bot=2 player=0 channel=team "
            "team=1 map=de_dust2 place=bombsiteb weapon=weapon_ak47 hp=67 "
            "money=3200 friends=2 enemies=3 bomb=1 round=42 visible=2 "
            "slots=4,5,-1 text=bot can you see mid\n"
        )

    def test_context_is_parsed(self):
        self.assertEqual(self.event["map"], "de_dust2")
        self.assertEqual(self.event["place"], "bombsiteb")
        self.assertEqual(self.event["visible_slots"], [4, 5])
        self.assertTrue(self.event["bomb_planted"])

    def test_validated_reply_and_action(self):
        self.assertEqual(sidecar.validate_model_choice(
            {"reply": "on my way", "action": "follow"}, self.event),
            ["yb ai 2 follow 0", 'yb ai 2 chat team "on my way"'])

    def test_rejects_unsafe_output_and_all_chat_action(self):
        for choice in ({"reply": "HELLO", "action": "none"},
                       {"reply": "hi", "action": "exec"},
                       {"reply": 'hi"; quit', "action": "none"}):
            with self.subTest(choice=choice), self.assertRaises(ValueError):
                sidecar.validate_model_choice(choice, self.event)
        self.event["channel"] = "all"
        with self.assertRaises(ValueError):
            sidecar.validate_model_choice({"reply": "hi", "action": "jump"}, self.event)

    def test_local_queue_is_private_and_atomic(self):
        with tempfile.TemporaryDirectory() as directory:
            queue = pathlib.Path(directory) / "ai"
            sidecar.send_local('yb ai 2 chat team "on my way"', queue)
            self.assertEqual((queue / "pending.cmd").read_text(),
                             'yb ai 2 chat team "on my way"')
            self.assertEqual(queue.stat().st_mode & 0o077, 0)
            with self.assertRaises(TimeoutError):
                sidecar.send_local("yb ai 2 jump", queue, timeout=0.05)
            (queue / "pending.cmd").unlink()
            sidecar.send_local("yb ai 2 jump", queue)
            self.assertEqual((queue / "pending.cmd").read_text(), "yb ai 2 jump")
            (queue / "pending.cmd").unlink()
            sidecar.send_local("yb ai 2 dead_chat 1", queue)
            self.assertEqual((queue / "pending.cmd").read_text(), "yb ai 2 dead_chat 1")
            with self.assertRaises(ValueError):
                sidecar.send_local('yb ai 2 chat all "hi"; quit', queue)

    def test_provider_receives_game_context(self):
        payloads = []

        class Response:
            status = 200

            def __enter__(self):
                return self

            def __exit__(self, *_):
                return False

            def read(self, _):
                return json.dumps({"choices": [{"message": {"content":
                    '{"reply":"one b","action":"none"}'}}]}).encode()

        def fake_urlopen(req, timeout):
            payloads.append(json.loads(req.data))
            self.assertEqual(timeout, 6)
            return Response()

        with patch.object(sidecar.request, "urlopen", fake_urlopen):
            answer = sidecar.ask_provider(self.event, "key", "https://example.test/v1/chat/completions", "model", 6)
        self.assertEqual(answer, {"reply": "one b", "action": "none"})
        state = json.loads(payloads[0]["messages"][1]["content"])["bot_state"]
        self.assertEqual(state["weapon"], "weapon_ak47")
        self.assertEqual(state["visible_slots"], [4, 5])

    def test_env_file_and_process_override(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / ".env"
            path.write_text("# private credentials\nexport DEEPSEEK_API_KEY='file-key'\n"
                            "YAPB_AI_MODEL=deepseek-chat\n")
            path.chmod(0o600)
            with patch.dict(sidecar.os.environ, {}, clear=True):
                settings = sidecar.get_settings(path, True)
            self.assertEqual(settings["api_key"], "file-key")
            with patch.dict(sidecar.os.environ, {"YAPB_AI_API_KEY": "process-key"}, clear=True):
                settings = sidecar.get_settings(path, True)
            self.assertEqual(settings["api_key"], "process-key")

    def test_env_file_rejects_unsafe_permissions_and_syntax(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / ".env"
            path.write_text("DEEPSEEK_API_KEY=key\n")
            path.chmod(0o644)
            with self.assertRaises(ValueError):
                sidecar.load_env_file(path, True)
            path.chmod(0o600)
            path.write_text("THIS IS NOT ENV\n")
            with self.assertRaises(ValueError):
                sidecar.load_env_file(path, True)
            with self.assertRaises(ValueError):
                sidecar.load_env_file(path.with_name("missing"), True)


if __name__ == "__main__":
    unittest.main()
