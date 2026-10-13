#include <cassert>
#include <initializer_list>
#include "lagging_bot.h"

int main () {
   for (const char *style : { "quiet", "sweet", "dry" }) {
      assert (pingTemperamentForStyle (PingTemperament::Tilted, 1, style) == PingTemperament::Quiet);
   }
   assert (pingTemperamentForStyle (PingTemperament::Quiet, 2, "rude") == PingTemperament::Quiet);
   assert (pingTemperamentForStyle (PingTemperament::Quiet, 1, "rude") == PingTemperament::Tilted);
   assert (pingTemperamentForStyle (PingTemperament::Tilted, 0, "talkative") == PingTemperament::Grumbler);
   assert (pingTemperamentForStyle (PingTemperament::Quiet, 0, "normal") == PingTemperament::Quiet);
   for (int roll = 0; roll < 100; ++roll) {
      assert (pingTemperament (2, roll) == PingTemperament::Quiet);
      assert (!shouldComplainAboutPing (PingTemperament::Quiet, 250, true, 100, 0, roll));
      assert (!shouldComplainAboutPing (PingTemperament::Tilted, 59, true, 100, 0, roll));
      assert (!shouldComplainAboutPing (PingTemperament::Tilted, 250, true, 100, 101, roll));
      assert (!shouldComplainAboutPing (PingTemperament::Tilted, 80, false, 100, 0, roll));
   }
   assert (pingTemperament (1, 80) == PingTemperament::Quiet);
   assert (pingTemperament (0, 40) == PingTemperament::Quiet);
   assert (shouldComplainAboutPing (PingTemperament::Tilted, 200, true, 100, 100, 64));
   assert (!shouldComplainAboutPing (PingTemperament::Tilted, 200, true, 100, 100, 65));
   assert (shouldComplainAboutPing (PingTemperament::Grumbler, 80, true, 100, 0, 4));
   assert (!shouldComplainAboutPing (PingTemperament::Grumbler, 80, true, 100, 0, 5));
   for (int base = 60; base <= 90; ++base) {
      for (int jitter = -8; jitter <= 8; ++jitter) {
         const int ping = laggingDisplayPing (base, jitter);
         assert (ping >= 45 && ping < 100);
      }
   }
   for (int base = 120; base <= 220; ++base) {
      for (int jitter = -15; jitter <= 15; ++jitter) {
         assert (laggingDisplayPing (base, jitter) >= 100);
      }
   }
   assert (laggingDisplayPing (200, 200) == 300);
}
