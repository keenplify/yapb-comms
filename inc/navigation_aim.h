// SPDX-License-Identifier: MIT
#pragma once

inline float navigationTurnStep (float error, float &velocity, float delta, float speedLimit = 240.0f) {
   // A heading crossing +/-180 is a small turn, never a full revolution.
   while (error > 180.0f) error -= 360.0f;
   while (error < -180.0f) error += 360.0f;
   if (delta <= 0.0f) return 0.0f;
   if (delta > 0.1f) delta = 0.1f;
   if (error == 0.0f) { velocity = 0.0f; return 0.0f; }

   // Discard momentum from the old aim point when the next point reverses.
   if (error * velocity < 0.0f) velocity = 0.0f;
   float acceleration = 120.0f * error - 25.0f * velocity;
   if (acceleration > 1800.0f) acceleration = 1800.0f;
   if (acceleration < -1800.0f) acceleration = -1800.0f;
   velocity += delta * acceleration;
   if (error * velocity < 0.0f) velocity = 0.0f;
   if (velocity > speedLimit) velocity = speedLimit;
   if (velocity < -speedLimit) velocity = -speedLimit;
   const float step = delta * velocity;
   if ((error > 0.0f && step >= error) || (error < 0.0f && step <= error)) {
      velocity = 0.0f;
      return error; // stop on the aim point instead of swinging past it
   }
   return step;
}
