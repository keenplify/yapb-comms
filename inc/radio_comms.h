// SPDX-License-Identifier: MIT
#pragma once

enum class TacticalRadioRoute { Radio, Chat, Suppress };

inline TacticalRadioRoute tacticalRadioRoute (bool takingFire, bool chatOnly,
   bool both, bool chatReady, int &sequence) {
   // Combat distress never requires the bot to stop and type, even for a
   // chat-only personality or while its tactical chat is on cooldown.
   if (takingFire) return TacticalRadioRoute::Radio;
   if (chatOnly) return chatReady ? TacticalRadioRoute::Chat : TacticalRadioRoute::Suppress;
   if (both && (++sequence % 2 == 0) && chatReady) return TacticalRadioRoute::Chat;
   return TacticalRadioRoute::Radio;
}
