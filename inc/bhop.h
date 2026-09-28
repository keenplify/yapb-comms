// SPDX-License-Identifier: MIT
#pragma once

// Noob, Easy, Normal, Hard, Expert. Percent per eligible opportunity.
constexpr int bhopStartChance (int difficulty) {
   return difficulty <= 0 ? 18 : difficulty == 1 ? 30 : difficulty == 2 ? 45 : difficulty == 3 ? 60 : 75;
}

constexpr int bhopContinueChance (int difficulty) {
   return difficulty <= 0 ? 65 : difficulty == 1 ? 75 : difficulty == 2 ? 85 : difficulty == 3 ? 92 : 98;
}

constexpr int bhopBurstLength (int difficulty) {
   return difficulty <= 1 ? 3 : difficulty == 2 ? 4 : 5;
}
