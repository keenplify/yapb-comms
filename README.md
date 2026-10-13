## YaPB
[![Latest YaPB](https://img.shields.io/github/v/release/yapb/yapb)](https://github.com/yapb/yapb/releases/latest) [![Latest YaPB](https://github.com/yapb/yapb/workflows/build/badge.svg)](https://github.com/yapb/yapb/actions) [![YaPB License](https://img.shields.io/github/license/yapb/yapb)](https://github.com/yapb/yapb/blob/master/LICENSE) [![Downloads](https://img.shields.io/github/downloads/yapb/yapb/total)](https://github.com/yapb/yapb/releases/latest)

## ☉ About
It's a computer controlled players (bots) for the Counter-Strike b6.5 - 1.6 and Counter-Strike: Condition Zero. Bots allows you to play that games without connecting any game server or even without internet.

## ☉ Documentation
* English: https://yapb.readthedocs.io/en/latest/
* Russian: https://yapb.readthedocs.io/ru/latest/

## ☉ Waypoints
All requests/bugs regarding bots navigation graph (waypoints) are located in this [repository](https://github.com/yapb/graph). if you have  waypoint request, please post an issue there.

## Local GitHub releases

From this YaPB repository, run `npm run release:local`. It builds the Linux x86
32-bit module locally, runs the C++ behavior and Python tests, verifies the ELF
architecture, and publishes `comms-local-<commit SHA>` to the GitHub `origin` repository.
Assets are `yapb-linux-x86.so` and its SHA-256 checksum, matching the CI release
format. GitHub generates release notes from the committed changes.
Local tags are separate from CI's `comms-<commit SHA>` releases to avoid collisions.

Requirements: Node.js 20+, npm, Git, authenticated GitHub CLI (`gh auth login`),
CMake, Python 3, and a C++ compiler with 32-bit development libraries. The command
initializes missing submodules, stages and commits pending YaPB changes, and
pushes the current branch to origin before building and publishing. Automatic
commits use `[skip ci]` to avoid a redundant Actions build. Ignored files are
excluded. It stops on merge conflicts, detached HEAD, changed dependency revisions,
source changes during the build, push failures and existing releases. It never
force-pushes or resets changes. Assets are uploaded to
a draft before publication; a failed upload leaves the draft for inspection.
Local artifacts are kept under `dist/local-release/`. No GitHub Actions build
is required. Run `npm run test:release:local` to test the tooling without publishing.

## Competitive enemy reports

With the coordinated core (competitive_bot_reports=1) and yb_ping_comms=1, enemy sightings choose one of count + enemy NAV callout, Enemy spotted radio, or enemy ping. Count and ping commands go through the server-only 16competitive_bot_report bridge; ordinary bot client commands bypass AMXX and cannot provide this integration. Core validates current visibility/team/cooldowns and excludes FFA. A missing callout falls back to a ping. The default rate is 12 seconds per bot and five seconds per team; legacy AMXX polling is disabled in this build. yb_comms_debug=1 prints the selected channel; the core logs actual count callouts and marker creation. Build the 32-bit Linux module and ship it together with the updated core.
