#include <graph_exploration.h>
#include <cassert>

int main () {
   ExploreEvidence evidence;
   assert (evidence.record (true) == ExploreVerdict::Retry);
   assert (evidence.record (false) == ExploreVerdict::Retry);
   assert (evidence.record (true) == ExploreVerdict::Accepted);
   evidence = {};
   assert (evidence.record (false) == ExploreVerdict::Retry);
   assert (evidence.record (true) == ExploreVerdict::Retry);
   assert (evidence.record (false) == ExploreVerdict::Rejected);
   evidence = {};
   assert (evidence.successes == 0 && evidence.attempts == 0);

   ExploreLanding landing;
   assert (!landing.update (1.0f, false, true, true)); // fly-through
   assert (!landing.update (1.1f, true, true, false)); // damaged landing
   assert (!landing.update (1.2f, true, true, true));
   assert (!landing.update (1.4f, true, false, true)); // slides off platform
   assert (!landing.update (1.5f, true, true, true));
   assert (landing.update (1.8f, true, true, true));
   assert (!landing.update (1.9f, false, true, true));
   assert (exploreFrameAllowsWork (0.01f));
   assert (!exploreFrameAllowsWork (0.05f));
   assert (!exploreFrameAllowsWork (0.0f));
   assert (exploreFrameAllowsWork (0.1f, false)); // deliberately slow empty server
   assert (!exploreFrameAllowsWork (0.1f, true)); // CPU-bound slow frame

   assert (exploreWorkerCount (8, 8192, 32) == 14);
   assert (exploreWorkerCount (16, 8192, 32) == 16);
   assert (exploreWorkerCount (1, 8192, 32) == 1);
   assert (exploreWorkerCount (4, 1024, 32) == 6);
   assert (exploreWorkerCount (16, 1024, 32) == 8); // available RAM limits CPU guess
   assert (exploreWorkerCount (16, 256, 32) == 1);
   assert (exploreWorkerCount (16, 8192, 8) == 6); // reserve two spectator slots
   assert (exploreWorkerCount (16, 8192, 2) == 0);
   assert (exploreWorkerCount (16, 0, 32) == 2); // failed RAM detection is conservative
   assert (exploreWorkerCount (0, 8192, 32) == 2);
   assert (exploreWorkerCount (4, 8192, 32, 12) == 12);
   assert (exploreWorkerCount (4, 1024, 32, 12) == 8); // override still respects memory
   assert (exploreWorkerCount (128, ~0ULL, 32, 999) == 16);

   ExploreLoad load;
   load.limit = 16;
   load.observe (0.04f, 0.9f, 16);
   assert (load.busy && load.limit == 8);
   load.observe (0.04f, 0.9f, 16);
   assert (load.limit == 4);
   load.observe (0.1f, 0.1f, 16);
   load.observe (0.1f, 0.1f, 16);
   assert (load.limit == 4);
   load.observe (0.1f, 0.1f, 16);
   assert (!load.busy && load.limit == 5); // slow idle pacing is not overload
   load.observe (0.01f, 0.2f, 2);
   assert (load.limit == 2); // falling memory/slot ceiling

   struct Point { float x, y, z; };
   assert (exploreProbeDistance (0, 96) == 192);
   assert (exploreProbeDistance (7, 96) == 192);
   assert (exploreProbeDistance (8, 96) == 128);
   assert (exploreProbeDistance (23, 96) == 64);
   assert (exploreProbeDistance (0, 192) > 192); // large spacing still reaches beyond itself
   assert (!exploreTimeBudgetReached (1000000, 0));
   assert (!exploreTimeBudgetReached (29, 30));
   assert (exploreTimeBudgetReached (30, 30));
   const Point first { 0, 0, 36 }, nearby { 64, 0, 36 }, distant { 128, 0, 36 };
   const auto clear = [] (Point, Point) { return true; };
   const auto floor = [] (Point) { return true; };
   assert (exploreCanReuse (first, nearby, false, false, 96, clear, floor));
   assert (!exploreCanReuse (first, distant, false, false, 96, clear, floor));
   assert (!exploreCanReuse (first, nearby, false, true, 96, clear, floor));
   assert (!exploreCanReuse (first, Point { 64, 0, 52 }, false, false, 96, clear, floor));
   assert (!exploreCanReuse (first, nearby, false, false, 96, [] (Point, Point) { return false; }, floor)); // thin wall
   assert (!exploreCanReuse (first, nearby, false, false, 96, clear,
      [] (Point p) { return p.x < 20 || p.x > 44; })); // supported endpoints, gap in between
   const auto route = exploreBounds (Point { 0, 0, 36 }, Point { 192, 0, 36 });
   assert (exploreBoundsOverlap (route, exploreBounds (Point { 96, -96, 36 }, Point { 96, 96, 36 })));
   assert (!exploreBoundsOverlap (route, exploreBounds (Point { 0, 96, 36 }, Point { 192, 96, 36 })));
   assert (exploreBoundsOverlap (route, exploreBounds (Point { 0, 0, 36 }, Point { -192, 0, 36 })));
   assert (!exploreBoundsOverlap (route, exploreBounds (Point { 0, 0, 256 }, Point { 192, 0, 256 })));
   assert (exploreInsideBounds (route, Point { 128, 10, 70 }));
   assert (!exploreInsideBounds (route, Point { 128, 50, 36 })); // stop wander into another trial
   static_assert (kExploreWorkers == 16 && kExploreJobs == 128);
}
