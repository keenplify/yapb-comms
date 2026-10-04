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
        self.assertEqual(self.event["mode"], "teams")
        self.assertEqual(self.event["map"], "de_dust2")
        self.assertEqual(self.event["place"], "bombsiteb")
        self.assertEqual(self.event["visible_slots"], [4, 5])
        self.assertTrue(self.event["bomb_planted"])

    def test_ffa_chat_is_public_and_cannot_order_a_bot(self):
        event = sidecar.parse_event(
            "2026-09-28 (INFO): [YaPB ai] chat bot=2 player=0 channel=all "
            "mode=ffa team=2 map=de_dust2 place=middle weapon=weapon_ak47 hp=67 "
            "money=3200 friends=0 enemies=9 bomb=0 round=42 visible=1 "
            "slots=4,-1,-1 text=hello\n")
        self.assertEqual(event["mode"], "ffa")
        self.assertEqual(sidecar.validate_model_choice(
            {"reply": "yo", "action": "none"}, event),
            ['yb ai 2 chat all 0 "yo"'])
        with self.assertRaises(ValueError):
            sidecar.validate_model_choice({"reply": "yo", "action": "jump"}, event)

    def test_player_reply_language_tracks_english_and_tagalog(self):
        self.assertEqual(sidecar.chat_language("hello bots"), "en")
        self.assertEqual(sidecar.chat_language("kamusta bots"), "tl")
        self.assertEqual(sidecar.chat_language("pwede drop ak ba"), "tl")
        self.assertEqual(sidecar.chat_language("ikaw bot ba"), "tl")
        self.assertEqual(sidecar.chat_language("hola amigo"), "auto")
        self.assertEqual(sidecar.chat_language("привет"), "auto")
        english = dict(self.event, text="hello", channel="all")
        with self.assertRaises(ValueError):
            sidecar.validate_model_choice({"reply": "sige", "action": "none"}, english)
        tagalog = dict(self.event, text="kamusta", channel="all")
        self.assertEqual(sidecar.validate_model_choice(
            {"reply": "ayos lang", "action": "none"}, tagalog),
            ['yb ai 2 chat all 0 "ayos lang"'])
        with self.assertRaises(ValueError):
            sidecar.validate_model_choice({"reply": "im good", "action": "none"}, tagalog)
        spanish = dict(self.event, text="hola amigo", channel="all")
        self.assertEqual(sidecar.validate_model_choice(
            {"reply": "hola, que tal", "action": "none"}, spanish),
            ['yb ai 2 chat all 0 "hola, que tal"'])
        cyrillic = dict(self.event, text="привет", channel="all")
        self.assertEqual(sidecar.validate_model_choice(
            {"reply": "привет", "action": "none"}, cyrillic),
            ['yb ai 2 chat all 0 "привет"'])

    def test_human_chat_preempts_event_flood(self):
        prefix = "2026-09-28 11:00:00 (INFO): [YaPB ai] "
        state = ("team=1 map=de_dust2 place=middle weapon=weapon_ak47 hp=67 "
                 "money=3200 friends=2 enemies=3 bomb=0 round=42 visible=0 "
                 "slots=-1,-1,-1 text=")
        lines = [prefix + f"event bot=2 player=-1 channel=team {state}nt\n" for _ in range(20)]
        lines.insert(10, prefix + f"chat bot=2 player=0 channel=all {state}bots\n")
        now = sidecar.datetime(2026, 9, 28, 11, 0, 1).timestamp()
        selected = sidecar.select_live_events(lines, now)
        self.assertEqual([item["kind"] for item in selected], ["chat", "event", "event"])
        self.assertEqual(sidecar.select_live_events(lines, now + 20), [])

    def test_validated_reply_and_action(self):
        self.assertEqual(sidecar.validate_model_choice(
            {"reply": "on my way", "action": "follow"}, self.event),
            ["yb ai 2 follow 0", 'yb ai 2 chat team 0 "on my way"'])

    def test_rejects_unsafe_output_and_all_chat_action(self):
        for choice in ({"reply": "HELLO", "action": "none"},
                       {"reply": "hi", "action": "exec"},
                       {"reply": 'hi"; quit', "action": "none"}):
            with self.subTest(choice=choice), self.assertRaises(ValueError):
                sidecar.validate_model_choice(choice, self.event)
        self.event["channel"] = "all"
        with self.assertRaises(ValueError):
            sidecar.validate_model_choice({"reply": "hi", "action": "jump"}, self.event)

    def test_sentence_periods_are_removed(self):
        self.assertEqual(sidecar.validate_model_choice(
            {"reply": "sure.", "action": "none"}, self.event),
            ['yb ai 2 chat team 0 "sure"'])

    def test_canned_event_preserves_tactical_facts(self):
        event = sidecar.parse_event(
            "2026-09-28 (INFO): [YaPB ai] event bot=2 player=-1 channel=team "
            "team=1 map=de_dust2 place=middle weapon=weapon_ak47 hp=67 "
            "money=3200 friends=2 enemies=3 bomb=0 round=42 visible=2 "
            "slots=4,5,-1 text=3 b, 2 mid\n")
        self.assertEqual(event["kind"], "event")
        self.assertEqual(event["player_slot"], -1)
        self.assertEqual(sidecar.validate_model_choice(
            {"reply": "3 b 2 mid", "action": "none"}, event),
            ['yb ai 2 chat team "3 b 2 mid"'])
        with self.assertRaises(ValueError):
            sidecar.validate_model_choice({"reply": "rush b", "action": "none"}, event)
        with self.assertRaises(ValueError):
            sidecar.validate_model_choice({"reply": "3 b 2 mid 1 a", "action": "none"}, event)
        with self.assertRaises(ValueError):
            sidecar.validate_model_choice({"reply": "3 b 2 mid", "action": "follow"}, event)

    def test_event_cache_is_private_persistent_and_bounded(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "replies.sqlite3"
            cache = sidecar.ReplyCache(path, max_variants=100)
            key = "de_dust2\x1fteams\x1fteam\x1fen\x1fnt"
            for number in range(102):
                cache.remember(key, f"nt {number}")
            self.assertEqual(len(cache.variants(key)), 100)
            self.assertEqual(path.stat().st_mode & 0o077, 0)
            cache.db.close()
            reopened = sidecar.ReplyCache(path, max_variants=100)
            self.assertEqual(len(reopened.variants(key)), 100)
            reopened.db.close()

    @unittest.skipUnless(sidecar.os.name == "posix", "POSIX file locking")
    def test_second_sidecar_cannot_use_same_queue(self):
        with tempfile.TemporaryDirectory() as directory:
            queue = pathlib.Path(directory)
            first = sidecar.acquire_instance_lock(queue)
            try:
                with self.assertRaises(ValueError):
                    sidecar.acquire_instance_lock(queue)
            finally:
                first.close()

    def test_discovers_separate_match_server_queues(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            expected = set()
            for name in ("match-one", "match-two"):
                logs = root / name / "cstrike" / "addons" / "yapb" / "data" / "logs"
                logs.mkdir(parents=True)
                (logs / "yapb_L28092026.txt").touch()
                expected.add(logs)
            self.assertEqual(sidecar.discover_instance_logs(root), expected)
            self.assertEqual(len({log.parent / "ai" for log in expected}), 2)
            old = next(iter(expected)) / "yapb_L28092026.txt"
            with patch.object(sidecar.time, "time", return_value=old.stat().st_mtime + 61):
                self.assertNotIn(old.parent, sidecar.discover_instance_logs(root))

    def test_shared_budget_limits_all_instances_together(self):
        budget = sidecar.SharedRequestBudget(2, 1, 0)
        self.assertTrue(budget.reserve("event"))
        self.assertFalse(budget.reserve("event"))
        self.assertTrue(budget.reserve("chat"))
        self.assertFalse(budget.reserve("chat"))

    def test_provider_probe_is_shared_across_instances(self):
        status = sidecar.SharedProviderStatus()
        with patch.object(sidecar, "probe_provider") as probe:
            self.assertTrue(status.ready("key", "https://example.test", "model", 6))
            self.assertTrue(status.ready("key", "https://example.test", "model", 6))
            probe.assert_called_once()
            status.failed()
            self.assertFalse(status.ready("key", "https://example.test", "model", 6))
            probe.assert_called_once()

    def test_shared_cache_fetches_and_publishes_validated_variants(self):
        with tempfile.TemporaryDirectory() as directory:
            local = sidecar.ReplyCache(pathlib.Path(directory) / "replies.sqlite3")
            shared = sidecar.SharedReplyCache(local, "https://example.test/internal/yapb/replies",
                                              "x" * 32)
            key = "de_dust2\x1fteams\x1fteam\x1fen\x1fnt"
            calls = []
            class Response:
                def __init__(self, data): self.data = data
                def __enter__(self): return self
                def __exit__(self, *_): return False
                def read(self, _): return self.data
            def fake_urlopen(req, timeout):
                calls.append((req, timeout))
                return Response(b'{"variants":["nice try"]}' if req.get_method() == "GET" else b'{}')
            with patch.object(sidecar.request, "urlopen", fake_urlopen):
                self.assertEqual(shared.variants(key), ["nice try"])
                shared.remember(key, "unlucky")
            self.assertEqual(len(calls), 2)
            self.assertEqual(json.loads(calls[1][0].data)["reply"], "unlucky")
            self.assertEqual(set(local.variants(key)), {"nice try", "unlucky"})
            local.db.close()

    def test_cache_separates_ffa_public_speech_from_team_speech(self):
        team_event = {"map": "de_dust2", "mode": "teams", "channel": "team", "text": "my bad"}
        ffa_event = {"map": "de_dust2", "mode": "ffa", "channel": "all", "text": "my bad"}
        self.assertNotEqual(sidecar.ReplyCache.key(team_event), sidecar.ReplyCache.key(ffa_event))

    def test_conversation_context_is_bounded_and_isolated(self):
        context = sidecar.ConversationContext(ttl=30, max_exchanges=2)
        event = dict(self.event, kind="chat", text="hello", player_slot=0,
                     bot_slot=2, channel="all", mode="teams")
        context.remember(event, "hey", now=10)
        self.assertEqual(context.recent(event, now=11)[-1]["text"], "hey")
        self.assertEqual(context.recent(dict(event, player_slot=1), now=11), [])
        self.assertEqual(context.recent(dict(event, channel="team"), now=11), [])
        self.assertEqual(context.recent(dict(event, mode="ffa"), now=11), [])
        context.remember(dict(event, text="you good"), "yeah", now=12)
        context.remember(dict(event, text="and now"), "still good", now=13)
        self.assertEqual(len(context.recent(event, now=14)), 4)
        self.assertEqual(context.recent(event, now=44), [])

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
            (queue / "pending.cmd").unlink()
            sidecar.send_local('yb ai 2 chat team 0 "on my way"', queue)
            self.assertEqual((queue / "pending.cmd").read_text(),
                             'yb ai 2 chat team 0 "on my way"')
            (queue / "pending.cmd").unlink()
            sidecar.send_local('yb ai 2 chat all 0 "привет"', queue)
            self.assertEqual((queue / "pending.cmd").read_text(encoding="utf-8"),
                             'yb ai 2 chat all 0 "привет"')

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
        self.assertEqual(payloads[0]["max_tokens"], 128)
        self.assertEqual(state["weapon"], "weapon_ak47")
        self.assertEqual(state["visible_slots"], [4, 5])

    def test_plain_and_fenced_provider_replies(self):
        self.assertEqual(sidecar.parse_model_content("im from cali u"),
                         {"reply": "im from cali u", "action": "none"})
        self.assertEqual(sidecar.parse_model_content('```json\n{"reply":"yo","action":"none"}\n```'),
                         {"reply": "yo", "action": "none"})

    def test_probe_checks_configured_model(self):
        class Response:
            status = 200
            def __enter__(self): return self
            def __exit__(self, *_): return False
            def read(self, _): return b'{}'

        with patch.object(sidecar.request, "urlopen", return_value=Response()) as send:
            sidecar.probe_provider("key", "https://example.test/chat", "deepseek-flash", 6)
        self.assertEqual(json.loads(send.call_args.args[0].data)["model"], "deepseek-flash")

    def test_event_prompt_includes_cached_variants(self):
        event = dict(self.event, kind="event", player_slot=-1, text="nt")
        class Response:
            status = 200
            def __enter__(self): return self
            def __exit__(self, *_): return False
            def read(self, _): return b'{"choices":[{"message":{"content":"{\\"reply\\":\\"nice try\\",\\"action\\":\\"none\\"}"}}]}'
        with patch.object(sidecar.request, "urlopen", return_value=Response()) as send:
            result = sidecar.ask_provider(event, "key", "https://example.test/chat",
                                          "deepseek-flash", 6, ["nt"])
        self.assertEqual(result["reply"], "nice try")
        body = json.loads(send.call_args.args[0].data)
        self.assertEqual(json.loads(body["messages"][1]["content"])["avoid_repeating"], ["nt"])

    def test_env_file_and_process_override(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / ".env"
            path.write_text("# private credentials\nexport DEEPSEEK_API_KEY='file-key'\n"
                            "YAPB_AI_MODEL=deepseek-chat\n")
            path.chmod(0o600)
            with patch.dict(sidecar.os.environ, {}, clear=True):
                settings = sidecar.get_settings(path, True)
            self.assertEqual(settings["api_key"], "file-key")
            self.assertEqual(settings["model"], "deepseek-chat")
            with patch.dict(sidecar.os.environ, {"YAPB_AI_API_KEY": "process-key"}, clear=True):
                settings = sidecar.get_settings(path, True)
            self.assertEqual(settings["api_key"], "process-key")

    def test_default_model_is_deepseek_flash(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / ".env"
            path.write_text("DEEPSEEK_API_KEY=test-key\n")
            path.chmod(0o600)
            with patch.dict(sidecar.os.environ, {}, clear=True):
                self.assertEqual(sidecar.get_settings(path, True)["model"], "deepseek-flash")

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
