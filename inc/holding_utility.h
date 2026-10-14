// SPDX-License-Identifier: MIT
#pragma once

inline constexpr bool learnedHoldingSpotAvailable (int seenRound, int currentRound,
   int seenTeam, int currentTeam, float secondsIntoRound, bool used) {
   return seenRound > 0 && currentRound == seenRound + 1 && seenTeam == currentTeam
      && secondsIntoRound >= 5.0f && secondsIntoRound <= 45.0f && !used;
}

inline constexpr bool holdingUtilityAllowed (bool liveRound, bool busy, bool seeingEnemy,
   bool outsideSpawns, float distance, float heightDifference, int throwsThisRound) {
   return liveRound && !busy && !seeingEnemy && outsideSpawns && throwsThisRound < 3
      && distance >= 400.0f && distance <= 1100.0f
      && heightDifference >= -256.0f && heightDifference <= 256.0f;
}
