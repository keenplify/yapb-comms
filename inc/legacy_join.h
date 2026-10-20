#pragma once

// A roster plugin may finish the legacy menus before YaPB consumes its queued
// TeamSelect/ClassSelect message. Never replay those menus on a live combatant.
constexpr bool legacyJoinComplete (bool combatTeam, bool alive, bool solidPlayer, bool observer) {
   return combatTeam && alive && solidPlayer && !observer;
}
