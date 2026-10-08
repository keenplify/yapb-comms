#include <combat_behavior.h>
#include <tactical_comms.h>
#include <cassert>
#include <initializer_list>

int main () {
   // A wall immediately in front of a target is still a wall. Clear rays and
   // rays actually hitting the target remain valid; traces starting solid do not.
   assert (!combatTraceClear (0.9999f, false, false, false));
   assert (!combatTraceClear (0.5f, false, false, false));
   assert (combatTraceClear (1.0f, false, false, false));
   assert (combatTraceClear (0.8f, true, false, false));
   assert (!combatTraceClear (1.0f, true, true, false));
   assert (!combatTraceClear (1.0f, true, false, true));
   for (float reaction : {0.0f, 0.1f, 0.6f, 1.5f}) {
      assert (combatReactionDelay (reaction, true) >= 0.12f);
      assert (combatReactionDelay (reaction, false) >= combatReactionDelay (reaction, true));
   }
   assert (combatBehavior (1).earlyFirePercent > 50);
   assert (combatBehavior (4).earlyFirePercent < 50);
   for (int i = 1; i < 5; ++i) {
      const auto easy = combatBehavior (i - 1), hard = combatBehavior (i);
      assert (easy.earlyFirePercent > hard.earlyFirePercent);
      assert (easy.trackingInterval > hard.trackingInterval);
      assert (easy.settleSeconds > hard.settleSeconds);
      assert (easy.errorScale > hard.errorScale && hard.errorScale > 0.0f);
      assert (easy.turnAcceleration < hard.turnAcceleration);
   }
   assert (combatBehavior (-1).earlyFirePercent == combatBehavior (0).earlyFirePercent);
   assert (combatBehavior (9).earlyFirePercent == combatBehavior (4).earlyFirePercent);

   // Different teammates/wording must not repeat the same tactical fact.
   TacticalCommsGate team;
   const int contact = tacticalPhraseIndex ("Contact. Enemy spotted.");
   const int reload = tacticalPhraseIndex ("Reloading. Cover me.");
   assert (contact >= 0 && reload >= 0);
   assert (team.available (10.0f, contact));
   team.record (10.0f, contact);
   assert (!team.available (12.0f, reload));
   assert (team.available (13.0f, reload));
   assert (!team.available (21.0f, contact));
   assert (team.available (22.0f, contact));
   team.record (22.0f, contact);
   // New map restarts the clock; no stale cooldown may silence the team.
   assert (team.available (1.0f, contact));
   TacticalCommsGate opponents;
   assert (opponents.available (22.0f, contact));
   assert (tacticalPhraseIndex ("arbitrary player text") == -1);
   for (const auto &event : tacticalPhrases) {
      assert (std::strcmp (event.variants[0], event.variants[1]) != 0);
      assert (std::strcmp (event.variants[1], event.variants[2]) != 0);
   }
}
