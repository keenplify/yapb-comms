#include <team_order.h>
#include <chat_intent.h>
#include <team_allocation.h>
#include <cassert>
#include <cstdio>

int main () {
   struct Case { const char *text; TeamOrder expected; };
   constexpr Case cases[] = {
      {"eco", TeamOrder::Eco}, {"force", TeamOrder::Force}, {"force buy", TeamOrder::Force},
      {"full buy", TeamOrder::FullBuy}, {"save", TeamOrder::Save}, {"half buy", TeamOrder::HalfBuy},
      {"drop", TeamOrder::Drop}, {"buy me", TeamOrder::BuyMe}, {"can you drop", TeamOrder::BuyMe},
      {"drop ak pls", TeamOrder::DropWeapon}, {"can you drop me an ak?", TeamOrder::DropWeapon},
      {"buy me ak47", TeamOrder::DropWeapon}, {"drop awp pls", TeamOrder::DropWeapon},
      {"can i get an awm?", TeamOrder::DropWeapon}, {"drop m4", TeamOrder::DropWeapon},
      {"drop deag", TeamOrder::DropWeapon}, {"drop scout", TeamOrder::DropWeapon},
      {"drop fiveseven", TeamOrder::DropWeapon}, {"drop m249", TeamOrder::DropWeapon},
      {"rotate", TeamOrder::Rotate}, {"rotate a", TeamOrder::RotateA}, {"rotate b", TeamOrder::RotateB},
      {"go a", TeamOrder::GoA}, {"go b", TeamOrder::GoB}, {"leave a", TeamOrder::LeaveA},
      {"all a", TeamOrder::AllA}, {"all b", TeamOrder::AllB},
      {"all mid", TeamOrder::AllMid}, {"rush b", TeamOrder::RushB},
      {"rush mid", TeamOrder::RushMid},
      {"leave b", TeamOrder::LeaveB}, {"play safe", TeamOrder::PlaySafe},
      {"don't peek", TeamOrder::PlaySafe}, {"wait", TeamOrder::PlaySafe}, {"hold", TeamOrder::Hold},
      {"push", TeamOrder::Push}, {"rush", TeamOrder::Rush}, {"contact", TeamOrder::Contact},
      {"default", TeamOrder::Default}, {"split", TeamOrder::Split}, {"execute", TeamOrder::Execute},
      {"retake", TeamOrder::Retake}, {"one mid", TeamOrder::Info}, {"two b", TeamOrder::Info},
      {"last b", TeamOrder::Info}, {"bomb b", TeamOrder::Info}, {"bomb down", TeamOrder::Info},
      {"awp", TeamOrder::Info}, {"one hp", TeamOrder::Info}, {"low", TeamOrder::Info},
      {"lit 50", TeamOrder::Info}, {"tagged", TeamOrder::Info},
      {"smoke", TeamOrder::Utility}, {"flash", TeamOrder::Utility},
      {"flash me", TeamOrder::Utility}, {"molly", TeamOrder::Utility},
      {"nade", TeamOrder::Utility}, {"HE", TeamOrder::Utility},
      {"smoke CT", TeamOrder::Utility}, {"smoke jungle", TeamOrder::Utility},
      {"trade", TeamOrder::Trade}, {"trade me", TeamOrder::Trade},
      {"can u jump", TeamOrder::Jump}, {"jump", TeamOrder::Jump},
      {"refrag", TeamOrder::Trade}, {"bait", TeamOrder::Bait},
      {"double peek", TeamOrder::DoublePeek}, {"swing", TeamOrder::Swing},
      {"follow me", TeamOrder::FollowMe}, {"hold for me", TeamOrder::HoldForMe},
      {"watch flank", TeamOrder::WatchOther}, {"watch back", TeamOrder::WatchBack},
      {"watch short", TeamOrder::WatchOther}, {"watch long", TeamOrder::WatchOther},
      {"watch apps", TeamOrder::WatchApps}, {"watch mid", TeamOrder::WatchMid}
   };
   for (const auto &test : cases) assert (parseTeamOrder (test.text) == test.expected);
   assert (parseTeamOrder ("\"force buy\"") == TeamOrder::Force);
   assert (parseTeamOrder ("DON'T PEEK") == TeamOrder::PlaySafe);
   assert (parseTeamOrder ("can you drop?") == TeamOrder::BuyMe);
   assert (parseTeamOrder ("rotate b") == TeamOrder::RotateB);
   assert (parseTeamOrder ("smoke jungle") == TeamOrder::Utility);
   assert (parseTeamOrder ("watch flank") == TeamOrder::WatchOther);
   assert (parseTeamOrder ("guys, rotate to B now") == TeamOrder::RotateB);
   assert (parseTeamOrder ("please go to A") == TeamOrder::GoA);
   assert (parseTeamOrder ("let's leave A") == TeamOrder::LeaveA);
   assert (parseTeamOrder ("hold here please") == TeamOrder::Hold);
   assert (parseTeamOrder ("we need to eco this round") == TeamOrder::Eco);
   assert (parseTeamOrder ("can you drop me?") == TeamOrder::BuyMe);
   assert (parseTeamOrder ("can someone drop me?") == TeamOrder::BuyMe);
   assert (parseTeamOrder ("watch middle please") == TeamOrder::WatchMid);
   assert (parseTeamOrder ("please push through mid") == TeamOrder::RushMid);
   assert (parseTeamOrder ("everyone go B now") == TeamOrder::AllB);
   assert (parseTeamOrder ("guys, all go A") == TeamOrder::AllA);
   assert (parseTeamOrder ("whole team to mid") == TeamOrder::AllMid);
   assert (parseTeamOrder ("let's rush through mid") == TeamOrder::RushMid);
   assert (parseTeamOrder ("bots rush B now") == TeamOrder::RushB);
   assert (parseTeamOrder ("let's all push B") == TeamOrder::RushB);
   assert (parseTeamOrder ("team to B") == TeamOrder::AllB);
   assert (parseTeamOrder ("all A or B") == TeamOrder::None);
   assert (parseTeamOrder ("don't push") == TeamOrder::None);
   assert (parseTeamOrder ("don't drop ak") == TeamOrder::None);
   assert (std::strcmp (parseRequestedDropWeapon ("can i get an awm?"), "weapon_awp") == 0);
   assert (std::strcmp (parseRequestedDropWeapon ("drop m4 pls"), "weapon_m4a1") == 0);
   assert (parseRequestedDropWeapon ("don't drop awp") == nullptr);
   constexpr const char *firearms[] = {
      "usp", "glock", "deagle", "p228", "elite", "fiveseven",
      "m3", "xm1014", "mp5", "tmp", "p90", "mac10", "ump45",
      "ak47", "sg552", "m4a1", "galil", "famas", "aug",
      "scout", "awp", "g3sg1", "sg550", "m249"
   };
   for (const auto *name : firearms) {
      char request[32] {};
      std::snprintf (request, sizeof (request), "drop %s pls", name);
      assert (parseRequestedDropWeapon (request) != nullptr);
      assert (parseTeamOrder (request) == TeamOrder::DropWeapon);
   }
   assert (parseTeamOrder ("please don't push") == TeamOrder::None);
   assert (parseTeamOrder ("do not rotate b") == TeamOrder::None);
   assert (parseTeamOrder ("can we push?") == TeamOrder::None);
   assert (parseTeamOrder ("push was bad") == TeamOrder::None);
   assert (parseTeamOrder ("saver") == TeamOrder::None);
   assert (parseTeamOrder ("rotate a or b") == TeamOrder::None);
   assert (parseTeamOrder ("save the round?") == TeamOrder::None);
   assert (parseTeamOrder ("this is way too long and should never be interpreted as a command to any of the bots on my team") == TeamOrder::None);
   char alias[97] {};
   bool question = false;
   assert (normalizeTeamPhrase ("\"Stack B!\"", alias, question));
   assert (std::strcmp (alias, "stack b") == 0);
   assert (!normalizeTeamPhrase ("bad; command", alias, question));
   assert (asksAboutBots ("is everyone a bot?"));
   assert (asksAboutBots ("are we all bots"));
   assert (asksAboutBots ("any humans here?"));
   assert (!asksAboutBots ("bots follow me"));
   assert (!asksAboutBots ("one bot at B"));
   TeamAllocation allocation {};
   assert (parseTeamAllocation ("3 A 2 B", allocation));
   assert (allocation.a == 3 && allocation.b == 2 && allocation.mid == 0);
   assert (parseTeamAllocation ("2A 1 mid 2B", allocation));
   assert (allocation.a == 2 && allocation.mid == 1 && allocation.b == 2);
   assert (!parseTeamAllocation ("3 A 3 A", allocation));
   assert (!parseTeamAllocation ("2 A 2 B 2 mid?", allocation));
}
