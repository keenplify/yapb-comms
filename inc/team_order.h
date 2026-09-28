// SPDX-License-Identifier: MIT
#pragma once

#include <cctype>
#include <cstring>

// Bounded team-chat vocabulary with exact phrases and a conservative
// whole-word fallback. A substring of ordinary chat is never an order.
enum class TeamOrder {
   None, Eco, Force, FullBuy, Save, HalfBuy, Drop, DropWeapon, BuyMe,
   Rotate, RotateA, RotateB, GoA, GoB, LeaveA, LeaveB,
   AllA, AllB, AllMid, RushA, RushB, RushMid,
   PlaySafe, Hold, Push, Rush, Contact, Default, Split, Execute, Retake,
   Trade, Bait, DoublePeek, Swing, FollowMe, HoldForMe, Jump,
   Info, Utility, WatchMid, WatchApps, WatchBack, WatchOther
};

inline bool normalizeTeamPhrase (const char *raw, char (&text)[97], bool &question) {
   if (raw == nullptr) return false;
   text[0] = '\0';
   size_t length = 0;
   question = false;
   for (const unsigned char *it = reinterpret_cast <const unsigned char *> (raw); *it; ++it) {
      if (length >= 96) return false;
      const auto ch = *it;
      if (std::isalnum (ch)) {
         text[length++] = static_cast <char> (std::tolower (ch));
      }
      else if (ch == ' ' || ch == '\t' || ch == '-' || ch == '_' || ch == '\'' || ch == '?' || ch == ',' || ch == '.') {
         question |= ch == '?';
         if (length > 0 && text[length - 1] != ' ') {
            text[length++] = ' ';
         }
      }
      else if (ch != '"' && ch != '!') {
         return false;
      }
   }
   while (length > 0 && text[length - 1] == ' ') {
      text[--length] = '\0';
   }
   text[length] = '\0';
   return length > 0;
}

// Resolve common CS 1.6 firearm names, including player shorthand. Return
// the game's weapon class so runtime buy restrictions and prices stay in YaPB.
inline const char *parseRequestedDropWeapon (const char *raw) {
   char text[97] {};
   bool question = false;
   if (!normalizeTeamPhrase (raw, text, question)) return nullptr;
   const auto starts = [] (const char *value, const char *phrase) {
      const size_t size = std::strlen (phrase);
      return std::strncmp (value, phrase, size) == 0 && (value[size] == '\0' || value[size] == ' ');
   };
   const auto has = [] (const char *value, const char *phrase) {
      const size_t size = std::strlen (phrase);
      for (const char *at = value; (at = std::strstr (at, phrase)) != nullptr; ++at) {
         if ((at == value || at[-1] == ' ') && (at[size] == '\0' || at[size] == ' ')) return true;
      }
      return false;
   };
   if (has (text, "dont") || has (text, "don t") || has (text, "not")
      || has (text, "never") || has (text, "no")) return nullptr;
   const char *request = text;
   constexpr const char *prefixes[] = { "bot", "bots", "guys", "team", "please", "pls", "hey" };
   for (int skipped = 0; skipped < 2; ++skipped) {
      bool found = false;
      for (const auto *prefix : prefixes) {
         if (starts (request, prefix) && request[std::strlen (prefix)] == ' ') {
            request += std::strlen (prefix) + 1;
            found = true;
            break;
         }
      }
      if (!found) break;
   }
   if (!(starts (request, "drop") || starts (request, "buy me")
      || starts (request, "can you drop") || starts (request, "could you drop")
      || starts (request, "can i get") || starts (request, "need"))) return nullptr;
   struct Alias { const char *spoken; const char *weapon; };
   constexpr Alias aliases[] = {
      { "ak", "weapon_ak47" }, { "ak47", "weapon_ak47" }, { "ak 47", "weapon_ak47" },
      { "m4", "weapon_m4a1" }, { "m4a", "weapon_m4a1" }, { "m4a1", "weapon_m4a1" }, { "colt", "weapon_m4a1" },
      { "aw", "weapon_awp" }, { "awp", "weapon_awp" }, { "awm", "weapon_awp" },
      { "deag", "weapon_deagle" }, { "deagle", "weapon_deagle" }, { "desert eagle", "weapon_deagle" },
      { "usp", "weapon_usp" }, { "glock", "weapon_glock18" }, { "glock18", "weapon_glock18" },
      { "p228", "weapon_p228" }, { "elite", "weapon_elite" }, { "dualies", "weapon_elite" },
      { "fiveseven", "weapon_fiveseven" }, { "five seven", "weapon_fiveseven" }, { "fn57", "weapon_fiveseven" },
      { "m3", "weapon_m3" }, { "xm1014", "weapon_xm1014" },
      { "mp5", "weapon_mp5navy" }, { "mp5navy", "weapon_mp5navy" },
      { "tmp", "weapon_tmp" }, { "p90", "weapon_p90" },
      { "mac10", "weapon_mac10" }, { "mac 10", "weapon_mac10" },
      { "ump", "weapon_ump45" }, { "ump45", "weapon_ump45" },
      { "sg552", "weapon_sg552" }, { "krieg", "weapon_sg552" }, { "galil", "weapon_galil" },
      { "famas", "weapon_famas" }, { "aug", "weapon_aug" },
      { "scout", "weapon_scout" }, { "g3sg1", "weapon_g3sg1" },
      { "sg550", "weapon_sg550" }, { "m249", "weapon_m249" }, { "para", "weapon_m249" }
   };
   for (const auto &alias : aliases) {
      if (has (request, alias.spoken)) return alias.weapon;
   }
   return nullptr;
}

inline TeamOrder parseTeamOrder (const char *raw) {
   char text[97] {};
   bool question = false;
   if (!normalizeTeamPhrase (raw, text, question)) return TeamOrder::None;
   if (parseRequestedDropWeapon (raw) != nullptr) return TeamOrder::DropWeapon;
   const auto equals = [&text] (const char *phrase) { return std::strcmp (text, phrase) == 0; };
   if (equals ("eco")) return TeamOrder::Eco;
   if (equals ("force") || equals ("force buy")) return TeamOrder::Force;
   if (equals ("full buy")) return TeamOrder::FullBuy;
   if (equals ("save")) return TeamOrder::Save;
   if (equals ("half buy")) return TeamOrder::HalfBuy;
   if (equals ("drop")) return TeamOrder::Drop;
   if (equals ("buy me") || equals ("can you drop")) return TeamOrder::BuyMe;
   if (equals ("rotate")) return TeamOrder::Rotate;
   if (equals ("rotate a")) return TeamOrder::RotateA;
   if (equals ("rotate b")) return TeamOrder::RotateB;
   if (equals ("go a")) return TeamOrder::GoA;
   if (equals ("go b")) return TeamOrder::GoB;
   if (equals ("all a")) return TeamOrder::AllA;
   if (equals ("all b")) return TeamOrder::AllB;
   if (equals ("all mid") || equals ("all middle")) return TeamOrder::AllMid;
   if (equals ("rush a")) return TeamOrder::RushA;
   if (equals ("rush b")) return TeamOrder::RushB;
   if (equals ("rush mid") || equals ("rush middle")) return TeamOrder::RushMid;
   if (equals ("leave a")) return TeamOrder::LeaveA;
   if (equals ("leave b")) return TeamOrder::LeaveB;
   if (equals ("play safe") || equals ("don t peek") || equals ("wait")) return TeamOrder::PlaySafe;
   if (equals ("hold")) return TeamOrder::Hold;
   if (equals ("push")) return TeamOrder::Push;
   if (equals ("rush")) return TeamOrder::Rush;
   if (equals ("contact")) return TeamOrder::Contact;
   if (equals ("default")) return TeamOrder::Default;
   if (equals ("split")) return TeamOrder::Split;
   if (equals ("execute")) return TeamOrder::Execute;
   if (equals ("retake")) return TeamOrder::Retake;
   if (equals ("trade") || equals ("trade me") || equals ("refrag")) return TeamOrder::Trade;
   if (equals ("bait")) return TeamOrder::Bait;
   if (equals ("double peek")) return TeamOrder::DoublePeek;
   if (equals ("swing")) return TeamOrder::Swing;
   if (equals ("follow me")) return TeamOrder::FollowMe;
   if (equals ("jump") || equals ("can you jump") || equals ("can u jump") || equals ("bots jump")) return TeamOrder::Jump;
   if (equals ("hold for me")) return TeamOrder::HoldForMe;
   if (equals ("one mid") || equals ("two b") || equals ("last b") || equals ("bomb b") || equals ("bomb down") || equals ("awp") || equals ("one hp") || equals ("low") || equals ("tagged") || equals ("lit 50")) return TeamOrder::Info;
   if (equals ("smoke") || equals ("flash") || equals ("flash me") || equals ("molly") || equals ("nade") || equals ("he") || equals ("smoke ct") || equals ("smoke jungle")) return TeamOrder::Utility;
   if (equals ("watch mid")) return TeamOrder::WatchMid;
   if (equals ("watch apps")) return TeamOrder::WatchApps;
   if (equals ("watch back")) return TeamOrder::WatchBack;
   if (equals ("watch flank") || equals ("watch short") || equals ("watch long")) return TeamOrder::WatchOther;

   // Keyword fallback: only leading command words after common forms of
   // address are accepted. Whole-word matching prevents "saver" -> "save".
   const auto starts = [] (const char *value, const char *phrase) {
      const size_t size = std::strlen (phrase);
      return std::strncmp (value, phrase, size) == 0 && (value[size] == '\0' || value[size] == ' ');
   };
   const auto has = [] (const char *value, const char *phrase) {
      const size_t size = std::strlen (phrase);
      for (const char *at = value; (at = std::strstr (at, phrase)) != nullptr; ++at) {
         if ((at == value || at[-1] == ' ') && (at[size] == '\0' || at[size] == ' ')) return true;
      }
      return false;
   };
   const char *command = text;
   constexpr const char *prefixes[] = {
      "bots", "bot", "team", "guys", "everyone", "all", "please", "pls", "hey",
      "let s", "lets", "we need to", "we should", "can you", "could you", "can someone",
      "whole team", "entire team"
   };
   bool directQuestion = false;
   for (int n = 0; n < 4; ++n) {
      bool skipped = false;
      for (const char *prefix : prefixes) {
         if (starts (command, prefix) && command[std::strlen (prefix)] == ' ') {
            directQuestion |= std::strcmp (prefix, "can you") == 0 || std::strcmp (prefix, "could you") == 0 || std::strcmp (prefix, "can someone") == 0;
            command += std::strlen (prefix) + 1;
            skipped = true;
            break;
         }
      }
      if (!skipped) break;
   }
   if (starts (command, "don t peek") || starts (command, "dont peek") || starts (command, "do not peek")) return TeamOrder::PlaySafe;
   if (has (command, "don t") || has (command, "dont") || has (command, "not") || has (command, "never") || has (command, "no")) return TeamOrder::None;
   if (question && !directQuestion) return TeamOrder::None;

   // Constrain the remaining words as well as the leading verb. This avoids
   // acting on a narrative such as "push was bad".
   constexpr char allowed[] = "rotate go leave watch hold for me play safe wait follow trade refrag push rush execute swing contact default split retake smoke flash molly nade eco save force full half buy drop can someone a b to the site mid middle apps apartments back flank short long here there now please pls team guys everyone all whole entire this round ct jungle awp with us together through";
   for (const char *at = command; *at;) {
      char word[24] {};
      size_t size = 0;
      while (*at && *at != ' ') {
         if (size >= sizeof (word) - 1) return TeamOrder::None;
         word[size++] = *at++;
      }
      while (*at == ' ') ++at;
      if (!has (allowed, word)) return TeamOrder::None;
   }

   if (starts (command, "buy me") || (starts (command, "drop") && has (command, "me"))) return TeamOrder::BuyMe;
   if (starts (command, "full buy")) return TeamOrder::FullBuy;
   if (starts (command, "half buy")) return TeamOrder::HalfBuy;
   if (starts (command, "force")) return TeamOrder::Force;
   if (starts (command, "eco")) return TeamOrder::Eco;
   if (starts (command, "save")) return TeamOrder::Save;
   if (starts (command, "drop")) return TeamOrder::Drop;

   const bool a = has (command, "a");
   const bool b = has (command, "b");
   const bool mid = has (command, "mid") || has (command, "middle");
   const bool group = has (text, "all") || has (text, "everyone") || has (text, "whole")
      || has (text, "entire") || has (text, "team") || has (text, "guys");
   if (starts (command, "rush") || starts (command, "push")) {
      if (static_cast <int> (a) + static_cast <int> (b) + static_cast <int> (mid) > 1) return TeamOrder::None;
      return a ? TeamOrder::RushA : b ? TeamOrder::RushB : mid ? TeamOrder::RushMid
         : starts (command, "rush") ? TeamOrder::Rush : TeamOrder::Push;
   }
   if (group && (starts (command, "go") || starts (command, "all") || starts (command, "to")
      || starts (command, "a") || starts (command, "b") || starts (command, "mid") || starts (command, "middle"))) {
      if (static_cast <int> (a) + static_cast <int> (b) + static_cast <int> (mid) != 1) return TeamOrder::None;
      return a ? TeamOrder::AllA : b ? TeamOrder::AllB : TeamOrder::AllMid;
   }
   if (starts (command, "rotate")) {
      if (a && b) return TeamOrder::None;
      return a ? TeamOrder::RotateA : b ? TeamOrder::RotateB : TeamOrder::Rotate;
   }
   if (starts (command, "go")) {
      if (a == b) return TeamOrder::None;
      return a ? TeamOrder::GoA : TeamOrder::GoB;
   }
   if (starts (command, "leave")) {
      if (a == b) return TeamOrder::None;
      return a ? TeamOrder::LeaveA : TeamOrder::LeaveB;
   }
   if (starts (command, "watch")) {
      if (has (command, "mid") || has (command, "middle")) return TeamOrder::WatchMid;
      if (has (command, "apps") || has (command, "apartments")) return TeamOrder::WatchApps;
      if (has (command, "back")) return TeamOrder::WatchBack;
      if (has (command, "flank") || has (command, "short") || has (command, "long")) return TeamOrder::WatchOther;
   }
   if (starts (command, "hold for me")) return TeamOrder::HoldForMe;
   if (starts (command, "play safe") || starts (command, "wait")) return TeamOrder::PlaySafe;
   if (starts (command, "hold")) return TeamOrder::Hold;
   if (starts (command, "follow me")) return TeamOrder::FollowMe;
   if (starts (command, "trade") || starts (command, "refrag")) return TeamOrder::Trade;
   if (starts (command, "execute")) return TeamOrder::Execute;
   if (starts (command, "swing")) return TeamOrder::Swing;
   if (starts (command, "contact")) return TeamOrder::Contact;
   if (starts (command, "default")) return TeamOrder::Default;
   if (starts (command, "split")) return TeamOrder::Split;
   if (starts (command, "retake")) return TeamOrder::Retake;
   if (starts (command, "smoke") || starts (command, "flash") || starts (command, "molly") || starts (command, "nade")) return TeamOrder::Utility;
   return TeamOrder::None;
}
