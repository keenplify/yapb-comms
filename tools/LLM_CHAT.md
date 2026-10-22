# Optional LLM teammate chat

YaPB keeps combat, target selection, and movement in the game plugin. The local
bridge only writes short chat replies. It sends a compact summary of the current
round and up to four recent chat lines to the DeepSeek Chat Completions API. API keys stay
in the bridge process, not in the game plugin or `yapb.cfg`.

1. On the game server, install Node.js 20 or later and set `DEEPSEEK_API_KEY` in
   the bridge process environment.
2. Run `node tools/llm-chat-bridge.mjs` from this repository. It listens on
   `127.0.0.1:8765` by default. Set `YAPB_LLM_PORT` and `YAPB_LLM_MODEL` to
   choose another port or model. The default model is `deepseek-chat`.
3. Set `yb_chat_llm_port "8765"` in `cfg/addons/yapb/conf/yapb.cfg` or the
   server console. The default value `0` leaves LLM chat disabled.

At most two bots are offered each human message. At most four bridge requests
run concurrently, with one request per bot. YaPB samples replies by personality
and talkativeness, uses short prompts and a 72-token output cap, and falls back
to its built-in chat when the bridge is unavailable or slow. Bot-to-bot social
chat requires a human teammate. The bridge binds to loopback only.

The plugin records kills, streaks, a verified whole-team ace at round end,
alive counts, bomb plant/defuse events, round winner, and recent chat. It does
not send hidden enemy locations to the LLM. Chat text is sent to the configured
DeepSeek API account when this feature is enabled.

Run `node --test tools/test-llm-chat-bridge.mjs` to check concurrent requests
and reply sanitization without an API key or external API call.
