// SPDX-License-Identifier: MIT
#pragma once

struct IdleAimDrift {
   static constexpr float bounded (float value, float low, float high) {
      return value < low ? low : value > high ? high : value;
   }
   float yaw = 0.0f, pitch = 0.0f;
   float targetYaw = 0.0f, targetPitch = 0.0f;
   float nextChange = 0.0f;

   void target (float y, float p) {
      targetYaw = bounded (y, -3.0f, 3.0f);
      targetPitch = bounded (p, -6.0f, 6.0f);
   }
   void advance (float delta) {
      // Slow wandering, independent of the frame rate; no random per-frame jitter.
      const float step = bounded (delta, 0.0f, 0.1f) * 2.5f;
      yaw += bounded (targetYaw - yaw, -step, step);
      pitch += bounded (targetPitch - pitch, -step, step);
   }
};
