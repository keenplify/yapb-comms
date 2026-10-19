// SPDX-License-Identifier: MIT
#pragma once

// Shared by physical trials and normal navigation of their accepted links.
constexpr float kExploreJumpRunup = 12.0f;
constexpr int kExploreWorkers = 16; // storage ceiling, not the selected count
constexpr int kExploreJobs = 128;
constexpr int kExploreNodeLimit = 4096; // YaPB graph format capacity

inline float exploreProbeDistance (int probe, float spacing) {
   const float step = spacing * (2.0f / 3.0f) > 64.0f ? spacing * (2.0f / 3.0f) : 64.0f;
   // Spend the eight outgoing slots reaching outward before closing short
   // local loops. All three lengths remain available around obstacles/gaps.
   return step * (3 - probe / 8);
}

inline bool exploreTimeBudgetReached (float elapsed, float seconds) {
   return seconds > 0.0f && elapsed >= seconds; // zero means no time cutoff
}

// Reuse nearby nodes only on the same stance/surface, with continuous floor
// support. A clear sweep alone would incorrectly merge opposite sides of a gap.
template <typename Point, typename Clear, typename Supported>
bool exploreCanReuse (const Point &from, const Point &to, bool fromCrouch, bool toCrouch,
   float spacing, Clear clear, Supported supported) {
   if (fromCrouch != toCrouch) return false;
   const float dz = from.z - to.z;
   if (dz < -4 || dz > 4) return false;
   const float dx = to.x - from.x, dy = to.y - from.y;
   const float distanceSq = dx * dx + dy * dy;
   if (distanceSq > spacing * spacing || !clear (from, to)) return false;
   // At most 18 intervals for the configured spacing range (48..256).
   int steps = 1;
   while (steps * steps * 256.0f < distanceSq) ++steps;
   for (int n = 0; n <= steps; ++n) {
      const float fraction = static_cast <float> (n) / steps;
      const Point sample { from.x + dx * fraction, from.y + dy * fraction, from.z - dz * fraction };
      if (!supported (sample)) return false;
   }
   return true;
}

enum class ExploreVerdict { Running, Retry, Accepted, Rejected };

struct ExploreEvidence {
   int attempts {};
   int successes {};

   ExploreVerdict record (bool success) {
      ++attempts;
      if (success) ++successes;
      if (successes >= 2) return ExploreVerdict::Accepted;
      if (attempts >= 3) return ExploreVerdict::Rejected;
      return ExploreVerdict::Retry;
   }
};

// Success requires a stable grounded landing, not crossing the target in air.
struct ExploreLanding {
   float since { -1.0f };

   bool update (float now, bool grounded, bool inside, bool safe) {
      if (!grounded || !inside || !safe) { since = -1.0f; return false; }
      if (since < 0.0f) since = now;
      return now - since >= 0.25f;
   }
};

inline bool exploreFrameAllowsWork (float frameSeconds, bool cpuBusy = true) {
   // Empty GoldSrc servers can intentionally tick slowly. A long frame alone
   // is not evidence of CPU saturation and must not stall the entire frontier.
   return frameSeconds > 0.0f && (frameSeconds <= 0.025f || !cpuBusy);
}

inline int exploreWorkerCount (int logicalCpus, unsigned long long availableMiB, int clientSlots, int requested = 0) {
   const int slots = clientSlots > 2 ? clientSlots - 2 : 0; // reserve spectators
   if (slots == 0) return 0;
   int cpu = logicalCpus <= 0 ? 2 : logicalCpus == 1 ? 1 : logicalCpus >= 9 ? 16 : (logicalCpus - 1) * 2;
   // Conservative heuristic, not a measured per-bot memory requirement.
   const auto memoryBudget = availableMiB == 0 ? 2ULL : availableMiB <= 576 ? 1ULL : (availableMiB - 512) / 64;
   const int memory = memoryBudget > 16 ? 16 : static_cast <int> (memoryBudget);
   int result = requested > 0 ? requested : cpu;
   if (result > memory) result = memory;
   if (result > slots) result = slots;
   if (result > kExploreWorkers) result = kExploreWorkers;
   return result < 1 ? 1 : result;
}

struct ExploreLoad {
   int limit { 1 };
   int healthyWindows {};
   bool busy {};

   void observe (float meanFrame, float cpuSecondsPerSecond, int ceiling) {
      busy = cpuSecondsPerSecond >= 0.8f;
      if (limit > ceiling) limit = ceiling;
      if (busy && meanFrame > 0.025f) {
         limit = limit > 1 ? (limit + 1) / 2 : 1;
         healthyWindows = 0;
      }
      else if (!busy && ++healthyWindows >= 3) {
         if (limit < ceiling) ++limit;
         healthyWindows = 0;
      }
      else if (busy) healthyWindows = 0;
   }
};

struct ExploreBounds { float minX, minY, minZ, maxX, maxY, maxZ; };

template <typename Point>
ExploreBounds exploreBounds (const Point &from, const Point &to) {
   return {
      (from.x < to.x ? from.x : to.x) - 32.0f, (from.y < to.y ? from.y : to.y) - 32.0f,
      (from.z < to.z ? from.z : to.z) - 36.0f,
      (from.x > to.x ? from.x : to.x) + 32.0f, (from.y > to.y ? from.y : to.y) + 32.0f,
      (from.z > to.z ? from.z : to.z) + 80.0f
   };
}

inline bool exploreBoundsOverlap (const ExploreBounds &a, const ExploreBounds &b) {
   return a.minX <= b.maxX && b.minX <= a.maxX && a.minY <= b.maxY && b.minY <= a.maxY
      && a.minZ <= b.maxZ && b.minZ <= a.maxZ;
}

template <typename Point>
bool exploreInsideBounds (const ExploreBounds &bounds, const Point &pos) {
   return pos.x >= bounds.minX && pos.x <= bounds.maxX && pos.y >= bounds.minY && pos.y <= bounds.maxY
      && pos.z >= bounds.minZ && pos.z <= bounds.maxZ;
}
