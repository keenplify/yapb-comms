#!/usr/bin/env python3
"""Validate one provider-neutral LLM action and print a safe YaPB command.

This process runs outside the game server. The caller owns model requests and
rate limits. The sidecar writes validated commands to a private local queue.
"""

import json
import re
import sys


def slot(value: object, name: str) -> int:
    if type(value) is not int or not 0 <= value < 32:
        raise ValueError(f"{name} must be an integer bot/player slot from 0 to 31")
    return value


def to_command(payload: object) -> str:
    if not isinstance(payload, dict):
        raise ValueError("intent must be a JSON object")
    action = payload.get("action")
    bot = slot(payload.get("bot_slot"), "bot_slot")
    if action == "follow":
        if set(payload) != {"action", "bot_slot", "player_slot"}:
            raise ValueError("follow requires only action, bot_slot, player_slot")
        return f"yb ai {bot} follow {slot(payload['player_slot'], 'player_slot')}"
    if action == "jump":
        if set(payload) != {"action", "bot_slot"}:
            raise ValueError("jump requires only action and bot_slot")
        return f"yb ai {bot} jump"
    if action == "dead_chat":
        if set(payload) != {"action", "bot_slot", "line"}:
            raise ValueError("dead_chat requires only action, bot_slot, line")
        line = payload["line"]
        if type(line) is not int or not 0 <= line <= 2:
            raise ValueError("dead_chat line must be 0, 1, or 2")
        return f"yb ai {bot} dead_chat {line}"
    if action == "chat":
        if set(payload) != {"action", "bot_slot", "channel", "text"}:
            raise ValueError("chat requires only action, bot_slot, channel, text")
        channel = payload["channel"]
        line = payload["text"]
        if channel not in ("team", "all") or not isinstance(line, str):
            raise ValueError("invalid chat channel or text")
        if not 1 <= len(line) <= 30 or not re.fullmatch(r"[a-z0-9 .,?!'-]+", line):
            raise ValueError("chat text must be lowercase, safe, and at most 30 characters")
        return f'yb ai {bot} chat {channel} "{line}"'
    raise ValueError("unsupported action")


def main() -> int:
    raw = sys.stdin.read(1024)
    if not raw or len(raw) >= 1024:
        print("invalid input length", file=sys.stderr)
        return 2
    try:
        command = to_command(json.loads(raw))
    except (ValueError, json.JSONDecodeError) as exc:
        print(f"rejected: {exc}", file=sys.stderr)
        return 2
    print(command)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
