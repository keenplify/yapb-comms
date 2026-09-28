# YaPB team comms test build

This archive contains a 32-bit Linux `yapb.so`, optional approved aliases,
and map callouts. It is for a native Linux Counter-Strike 1.6 server using
Metamod. The server package used for validation loads
`addons/yapb/bin/yapb.so` from `addons/metamod/plugins.ini`.

1. Stop the server.
2. Back up the current `cstrike/addons/yapb/bin/yapb.so`.
3. Copy the archive's `cstrike/addons/yapb/bin/yapb.so` into that path.
4. Copy `cstrike/addons/yapb/conf/team_aliases.cfg` and the `callouts`
   directory into the existing `cstrike/addons/yapb/conf/` directory.
5. In your existing `yapb.cfg`, set `yb_chat "0"` and `yb_radio_mode "1"`.
   Preserve your other settings.
6. Start the server and try `say_team rotate b`, `say_team hold`, and
   `say_team bot, follow me`. Address a chat-capable bot in `say` and
   `say_team`; check that its reply uses the same channel. Hit a teammate bot
   once and check that any text reaction appears only in team chat.
   Ask `is everyone a bot?` in either chat channel and check that one bot
   answers briefly in that channel. Try `hello` and `whyy are no one
   responding` without naming a bot; one chat-capable bot should answer.
7. Try `say_team 3 A 2 B` on a supported map with five available teammate
   bots. Test `say_team can u jump` near a grounded bot. From the server
   console, try `yb ai <bot_slot> jump` or `yb ai <bot_slot> follow <player_slot>`;
   `yb list` shows bot slots. `dead_chat` can be tried while the target bot is
   dead. These commands may sometimes be declined by design.
8. Try `say_team all A`, `say_team everyone go B`, and
   `say_team let's rush through mid`. These use named map routes and leave
   planting, defusing, and hostage escorting bots on their objective.
9. Plant the bomb while one or more Terrorist bots are on the other side of
   the map. With no active fight and more than eight seconds left, they should
   move toward the planted site, then choose nearby defensive positions.
10. Watch bots running between sites at different `yb_difficulty` values.
    Bhop should be occasional, more likely on higher difficulty, and stop
    when a bot engages an enemy. Set `yb_bhop "0"` to turn it off.
11. On a demolition map with a verified B route, stay silent for 12 seconds
    after round start. Early in the round, expect one `buy` or `eco` team call
    matching YaPB's team economy. With two to four available bots, the
    highest-scoring living bot should then call `rush b` in team chat and the
    bots should move. With exactly five available bots and a Middle route,
    expect `3 b, 2 mid`.
    One other bot should acknowledge. In the next round, use `say_team go A`
    before 12 seconds; one bot should acknowledge and no bot captain call
    should follow. Try an unavailable recognized order and expect one negative
    acknowledgement.
12. To diagnose silent or stuck bots, enter `yb_comms_debug 1` in the game
    console, then reproduce the issue. Look for `[YaPB comms]` and `[YaPB nav]`
    lines in the console and the daily `addons/yapb/data/logs/yapb_L*.txt`
    file. `[YaPB bhop]` also records a moving bot's speed. Compare
    `yb_bhop_test 1` (jump whenever movement is eligible) with
    `yb_bhop 0` (no bhop). Restore `yb_bhop 1`, `yb_bhop_test 0`, and
    `yb_comms_debug 0` after testing. The same log records `[YaPB ai] queue
    accepted` and `chat dispatched` for sidecar replies. Stock `sector clear`
    radio is limited to CT bomb searches, at most once every 30 seconds per team.
13. Let bots see one or two enemies near Middle, A, or B. Expect one short
    team-chat sighting with the visible count, such as `2 mid`, and a
    `[YaPB comms] sighting` entry in the log. Repeated sightings at the same
    place should be suppressed for 12 seconds.
14. Send a recognized `say_team` order and expect exactly one acknowledgement
    after roughly 0.65–1.4 seconds. Start a CT bot defusing and expect
    `cover me.` in team chat near the start of its attempt.
15. Across two rounds, check for one short pre-round team line and one
    post-round team line per team when a chat-capable bot is present. The
    losing team should say `nt`. On demolition maps, the `buy` or `eco` call
    replaces the social pre-round line for that team. `yb_round_chat 0`
    disables the social and post-round lines.
16. Plant as Terrorist and expect one short team line such as `hold site pls`
    from a living chat-capable bot. Make two through five enemy kills in one
    round while a chat-capable teammate bot is alive; expect short `2k`, `3k`,
    `4k`, or `ace` reactions, with rapid kills grouped by the delay.
17. For DeepSeek, start `tools/start_ai_sidecar.sh` from the installed
    `addons/yapb/tools` directory. Copy `tools/.env.example` to `tools/.env`,
    set the key and run `chmod 600 tools/.env`; process
    environment variables are also supported. `yb_ai_bridge` defaults to 1
    and uses the provider only while the sidecar confirms it is reachable.
    Address a bot in all chat and team chat;
    its lowercase answer should stay in the same channel. Check sidecar
    stderr on provider or local queue failures. See `TEAM_COMMS.md` for limits and
    context sent to the model. Once a fixed bot line is generated, its
    variants are stored in `data/ai/replies.sqlite3` and reused across restarts.
    With `YAPB_CACHE_API_URL` and `YAPB_CACHE_TOKEN` configured on the sidecar,
    they are also shared through the 16competitive API and PostgreSQL.
    For several 16competitive HLDS instances on one host, run a single
    `start_ai_sidecar.sh --instances-root GAME_SERVER_INSTANCES_PATH` instead.
18. During buy time, leave one bot with a primary and at least $6000 and
    another nearby bot without a primary and at most $2000. Expect an offer
    or request, a delayed answer, and a real weapon drop. With a rich nearby
    teammate bot, try `say_team drop ak pls`, `say_team drop awm`, and
    `say_team drop deag`; it should drop the requested firearm when the team
    economy is good and it can replace its own gun.

To roll back, stop the server, restore the saved `yapb.so`, and restore your
previous `yapb.cfg` values. The callout and alias files can remain unused.

Map routes work only where the included CZ NAV place names and the target
CS 1.6 BSP size were verified. Some requested intents are recognized but
reported unavailable; see `TEAM_COMMS.md` for the exact list. The optional
sidecar calls the configured model provider when it is running and has a
valid key.
