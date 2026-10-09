## YaPB
[![Latest YaPB](https://img.shields.io/github/v/release/yapb/yapb)](https://github.com/yapb/yapb/releases/latest) [![Latest YaPB](https://github.com/yapb/yapb/workflows/build/badge.svg)](https://github.com/yapb/yapb/actions) [![YaPB License](https://img.shields.io/github/license/yapb/yapb)](https://github.com/yapb/yapb/blob/master/LICENSE) [![Downloads](https://img.shields.io/github/downloads/yapb/yapb/total)](https://github.com/yapb/yapb/releases/latest)

## ☉ About
It's a computer controlled players (bots) for the Counter-Strike b6.5 - 1.6 and Counter-Strike: Condition Zero. Bots allows you to play that games without connecting any game server or even without internet.

## ☉ Documentation
* English: https://yapb.readthedocs.io/en/latest/
* Russian: https://yapb.readthedocs.io/ru/latest/

## ☉ Waypoints
All requests/bugs regarding bots navigation graph (waypoints) are located in this [repository](https://github.com/yapb/graph). if you have  waypoint request, please post an issue there.

## Competitive enemy reports

With the coordinated core (competitive_bot_reports=1) and yb_ping_comms=1, enemy sightings choose one of count + enemy NAV callout, Enemy spotted radio, or enemy ping. Count and ping commands go through the server-only 16competitive_bot_report bridge; ordinary bot client commands bypass AMXX and cannot provide this integration. Core validates current visibility/team/cooldowns and excludes FFA. A missing callout falls back to a ping. The default rate is 12 seconds per bot and five seconds per team; legacy AMXX polling is disabled in this build. yb_comms_debug=1 prints the selected channel; the core logs actual count callouts and marker creation. Build the 32-bit Linux module and ship it together with the updated core.
