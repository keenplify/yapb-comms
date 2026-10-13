// SPDX-License-Identifier: MIT
#pragma once
#include <cstring>

enum class PingTemperament { Quiet, Grumbler, Tilted };

inline PingTemperament pingTemperamentForStyle (PingTemperament fallback, int personality, const char *style) {
   if (personality == 2 || std::strcmp (style, "quiet") == 0
      || std::strcmp (style, "sweet") == 0 || std::strcmp (style, "dry") == 0) return PingTemperament::Quiet;
   if (std::strcmp (style, "rude") == 0) return PingTemperament::Tilted;
   if (std::strcmp (style, "talkative") == 0) return PingTemperament::Grumbler;
   return fallback;
}

inline PingTemperament pingTemperament (int personality, int roll) {
   // Careful bots never blame latency; even rushers can be stoic.
   if (personality == 1 && roll < 80) return PingTemperament::Tilted;
   if (personality == 0 && roll < 40) return PingTemperament::Grumbler;
   return PingTemperament::Quiet;
}

inline bool shouldComplainAboutPing (PingTemperament mood, int ping, bool death,
   float now, float nextAllowed, int roll) {
   if (mood == PingTemperament::Quiet || ping < 60 || now < nextAllowed) return false;
   if (ping < 100) return death && roll < (mood == PingTemperament::Tilted ? 15 : 5);
   const int chance = mood == PingTemperament::Tilted ? (death ? 65 : 25) : (death ? 30 : 10);
   return roll < chance;
}

inline int laggingDisplayPing (int base, int jitter) {
   const int ping = base + jitter;
   const int minimum = base < 100 ? 45 : 100;
   const int maximum = base < 100 ? 99 : 300;
   return ping < minimum ? minimum : ping > maximum ? maximum : ping;
}
