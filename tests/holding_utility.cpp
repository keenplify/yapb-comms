#include <cassert>
#include <holding_utility.h>

int main () {
   assert (holdingUtilityAllowed (true, false, false, true, 650, 0, 0));
   assert (!holdingUtilityAllowed (true, false, false, false, 650, 0, 0)); // spawn
   assert (!holdingUtilityAllowed (false, false, false, true, 650, 0, 0)); // ended
   assert (!holdingUtilityAllowed (true, true, false, true, 650, 0, 0)); // objective
   assert (!holdingUtilityAllowed (true, false, true, true, 650, 0, 0)); // firefight
   assert (!holdingUtilityAllowed (true, false, false, true, 150, 0, 0)); // self damage
   assert (!holdingUtilityAllowed (true, false, false, true, 1200, 0, 0));
   assert (!holdingUtilityAllowed (true, false, false, true, 650, 400, 0));
   assert (!holdingUtilityAllowed (true, false, false, true, 650, 0, 3));
   assert (learnedHoldingSpotAvailable (2, 3, 1, 1, 20, false));
   assert (!learnedHoldingSpotAvailable (2, 2, 1, 1, 20, false));
   assert (!learnedHoldingSpotAvailable (2, 4, 1, 1, 20, false));
   assert (!learnedHoldingSpotAvailable (2, 3, 1, 2, 20, false)); // halftime
   assert (!learnedHoldingSpotAvailable (2, 3, 1, 1, 46, false));
   assert (!learnedHoldingSpotAvailable (2, 3, 1, 1, 2, false));
   assert (!learnedHoldingSpotAvailable (2, 3, 1, 1, 20, true)); // no repeat
}
