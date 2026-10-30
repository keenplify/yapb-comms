// SPDX-License-Identifier: MIT
#pragma once
#include <cmath>

// AMXX owns the single server lease. Providers only consume it: no independent
// timers, no extension on respawn, and no permanent freeze if the owner dies.
inline bool simulatedAwayLease(float now, float until, float owner, int userid,
   float mode, bool enabled, bool live, bool completed) {
   const float remaining = until - now;
   return enabled && live && !completed && (mode == 1.0f || mode == 2.0f)
      && userid > 0 && owner == static_cast<float>(userid)
      && std::isfinite(now) && std::isfinite(until)
      && remaining > 0.0f && remaining <= 180.0f;
}
