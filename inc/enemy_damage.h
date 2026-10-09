// SPDX-License-Identifier: MIT
#pragma once

// Estimate from this bot's confirmed, visible hits only. No enemy-health reads.
struct EnemyDamageEstimate {
   int damage = 0;
   int reportedTier = 0;
   float lastHit = -100.0f;
   float due = 0.0f;

   void hit (int amount, float now) {
      if (amount <= 0) return;
      if (now < lastHit || now - lastHit > 10.0f) *this = {};
      damage = damage + amount > 100 ? 100 : damage + amount;
      lastHit = now;
      due = now + 0.8f; // let the burst/kill resolve before announcing health
   }
   int tier () const { return damage >= 80 ? 2 : damage >= 60 ? 1 : 0; }
   bool ready (float now) const {
      return tier () > reportedTier && now >= due && now >= lastHit && now - lastHit <= 8.0f;
   }
   const char *label () const { return tier () == 2 ? "one shot" : "low hp"; }
};
