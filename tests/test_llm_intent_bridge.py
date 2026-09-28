import importlib.util
import pathlib
import unittest


MODULE_PATH = pathlib.Path(__file__).resolve().parents[1] / "tools" / "llm_intent_bridge.py"
SPEC = importlib.util.spec_from_file_location("llm_intent_bridge", MODULE_PATH)
bridge = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(bridge)


class LlmIntentBridgeTests(unittest.TestCase):
    def test_approved_actions(self):
        self.assertEqual(bridge.to_command({"action": "follow", "bot_slot": 2, "player_slot": 0}), "yb ai 2 follow 0")
        self.assertEqual(bridge.to_command({"action": "jump", "bot_slot": 2}), "yb ai 2 jump")
        self.assertEqual(bridge.to_command({"action": "dead_chat", "bot_slot": 2, "line": 1}), "yb ai 2 dead_chat 1")
        self.assertEqual(bridge.to_command({"action": "chat", "bot_slot": 2, "channel": "team", "text": "yeah, maybe"}), 'yb ai 2 chat team "yeah, maybe"')
        self.assertEqual(bridge.to_command({"action": "chat", "bot_slot": 2, "channel": "team", "player_slot": 0, "text": "yeah, maybe"}), 'yb ai 2 chat team 0 "yeah, maybe"')

    def test_rejects_unapproved_or_injected_values(self):
        cases = [
            {"action": "exec", "bot_slot": 2, "command": "quit"},
            {"action": "jump", "bot_slot": "2; quit"},
            {"action": "follow", "bot_slot": 2, "player_slot": 32},
            {"action": "dead_chat", "bot_slot": 2, "line": "1; quit"},
            {"action": "jump", "bot_slot": 2, "extra": "anything"},
            {"action": "chat", "bot_slot": 2, "channel": "all", "text": "quit\";"},
            {"action": "chat", "bot_slot": 2, "channel": "team", "text": "A"},
            {"action": "chat", "bot_slot": 2, "channel": "team", "player_slot": 32, "text": "hi"},
        ]
        for case in cases:
            with self.subTest(case=case), self.assertRaises(ValueError):
                bridge.to_command(case)


if __name__ == "__main__":
    unittest.main()
