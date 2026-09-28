// SPDX-License-Identifier: MIT
#pragma once

// Noob, Easy, Normal, Hard, Expert. Percent per eligible opportunity.
constexpr int bhopStartChance (int difficulty) {
   return difficulty <= 0 ? 3 : difficulty == 1 ? 8 : difficulty == 2 ? 16 : difficulty == 3 ? 26 : 38;
}

constexpr int bhopContinueChance (int difficulty) {
   return difficulty <= 0 ? 20 : difficulty == 1 ? 30 : difficulty == 2 ? 40 : difficulty == 3 ? 55 : 70;
}

constexpr int bhopBurstLength (int difficulty) {
   return difficulty >= 3 ? 3 : 2;
}
