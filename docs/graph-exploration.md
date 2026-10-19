# Experimental graph exploration

For manual jumps and expanding an existing graph, see [Manual parkour](manual-parkour.md).

## Virtual player mode

Set `yb_graph_analyze_swarm 1` and `yb_graph_analyze_virtual 1` before loading
a map with no graph. The installed Windows test uses this mode. It spawns **zero
client bots**. Position, velocity, grounded state, standing/crouching hull and
jump state live in a small virtual player record. Full engine hull sweeps check
every movement segment, including step-up and step-down attempts. A one-unit
wall still blocks a sweep even if a simulated tick moves farther than its width.

The model includes dry-ground acceleration, friction, air acceleration, gravity,
18-unit steps (or `sv_stepsize`), standard jump impulse, and crouch clearance.
It uses the server movement cvars, with standard CS knife speed capped at 250.
Two predictions at different tick intervals must reach a stable grounded target
before a directed connection is accepted. These are predictions, **not measured
player traversals**; the graph uses `InputSimulated` separately from `InputVerified`.
Normal bot navigation uses movement buttons for either type, without injected
jump velocities.

Simulation advances independently of real time, up to 65,536 substeps and a
default 4 ms work budget per server frame. `yb_graph_analyze_virtual_budget`
accepts 0.25–20 ms. The budget is checked between substeps; one engine query can
overrun it. Engine traces must remain on the server thread, so this version does
not spread collision queries across CPU cores. More CPU usage alone is not a
measure of better graph generation.

This is an approximate movement model, not the CS game DLL's `PM_Move`. Water,
collisions with non-world brush entities, known movement/touch trigger
volumes and damage volumes are rejected. Custom parkour plugin impulses, surf, bunnyhop timing, duck-transition
timing, lifts, breakables, touch triggers and moving platforms are not reproduced.
Coverage can therefore be incomplete, and predicted parkour routes still need
in-game validation. Use `yb_graph_analyze_virtual 0` for the physical bot mode.
Existing graph files load normally and prevent automatic regeneration.

Regression fixtures cover a thin wall, a low ceiling, crouching, small steps,
steps requiring a jump, jumping a gap, solid starts, unsupported movement, and
missing landing support. Fixtures test model behavior, not parity with CS.

## Physical player mode

This is an opt-in first prototype for a **dedicated analysis server**, with no
other playing humans or bots. Spectators may watch. It sizes up to 16 YaPB explorers
through actual `RunPlayerMove` inputs and observes the game DLL's movement. It
does not call DeepSeek, run a learned movement model, or assign velocity during
a traversal. The normal graph analyzer remains the default.

## Running it

Use a disposable server installation with the same map, movement settings and
plugins as the intended parkour server. Keep the original graph elsewhere when
preparing that installation; existing graphs are loaded normally and are not
automatically replaced. No user-owned game configuration is accessed.

Set these in the analysis installation's `addons/yapb/conf/yapb.cfg`, before
loading a map that has no local graph:

```text
yb_graph_url ""
yb_graph_analyze_auto_start "1"
yb_graph_analyze_auto_save "1"
yb_graph_analyze_swarm "1"
yb_graph_analyze_swarm_bots "0"
yb_graph_analyze_swarm_seconds "0"
yb_graph_analyze_swarm_spacing "96"
```

The blank graph URL disables downloading an existing graph before analysis.
For custom maps that exhaust GoldSrc's engine hunk when player models first
load, start the dedicated server with `-heapsize 131072` (128 MiB). This sizes
the engine heap; it is not the process's total RAM limit. The Windows launcher
uses this setting. A short smoke test on `de_downtown_csneo` with this setting
loaded both plugins, spawned both explorers and began a movement trial without
the reported `Cache_TryAlloc` failure. This does not establish full-run stability
or complete map coverage.
Wait for the completion/budget message. Logs include `[YaPB explore]` physics
settings, trial outcomes and accepted source/target/action IDs. The existing
graph save/load path stores accepted directed links when auto-save is enabled.
All owned explorers are removed at completion. Normal bot quota remains zero; add
ordinary bots explicitly after reviewing the generated graph. Setting
`yb_graph_analyze_swarm 0` cancels an active run without saving its partial graph.
Reload the map to discard cancelled in-memory nodes.

A playing human/foreign bot or a change to sampled movement settings cancels
analysis without saving. A run with no accepted links is not saved. A map change
stops analysis and removes only its owned explorers. Failed connection/spawn
attempts and maps whose game DLL cannot respawn explorers cannot run forever:
the session deadline still applies.

## Work and evidence limits

- Auto selects up to 16 explorers from logical CPU count, available memory and
  client slots. `yb_graph_analyze_swarm_bots 0` is auto; 1–16 requests a manual
  count, still subject to memory and slot limits. Reserve two spectator slots.
  The Windows test launcher provides 32 client slots. Normal combat, buying, chatter, sidecar polling,
  tactical maintenance and visibility rebuilding disabled during exploration.
- 128 pending candidate jobs and the standard 4,096-node graph-format capacity.
  No trial-count cutoff. Time limit defaults to zero (disabled); an explicitly
  configured positive `yb_graph_analyze_swarm_seconds` stops and saves at that time.
  Normal completion means the sampled frontier is exhausted, not proof every
  reachable part of the map was found.
- Physical mode creates at most four candidate probes per 50 ms tick. Virtual
  mode runs each server frame within its millisecond budget. Frontier nodes
  rotate between probes, so a single spawn cannot monopolize the pending queue.
  Trials reserve their movement bounds; overlapping routes are serialized.
- Windows samples available physical RAM and process virtual address space;
  Linux samples available physical pages. Capacity leaves a 512 MiB reserve and
  budgets 64 MiB per explorer as a conservative heuristic, not a measurement of
  actual per-bot use. Unknown memory detection falls back to at most two bots.
  These coarse local resource readings are not sent to DeepSeek or a backend.
- Once per second, sample process CPU time and mean server frame time. CPU use
  of at least 80% of one core combined with frames over 25 ms halves the allowed
  active-worker count. Three healthy windows restore one worker at a time.
  New work pauses on slow frames when CPU is busy; existing trials finish.
  Deliberately slow empty-server pacing alone does not stall exploration.
  Workers already created remain idle when throttled; the active-work limit
  is distinct from the number of connected bots. This is responsiveness control,
  not a guarantee of total process memory use or a particular completion time.
- Each source probes eight horizontal directions at 64, 128 and 192 units.
  Local downward hull traces seek supported, sufficiently flat landing areas.
  Full standing/crouching hulls check target clearance; damage volumes are
  rejected. This bounded sampler does not cover every possible platform.
- Test walking, crouching, jumping and crouch-jumping in that order. Each action
  gets at most three attempts and requires two successful landings. Jump actions
  must actually issue jump and become airborne. Evidence is independent per
  action. A failure does not prove that a route is physically impossible.
- Success means grounded within 12 horizontal units of the target and 8 units
  of its floor height, without damage, continuously observed for 250 ms.
- Workers reset between attempts, settle for 500 ms, and get three seconds to
  traverse. Only resets clear velocity/teleport. Respawn is requested through
  the game DLL after death. Idle workers are non-solid; active workers restore
  normal player solidity. Trials close enough to interfere are serialized.

Only tested directed connections are added. Nearby nodes do not receive
automatic links, full link arrays do not evict earlier verified links, and
smoothing cannot move verified endpoints. Links store `InputVerified`, optional
`Jump` and `InputCrouch` flags with an empty velocity. This fork's normal
navigation uses jump/crouch buttons for these links, bypasses velocity injection,
suppresses opportunistic bhop/ground-strafe, and requires a grounded arrival.
Use the matching fork binary with these generated graphs; upstream/older YaPB
does not implement these connection flags.

## Current parkour limits and validation

The first prototype tests basic isolated movements, starting from rest. Jump
attempts use a fixed 12-unit approach. It does not search strafe sequences,
long-jump takeoff profiles, momentum-preserving chains, surf movement, moving
platforms or teleport triggers. A route tested from rest is not proof that an
ordinary combat bot can reproduce it with different incoming momentum. Graphs
are specific to the map and movement/plugin setup used for testing; regenerate
after changing that setup. Physics samples are logged, not a persistent runtime
authorization or compatibility guarantee.

Standalone tests cover clear/solid trace interpretation, repeated evidence,
stable landing rejection, resource sizing, pressure/recovery and trial-bound
overlap. A short stock Steam Windows/Dust2 smoke test auto-selected 16 explorers
on a 16-logical-CPU machine, spawned all 16, and recorded 233 trials and 22
accepted links. This does not validate complete parkour coverage or route replay.
On a test server, check a thin wall, an offset low ceiling, a crouch tunnel,
stairs, a short platform gap, a drop causing damage and a fatal pit. Watch two
attempts produce one directed link, verify no mid-flight velocity assignment,
and check that a fly-through or damaged landing never succeeds. Replay the
accepted links with ordinary bots. Repeat cancellation, spectator watching,
playing-team joins, physics changes, time-budget expiry and a map change.

Future DeepSeek integration can rank a bounded batch of local experiments. It
must not invent successful evidence or bypass engine execution and validation.

The virtual4 Windows build was also tested on stock Steam HLDS with
`de_downtown_csneo`: a 30-second analysis budget produced 521 predicted links
from 1,165 completed trials, saved a 6,306-byte graph, and reloaded it successfully.
The server reported zero client bots throughout; nine movement/touch volumes
were excluded. All 14 standalone behavior fixtures passed, the Windows x86 DLL
loaded/unloaded with required exports, and the Linux x86 shared module built.
This save-path check does not prove full map coverage or route replay.

## Sparse outward exploration and inspection (virtual5)

Probes now try 192-unit routes before 128-unit and 64-unit routes with default
spacing. Previously, eight short links could consume all outgoing slots before
longer probes ran. Nearby spawn seeds and open-ground endpoints prefer 96-unit
spacing. Reuse requires the same stance and floor height, a clear full-hull
sweep, and floor support sampled along the intervening segment. Walls, gaps,
and distinct heights therefore keep separate endpoints. Physical workers never
retarget completed evidence when another worker adds a nearby node.

The supplied local play-test server sets its own development inspection password.
From the connected Counter-Strike console, use `exec yapb_graph_inspect` to acquire
the editor and display the graph. Equivalent commands on this local test server:

```text
setinfo _ybpw local_graph_test
yb graph acquire_editor
yb graph on
```

`yb graph on noclip` enables flying inspection; `yb graph off` turns the overlay
off, and `yb graph release_editor` releases the editor. These are existing YaPB
editor commands; do not use `on auto` while inspecting unless adding nodes is
intended. On other servers, use their administrator-configured credentials.
Existing graphs are not automatically regenerated. Preserve them and remove the
active `.graph` from the graph folder before a new generation run.

Virtual5's unrestricted `de_downtown_csneo` check exhausted its sampled frontier
after 18,252 trials: 567 nodes, 3,836 directed links, and a 27,985-byte saved graph.
The graph reloaded successfully and offline connectivity inspection reached all
567 nodes from node 0. The previous 4,096-trial graph had 428 nodes, median nearest
spacing 32 units, and a maximum nearest-spawn distance of about 289 units. The
new graph has median nearest spacing about 64 units and reaches about 1,791 units
from the nearest spawn, with node heights up to 645 instead of 97. These metrics
demonstrate expansion and reduced local density, not complete traversable-map
coverage or successful replay by ordinary bots. All 14 behavior fixtures passed;
Windows x86 load/export checks passed and the Linux x86 module built.

## Stairs, ladders, crouch-jumps and breakables (virtual9)

Floor discovery retries at overhead, body and crouching heights. The previous
overhead-only probe could start inside a low ceiling and miss a valid crouch
passage. Trials now include walking, crouching, jumping, crouch-first jumping,
and running takeoff followed by crouching in the air. Air-duck connections carry
their own flag so normal navigation preserves the takeoff timing.

Standard vertical `func_ladder` brushes get separate mounting, contact, climbing,
and grounded-exit trials in both directions, repeated at two tick intervals.
Contact uses `TraceModel` with the full player hull against the actual brush.
The model predicts a 200-unit/s climb, checks every segment, rejects lost contact
or obstructions, and places intermediate ladder nodes along accepted climbs.
Normal navigation requires actual ladder contact at ladder nodes and a grounded
landing at floor endpoints. Ladder links do not inject player velocity.
The model is conservative about approach geometry; angled ladders, crouching
ladder variants, unusual dismounts and plugin movement remain unverified.

`yb_graph_analyze_breakables 1` temporarily makes shootable glass `func_breakable`
entities non-solid, hidden and non-damageable during dedicated exploration.
Material keyvalues are tracked per entity/serial; wood, metal, unknown materials
and unbreakable glass remain solid.
Only entities YaPB already considers shootable are eligible; trigger-only and
unbreakable objects remain obstacles. Original solidity, rendering and damage
state are restored on completion, cancellation, map shutdown and plugin detach,
with serial-number checks protecting reused entity slots. No BSP files are
edited and no break target/use events are fired. Routes through their bounds
carry a breakable-obstruction flag; normal bots use their breakable-shooting
task when the restored object obstructs a route. Breakable floors are also
absent during probing, so the graph does not rely on destroyed glass bridges.

New fixtures include a staircase, a low ceiling missed by an overhead probe,
a ledge requiring running air-duck rather than a slow crouch-first jump, ladder
ascent/descent at two tick intervals, broken contact, an obstruction and a
missing upper landing. On `de_downtown_csneo`, ladder ascent and descent each
passed twice, and all 33 temporarily excluded breakables were restored in the
bounded save-path check. These remain predictions; ordinary-bot route replay is
a separate in-game validation. The 33-object result above describes virtual9;
virtual11 narrows exclusions to glass.

### Virtual11 collision and spacing corrections

Normal trials must stay within two units of the horizontal source-target line;
sliding around an obstacle cannot justify a straight connection through it.
Walking links also pass a full-hull corridor audit with eight-unit samples,
step-height limits and floor support. Nearby stair/ramp samples can share a node
only when this walking corridor works in both directions. Gaps, walls and stance
changes prevent that reuse. Jump arcs and ladder contact retain separate checks.

Stationary, non-damageable solid `func_wall` platforms participate as geometry.
Floor candidates and stable arrivals require support below the feet center,
accounting for ramp inclination, rather than only a hull corner touching a ledge.
Regenerate older graphs to apply these rules; loading an old file does not rewrite
its routes. Ordinary-bot traversal still requires in-game validation.

### Virtual12 stair and corner coverage repair

Virtual11's feet-center support check was too shallow at stair edges: the hull
can rest on the next tread while the feet center remains over the previous
tread. Support depth now includes the configured step height, while voids and
drops beyond that allowance still fail the check.

Each source first probes eight directions by walking the hull forward in
eight-unit increments. Unlike the fixed-height distant floor probe, this follows
successive risers up an entire flight. On encountering a blocker or unsupported
drop, it keeps the last reachable position as a candidate for a turn or landing.
These endpoints use a 32-unit reuse radius; ordinary open-ground nodes retain
the configured spacing. All candidates still need repeated movement trials;
walking links retain the full-hull corridor audit. Snapping a walking candidate
to an existing node must also pass a direct walking audit from its source.

Fixtures cover center support over adjacent treads, discovery up a flight taller
than the old overhead probe, stopping before a stair obstruction, and an L-shaped
corridor that requires an intermediate corner node. Windows x86 and Linux x86
builds and the standalone behavior suites are checked separately from in-game
route replay.

Virtual13 also normalizes crouched walking-frontier endpoints to standing height
when a full standing hull fits there. Crouch-only passages retain their stance.
This prevents crouched sources from propagating a duplicate lower waypoint layer
through rooms with standing headroom; route stance still controls the traversal.
Fixtures cover both an open-floor endpoint that can stand and a tunnel that cannot.
