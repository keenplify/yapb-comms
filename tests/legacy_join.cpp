#include <legacy_join.h>
#include <cassert>
int main () {
   assert (legacyJoinComplete (true, true, true, false));
   assert (!legacyJoinComplete (false, true, true, false));
   assert (!legacyJoinComplete (true, false, true, false));
   assert (!legacyJoinComplete (true, true, false, false));
   assert (!legacyJoinComplete (true, true, true, true));
}
