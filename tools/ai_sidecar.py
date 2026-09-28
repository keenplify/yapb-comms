#!/usr/bin/env python3
"""Optional YaPB chat sidecar for OpenAI-compatible providers such as DeepSeek.

One process serves one HLDS instance. It reads addressed chat and bot speech
events from YaPB's local log and returns bounded chat through a local queue.
"""

import argparse
from collections import deque
import copy
from datetime import datetime
import json
import logging
from logging.handlers import RotatingFileHandler
import os
if os.name == "posix":
    import fcntl
from pathlib import Path
import random
import re
import sqlite3
import sys
import tempfile
import unicodedata
import threading
import time
from urllib import request
from urllib.parse import urlencode, urlparse

from llm_intent_bridge import to_command


EVENT = re.compile(
    r"\[YaPB ai\] (chat|event) bot=(\d+) player=(-?\d+) channel=(team|all) "
    r"(?:mode=(teams|ffa) )?team=(\d+) map=([a-z0-9_]+) place=([a-z0-9_]+) "
    r"weapon=([a-zA-Z0-9_]+) hp=(-?\d+) money=(\d+) friends=(\d+) "
    r"enemies=(\d+) bomb=([01]) round=(-?\d+) visible=(\d+) "
    r"slots=(-?\d+),(-?\d+),(-?\d+) text=([^\r\n]{1,96})$"
)
SAFE_REPLY = re.compile(r"[^\r\n]{1,30}\Z")
SYSTEM_PROMPT = (
    "You are a Counter-Strike 1.6 player. Reply like a real player: "
    "lowercase, at most 30 characters, brief and natural. No periods. Never reveal that "
    "you are an AI or claim certainty about being human. Return only a JSON "
    "object with keys reply and action. action is one of none, jump, follow. "
    "Use jump or follow only for a direct teammate request in teams mode; otherwise none. "
    "In ffa mode everyone is an opponent: use all chat and never issue actions. "
    "Detect the player's language and reply in that same language, including "
    "English, Tagalog, Taglish, and other languages. Keep their script when "
    "the game chat supports it. Never switch to an unrelated language. "
    "If asked why you cannot use voice chat, give a brief plausible excuse "
    "such as a broken mic or noisy room, phrased in the player's language. "
    "Use recent_dialogue when present to understand follow-up messages; "
    "the current player_message and bot_state always take priority. "
    "No tactics or enemy facts unless provided by the player."
)
EVENT_PROMPT = (
    "You are a Counter-Strike 1.6 player. Rephrase the supplied bot line "
    "like brief real player chat. lowercase, no periods, at most 30 characters. "
    "Keep every number, location, and buy or eco decision exactly. Do not "
    "invent an enemy, position, weapon, or action. Use fresh wording when "
    "possible. Always use English for these bot lines. In ffa mode everyone is "
    "an opponent and speech is public. Return only JSON with "
    "reply and action; action must be none."
)
CONFIG_KEYS = (
    "YAPB_AI_API_KEY", "DEEPSEEK_API_KEY",
    "YAPB_AI_API_URL", "YAPB_AI_MODEL",
    "YAPB_CACHE_API_URL", "YAPB_CACHE_TOKEN",
)


def load_env_file(path, required=False):
    """Read a small .env file as data; never execute it as shell code."""
    if not path.exists():
        if required:
            raise ValueError(f"environment file was not found: {path}")
        return {}
    if not path.is_file() or path.stat().st_size > 16_384:
        raise ValueError("environment file must be a regular file under 16 KB")
    if os.name == "posix" and path.stat().st_mode & 0o077:
        raise ValueError(f"environment file is readable by others; run: chmod 600 {path}")
    values = {}
    for number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("export "):
            line = line[7:].lstrip()
        if "=" not in line:
            raise ValueError(f"invalid environment file line {number}")
        key, value = (part.strip() for part in line.split("=", 1))
        if not re.fullmatch(r"[A-Z][A-Z0-9_]*", key):
            raise ValueError(f"invalid environment variable name on line {number}")
        if len(value) >= 2 and value[0] in ("'", '"') and value[-1] == value[0]:
            value = value[1:-1]
        elif value.startswith(("'", '"')):
            raise ValueError(f"unclosed quoted value on line {number}")
        if key in CONFIG_KEYS:
            values[key] = value
    return values


def get_settings(path, required=False):
    file_values = load_env_file(path, required)
    values = {key: os.environ.get(key) or file_values.get(key, "") for key in CONFIG_KEYS}
    return {
        "api_key": (os.environ.get("YAPB_AI_API_KEY") or os.environ.get("DEEPSEEK_API_KEY")
                    or file_values.get("YAPB_AI_API_KEY") or file_values.get("DEEPSEEK_API_KEY", "")),
        "api_url": values["YAPB_AI_API_URL"] or "https://api.deepseek.com/chat/completions",
        "model": values["YAPB_AI_MODEL"] or "deepseek-flash",
        "cache_api_url": values["YAPB_CACHE_API_URL"],
        "cache_token": values["YAPB_CACHE_TOKEN"],
    }


def parse_event(line):
    match = EVENT.search(line.rstrip("\r\n"))
    if not match:
        return None
    (kind, bot, player, channel, mode, team, map_name, place, weapon, hp, money,
     friends, enemies, bomb, round_seconds, visible_count,
     slot1, slot2, slot3, text) = match.groups()
    if (int(bot) >= 32 or (kind == "chat" and not 0 <= int(player) < 32)
            or (kind == "event" and int(player) != -1)):
        return None
    return {"kind": kind, "bot_slot": int(bot), "player_slot": int(player),
            "channel": channel, "mode": mode or "teams", "team": int(team), "text": text,
            "map": map_name, "place": place, "weapon": weapon,
            "hp": int(hp), "money": int(money), "friends_alive": int(friends),
            "enemies_alive": int(enemies), "bomb_planted": bomb == "1",
            "round_seconds": int(round_seconds), "visible_count": int(visible_count),
            "visible_slots": [int(value) for value in (slot1, slot2, slot3) if int(value) >= 0]}


def chat_language(text):
    """Hint for short known phrases; let the model detect other languages."""
    words = set(re.findall(r"[a-z]+", text.lower()))
    strong = {"kamusta", "kumusta", "musta", "bakit", "saan", "paano", "pwede",
              "salamat", "sige", "hindi", "wala", "meron", "sakin", "natin",
              "nyo", "mga", "dito", "diyan", "yan", "ganun", "ganyan", "tara",
              "opo", "oo", "pre", "ano", "ikaw", "kayo", "tayo", "kami",
              "lang", "naman", "talaga", "ingat", "ayos", "gusto", "kailangan",
              "sino", "kailan", "sali", "laro", "laban", "tama",
              "pasensya", "sandali", "ulit", "teka", "grabe"}
    weak = {"ako", "ko", "mo", "ka", "ba", "ang", "sa", "ng", "pa", "po"}
    if words & strong or len(words & weak) >= 2:
        return "tl"
    english = {"hello", "hey", "hi", "how", "what", "why", "where", "who",
               "are", "is", "you", "your", "thanks", "please", "sorry",
               "good", "can", "could", "everyone", "anyone", "yes", "no"}
    return "en" if words & english else "auto"


class ConversationContext:
    """Small per-instance memory; never persists player chat to the reply cache."""

    def __init__(self, ttl=180.0, max_pairs=64, max_exchanges=4):
        self.ttl = ttl
        self.max_pairs = max_pairs
        self.max_exchanges = max_exchanges
        self.dialogues = {}

    @staticmethod
    def key(event):
        return (event["player_slot"], event["bot_slot"], event["channel"],
                event.get("mode", "teams"), event["map"])

    def recent(self, event, now=None):
        now = time.monotonic() if now is None else now
        key = self.key(event)
        updated, turns = self.dialogues.get(key, (0.0, []))
        if now - updated > self.ttl:
            self.dialogues.pop(key, None)
            return []
        return list(turns)

    def remember(self, event, reply, now=None):
        now = time.monotonic() if now is None else now
        key = self.key(event)
        turns = self.recent(event, now)
        turns.extend(({"speaker": "player", "text": event["text"]},
                      {"speaker": "bot", "text": reply}))
        self.dialogues[key] = (now, turns[-2 * self.max_exchanges:])
        if len(self.dialogues) > self.max_pairs:
            oldest = min(self.dialogues, key=lambda item: self.dialogues[item][0])
            self.dialogues.pop(oldest, None)


def select_live_events(lines, now=None):
    """Answer people first; discard old speech instead of replaying a backlog."""
    now = time.time() if now is None else now
    chats = []
    canned = []
    for line in lines:
        event = parse_event(line)
        if event is None:
            continue
        try:
            logged_at = datetime.strptime(line[:19], "%Y-%m-%d %H:%M:%S").timestamp()
        except ValueError:
            logged_at = now
        if logged_at < now - 15 or logged_at > now + 5:
            continue
        (chats if event["kind"] == "chat" else canned).append(event)
    # Canned callouts are frequent; cap each read so they cannot delay people.
    return chats + canned[-2:]


def validate_model_choice(choice, event):
    if not isinstance(choice, dict) or set(choice) != {"reply", "action"}:
        raise ValueError("model response must contain reply and action")
    reply = choice["reply"]
    action = choice["action"]
    if not isinstance(reply, str):
        raise ValueError("model reply is unsafe or too long")
    reply = reply.replace(".", "").strip()
    if (not SAFE_REPLY.fullmatch(reply) or len(reply.encode("utf-8")) > 90
            or not all((unicodedata.category(char)[0] in "LMN"
                        and char == char.lower()) or char in " .,?!'-" for char in reply)):
        raise ValueError("model reply is unsafe or too long")
    expected_language = "en" if event.get("kind") == "event" else chat_language(event["text"])
    if expected_language == "en" and chat_language(reply) == "tl":
        raise ValueError("model switched an English reply to Tagalog")
    if expected_language == "en" and not reply.isascii():
        raise ValueError("model switched an English reply to another script")
    if expected_language == "tl" and chat_language(reply) != "tl":
        raise ValueError("model switched a Tagalog reply to English")
    if action not in ("none", "jump", "follow"):
        raise ValueError("model action is not approved")
    if event.get("kind") == "event":
        if action != "none":
            raise ValueError("bot speech event cannot request an action")
        required = re.findall(r"\b(?:\d+|mid|eco|buy|drop|cover)\b", event["text"])
        required += re.findall(r"\b(?:all|rush|go|rotate|leave|watch|hold|bombsite|\d+)\s+([ab])\b",
                               event["text"])
        words = re.findall(r"\b(?:\d+|a|b|mid|eco|buy|drop|cover)\b", reply)
        if any(words.count(word) < required.count(word) for word in set(required)):
            raise ValueError("model changed a tactical fact")
        if any(words.count(word) > required.count(word) for word in set(words)
               if word.isdigit() or word in ("mid", "eco", "buy")):
            raise ValueError("model invented a tactical fact")
    if action != "none" and (event["channel"] != "team" or event.get("mode") == "ffa"):
        raise ValueError("all-chat and ffa cannot request bot actions")
    commands = []
    if action == "jump":
        commands.append(to_command({"action": "jump", "bot_slot": event["bot_slot"]}))
    elif action == "follow":
        commands.append(to_command({"action": "follow", "bot_slot": event["bot_slot"],
                                    "player_slot": event["player_slot"]}))
    commands.append(chat_command(event, reply))
    return commands


def chat_command(event, reply):
    payload = {"action": "chat", "bot_slot": event["bot_slot"],
               "channel": event["channel"], "text": reply}
    if event["kind"] == "chat":
        payload["player_slot"] = event["player_slot"]
    return to_command(payload)


def ask_provider(event, api_key, api_url, model, timeout, variants=(), dialogue=()):
    message = {
        "mode": event.get("mode", "teams"),
        "channel": event["channel"],
        "reply_language": "en" if event.get("kind") == "event" else chat_language(event["text"]),
        "bot_line" if event.get("kind") == "event" else "player_message": event["text"],
        "bot_state": {key: event[key] for key in (
            "map", "place", "weapon", "hp", "money", "friends_alive",
            "enemies_alive", "bomb_planted", "round_seconds",
            "visible_count", "visible_slots")},
    }
    if event.get("kind") == "event" and variants:
        message["avoid_repeating"] = random.sample(list(variants), min(12, len(variants)))
    if event.get("kind") == "chat" and dialogue:
        message["recent_dialogue"] = list(dialogue)
    body = {
        "model": model,
        "messages": [
            {"role": "system", "content": EVENT_PROMPT if event.get("kind") == "event" else SYSTEM_PROMPT},
            {"role": "user", "content": json.dumps(message)},
        ],
        "temperature": 0.7,
        "max_tokens": 256,
    }
    payload = json.dumps(body).encode("utf-8")
    req = request.Request(api_url, data=payload, method="POST", headers={
        "Authorization": f"Bearer {api_key}",
        "Content-Type": "application/json",
    })
    with request.urlopen(req, timeout=timeout) as response:
        if response.status != 200:
            raise RuntimeError(f"provider status {response.status}")
        raw = response.read(65537)
    if len(raw) > 65536:
        raise ValueError("provider response too large")
    content = json.loads(raw)["choices"][0]["message"]["content"]
    return json.loads(content)


def probe_provider(api_key, api_url, model, timeout):
    """Check the configured model and credentials with one tiny request."""
    payload = json.dumps({
        "model": model,
        "messages": [{"role": "user", "content": "Reply ok."}],
        "max_tokens": 1,
    }).encode("utf-8")
    req = request.Request(api_url, data=payload, method="POST", headers={
        "Authorization": f"Bearer {api_key}",
        "Content-Type": "application/json",
    })
    with request.urlopen(req, timeout=timeout) as response:
        if response.status != 200:
            raise RuntimeError(f"provider status {response.status}")
        response.read(4096)


def send_local(command, directory, timeout=3.0):
    """Atomically publish one approved command; YaPB removes it after reading."""
    chat = re.fullmatch(r'yb ai (\d{1,2}) chat (team|all)(?: (\d{1,2}))? "([^"\r\n]+)"', command)
    if chat:
        bot, channel, player, line = chat.groups()
        payload = {"action": "chat", "bot_slot": int(bot), "channel": channel, "text": line}
        if player is not None:
            payload["player_slot"] = int(player)
        try:
            valid = to_command(payload) == command
        except ValueError:
            valid = False
    else:
        valid = bool(re.fullmatch(r'yb ai (?:[0-9]|[12][0-9]|3[01]) '
                                  r'(?:jump|follow (?:[0-9]|[12][0-9]|3[01])|dead_chat [012])', command))
    if not valid:
        raise ValueError("invalid local AI command")
    directory.mkdir(mode=0o700, parents=True, exist_ok=True)
    if os.name == "posix":
        directory.chmod(0o700)
    if os.name == "posix" and directory.stat().st_mode & 0o077:
        raise ValueError("AI queue directory must be private (chmod 700)")
    pending = directory / "pending.cmd"
    deadline = time.monotonic() + timeout
    while pending.exists():
        if time.monotonic() >= deadline:
            raise TimeoutError("HLDS did not consume the AI command; check yb_ai_bridge 1")
        time.sleep(0.05)
    tmp = None
    try:
        with tempfile.NamedTemporaryFile("w", encoding="utf-8", prefix=".ai-",
                                         suffix=".tmp", dir=directory, delete=False) as stream:
            tmp = Path(stream.name)
            stream.write(command)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(tmp, pending)
    finally:
        if tmp is not None:
            tmp.unlink(missing_ok=True)


def latest_log(directory):
    return max(directory.glob("yapb_L*.txt"), key=lambda path: path.stat().st_mtime, default=None)


def acquire_instance_lock(directory):
    if os.name != "posix":
        return None
    lock_path = directory / "sidecar.lock"
    stream = lock_path.open("a+b")
    lock_path.chmod(0o600)
    try:
        fcntl.flock(stream.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError as exc:
        stream.close()
        raise ValueError(f"another sidecar is already using {directory}") from exc
    return stream


class ReplyCache:
    """Bounded per-instance SQLite variants for fixed bot speech events."""

    def __init__(self, path, max_variants=1000):
        self.max_variants = max_variants
        self.global_limit = max(5000, 50 * max_variants)
        self.db = sqlite3.connect(path, timeout=2)
        if os.name == "posix":
            path.chmod(0o600)
        self.db.execute("CREATE TABLE IF NOT EXISTS replies ("
                        "key TEXT NOT NULL, reply TEXT NOT NULL, created INTEGER NOT NULL, "
                        "PRIMARY KEY (key, reply))")
        self.db.commit()

    @staticmethod
    def key(event):
        return "\x1f".join((event["map"], event.get("mode", "teams"),
                            event["channel"], "en", event["text"]))

    def variants(self, key):
        return [row[0] for row in self.db.execute(
            "SELECT reply FROM replies WHERE key = ?", (key,))]

    def remember(self, key, reply):
        self.merge(key, [reply])

    def merge(self, key, replies):
        present = set(self.variants(key))
        additions = [reply for reply in replies if reply not in present]
        additions = additions[:max(0, self.max_variants - len(present))]
        if not additions:
            return
        self.db.executemany("INSERT OR IGNORE INTO replies VALUES (?, ?, ?)",
                            ((key, reply, int(time.time())) for reply in additions))
        count = self.db.execute("SELECT COUNT(*) FROM replies").fetchone()[0]
        if count > self.global_limit:
            self.db.execute("DELETE FROM replies WHERE rowid IN ("
                            "SELECT rowid FROM replies ORDER BY created, rowid LIMIT ?)",
                            (count - self.global_limit,))
        self.db.commit()


class SharedReplyCache:
    """Local hot cache with bounded synchronization to the backend's shared DB."""

    def __init__(self, local, url, token, timeout=2.0):
        self.local = local
        self.url = url.rstrip("/")
        self.token = token
        self.timeout = timeout
        self.last_fetch = {}
        self.max_variants = local.max_variants

    key = staticmethod(ReplyCache.key)

    @staticmethod
    def fields(key):
        map_name, mode, channel, language, line = key.split("\x1f", 4)
        return {"map": map_name, "mode": mode, "channel": channel,
                "language": language, "line": line}

    def variants(self, key):
        now = time.monotonic()
        if now - self.last_fetch.get(key, -300.0) >= 300.0:
            self.last_fetch[key] = now
            try:
                url = f"{self.url}?{urlencode(self.fields(key))}"
                req = request.Request(url, headers={"Authorization": f"Bearer {self.token}"})
                with request.urlopen(req, timeout=self.timeout) as response:
                    raw = response.read(65537)
                if len(raw) > 65536:
                    raise ValueError("shared cache response is too large")
                values = json.loads(raw)["variants"]
                if not isinstance(values, list) or len(values) > 1000 or any(
                        not isinstance(value, str) or not SAFE_REPLY.fullmatch(value)
                        for value in values):
                    raise ValueError("shared cache returned invalid variants")
                self.local.merge(key, values)
            except Exception as exc:
                logging.getLogger("yapb_ai_sidecar").warning(
                    "shared cache read unavailable: %s", type(exc).__name__)
        return self.local.variants(key)

    def remember(self, key, reply):
        self.local.remember(key, reply)
        try:
            payload = json.dumps({**self.fields(key), "reply": reply}).encode("utf-8")
            req = request.Request(self.url, data=payload, method="POST", headers={
                "Authorization": f"Bearer {self.token}",
                "Content-Type": "application/json",
            })
            with request.urlopen(req, timeout=self.timeout) as response:
                response.read(1024)
        except Exception as exc:
            logging.getLogger("yapb_ai_sidecar").warning(
                "shared cache write unavailable: %s", type(exc).__name__)


class SharedRequestBudget:
    """One provider request budget for every HLDS handled by this process."""

    def __init__(self, hourly_limit, event_hourly_limit, min_interval):
        self.hourly_limit = hourly_limit
        self.event_hourly_limit = event_hourly_limit
        self.min_interval = min_interval
        self.requests = deque()
        self.events = deque()
        self.last_request = 0.0
        self.last_chat_request = 0.0
        self.lock = threading.Lock()

    def reserve(self, kind):
        with self.lock:
            now = time.monotonic()
            while self.requests and self.requests[0] < now - 3600:
                self.requests.popleft()
            while self.events and self.events[0] < now - 3600:
                self.events.popleft()
            interval = 2.0 if kind == "chat" else self.min_interval
            previous = self.last_chat_request if kind == "chat" else self.last_request
            if (len(self.requests) >= self.hourly_limit
                    or now - previous < interval
                    or (kind == "event" and len(self.events) >= self.event_hourly_limit)):
                return False
            self.requests.append(now)
            if kind == "event":
                self.events.append(now)
            else:
                self.last_chat_request = now
            self.last_request = now
            return True


class SharedProviderStatus:
    """Probe a provider once for all HLDS instances on this host."""

    def __init__(self):
        self.connected = False
        self.next_probe = 0.0
        self.lock = threading.Lock()

    def ready(self, api_key, api_url, model, timeout):
        with self.lock:
            if self.connected:
                return True
            now = time.monotonic()
            if now < self.next_probe:
                return False
            self.next_probe = now + 30.0
        try:
            probe_provider(api_key, api_url, model, timeout)
        except Exception:
            return False
        with self.lock:
            self.connected = True
        return True

    def failed(self):
        with self.lock:
            self.connected = False
            self.next_probe = time.monotonic() + 30.0


def discover_instance_logs(root):
    """Find recently active YaPB instances, skipping retained old matches."""
    if not root.is_dir():
        return set()
    found = set()
    now = time.time()
    for instance in root.iterdir():
        if not instance.is_dir() or instance.is_symlink():
            continue
        logs = instance / "cstrike" / "addons" / "yapb" / "data" / "logs"
        try:
            latest = latest_log(logs) if logs.is_dir() else None
            if latest is not None and now - latest.stat().st_mtime <= 60:
                found.add(logs)
        except OSError:
            continue  # a match may be removed while it is being scanned
    return found


def run(args, stop_event=None, shared_budget=None, provider_status=None):
    settings = get_settings(args.env_file, args.env_file_explicit)
    api_key = settings["api_key"]
    api_url = settings["api_url"]
    model = settings["model"]
    if not api_key:
        raise ValueError("set YAPB_AI_API_KEY (or DEEPSEEK_API_KEY)")
    provider_url = urlparse(api_url)
    if (provider_url.scheme != "https" and
            not (provider_url.scheme == "http" and
                 provider_url.hostname in ("127.0.0.1", "localhost", "::1"))):
        raise ValueError("YAPB_AI_API_URL must use https or loopback http")
    if not provider_url.hostname or provider_url.username or provider_url.password:
        raise ValueError("invalid provider URL")
    cache_api_url = settings["cache_api_url"]
    cache_token = settings["cache_token"]
    if bool(cache_api_url) != bool(cache_token):
        raise ValueError("set both YAPB_CACHE_API_URL and YAPB_CACHE_TOKEN")
    if cache_api_url:
        parsed = urlparse(cache_api_url)
        if (parsed.scheme != "https" and
                not (parsed.scheme == "http" and parsed.hostname in ("127.0.0.1", "localhost", "::1"))):
            raise ValueError("shared cache URL must use https or loopback http")
        if parsed.username or parsed.password or parsed.query or parsed.fragment or len(cache_token) < 32:
            raise ValueError("invalid shared cache URL or token")
    args.log_dir.mkdir(parents=True, exist_ok=True)
    args.queue_dir.mkdir(mode=0o700, parents=True, exist_ok=True)
    if os.name == "posix":
        args.queue_dir.chmod(0o700)
    instance_lock = acquire_instance_lock(args.queue_dir)
    diagnostics = logging.getLogger(f"yapb_ai_sidecar.instance_{id(args)}")
    diagnostics.setLevel(logging.INFO)
    handler = RotatingFileHandler(
        args.log_dir / "ai_sidecar.log", maxBytes=1_000_000, backupCount=2)
    handler.setFormatter(logging.Formatter("%(asctime)s %(levelname)s %(message)s"))
    diagnostics.addHandler(handler)
    handled = deque()
    current = None
    offset = 0
    last_request = 0.0
    last_chat_request = 0.0
    print("YaPB AI sidecar watching chat and bot events", flush=True)
    diagnostics.info("sidecar started with local queue %s", args.queue_dir)
    ready = args.queue_dir / "ready"
    ready.unlink(missing_ok=True)
    cache = ReplyCache(args.queue_dir / "replies.sqlite3", args.max_event_variants)
    conversations = ConversationContext()
    if cache_api_url:
        cache = SharedReplyCache(cache, cache_api_url, cache_token)
    event_handled = deque()
    connected = False
    next_probe = 0.0
    last_heartbeat = 0.0
    while stop_event is None or not stop_event.is_set():
        now = time.monotonic()
        if provider_status is not None:
            was_connected = connected
            connected = provider_status.ready(api_key, api_url, model, args.timeout)
            if connected and not was_connected:
                current = latest_log(args.log_dir)
                offset = current.stat().st_size if current is not None else 0
                diagnostics.info("provider ready model=%s", model)
            elif not connected and was_connected:
                ready.unlink(missing_ok=True)
        elif not connected and now >= next_probe:
            try:
                probe_provider(api_key, api_url, model, args.timeout)
                connected = True
                current = latest_log(args.log_dir)
                offset = current.stat().st_size if current is not None else 0
                diagnostics.info("provider ready model=%s", model)
                print(f"DeepSeek sidecar ready: {model}", flush=True)
            except Exception as exc:
                next_probe = now + 30.0
                diagnostics.error("provider unavailable: %s: %s", type(exc).__name__, exc)
        if not connected:
            time.sleep(0.5)
            continue
        if now - last_heartbeat >= 2.0:
            ready.touch(mode=0o600, exist_ok=True)
            last_heartbeat = now
        path = latest_log(args.log_dir)
        if path is None:
            time.sleep(0.5)
            continue
        if path != current:
            current = path
            offset = path.stat().st_size  # start live; do not replay old chat
        size = path.stat().st_size
        if size < offset:
            offset = 0
        if size == offset:
            time.sleep(0.25)
            continue
        with path.open("r", encoding="utf-8", errors="replace") as stream:
            stream.seek(offset)
            lines = []
            while True:
                start = stream.tell()
                line = stream.readline()
                if not line:
                    break
                if not line.endswith("\n"):
                    stream.seek(start)
                    break
                lines.append(line)
            offset = stream.tell()
        for event in select_live_events(lines):
            if not connected:
                language = chat_language(event["text"])
                fallback = event["text"] if event["kind"] == "event" else (
                    "sandali" if language == "tl" else "sry lagging" if language == "en" else "?")
                try:
                    send_local(chat_command(event, fallback), args.queue_dir)
                except Exception as exc:
                    diagnostics.error("offline fallback failed: %s: %s", type(exc).__name__, exc)
                continue
            now = time.monotonic()
            while handled and handled[0] < now - 3600:
                handled.popleft()
            while event_handled and event_handled[0] < now - 3600:
                event_handled.popleft()
            interval = 2.0 if event["kind"] == "chat" else args.min_interval
            previous = last_chat_request if event["kind"] == "chat" else last_request
            can_request = len(handled) < args.hourly_limit and now - previous >= interval
            if event["kind"] == "event":
                key = cache.key(event)
                variants = cache.variants(key)
                can_generate = can_request and len(event_handled) < args.event_hourly_limit
                if variants and (len(variants) >= cache.max_variants or not can_generate or random.random() < 0.3):
                    reply = random.choice(variants)
                    source = "cache"
                elif can_generate and (shared_budget is None or shared_budget.reserve("event")):
                    handled.append(now)
                    event_handled.append(now)
                    last_request = now
                    try:
                        choice = ask_provider(event, api_key, api_url, model, args.timeout, variants)
                        validate_model_choice(choice, event)
                        reply = choice["reply"].replace(".", "").strip()
                        cache.remember(key, reply)
                        source = "model"
                    except Exception as exc:
                        reply = random.choice(variants) if variants else event["text"]
                        source = "fallback"
                        diagnostics.error("event generation failed: %s: %s", type(exc).__name__, exc)
                        if not isinstance(exc, ValueError):
                            connected = False
                            if provider_status is not None:
                                provider_status.failed()
                            ready.unlink(missing_ok=True)
                            next_probe = time.monotonic() + 30.0
                else:
                    reply = event["text"]
                    source = "original"
                try:
                    time.sleep(random.uniform(0.6, 1.5))
                    send_local(chat_command(event, reply), args.queue_dir)
                    diagnostics.info("event bot=%d source=%s", event["bot_slot"], source)
                except Exception as exc:
                    diagnostics.error("event delivery failed: %s: %s", type(exc).__name__, exc)
                continue
            if can_request and shared_budget is not None:
                can_request = shared_budget.reserve("chat")
            if not can_request:
                try:
                    time.sleep(random.uniform(0.6, 1.5))
                    language = chat_language(event["text"])
                    line = "sandali" if language == "tl" else "one sec" if language == "en" else "?"
                    send_local(chat_command(event, line), args.queue_dir)
                except Exception as exc:
                    diagnostics.error("rate-limit reply failed: %s: %s", type(exc).__name__, exc)
                continue
            handled.append(now)
            last_request = now
            last_chat_request = now
            try:
                choice = ask_provider(event, api_key, api_url, model, args.timeout,
                                      dialogue=conversations.recent(event))
            except Exception as exc:
                connected = False
                if provider_status is not None:
                    provider_status.failed()
                ready.unlink(missing_ok=True)
                next_probe = time.monotonic() + 30.0
                diagnostics.error("provider request failed: %s: %s", type(exc).__name__, exc)
                try:
                    time.sleep(random.uniform(0.6, 1.5))
                    language = chat_language(event["text"])
                    line = "sandali lag ako" if language == "tl" else "sry lagging" if language == "en" else "?"
                    send_local(chat_command(event, line), args.queue_dir)
                except Exception as queue_exc:
                    diagnostics.error("fallback failed: %s: %s",
                                      type(queue_exc).__name__, queue_exc)
                continue
            try:
                commands = validate_model_choice(choice, event)
                time.sleep(random.uniform(0.6, 1.5))
                for command in commands:
                    send_local(command, args.queue_dir)
                conversations.remember(event, choice["reply"].replace(".", "").strip())
                print(f"answered bot={event['bot_slot']} channel={event['channel']}", flush=True)
                diagnostics.info("answered bot=%d channel=%s", event["bot_slot"], event["channel"])
            except Exception as exc:
                print(f"AI event skipped: {type(exc).__name__}: {exc}", file=sys.stderr, flush=True)
                diagnostics.error("event skipped: %s: %s", type(exc).__name__, exc)
                try:
                    language = chat_language(event["text"])
                    line = "ano ibig mo sabihin" if language == "tl" else "what u mean" if language == "en" else "?"
                    send_local(chat_command(event, line), args.queue_dir)
                except Exception:
                    pass
    ready.unlink(missing_ok=True)
    cache.local.db.close() if isinstance(cache, SharedReplyCache) else cache.db.close()
    if instance_lock is not None:
        instance_lock.close()
    diagnostics.removeHandler(handler)
    handler.close()


def run_instances(args):
    root = args.instances_root.resolve()
    if not root.is_dir():
        raise ValueError(f"instances root does not exist: {root}")
    root_lock = acquire_instance_lock(root)
    budget = SharedRequestBudget(args.hourly_limit, args.event_hourly_limit,
                                 args.min_interval)
    provider_status = SharedProviderStatus()
    workers = {}
    print(f"YaPB AI sidecar watching instances under {root}", flush=True)
    try:
        while True:
            found = discover_instance_logs(root)
            # Keep a watched match through quiet stretches, then free its
            # worker when YaPB has produced no game log for ten minutes.
            for log_dir in workers:
                try:
                    latest = latest_log(log_dir) if log_dir.is_dir() else None
                    if latest is not None and time.time() - latest.stat().st_mtime <= 600:
                        found.add(log_dir)
                except OSError:
                    continue
            for log_dir in found:
                worker = workers.get(log_dir)
                if worker is not None and (worker[0].is_alive()
                                           or time.monotonic() < worker[2]):
                    continue
                child = copy.copy(args)
                child.log_dir = log_dir
                child.queue_dir = log_dir.parent / "ai"
                stop = threading.Event()

                def work(options=child, shutdown=stop):
                    try:
                        run(options, shutdown, budget, provider_status)
                    except Exception as exc:
                        print(f"YaPB sidecar instance {options.log_dir}: "
                              f"{type(exc).__name__}: {exc}", file=sys.stderr, flush=True)

                thread = threading.Thread(target=work, name=f"yapb-{log_dir.parent.parent.name}",
                                          daemon=False)
                workers[log_dir] = (thread, stop, time.monotonic() + 30)
                thread.start()
            for log_dir, (thread, stop, retry_at) in list(workers.items()):
                if log_dir not in found:
                    stop.set()
                    thread.join(timeout=10)
                    if not thread.is_alive():
                        del workers[log_dir]
            time.sleep(2)
    finally:
        for thread, stop, retry_at in workers.values():
            stop.set()
        for thread, stop, retry_at in workers.values():
            thread.join(timeout=10)
        if root_lock is not None:
            root_lock.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--log-dir", type=Path)
    source.add_argument("--instances-root", type=Path,
                        help="watch 16competitive match instances with one sidecar process")
    parser.add_argument("--env-file", type=Path,
                        help="credential file (default: .env next to this script)")
    parser.add_argument("--queue-dir", type=Path,
                        help="instance queue directory (default: sibling ai directory of log-dir)")
    parser.add_argument("--hourly-limit", type=int, default=30)
    parser.add_argument("--event-hourly-limit", type=int, default=10)
    parser.add_argument("--max-event-variants", type=int, default=1000)
    parser.add_argument("--min-interval", type=float, default=8.0)
    parser.add_argument("--timeout", type=float, default=6.0)
    args = parser.parse_args()
    args.env_file_explicit = args.env_file is not None
    if args.env_file is None:
        args.env_file = Path(__file__).resolve().with_name(".env")
    if args.instances_root and args.queue_dir is not None:
        parser.error("--queue-dir cannot be combined with --instances-root")
    if args.log_dir is not None and args.queue_dir is None:
        args.queue_dir = args.log_dir.parent / "ai"
    if (not 1 <= args.hourly_limit <= 300
            or not 0 <= args.event_hourly_limit <= args.hourly_limit
            or not 1 <= args.max_event_variants <= 1000 or args.min_interval < 1):
        parser.error("invalid rate limit")
    try:
        if args.instances_root:
            run_instances(args)
        else:
            run(args)
    except (KeyboardInterrupt, ValueError) as exc:
        if isinstance(exc, KeyboardInterrupt):
            if args.queue_dir is not None:
                (args.queue_dir / "ready").unlink(missing_ok=True)
            return 0
        parser.error(str(exc))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
