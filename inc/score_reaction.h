// SPDX-License-Identifier: MIT
#pragma once

enum class ScoreFeeling { Neutral, Struggling, Confident };

inline ScoreFeeling scoreFeeling (int kills, int deaths) {
   if (deaths >= 3 && deaths >= kills + 2) return ScoreFeeling::Struggling;
   if (kills >= 5 && kills >= deaths + 3) return ScoreFeeling::Confident;
   return ScoreFeeling::Neutral;
}
