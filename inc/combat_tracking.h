// SPDX-License-Identifier: MIT
#pragma once

inline float combatTrackingInterval (float base, float speed, int difficulty) {
   if (speed < 1.0f) return base;
   const float minimum = difficulty <= 0 ? .06f : difficulty == 1 ? .04f : .025f;
   const float interval = 8.0f / speed < minimum ? minimum : 8.0f / speed;
   return base < interval ? base : interval;
}
inline float combatPredictionTime (float age, float lead) {
   const float value = age + lead;
   return value < 0 ? 0 : value > .18f ? .18f : value;
}
inline float combatPredictionHeight (float vz, float gravity, float seconds, bool ballistic) {
   return vz * seconds - (ballistic ? 0.5f * gravity * seconds * seconds : 0.0f);
}

// Runtime aim controls are independent of the navigation laboratory.
float activeAimLead (int difficulty);
float activeAimStiffness ();
