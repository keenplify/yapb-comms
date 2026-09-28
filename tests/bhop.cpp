#include <bhop.h>
#include <cassert>

int main () {
   for (int difficulty = 1; difficulty <= 4; ++difficulty) {
      assert (bhopStartChance (difficulty) > bhopStartChance (difficulty - 1));
      assert (bhopContinueChance (difficulty) > bhopContinueChance (difficulty - 1));
   }
   assert (bhopBurstLength (0) == 3);
   assert (bhopBurstLength (2) == 4);
   assert (bhopBurstLength (4) == 5);
}
