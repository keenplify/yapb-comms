#include <virtual_player.h>
#include <cassert>
#include <vector>
#include <algorithm>

struct Box { VirtualVec low, high; };
struct World {
   std::vector<Box> boxes { { { -4096, -4096, -512 }, { 4096, 4096, 0 } } };
   bool unsupported {};
   VirtualTrace operator () (VirtualVec a, VirtualVec b, bool crouch) {
      VirtualTrace result { b, {}, 1, false, unsupported };
      const float height = crouch ? 18.0f : 36.0f;
      for (const auto &box : boxes) {
         const float low[] = { box.low.x - 16, box.low.y - 16, box.low.z - height };
         const float high[] = { box.high.x + 16, box.high.y + 16, box.high.z + height };
         const float start[] = { a.x, a.y, a.z }, delta[] = { b.x - a.x, b.y - a.y, b.z - a.z };
         bool interior = true;
         for (int i = 0; i < 3; ++i) interior &= start[i] > low[i] + 0.0001f && start[i] < high[i] - 0.0001f;
         if (interior) { result.solid = true; result.end = a; result.fraction = 0; return result; }
         float enter = -1e9f, exit = 1e9f;
         VirtualVec normal {};
         bool miss = false;
         for (int i = 0; i < 3; ++i) {
            if (std::fabs (delta[i]) < 0.000001f) {
               if (start[i] <= low[i] || start[i] >= high[i]) miss = true;
               continue;
            }
            float t1 = (low[i] - start[i]) / delta[i], t2 = (high[i] - start[i]) / delta[i];
            const float sign = delta[i] > 0 ? -1.0f : 1.0f;
            if (t1 > t2) std::swap (t1, t2);
            if (t1 > enter) { enter = t1; normal = {}; if (i == 0) normal.x = sign; if (i == 1) normal.y = sign; if (i == 2) normal.z = sign; }
            exit = std::min (exit, t2);
         }
         if (miss || enter > exit || exit <= 0 || enter < -0.0001f || enter > result.fraction) continue;
         result.fraction = std::max (0.0f, enter);
         result.end = a + (b - a) * result.fraction;
         result.normal = normal;
      }
      if (result.fraction < 1 && result.normal.z >= 0.7f) {
         result.centerSupported = false;
         for (const auto &box : boxes) {
            if (result.end.x >= box.low.x && result.end.x <= box.high.x
               && result.end.y >= box.low.y && result.end.y <= box.high.y
               && result.end.z - height - box.high.z >= -0.1f
               && result.end.z - height - box.high.z <= virtualSupportDepth (result.normal, 18)) result.centerSupported = true;
         }
      }
      return result;
   }
};

VirtualResult run (World world, int mode = 0, float distance = 160, bool targetCrouch = false, float targetHeight = 36, float dt = 0.01f) {
   VirtualPlayer player {};
   player.source = player.origin = { 0, 0, 36 };
   player.target = { distance, 0, targetHeight };
   player.targetCrouch = targetCrouch;
   player.grounded = true;
   player.mode = mode;
   player.constrainRoute = true;
   VirtualPhysics physics {};
   for (int i = 0; i < 500; ++i) {
      auto result = player.step (physics, dt, world);
      if (result != VirtualResult::Running) return result;
   }
   return VirtualResult::Running;
}

int main () {
   assert (run ({}) == VirtualResult::Arrived);
   assert (run ({}, 0, 160, false, 36, 0.008f) == VirtualResult::Arrived);
   World wall;
   wall.boxes.push_back ({ { 70, -100, 0 }, { 71, 100, 512 } });
   for (int mode = 0; mode < 4; ++mode) assert (run (wall, mode) == VirtualResult::Rejected);
   World ceiling;
   ceiling.boxes.push_back ({ { 40, -100, 48 }, { 120, 100, 128 } });
   assert (run (ceiling) == VirtualResult::Rejected);
   assert (run (ceiling, 1) == VirtualResult::Arrived);
   VirtualVec openCrouch { 0, 0, 18 };
   bool openDuck = true;
   virtualPreferStanding (openCrouch, openDuck, World {});
   assert (!openDuck && openCrouch.z == 36);
   VirtualVec tunnelCrouch { 80, 0, 18 };
   bool tunnelDuck = true;
   virtualPreferStanding (tunnelCrouch, tunnelDuck, ceiling);
   assert (tunnelDuck && tunnelCrouch.z == 18);
   VirtualVec crouchFloor {};
   bool needsCrouch = false;
   assert (virtualFloorCandidate (VirtualVec { 80, 0, 36 }, crouchFloor, needsCrouch, ceiling));
   assert (needsCrouch && crouchFloor.z >= 18 && crouchFloor.z <= 20);
   assert (!virtualFloorCandidate (VirtualVec { 70, 0, 36 }, crouchFloor, needsCrouch, wall));
   World smallStep;
   smallStep.boxes.push_back ({ { 60, -100, 0 }, { 300, 100, 16 } });
   assert (run (smallStep, 0, 160, false, 52) == VirtualResult::Arrived);
   World tallStep;
   tallStep.boxes.push_back ({ { 60, -100, 0 }, { 300, 100, 32 } });
   assert (run (tallStep, 0, 160, false, 68) == VirtualResult::Rejected);
   assert (run (tallStep, 2, 160, false, 68) == VirtualResult::Arrived);
   World duckJump;
   duckJump.boxes.push_back ({ { 90, -100, 0 }, { 400, 100, 56 } });
   assert (run (duckJump, 2, 128, false, 92) == VirtualResult::Rejected);
   assert (run (duckJump, 3, 128, false, 92) == VirtualResult::Rejected);
   assert (run (duckJump, 4, 128, false, 92) == VirtualResult::Arrived);
   World gap;
   gap.boxes = { { { -4096, -4096, -512 }, { 40, 4096, 0 } }, { { 100, -4096, -512 }, { 4096, 4096, 0 } } };
   assert (run (gap) == VirtualResult::Rejected);
   assert (run (gap, 2) == VirtualResult::Arrived);
   World unsupported; unsupported.unsupported = true;
   assert (run (unsupported) == VirtualResult::Rejected);
   World embedded;
   embedded.boxes.push_back ({ { -8, -8, 0 }, { 8, 8, 128 } });
   assert (run (embedded) == VirtualResult::Rejected);
   World noLanding;
   noLanding.boxes = { { { -4096, -4096, -512 }, { 40, 4096, 0 } } };
   assert (run (noLanding, 2) == VirtualResult::Rejected);
   // The hull's corner overlaps this platform, but its feet center is outside.
   assert (!virtualFloorCandidate (VirtualVec { 50, 0, 36 }, crouchFloor, needsCrouch, noLanding));
   assert (virtualFloorCandidate (VirtualVec { 30, 0, 36 }, crouchFloor, needsCrouch, noLanding));
   World stairs;
   for (int i = 0; i < 6; ++i) stairs.boxes.push_back ({ { 40.0f + i * 24.0f, -100, 0 }, { 400, 100, 16.0f * (i + 1) } });
   assert (run (stairs, 0, 232, false, 132) == VirtualResult::Arrived);
   // The hull rests on the next tread before the feet center passes its edge.
   VirtualVec stairCandidate {};
   bool stairCrouch = false;
   assert (virtualFloorCandidate (VirtualVec { 54, 0, 36 }, stairCandidate, stairCrouch, stairs));
   assert (!stairCrouch && stairCandidate.z >= 68);
   const auto stairFrontier = virtualWalkFrontier (VirtualVec { 0, 0, 36 }, VirtualVec { 232, 0, 36 }, false, 18, stairs);
   assert (stairFrontier.x == 232 && std::fabs (stairFrontier.z - 132) < 0.1f);
   assert (virtualWalkCorridor (VirtualVec { 0, 0, 36 }, VirtualVec { 232, 0, 132 }, false, 18, stairs));
   assert (virtualWalkCorridor (VirtualVec { 232, 0, 132 }, VirtualVec { 0, 0, 36 }, false, 18, stairs));
   auto blockedStairs = stairs;
   blockedStairs.boxes.push_back ({ { 174, -100, 96 }, { 180, 100, 300 } });
   assert (!virtualWalkCorridor (VirtualVec { 0, 0, 36 }, VirtualVec { 232, 0, 132 }, false, 18, blockedStairs));
   const auto blockedFrontier = virtualWalkFrontier (VirtualVec { 0, 0, 36 }, VirtualVec { 232, 0, 36 }, false, 18, blockedStairs);
   assert (blockedFrontier.x > 100 && blockedFrontier.x < 174 - 16);
   World corner;
   corner.boxes.push_back ({ { 116, -100, 0 }, { 128, 300, 512 } });
   corner.boxes.push_back ({ { -100, 32, 0 }, { 64, 300, 512 } });
   const auto turn = virtualWalkFrontier (VirtualVec { 0, 0, 36 }, VirtualVec { 192, 0, 36 }, false, 18, corner);
   assert (turn.x >= 80 && turn.x <= 100);
   assert (virtualWalkCorridor (turn, VirtualVec { turn.x, 160, 36 }, false, 18, corner));
   assert (!virtualWalkCorridor (VirtualVec { 0, 0, 36 }, VirtualVec { turn.x, 160, 36 }, false, 18, corner));
   assert (!virtualWalkCorridor (VirtualVec { 0, 0, 36 }, VirtualVec { 160, 0, 36 }, false, 18, wall));
   assert (!virtualWalkCorridor (VirtualVec { 0, 0, 36 }, VirtualVec { 160, 0, 36 }, false, 18, gap));
   assert (virtualWalkCorridor (VirtualVec { 0, 0, 18 }, VirtualVec { 160, 0, 18 }, true, 18, ceiling));
   assert (!virtualWalkCorridor (VirtualVec { 0, 0, 36 }, VirtualVec { 160, 0, 68 }, false, 18, tallStep));
   assert (virtualRouteCorridor ({ 0, 0, 0 }, { 100, 100, 0 }, { 50, 50, 80 }));
   assert (!virtualRouteCorridor ({ 0, 0, 0 }, { 100, 100, 0 }, { 50, 60, 0 }));
   auto ramp = [] (VirtualVec a, VirtualVec b, bool duck) {
      const float hull = (duck ? 18.0f : 36.0f) + 8; // 1:2 incline, 16-unit hull half-width
      const float da = a.z - 0.5f * a.x - hull, db = b.z - 0.5f * b.x - hull;
      VirtualTrace result { b };
      if (da < -0.001f) { result.solid = true; result.end = a; result.fraction = 0; }
      else if (db < da && db <= 0) {
         result.fraction = da / (da - db);
         result.end = a + (b - a) * result.fraction;
         result.normal = { -0.4472136f, 0, 0.8944272f };
      }
      return result;
   };
   assert (virtualWalkCorridor (VirtualVec { 0, 0, 44 }, VirtualVec { 96, 0, 92 }, false, 18, ramp));
   assert (virtualWalkCorridor (VirtualVec { 96, 0, 92 }, VirtualVec { 0, 0, 44 }, false, 18, ramp));
   // A ladder is a contact volume beside a wall, with a supported upper exit.
   World ladderWorld;
   ladderWorld.boxes.push_back ({ { -512, -100, 0 }, { -14, 100, 200 } });
   auto ladderRun = [&] (bool up, bool broken, bool obstructed, bool landing, float dt = 0.01f) {
      auto world = ladderWorld;
      if (!landing) world.boxes.pop_back ();
      if (obstructed) world.boxes.push_back ({ { -20, -100, 100 }, { 60, 100, 112 } });
      VirtualLadderPlayer probe {};
      probe.up = up;
      probe.low = 36;
      probe.high = 200;
      probe.player.source = probe.player.origin = up ? VirtualVec { 63, 0, 36 } : VirtualVec { -40, 0, 236 };
      probe.player.grounded = true;
      probe.mount = up ? VirtualVec { -1, 0, 36 } : VirtualVec { 15, 0, 224 };
      probe.destination = up ? VirtualVec { -40, 0, 236 } : VirtualVec { 63, 0, 36 };
      auto contact = [broken] (VirtualVec p) {
         return p.x >= -24 && p.x <= 16 && p.y >= -32 && p.y <= 32 && p.z >= -36 && p.z <= 236
            && !(broken && p.z > 90 && p.z < 130);
      };
      VirtualPhysics physics {};
      for (int i = 0; i < 2000; ++i) {
         const auto result = probe.step (physics, dt, world, contact);
         if (result != VirtualResult::Running) return result;
      }
      return VirtualResult::Running;
   };
   assert (ladderRun (true, false, false, true) == VirtualResult::Arrived);
   assert (ladderRun (false, false, false, true) == VirtualResult::Arrived);
   assert (ladderRun (true, false, false, true, 0.008f) == VirtualResult::Arrived);
   assert (ladderRun (false, false, false, true, 0.008f) == VirtualResult::Arrived);
   assert (ladderRun (true, true, false, true) == VirtualResult::Rejected);
   assert (ladderRun (false, true, false, true) == VirtualResult::Rejected);
   assert (ladderRun (true, false, true, true) == VirtualResult::Rejected);
   assert (ladderRun (true, false, false, false) == VirtualResult::Rejected);
}
