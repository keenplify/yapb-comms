// SPDX-License-Identifier: MIT
#pragma once
#include <cstring>

struct TacticalPhrase {
   const char *trigger;
   const char *variants[3];
};
inline constexpr TacticalPhrase tacticalPhrases[] = {
   { "Contact. Enemy spotted.", { "contact, enemy ahead", "enemy spotted", "got contact here" } },
   { "Lost contact. Watching last position.", { "lost sight, watching the corner", "lost contact", "they moved out of sight" } },
   { "Reloading. Cover me.", { "reloading, cover me", "need a second to reload", "cover, changing mag" } },
   { "Taking fire. Need help.", { "taking fire, need help", "under fire here", "need a hand, taking fire" } },
   { "Coming to help.", { "coming to help", "moving up to support", "on my way to you" } },
   { "Cover me.", { "cover me", "watch my angle", "keep me covered" } },
   { "Need backup.", { "need backup", "could use some help here", "need another gun here" } },
   { "Enemy down.", { "enemy down", "got one", "one down" } },
   { "Regroup.", { "regroup", "lets group up", "come back together" } },
   { "Fall back.", { "fall back", "back out of there", "lets pull back" } },
   { "Stay together.", { "stay together", "keep close", "lets not split up" } },
   { "Go now.", { "go now", "lets move", "moving out" } },
   { "Hold this position.", { "hold here", "keep this position", "hold this angle" } },
   { "In position.", { "in position", "ready here", "set up here" } },
   { "Bomb spotted.", { "bomb spotted here", "i can see the bomb", "bomb down here" } },
   { "Rotating.", { "rotating", "moving to the bomb", "on the rotate" } }
};
inline constexpr int tacticalPhraseCount = sizeof (tacticalPhrases) / sizeof (tacticalPhrases[0]);

inline int tacticalPhraseIndex (const char *message) {
   for (int i = 0; i < tacticalPhraseCount; ++i) {
      if (std::strcmp (message, tacticalPhrases[i].trigger) == 0) return i;
   }
   return -1;
}

// Team-level semantic cooldown survives variant changes, but handles map-clock resets.
struct TacticalCommsGate {
   float lastTime = -100.0f;
   float events[tacticalPhraseCount] {};
   bool used[tacticalPhraseCount] {};
   int previousVariant[tacticalPhraseCount] {};

   bool available (float now, int event) {
      if (now < lastTime) *this = TacticalCommsGate {};
      if (now - lastTime < 3.0f) return false;
      return event < 0 || !used[event] || now - events[event] >= 12.0f;
   }
   void record (float now, int event) {
      lastTime = now;
      if (event >= 0) { used[event] = true; events[event] = now; }
   }
};
