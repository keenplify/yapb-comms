// SPDX-License-Identifier: MIT
#pragma once

#include <team_order.h>

struct TeamAllocation {
   int a {};
   int b {};
   int mid {};
};

inline bool parseTeamAllocation (const char *raw, TeamAllocation &allocation) {
   char text[97] {};
   bool question = false;
   if (!normalizeTeamPhrase (raw, text, question) || question) return false;
   TeamAllocation parsed {};
   const char *at = text;
   int groups = 0;
   while (*at) {
      if (*at < '1' || *at > '9') return false;
      const int count = *at++ - '0';
      while (*at == ' ') ++at;
      char place[12] {};
      int length = 0;
      while (*at >= 'a' && *at <= 'z') {
         if (length >= 11) return false;
         place[length++] = *at++;
      }
      if (length == 0 || (*at && *at != ' ')) return false;
      if (std::strcmp (place, "a") == 0 && parsed.a == 0) parsed.a = count;
      else if (std::strcmp (place, "b") == 0 && parsed.b == 0) parsed.b = count;
      else if ((std::strcmp (place, "mid") == 0 || std::strcmp (place, "middle") == 0) && parsed.mid == 0) parsed.mid = count;
      else return false;
      ++groups;
      while (*at == ' ') ++at;
   }
   if (groups < 2 || parsed.a + parsed.b + parsed.mid > 16) return false;
   allocation = parsed;
   return true;
}
