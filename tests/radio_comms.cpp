#include <radio_comms.h>
#include <cassert>
#include <initializer_list>

int main () {
   for (const bool chatReady : { false, true }) {
      for (int personality = 0; personality < 3; ++personality) {
         for (int sequence = 0; sequence < 4; ++sequence) {
            int next = sequence;
            assert (tacticalRadioRoute (true, personality == 1, personality == 2,
               chatReady, next) == TacticalRadioRoute::Radio);
            assert (next == sequence);
         }
      }
   }
   int sequence = 0;
   assert (tacticalRadioRoute (false, true, false, false, sequence) == TacticalRadioRoute::Suppress);
   assert (tacticalRadioRoute (false, true, false, true, sequence) == TacticalRadioRoute::Chat);
   assert (tacticalRadioRoute (false, false, true, true, sequence) == TacticalRadioRoute::Radio);
   assert (tacticalRadioRoute (false, false, true, true, sequence) == TacticalRadioRoute::Chat);
   assert (tacticalRadioRoute (false, false, false, true, sequence) == TacticalRadioRoute::Radio);
}
