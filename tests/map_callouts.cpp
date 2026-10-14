#include <cassert>
#include <cstring>
#include <map_callouts.h>
int main () {
   assert (!std::strcmp (modernMapCallout ("de_dust2", "BombsiteA"), "A"));
   assert (!std::strcmp (modernMapCallout ("de_dust", "BombsiteB"), "B"));
   assert (!std::strcmp (modernMapCallout ("de_dust2", "DoubleDoors"), "doors"));
   assert (!std::strcmp (modernMapCallout ("de_dust2", "Side"), "long"));
   assert (!std::strcmp (modernMapCallout ("de_dust2", "SideDoor"), "long doors"));
   assert (!std::strcmp (modernMapCallout ("de_unknown", "Side"), "Side"));
   assert (sightingReportAllowed (false, false, false));
   assert (!sightingReportAllowed (true, false, false));
   assert (!sightingReportAllowed (false, true, false));
   assert (!sightingReportAllowed (false, false, true));
}
