// SPDX-License-Identifier: MIT
#pragma once

inline bool arenaSpreadAllowed(float mode, bool enabled, bool objective, bool ordered) {
 return enabled && mode == 2.0f && !objective && !ordered;
}
inline float arenaSpreadClearance(float distance) {
 return distance < 0 ? 0 : distance > 700 ? 700 : distance;
}
inline float arenaSpreadScore(float clearance, float travel, float variation) {
 return arenaSpreadClearance(clearance) - travel * .08f + variation * 80.0f;
}
