# Team communication fork

This copy of YaPB accepts **exact phrases and conservative keyword variants** in
living human players' `say_team`. For example, `guys, rotate to B now`,
`hold here please`, and `we need to eco this round` are recognized. Matching
uses whole words, a bounded vocabulary, negation checks, and a small set of
command prefixes. Commands do not trigger from bot chat, dead chat, global
chat, or an arbitrary substring of ordinary conversation. Default random text
chat is off (`yb_chat 0`); prerecorded chatter is reduced to standard radio
(`yb_radio_mode 1`).

## Communication styles and channels

Bots get a stable radio-only, chat-only, or alternating radio/chat style from
their player slot. Purposeful text callouts are at most 30 characters and have
a 10-second per-bot cooldown. Tactical text is always sent with `say_team`;
radio is team-only too. The alternating style uses one channel for each message,
never both for the same callout. Random bot chat remains disabled.
Bot text chat is lowercase, including acknowledgements and round messages.

Chat-capable bots answer a human who addresses a bot by name, says `bot` or
`bots`, asks a question, greets with `hello`/`hi`/`hey`/`yo`, or asks why no
one is responding. A message in `say` gets a short `say` reply; a message in `say_team`
gets a `say_team` reply. A complete bot name or unique first name takes priority.
Short followups stay with that bot; ambiguous names get no substitute reply.
Replies have a per-player cooldown so one conversation does not block another.
Radio-only bots do not type. All-chat messages cannot issue tactical orders.
Human team-chat orders that are recognized take priority over social replies.
For example, `is everyone a bot?` gets a short, deliberately ambiguous answer
from one chat-capable bot, such as `Maybe.` or `You tell me.`. The answer uses
the same chat channel. This common question is handled locally with keywords,
so it does not incur model latency or cost.
When a human teammate hits a bot, chat-capable bots say `Watch your fire.` in
team chat. The old team-attack text bank could name an unrelated teammate when
its `%t` lookup missed the attacker; it is no longer used for this reaction.

## Provider-neutral vocabulary

`cfg/addons/yapb/conf/team_aliases.cfg` is an optional, reviewed mapping from
new phrases to canonical commands, for example `stack b = go b`. The bot reads
at most 256 aliases once and keeps them in memory. The game plugin itself never
invokes Codex, DeepSeek, an API, or a subprocess. Any external model or a
human can propose phrases using the same simple format; review and install the
approved file between server restarts. Model output cannot add new executable
behaviors, only map to an existing command. Built-in commands win over aliases.

Keep command recognition separate from bot speech. A future phrase generator
can produce multiple approved replies per intent, then the server can choose
among them locally with a cooldown and a configurable probability. It should
only speak after checking what the bot actually did.

## Optional AI sidecar

`tools/ai_sidecar.py` connects human questions, greetings, ordinary conversation,
and existing bot speech
events to DeepSeek or another OpenAI-compatible chat-completions endpoint.
YaPB never waits on HTTP. With `yb_ai_bridge 1`, it writes bounded log events;
the sidecar sends only validated `yb ai` commands through a private local
queue. Keyword team orders still run locally. The bridge is enabled by
default and routes chat to the sidecar only while its provider check is fresh.
Otherwise YaPB uses its local short replies. The sidecar checks the provider
at startup and after a failed request, with a 30-second retry delay.
Human messages take priority over canned bot speech. Stale events are dropped,
and at most two canned lines from one log read are handled, so a busy round
does not keep human replies waiting behind a growing queue.
Human replies carry the original player's slot to YaPB. If the selected bot
is no longer visible to that player when the reply arrives, the reply is
dropped; another bot must not impersonate the addressee. Human replies use a two-second
minimum interval; the hourly provider budget still applies.

Each event includes the selected bot's map, nearest verified CZ callout,
current weapon, health, money, alive teammate and enemy counts, bomb state,
round age, and player slots of enemies the bot currently sees with line of
sight. It also includes the player's normalized message or the bot's original
line and chat channel.
A team-chat reply stays in team chat, and an all-chat reply stays in all chat.
Bot replies are lowercase and at most 30 characters.
Purposeful bot chat removes periods, including from model replies.

Fixed bot lines such as `cover me`, kill reactions, buy or eco calls,
and sighting reports can be rephrased by the model. The original action is
still applied locally; only the wording changes. The sidecar checks that
numbers, sites, and key tactical facts survive a rephrase. It keeps up to
1,000 variants per map, channel, and original line in the shared 16competitive
PostgreSQL cache, reached through its authenticated API. The sidecar also keeps
a local hot copy in `addons/yapb/data/ai/replies.sqlite3`; HLDS never receives
database credentials. Once a variant exists, it has a 30%
chance of being reused while the cache grows. The cache is also capped at
50,000 rows per server instance. It always uses a cached or
original line when the event request budget is exhausted or the model fails.
The SQLite file is local to one server instance and mode `600`.
Set `YAPB_CACHE_API_URL` and `YAPB_CACHE_TOKEN` in the sidecar `.env` to share
variants between servers. The backend uses the same token in its private
`YAPB_CACHE_TOKEN` environment variable. Local loopback HTTP or HTTPS is
supported; remote cache URLs require HTTPS. Each sidecar refreshes a key from
the backend at most once every five minutes and writes new variants through.
If the backend is down, it continues using its local cache and tries again on
the next refresh. The backend route is `/internal/yapb/replies`; its database
migration is `0056_yapb_reply_cache.sql`.
The 16competitive backend supervises one copy of the sidecar per host when
`YAPB_AI_API_KEY` is set; it watches all local match instances. Opening `glhf`,
pre/post round chat, and occasional dead chat go directly to team chat so
these short lines still work when the provider is slow or unavailable.

During the early buy period, a chat-capable bot with a primary weapon and at
least $6000 left may ask `anyone need a drop?` if a nearby teammate bot lacks
a primary and has at most $2000. The teammate answers `can i get a drop?`
after a short delay. The rich bot then drops its primary within pickup range
and re-enters YaPB's normal buy sequence once the inventory update confirms
the drop. This exchange happens at most once per team per round and does not
run during a team eco, a fight, or after the plant.
If the rich bot recently chatted or prefers radio only, the poor bot asks
first and the rich bot acknowledges by radio. A human weapon request such as
`drop awm pls` takes priority. It is accepted only when the team economy is
positive and a nearby bot has enough money to replace its own gun.

The sidecar reads `.env` beside `ai_sidecar.py` by default. Copy
`tools/.env.example` to `tools/.env`, set `DEEPSEEK_API_KEY`, and
restrict the file to mode `600`. Use `--env-file`
to give each HLDS instance its own credential file. Process environment
variables override values from the file. The sidecar parses the file as data;
it never executes shell expressions in it. `YAPB_AI_API_URL` defaults to
`https://api.deepseek.com/chat/completions`;
`YAPB_AI_MODEL` defaults to `deepseek-flash`. For one HLDS, start the sidecar
with its log directory; the queue defaults to the sibling `data/ai` directory.
Use `--queue-dir` when the instance has a different layout. No RCON password
is needed.

For several 16competitive match servers on one host, start **one sidecar
process** with `--instances-root`. It discovers new match directories under
`GAME_SERVER_INSTANCES_PATH` and watches each server's own YaPB logs and queue.
Retained match directories are ignored until their YaPB game log becomes active;
idle workers are released after ten minutes without a game-log update.
Each instance keeps a separate lock and SQLite fallback; all threads in that
process share the same provider request budget and PostgreSQL variant cache.
Do not also start a separate sidecar on one of those queues. The default
30 total and 10 event-generation requests per hour apply to the entire host
process in this mode.

```sh
./tools/start_ai_sidecar.sh --instances-root /path/to/game-server-instances
```
Do not put credentials in `yapb.cfg` or a tracked file. `.env` is gitignored;
keep the same private permissions when copying it elsewhere. Defaults are one
request every eight seconds, 30 requests per hour total, up to 10 of those
for generating event variants, and a six-second provider timeout. No
historical log entries are replayed.

```bash
install -m 600 tools/.env.example tools/.env
${EDITOR:-vi} tools/.env
./tools/start_ai_sidecar.sh
```

Only `none`, `jump`, and `follow` actions are accepted from this model path.
YaPB checks bot and target availability again. Provider failures are logged
to `addons/yapb/data/logs/ai_sidecar.log` and stderr without blocking the
game server. The sidecar log rotates at 1 MB and never records the API key
or raw player message.

## Working commands

| Phrase | Current action |
| --- | --- |
| `follow me`, `trade`, `trade me`, `refrag` | Bots follow the speaker. |
| `push`, `rush`, `execute`, `swing` | Bots use YaPB's front push task. |
| `play safe`, `don't peek`, `wait`, `hold` | Bots stop pathing, pause for 15 seconds, and lower aggression. Combat may interrupt the pause. |
| `hold for me` | Bots move to a nearby defend node and hold. |
| `rotate`, `retake` | CT bots go to the planted bomb when its location is known. |
| `rotate a/b`, `go a/b`, `leave a/b` | Bots navigate to the named bombsite when a verified map callout and a nearby YaPB graph node exist. `leave a` routes to B and vice versa. |
| `watch mid`, `watch apps`, `watch back` | Bots navigate to a matching named place and hold for 45 seconds when that place exists on the map. |
| `eco`, `save` | Bots stop further purchases when in the buy zone. |
| `force`, `force buy`, `full buy` | Bots run their normal purchase sequence without team eco suppression when in the buy zone. Purchases remain limited by actual money. |
| `drop` | One nearby bot drops its currently equipped primary weapon. |
| `drop ak pls`, `drop awp`, `drop awm`, `drop m4`, `drop deag` | A nearby bot with good economy drops the requested gun during buy time. It may buy one first when its team is allowed to buy it. All 24 standard CS 1.6 firearms are recognized, including pistols, SMGs, shotguns, rifles, snipers, and the M249. Knife, C4, and grenades cannot be dropped through normal CS 1.6 weapon dropping. |
| `can u jump`, `can you jump`, `jump` | The nearest teammate bot within 512 units jumps when on the ground. |
| `3 A 2 B`, `2A 1 mid 2B` | Assigns exact bot counts to verified named A, B, and Middle routes. Requires enough available bots. |
| `all a/b/mid`, `rush a/b/mid` | Sends every available teammate bot toward the named route. Rush uses fast path selection and higher aggression. Variants such as `everyone go B`, `team to B`, and `let's rush through mid` are accepted. |

The acknowledgement says an order was **queued**, since navigation and combat
can interrupt it. Planting, defusing, and escorting hostages take precedence.
One available teammate bot also answers every recognized human team command:
`Copy.` or affirmative radio when an action was queued, and `Can't do that.`
or negative radio when it could not be performed. Only one bot acknowledges;
chat cooldown falls back to radio. Replies are queued with a randomized
0.65–1.4 second delay, in order per team. Both channels are team-only.
Bot-to-bot acknowledgement of a captain route call waits 1.5–2.8 seconds
and comes from a different available bot.

## Round captain

Early in a demolition round, a living bot calls `buy` or `eco` in team chat
using YaPB's team economy decision, which also governs normal bot purchases.
The call is skipped if a human has already taken captaincy with a tactical
order. This economy call takes the place of the social pre-round line for that
team. The economy captain speaks in text even if its normal style is radio-only.
That bot also makes the later route call if still alive and available; a new
highest-scoring eligible bot takes over if the captain cannot continue.
If a team's humans have not chatted for 12 seconds during a demolition round,
the highest-scoring living available bot makes one short team call. With five
available bots and verified B and Middle routes, it calls `3 b, 2 mid`;
otherwise, with at least two bots and a verified B route, it calls `rush b`.
The assigned bots immediately move to those map nodes. Another bot confirms
the call. A human tactical team-chat order or radio call takes captaincy for
the rest of the round and prevents later bot captain calls. Any human chat
resets the silence timer. Captaincy resets each round. Bomb plants and
objective tasks take priority, and the bot captain makes no call after a plant.

Bots also report enemies they currently see near verified A, B, or Middle
callouts, for example `2 mid` or `1 B`. The count includes only living enemies
in the reporting bot's view with a clear sight line, close to the same area.
Reports use team chat and have a 12-second per-team, per-place cooldown.
`yb_comms_debug 1` logs the reported count and place.
The stock `sector clear` radio now only fires for CT bomb searches, with a
30-second team cooldown. Ordinary goal visits no longer announce a clear sector.
When a CT starts defusing a planted bomb, it sends one urgent `Cover me.`
team-chat line. Retries are limited to once every 12 seconds.

## Round chat

One chat-capable bot per team says a short pre-round line such as `gl team`
or `play smart`. After the result, one chat-capable bot on the winning team
says a short positive line; the losing team says `nt`. Both use team chat and
are delayed slightly so they do not appear at the exact round transition.
Radio-only bots do not type these lines. `yb_round_chat 0` disables them.

After a plant, one living Terrorist says a short team line such as `hold site
pls`, `play time`, or `watch flank`. This accompanies the existing post-plant
repositioning. A teammate bot also reacts to a player's second, third,
fourth, or fifth enemy kill in one round with `nice 2k`, `yo 3k`, `4k, go ace`,
or `ace!!`. Reactions are delayed briefly so a rapid streak is reported at
its highest count. These lines have the same chat cooldown and require a
living chat-capable teammate.

When a human opponent kills a chat-capable bot, that dead bot now has an
occasional all-chat reaction. The reaction includes the live score and match
target so AI rephrasing stays grounded: normal 5v5 and unrated matches use the
MR12 first-to-13 target, while FFA uses first-to-90 and the bot/killer frag
scores. Bots become more likely to sound frustrated when trailing badly or
facing match point. Bot-vs-bot deaths do not trigger this opponent banter, and
a global cooldown prevents death-chat spam.
Knife and HE grenade kills by a human use a specific reaction when that
cooldown is free, instead of the usual random death reaction.
Bots that land a knife or HE grenade kill also use a short weapon-specific
line in place of random kill chat when their text cooldown is free.

In any mode, a living chat-capable bot comments on its own frag score after
crossing a high-score milestone (20 frags in team modes, 40 in FFA, then every
10 frags). Each bot announces a milestone once, with a shared cooldown between
high-score messages.

After a bomb plant, living Terrorist bots more than 700 units from the planted
site now interrupt stale routes and head to a nearby YaPB graph node at the
bomb. They wait for active combat to end, need enough bomb time to make the
move, and retry an interrupted route at most once every four seconds. Once
nearby, YaPB's existing defend-position logic takes over. This addresses the
old behavior where post-plant defense was chosen only when a bot happened to
pick a new goal, and its defend-node search could fail from far away.

## Movement personality

`yb_bhop 1` enables occasional bunny-hop bursts while a bot is running a
normal navigation, move, or follow task. A burst has at most three jumps on
lower difficulties, four on Normal, and five on Hard/Expert, followed by a 5–8 second
cooldown. The chance to start and continue a burst increases from Noob to
Expert. Combat, recent enemy sightings, crouch/ladder paths, water, bomb or
hostage handling, and low movement speed cancel or suppress it. Set
`yb_bhop 0` in the existing server config to disable it.
Downhill routes can start a burst earlier, and follow-up hops get a short
landing window without the automatic midair crouch. Active chains use small
side input in the air when the next waypoint is far enough ahead.
Ground strafing is disabled by default (`yb_ground_strafe_test 0`). When enabled for testing, bots pulse
crouch and use modest side input in short bursts, with a few seconds between
bursts. They pause near waypoints, turns, elevation changes, and special
routes so navigation keeps control. Bunny-hop bursts remain enabled separately. Diagnostics log pulse count, peak ground
speed, and frame time as `[YaPB ground-strafe]` every five seconds when
`yb_comms_debug 1` is enabled. Speed gains depend on the server's frame rate
and GoldSrc physics. Set `yb_ground_strafe_test 0` to disable it. Combat and
objective safety checks still apply.
Normal bhop also avoids a blocked path ahead. For diagnosis, `yb_bhop_test 1`
forces a jump at every eligible movement opportunity and bypasses that wall
check. While airborne, it applies alternating side input to build speed
without changing server physics; actual gains depend on GoldSrc's air
acceleration. The limited side input keeps forward speed while reducing route
corrections. In forced test mode, bots draw the knife when safely moving so
their equipped weapon does not reduce maximum speed. Combat can switch them
back to a gun. `yb_bhop_test 0` restores normal chance and cooldown. Combat and
objective safety checks remain active. `yb_comms_debug 1` prints a brief
captain status every five seconds and reports bots that remain near one spot
while trying to move. It also samples one moving bot's speed. These lines are
written to YaPB's daily file under `addons/yapb/data/logs/` as well as the
console. Both debug switches default to off.

## Recognized but not acted on yet

`half buy`, `buy me`, `can you drop`, `contact`, `default`, the plain `split`
command, `bait`,
`double peek`, the listed enemy/bomb info phrases, utility phrases, and
`watch flank/short/long`. These receive an explicit unavailable response.
Utility requests need grenade selection and safe target/trajectory logic.
No arbitrary grenade throw is attempted.

## External LLM action interface

An external model can request **only approved actions** through a small JSON
object. `tools/llm_intent_bridge.py` validates it and prints a `yb ai` server
console command. This validation process runs outside the game server; it does
not call a provider by itself. The sidecar places validated commands in the
private `data/ai/pending.cmd` mailbox. YaPB polls it and applies the same
restricted `yb ai` dispatcher. Use one queue directory per HLDS instance.

```json
{"action":"follow","bot_slot":2,"player_slot":0}
{"action":"jump","bot_slot":2}
{"action":"dead_chat","bot_slot":2,"line":1}
```

The matching server commands are `yb ai 2 follow 0`, `yb ai 2 jump`, and
`yb ai 2 dead_chat 1`. Slots are zero-based, as shown by `yb list`. `follow`
requires a living human teammate; `jump` requires a living grounded bot;
`dead_chat` requires a dead chat-capable bot. The three dead lines are
`My bad.`, `Unlucky.`, and `You got this.` and always use team chat. There is
no arbitrary command string in the model contract. The optional sidecar can
also send `yb ai <bot_slot> chat team/all "<text>"` after validating a short
lowercase line of at most 30 characters. The sidecar binds the channel to the
player's message. The `yb ai` command works only from the server console,
not an in-game player command.

Optional follow, push, rush, swing, and jump requests have an 8% per-bot
decline chance. A decline does not queue the action and the bot may say
`Sorry, can't rn.` or use negative radio. Orders for objectives and the
requested split are not randomly declined. A bot that team-kills a player
may say `My bad.` in team chat. A dead chat-capable bot has a 25% chance to
send one short team line after death. Normal Counter-Strike dead-chat
visibility rules still apply.

## CZ place data

The included exporter reads CZ NAV version 5 from the local Steam installation,
checks its recorded BSP size against the target CS 1.6 BSP, and emits named
place centers to `cfg/addons/yapb/conf/callouts`. YaPB routes those centers
through its own graph; it does not replace its navigation with CZ NAV data.
The plugin checks the BSP size again at runtime before accepting a callout file.
Twelve maps in this installation passed strict parsing and BSP-size checks,
including `de_dust2` and `de_inferno`. Mismatched or unparsed NAV files are
skipped. The callout text files must be deployed alongside the plugin.

```sh
python3 tools/export_cz_callouts.py \
  --cz-maps /home/keenplify-fedora/.local/share/Steam/steamapps/common/Half-Life/czero/maps \
  --target-maps /home/keenplify-fedora/.local/share/Steam/steamapps/common/Half-Life/cstrike/maps \
  --output cfg/addons/yapb/conf/callouts
```

## Build and parser check

```sh
git submodule update --init --recursive
cmake -S . -B build-comms -DCMAKE_BUILD_TYPE=Debug
cmake --build build-comms -j4
g++ -std=c++17 -Iinc tests/team_order.cpp -o build-comms/team_order_test
./build-comms/team_order_test
```

The built plugin is `build-comms/libyapb.so`. It has not yet been installed on
a live server or tested in a Counter-Strike match. Keep the existing plugin
until the new behavior is checked in game.

For native Linux CS 1.6, build the 32-bit module with `-m32` (the local
`build-comms-32/libyapb.so` was built this way).
