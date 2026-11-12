// SPDX-License-Identifier: MIT
#pragma once

struct CasualBanterSchedule {
   float next {}, lastHuman {-1000.0f}, checked {};
   unsigned sequence {};
   bool due (float now) {
      if (now < checked) { next = 0; lastHuman = -1000.0f; }
      if (now < checked + 1.0f) return false;
      checked = now;
      if (next == 0) { next = now + 35.0f; return false; }
      return now >= next && now >= lastHuman + 20.0f;
   }
   void sent (float now) { next = now + 100.0f + (sequence++ % 3) * 15.0f; }
};
inline const char *casualBanterLine (unsigned sequence) {
   constexpr const char *lines[] = {
      "how's the game treating you?",
      "nice movement. now try dodging the bullets",
      "what map do you usually play?",
      "save some bhops for the next round",
      "ak or m4? settle this for me",
      "your crosshair taking a coffee break?",
      "one more game after this?",
      "alright, warmup's over. probably"
   };
   return lines[sequence % 8];
}
