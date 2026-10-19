# Manual parkour jumps and incremental graph expansion

Requires `yb version` to show `4.5.0-repopulate15` or later. Supported on a
Counter-Strike **New Game / listen server** as well as a dedicated server.
A round restart (`sv_restart`) does not reload the DLL: save edits, completely
exit Counter-Strike, then reopen it and load the map.

If the ordinary save reports disconnected nodes, preserve your current work:

```text
yb graph save nocheck
```

This bypasses the connectivity check; it does not repair missing routes.

## Record each jump

While alive, at the start of the jump:

```text
yb kickall
yb graph on
yb graph off noclip
yb graph jump
```

Run and perform the actual jump, then stay landed briefly. The console reports
`Jump recorded: A -> B. Landing seed: B.` Record each hop separately: for example,
floor to air conditioner, then air conditioner through the window.
`yb graph jump cancel` cancels an armed recording. Noclip, death, a blocked jump
that never leaves the ground, and an unsupported fall do not count as landings.
This records a directed jump using YaPB's stored jump-velocity mechanism, not
a complete recording of every input; verify bot execution of precise parkour.

## Expand from the landing

Remove bots and switch every human to spectator. On a listen server, enable
spectators, die if you are alive outside freeze time, and open the team menu:

```text
allow_spectators 1
kill
chooseteam
```

Select Spectator in the team menu, then run:

```text
yb graph repopulate last
```

Other starting points:

```text
yb graph repopulate 123
yb graph repopulate nearest
yb graph repopulate all
```

Replace `123` with the landing's node ID. The last recorded landing is remembered
until graph reload/map change; use its explicit ID afterward. With no argument,
`repopulate` uses that landing if available, otherwise the editor's nearest node.
Keep the map running until `Repopulate complete` appears. It saves automatically,
even if normal automatic analysis/save is disabled. Existing node IDs, positions,
flags, and existing jump links are retained; new links use vacant slots.
The 4,096-node format limit still applies.

Before expansion, the existing graph file receives a uniquely named
`.before-repopulate-*.bak` copy, then current manual edits are saved. The operation
uses virtual probes and temporarily excludes eligible glass, restoring it when
finished or stopped. Unsupported custom movement still needs manual routes.

`yb graph repopulate stop` stops expansion or pruning. Partial changes remain in
memory, unsaved; the pre-operation manual graph remains on disk. Load it with
`yb graph load` to discard those partial changes, or save them explicitly.

## Prune invalid links without deleting work

With bots removed and all humans spectating:

```text
yb graph prune
```

It creates the same backup, then checks walking links with the whole player hull,
floor support and stair clearance. Proven blocked links, invalid destination
indices and self-links are removed. No nodes are deleted or renumbered. Jumps,
ladders, known breakable routes, water and unmodeled dynamic movement are retained
for review; a straight-line check cannot establish whether a jump arc is valid.
Completion saves automatically. Pruning does not invent connections between
disconnected regions: add the missing manual jump, then repopulate its landing.

Rejoin a team when finished and use `yb_quota 9` to resume the local bot test.

## Validation

An isolated map copy exercised incremental growth from an eight-node fixture,
repeat/no-op expansion, cancellation during probing, shutdown during probing,
backup creation and preservation of old geometry/flags and a manual jump link.
Prune validation includes a deliberately blocked walking link and a protected
jump. Separate fixtures check real airborne/stable landing versus death,
noclip, blocked takeoff, bouncing and timeout. The exact restaurant/window route
still needs in-game bot replay.
