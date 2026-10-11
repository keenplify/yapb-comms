// SPDX-License-Identifier: MIT
#pragma once

struct BotViewTiming {
   float thinkTime = 0.0f;
   float viewTime = 0.0f;

   float advance (float now, bool navigationAim, bool thinkUpdate,
      float maximumDelta, float minimumDelta) {
      const float elapsed = now - (navigationAim ? viewTime : thinkTime);
      // Navigation may advance between AI thinks. Combat retains the original
      // think cadence, including its first update when an enemy is acquired.
      if (thinkUpdate) thinkTime = now;
      viewTime = now;
      return elapsed < minimumDelta ? minimumDelta : elapsed > maximumDelta ? maximumDelta : elapsed;
   }
};
