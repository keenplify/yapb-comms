// SPDX-License-Identifier: MIT
#include "arena_spread.h"
#include <cassert>
#include <cmath>

int main() {
 assert(arenaSpreadAllowed(2,true,false,false));
 assert(!arenaSpreadAllowed(0,true,false,false));
 assert(!arenaSpreadAllowed(1,true,false,false));
 assert(!arenaSpreadAllowed(2,false,false,false));
 assert(!arenaSpreadAllowed(2,true,true,false));
 assert(!arenaSpreadAllowed(2,true,false,true));
 assert(arenaSpreadScore(500,700,0)>arenaSpreadScore(50,400,1));
 assert(arenaSpreadScore(500,600,0)>arenaSpreadScore(500,2000,0));
 assert(arenaSpreadScore(700,1000,0)==arenaSpreadScore(5000,1000,0));
 // Four equally useful lanes: subsequent bots avoid already assigned goals.
 const float lane[4]={-900,-300,300,900};
 bool chosen[4]={};
 for(int bot=0;bot<4;++bot) {
  int best=-1;float bestScore=-10000;
  for(int n=0;n<4;++n) {
   float gap=700;
   for(int peer=0;peer<4;++peer)if(chosen[peer]) {
    const float distance=std::fabs(lane[n]-lane[peer]);
    if(distance<gap)gap=distance;
   }
   float score=arenaSpreadScore(gap,1000,0);
   if(score>bestScore){best=n;bestScore=score;}
  }
  assert(best>=0 && !chosen[best]);
  chosen[best]=true;
 }
}
