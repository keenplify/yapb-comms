#!/usr/bin/env python3
"""Optional YaPB chat sidecar for OpenAI-compatible providers such as DeepSeek.

One process serves one HLDS instance. It reads only addressed-chat events from
YaPB's local log and returns bounded bot commands through a local file queue.
"""

import argparse
from collections import deque
import json
import logging
from logging.handlers import RotatingFileHandler
import os
from pathlib import Path
import random
import re
import sys
import tempfile
import time
from urllib import request

from llm_intent_bridge import to_command


EVENT = re.compile(
    r"\[YaPB ai\] chat bot=(\d+) player=(\d+) channel=(team|all) "
    r"team=(\d+) map=([a-z0-9_]+) place=([a-z0-9_]+) "
    r"weapon=([a-zA-Z0-9_]+) hp=(-?\d+) money=(\d+) friends=(\d+) "
    r"enemies=(\d+) bomb=([01]) round=(-?\d+) visible=(\d+) "
    r"slots=(-?\d+),(-?\d+),(-?\d+) text=([a-z0-9 ]{1,96})$"
)
SAFE_REPLY = re.compile(r"[a-z0-9 .,?!'-]{1,30}\Z")
SYSTEM_PROMPT = (
    "You are a Counter-Strike 1.6 teammate. Reply like a real player: "
    "lowercase, at most 30 characters, brief and natural. Never reveal that "
    "you are an AI or claim certainty about being human. Return only a JSON "
    "object with keys reply and action. action is one of none, jump, follow. "
    "Use jump or follow only for a direct teammate request; otherwise none. "
    "No tactics or enemy facts unless provided by the player."
)
CONFIG_KEYS = (
    "YAPB_AI_API_KEY", "DEEPSEEK_API_KEY",
    "YAPB_AI_API_URL", "YAPB_AI_MODEL",
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
        "model": values["YAPB_AI_MODEL"] or "deepseek-chat",
    }


def parse_event(line):
    match = EVENT.search(line.rstrip("\r\n"))
    if not match:
        return None
    (bot, player, channel, team, map_name, place, weapon, hp, money,
     friends, enemies, bomb, round_seconds, visible_count,
     slot1, slot2, slot3, text) = match.groups()
    if int(bot) >= 32 or int(player) >= 32:
        return None
    return {"bot_slot": int(bot), "player_slot": int(player),
            "channel": channel, "team": int(team), "text": text,
            "map": map_name, "place": place, "weapon": weapon,
            "hp": int(hp), "money": int(money), "friends_alive": int(friends),
            "enemies_alive": int(enemies), "bomb_planted": bomb == "1",
            "round_seconds": int(round_seconds), "visible_count": int(visible_count),
            "visible_slots": [int(value) for value in (slot1, slot2, slot3) if int(value) >= 0]}


def validate_model_choice(choice, event):
    if not isinstance(choice, dict) or set(choice) != {"reply", "action"}:
        raise ValueError("model response must contain reply and action")
    reply = choice["reply"]
    action = choice["action"]
    if not isinstance(reply, str) or not SAFE_REPLY.fullmatch(reply):
        raise ValueError("model reply is unsafe or too long")
    if action not in ("none", "jump", "follow"):
        raise ValueError("model action is not approved")
    if action != "none" and event["channel"] != "team":
        raise ValueError("all-chat cannot request bot actions")
    commands = []
    if action == "jump":
        commands.append(to_command({"action": "jump", "bot_slot": event["bot_slot"]}))
    elif action == "follow":
        commands.append(to_command({"action": "follow", "bot_slot": event["bot_slot"],
                                    "player_slot": event["player_slot"]}))
    commands.append(to_command({"action": "chat", "bot_slot": event["bot_slot"],
                                "channel": event["channel"], "text": reply}))
    return commands


def ask_provider(event, api_key, api_url, model, timeout):
    body = {
        "model": model,
        "messages": [
            {"role": "system", "content": SYSTEM_PROMPT},
            {"role": "user", "content": json.dumps({
                "channel": event["channel"],
                "player_message": event["text"],
                "bot_state": {key: event[key] for key in (
                    "map", "place", "weapon", "hp", "money", "friends_alive",
                    "enemies_alive", "bomb_planted", "round_seconds",
                    "visible_count", "visible_slots")},
            })},
        ],
        "temperature": 0.7,
        "max_tokens": 80,
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


def send_local(command, directory, timeout=3.0):
    """Atomically publish one approved command; YaPB removes it after reading."""
    if not re.fullmatch(r'yb ai (?:[0-9]|[12][0-9]|3[01]) '
                        r'(?:jump|follow (?:[0-9]|[12][0-9]|3[01])|dead_chat [012]|'
                        r'chat (?:team|all) "[a-z0-9 .,?!\'-]{1,30}")', command):
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
        with tempfile.NamedTemporaryFile("w", encoding="ascii", prefix=".ai-",
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


def run(args):
    settings = get_settings(args.env_file, args.env_file_explicit)
    api_key = settings["api_key"]
    api_url = settings["api_url"]
    model = settings["model"]
    if not api_key:
        raise ValueError("set YAPB_AI_API_KEY (or DEEPSEEK_API_KEY)")
    if not api_url.startswith("https://"):
        raise ValueError("YAPB_AI_API_URL must use https")
    args.log_dir.mkdir(parents=True, exist_ok=True)
    diagnostics = logging.getLogger("yapb_ai_sidecar")
    diagnostics.setLevel(logging.INFO)
    diagnostics.addHandler(RotatingFileHandler(
        args.log_dir / "ai_sidecar.log", maxBytes=1_000_000, backupCount=2))
    handled = deque()
    current = None
    offset = 0
    last_request = 0.0
    print("YaPB AI sidecar watching addressed chat", flush=True)
    diagnostics.info("sidecar started with local queue %s", args.queue_dir)
    while True:
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
        for line in lines:
            event = parse_event(line)
            if event is None:
                continue
            now = time.monotonic()
            while handled and handled[0] < now - 3600:
                handled.popleft()
            if len(handled) >= args.hourly_limit or now - last_request < args.min_interval:
                continue
            handled.append(now)
            last_request = now
            try:
                choice = ask_provider(event, api_key, api_url, model, args.timeout)
                commands = validate_model_choice(choice, event)
                time.sleep(random.uniform(0.6, 1.5))
                for command in commands:
                    send_local(command, args.queue_dir)
                print(f"answered bot={event['bot_slot']} channel={event['channel']}", flush=True)
                diagnostics.info("answered bot=%d channel=%s", event["bot_slot"], event["channel"])
            except Exception as exc:
                print(f"AI event skipped: {type(exc).__name__}: {exc}", file=sys.stderr, flush=True)
                diagnostics.error("event skipped: %s: %s", type(exc).__name__, exc)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--log-dir", required=True, type=Path)
    parser.add_argument("--env-file", type=Path,
                        help="credential file (default: .env next to this script)")
    parser.add_argument("--queue-dir", type=Path,
                        help="instance queue directory (default: sibling ai directory of log-dir)")
    parser.add_argument("--hourly-limit", type=int, default=30)
    parser.add_argument("--min-interval", type=float, default=8.0)
    parser.add_argument("--timeout", type=float, default=6.0)
    args = parser.parse_args()
    args.env_file_explicit = args.env_file is not None
    if args.env_file is None:
        args.env_file = Path(__file__).resolve().with_name(".env")
    if args.queue_dir is None:
        args.queue_dir = args.log_dir.parent / "ai"
    if not 1 <= args.hourly_limit <= 300 or args.min_interval < 1:
        parser.error("invalid rate limit")
    try:
        run(args)
    except (KeyboardInterrupt, ValueError) as exc:
        if isinstance(exc, KeyboardInterrupt):
            return 0
        parser.error(str(exc))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
