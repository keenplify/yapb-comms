// SPDX-License-Identifier: MIT
#pragma once
#include <cstring>

inline const char *modernMapCallout (const char *map, const char *place) {
   if (!std::strcmp (place, "BombsiteA")) return "A";
   if (!std::strcmp (place, "BombsiteB")) return "B";
   if (!std::strcmp (place, "Middle")) return "mid";
   if (!std::strcmp (place, "DoubleDoors")) return "doors";
   // Map-specific NAV names, never a global guess about an ambiguous label.
   if (!std::strcmp (map, "de_dust2")) {
      if (!std::strcmp (place, "Side")) return "long";
      if (!std::strcmp (place, "SideDoor")) return "long doors";
   }
   if (!std::strcmp (map, "de_dust") && !std::strcmp (place, "Side")) return "long";
   return place;
}
inline constexpr bool sightingReportAllowed (bool observerFiring, bool enemyFiring, bool underPressure) {
   return !observerFiring && !enemyFiring && !underPressure;
}
