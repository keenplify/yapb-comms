// SPDX-License-Identifier: MIT
#include "simulated_away.h"
#include <cassert>
int main() {
   assert(simulatedAwayLease(100,280,42,42,1,true,true,false));
   assert(simulatedAwayLease(100,279,42,42,2,true,true,false));
   assert(!simulatedAwayLease(100,281,42,42,1,true,true,false));
   assert(!simulatedAwayLease(280,280,42,42,1,true,true,false));
   assert(!simulatedAwayLease(100,200,42,43,1,true,true,false));
   assert(!simulatedAwayLease(100,200,42,42,0,true,true,false));
   assert(!simulatedAwayLease(100,200,42,42,1,false,true,false));
   assert(!simulatedAwayLease(100,200,42,42,1,true,false,false));
   assert(!simulatedAwayLease(100,200,42,42,1,true,true,true));
   assert(!simulatedAwayLease(100,NAN,42,42,1,true,true,false));
   assert(!simulatedAwayLease(100,INFINITY,42,42,1,true,true,false));
}
