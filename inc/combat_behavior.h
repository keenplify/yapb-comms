// SPDX-License-Identifier: MIT
#pragma once

// Mechanical execution only: navigation and objectives never depend on this profile.
struct CombatBehavior {
   int earlyFirePercent;
   float trackingInterval;
   float settleSeconds;
   float errorScale;
   float turnAcceleration;
   float turnStiffness;
};

inline constexpr CombatBehavior combatBehavior (int difficulty) {
   constexpr CombatBehavior profiles[] = {
      { 60, 0.10f, 0.55f, 0.65f, 1400.0f, 90.0f },
      { 55, 0.08f, 0.45f, 0.55f, 1700.0f, 115.0f },
      { 40, 0.06f, 0.42f, 0.45f, 1900.0f, 130.0f },
      { 20, 0.04f, 0.28f, 0.22f, 2400.0f, 170.0f },
      { 10, 0.03f, 0.18f, 0.10f, 2800.0f, 210.0f }
   };
   return profiles[difficulty < 0 ? 0 : difficulty > 4 ? 4 : difficulty];
}

inline constexpr bool combatTraceClear (float fraction, bool hitTarget, bool startSolid, bool allSolid) {
   return !startSolid && !allSolid && (hitTarget || fraction >= 1.0f);
}

inline constexpr float combatReactionDelay (float configured, bool earlyFire) {
   // Early fire is premature alignment, never zero-time perception.
   const float delay = configured * (earlyFire ? 0.55f : 1.0f);
   return delay < 0.12f ? 0.12f : delay;
}

// Learned motor tuning cannot erase the mechanical differences between tiers.
inline constexpr float combatStiffnessMultiplier (int difficulty, float requested) {
   const int level = difficulty < 0 ? 0 : difficulty > 4 ? 4 : difficulty;
   const float ceiling = 1.2f + .2f * level;
   return requested < .75f ? .75f : requested > ceiling ? ceiling : requested;
}
