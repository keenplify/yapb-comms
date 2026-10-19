#include <graph_clearance.h>
#include <cassert>
#include <limits>

struct Trace {
   int fStartSolid, fAllSolid;
   float flFraction;
};

int main () {
   assert (graphTraceIsClear (Trace { 0, 0, 1.0f }));

   // Exiting a thin wall can have a full fraction: it is still not a route.
   assert (!graphTraceIsClear (Trace { 1, 0, 1.0f }));
   assert (!graphTraceIsClear (Trace { 0, 1, 1.0f }));
   assert (!graphTraceIsClear (Trace { 1, 1, 1.0f }));

   // Even a collision just at the destination must block a generated link.
   assert (!graphTraceIsClear (Trace { 0, 0, 0.99999994f }));
   assert (!graphTraceIsClear (Trace { 0, 0, 0.5f }));
   assert (!graphTraceIsClear (Trace { 0, 0, 0.0f }));
   assert (!graphTraceIsClear (Trace { 0, 0, std::numeric_limits<float>::quiet_NaN () }));
}
