// SPDX-License-Identifier: MIT
#pragma once

#include <team_order.h>

// Match a spoken name part such as "sim" to "simslater" or "sam" to
// "sk8_sam_101". The caller checks ambiguity across the entire bot roster.
inline bool mentionsBotNamePrefix (const char *words, const char *normalizedName) {
   if (!words || !normalizedName) return false;
   for (const char *nameWord = normalizedName; *nameWord;) {
      const char *nameEnd = std::strchr (nameWord, ' ');
      const size_t nameLength = nameEnd ? static_cast <size_t> (nameEnd - nameWord) : std::strlen (nameWord);
      const bool common = (nameLength == 3 && (std::strncmp (nameWord, "the", 3) == 0
         || std::strncmp (nameWord, "man", 3) == 0 || std::strncmp (nameWord, "bot", 3) == 0));
      bool hasLetter = false;
      for (size_t i = 0; i < nameLength; ++i) hasLetter |= nameWord[i] >= 'a' && nameWord[i] <= 'z';
      if (nameLength >= 3 && !common && hasLetter) {
         for (const char *word = words; *word;) {
            const char *end = std::strchr (word, ' ');
            const size_t length = end ? static_cast <size_t> (end - word) : std::strlen (word);
            if (length >= 3 && length <= nameLength
               && (nameLength > 3 || length == nameLength)
               && !(length == 3 && std::strncmp (word, "bot", 3) == 0)
               && std::strncmp (word, nameWord, length) == 0) return true;
            if (!end) break;
            word = end + 1;
         }
      }
      if (!nameEnd) break;
      nameWord = nameEnd + 1;
   }
   return false;
}

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

inline bool asksForResponse (const char *raw) {
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
   const bool responseWord = has ("respond") || has ("responding") || has ("reply")
      || has ("replying") || has ("answer") || has ("answering");
   const bool personWord = has ("anyone") || has ("anybody") || has ("nobody")
      || has ("everyone") || has ("everybody") || has ("you")
      || (has ("no") && has ("one"));
   return responseWord && (personWord || question || has ("why") || has ("whyy"));
}

inline bool startsWithGreeting (const char *raw) {
   char words[97] {};
   bool question = false;
   if (!normalizeTeamPhrase (raw, words, question)) return false;
   const char *greetings[] = { "hello", "hey", "hi", "yo", "kamusta", "kumusta", "musta", "uy",
      "hola", "bonjour", "salut", "hallo", "ciao", "ola", "namaste", "privet", "nihao", "konnichiwa" };
   for (const auto *greeting : greetings) {
      const size_t length = std::strlen (greeting);
      if (std::strncmp (words, greeting, length) == 0
         && (words[length] == '\0' || words[length] == ' ')) return true;
   }
   return false;
}

// Forward ordinary conversation as well as direct questions. Short tactical
// fragments stay with the built-in game communication path.
inline bool isConversationalStatement (const char *words) {
   if (!words || !*words) return false;
   if (std::strcmp (words, "notice me") == 0 || std::strcmp (words, "busy huh") == 0
      || std::strcmp (words, "u there") == 0 || std::strcmp (words, "you there") == 0) return true;
   const char *tagalogOpeners[] = { "ano", "bakit", "saan", "paano", "pwede", "salamat" };
   for (const auto *opener : tagalogOpeners) {
      const size_t length = std::strlen (opener);
      if (std::strncmp (words, opener, length) == 0
         && (words[length] == '\0' || words[length] == ' ')) return true;
   }
   const char *firstSpace = std::strchr (words, ' ');
   if (!firstSpace) return false;
   if (std::strchr (firstSpace + 1, ' ')) return true;
   return std::strncmp (words, "i ", 2) == 0
      || std::strncmp (words, "im ", 3) == 0
      || std::strncmp (words, "my ", 3) == 0
      || std::strncmp (words, "we ", 3) == 0;
}
