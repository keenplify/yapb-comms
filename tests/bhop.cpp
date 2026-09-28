#include <bhop.h>
#include <cassert>

int main () {
   for (int difficulty = 1; difficulty <= 4; ++difficulty) {
      assert (bhopStartChance (difficulty) > bhopStartChance (difficulty - 1));
      assert (bhopContinueChance (difficulty) > bhopContinueChance (difficulty - 1));
   }
   assert (bhopBurstLength (0) == 2);
   assert (bhopBurstLength (4) == 3);
}
