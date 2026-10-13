#include <cassert>
#include "score_reaction.h"

int main () {
   assert (scoreFeeling (0, 0) == ScoreFeeling::Neutral);
   assert (scoreFeeling (2, 0) == ScoreFeeling::Neutral);
   assert (scoreFeeling (0, 2) == ScoreFeeling::Neutral);
   assert (scoreFeeling (1, 3) == ScoreFeeling::Struggling);
   assert (scoreFeeling (5, 2) == ScoreFeeling::Confident);
   assert (scoreFeeling (40, 120) == ScoreFeeling::Struggling);
   assert (scoreFeeling (40, 40) == ScoreFeeling::Neutral);
   assert (scoreFeeling (40, 20) == ScoreFeeling::Confident);
}
