#include <combat_behavior.h>
#include <tactical_comms.h>
#include <enemy_damage.h>
#include <idle_aim.h>
#include <navigation_aim.h>
#include <cassert>
#include <initializer_list>

int main () {
   float turnVelocity = 0.0f;
   const float wrapLeft = navigationTurnStep (358.0f, turnVelocity, 0.02f);
   assert (wrapLeft < 0.0f && wrapLeft >= -2.0f);
   turnVelocity = 0.0f;
   const float wrapRight = navigationTurnStep (-358.0f, turnVelocity, 0.02f);
   assert (wrapRight > 0.0f && wrapRight <= 2.0f);
   turnVelocity = 240.0f;
   assert (navigationTurnStep (-90.0f, turnVelocity, 0.02f) < 0.0f);
   turnVelocity = 240.0f;
   assert (navigationTurnStep (0.2f, turnVelocity, 0.02f) == 0.2f);
   assert (turnVelocity == 0.0f);
   turnVelocity = 100.0f;
   assert (navigationTurnStep (0.1f, turnVelocity, 0.1f) >= 0.0f);
   turnVelocity = 0.0f;
   float heading = 0.0f, totalTurn = 0.0f;
   for (int i = 0; i < 300; ++i) {
      const float step = navigationTurnStep (179.0f - heading, turnVelocity, 0.02f);
      assert (step >= 0.0f && step <= 4.801f);
      heading += step;
      totalTurn += step;
   }
   assert (heading > 178.9f && heading <= 179.001f && totalTurn < 180.0f);
   turnVelocity = 240.0f;
   assert (navigationTurnStep (0.0f, turnVelocity, 0.02f) == 0.0f && turnVelocity == 0.0f);
   IdleAimDrift drift;
   drift.target (100.0f, -100.0f);
   drift.advance (0.02f);
   assert (drift.yaw > 0.0f && drift.yaw <= 0.051f);
   assert (drift.pitch < 0.0f && drift.pitch >= -0.051f);
   for (int i = 0; i < 300; ++i) drift.advance (0.02f);
   assert (drift.yaw == 3.0f && drift.pitch == -6.0f);
   drift = {};
   assert (drift.yaw == 0.0f && drift.pitch == 0.0f);
   drift.target (-2.0f, 4.0f);
   drift.advance (-1.0f);
   assert (drift.yaw == 0.0f && drift.pitch == 0.0f);
   drift.advance (10.0f);
   assert (drift.yaw >= -0.25f && drift.pitch <= 0.25f);
   EnemyDamageEstimate damage;
   damage.hit (30, 10.0f);
   assert (damage.tier () == 0 && !damage.ready (11.0f));
   damage.hit (32, 10.2f);
   assert (damage.tier () == 1 && !damage.ready (10.5f));
   assert (damage.ready (11.1f));
   assert (std::strcmp (damage.label (), "low hp") == 0);
   damage.reportedTier = damage.tier ();
   assert (!damage.ready (11.2f));
   damage.hit (20, 11.3f);
   assert (damage.tier () == 2 && damage.ready (12.2f));
   assert (std::strcmp (damage.label (), "one shot") == 0);
   assert (!damage.ready (20.0f)); // stale damage is not team intel
   damage.hit (10, 25.0f);
   assert (damage.damage == 10 && damage.tier () == 0);
   damage.hit (10, 1.0f); // map-clock restart
   assert (damage.damage == 10);
   damage.hit (0, 1.1f);
   assert (damage.damage == 10); // armor-only damage cannot imply low HP
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
