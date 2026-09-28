// SPDX-License-Identifier: MIT
#pragma once

#include <team_order.h>

// Small, local conversational intents. No model or network call runs in-game.
inline bool asksAboutBots (const char *raw) {
   char words[97] {};
   bool question = false;
   if (!normalizeTeamPhrase (raw, words, question)) return false;
   const auto has = [&words] (const char *word) {
      const size_t length = std::strlen (word);
      for (const char *at = words; (at = std::strstr (at, word)) != nullptr; ++at) {
         if ((at == words || at[-1] == ' ') && (at[length] == '\0' || at[length] == ' ')) return true;
      }
      return false;
   };
   const bool aboutRoster = has ("everyone") || has ("everybody") || has ("all")
      || has ("anyone") || has ("anybody") || has ("any") || has ("you")
      || has ("we") || has ("humans");
   const bool aboutBots = has ("bot") || has ("bots") || has ("human") || has ("humans");
   const bool questionForm = question || has ("is") || has ("are") || has ("any");
   return aboutRoster && aboutBots && questionForm;
}
