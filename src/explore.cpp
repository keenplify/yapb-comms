// SPDX-License-Identifier: MIT
#include <yapb.h>
#include <graph_clearance.h>
#include <navigation_aim.h>
#if defined(CR_LINUX)
#include <sys/resource.h>
#endif

extern ConVar cv_graph_analyze_swarm;
static_assert (kExploreNodeLimit == kMaxNodes, "Exploration must use the graph format capacity.");
ConVar cv_graph_analyze_swarm_seconds ("graph_analyze_swarm_seconds", "0", "Optional exploration time limit in seconds. Zero explores until the frontier is exhausted; there is no trial-count cutoff.", true, 0.0f, 86400.0f);
ConVar cv_graph_analyze_swarm_spacing ("graph_analyze_swarm_spacing", "96", "Preferred node spacing on clear, continuously supported ground. Walls, gaps, stance changes and different floor heights preserve separate nodes.", true, 48.0f, 256.0f);
ConVar cv_graph_analyze_swarm_bots ("graph_analyze_swarm_bots", "0", "Explorer count: 0 chooses from CPU, available RAM and client slots; 1-16 requests a manual count, still bounded by memory and frame pressure.", true, 0.0f, 16.0f);
ConVar cv_graph_analyze_virtual ("graph_analyze_virtual", "0", "Fast virtual hull prediction instead of client bots. Predicted routes are marked separately; movement plugins are not simulated.", true, 0.0f, 1.0f);
ConVar cv_graph_analyze_virtual_budget ("graph_analyze_virtual_budget", "4", "Virtual simulation work budget per server frame in milliseconds; engine traces stay on the server thread.", true, 0.25f, 20.0f);
ConVar cv_graph_analyze_breakables ("graph_analyze_breakables", "1", "Temporarily exclude breakable glass during exploration, restoring it afterward. Other materials remain solid.", true, 0.0f, 1.0f);

namespace {
   double virtualBudgetSeconds () {
#if defined(CR_WINDOWS)
      LARGE_INTEGER counter {}, frequency {};
      QueryPerformanceCounter (&counter);
      QueryPerformanceFrequency (&frequency);
      return static_cast <double> (counter.QuadPart) / static_cast <double> (frequency.QuadPart);
#else
      timeval time {};
      gettimeofday (&time, nullptr);
      return time.tv_sec + time.tv_usec * 0.000001;
#endif
   }
   constexpr const char *kPhysicsCvars[] = {
      "sv_gravity", "sv_maxspeed", "sv_accelerate", "sv_airaccelerate", "sv_friction", "sv_stepsize", "sv_stopspeed"
   };

   unsigned long long availableMemoryMiB () {
#if defined(CR_WINDOWS)
      MEMORYSTATUSEX status {};
      status.dwLength = sizeof (status);
      if (!GlobalMemoryStatusEx (&status)) return 0;
      // Physical RAM is not the only limit in a 32-bit engine process.
      const auto available = status.ullAvailPhys < status.ullAvailVirtual ? status.ullAvailPhys : status.ullAvailVirtual;
      return available / (1024ULL * 1024ULL);
#elif defined(CR_LINUX)
      const auto pages = sysconf (_SC_AVPHYS_PAGES);
      const auto size = sysconf (_SC_PAGESIZE);
      if (pages <= 0 || size <= 0) return 0;
      return static_cast <unsigned long long> (pages) * static_cast <unsigned long long> (size) / (1024ULL * 1024ULL);
#else
      return 0;
#endif
   }

   double processCpuSeconds () {
#if defined(CR_WINDOWS)
      FILETIME created {}, exited {}, kernel {}, user {};
      if (!GetProcessTimes (GetCurrentProcess (), &created, &exited, &kernel, &user)) return 0.0;
      const auto kernelTicks = (static_cast <unsigned long long> (kernel.dwHighDateTime) << 32) | kernel.dwLowDateTime;
      const auto userTicks = (static_cast <unsigned long long> (user.dwHighDateTime) << 32) | user.dwLowDateTime;
      return static_cast <double> (kernelTicks + userTicks) * 0.0000001;
#elif defined(CR_LINUX)
      rusage usage {};
      if (getrusage (RUSAGE_SELF, &usage) != 0) return 0.0;
      return usage.ru_utime.tv_sec + usage.ru_stime.tv_sec + (usage.ru_utime.tv_usec + usage.ru_stime.tv_usec) * 0.000001;
#else
      return 0.0;
#endif
   }
}

void GraphExplore::start (int seedIndex) {
   cv_quota.set (0); // keep the dedicated analysis server empty after completion
   m_active = true;
   m_failed = false;
   m_incremental = seedIndex != -2;
   m_pruning = seedIndex == -3;
   m_pruneNode = m_pruneLink = m_pruned = m_pruneSkipped = 0;
   m_virtual = m_incremental || cv_graph_analyze_virtual.as <int> () != 0;
   m_source = m_trials = m_accepted = 0;
   for (int i = 0; i < kExploreNodeLimit; ++i) {
      const bool open = !m_incremental || seedIndex == -1 || seedIndex == i || i >= graph.length ();
      m_probes[i] = open ? 0 : 32;
      m_opened[i] = open;
   }
   if (graph.exists (seedIndex)) m_source = seedIndex;
   m_started = game.time ();
   m_nextUpdate = m_nextCreate = m_nextProgress = 0.0f;
   for (auto &job : m_jobs) job = {};
   for (auto &worker : m_workers) worker = {};
   const auto availableMiB = availableMemoryMiB ();
   m_workerLimit = m_virtual ? 0 : exploreWorkerCount (plat.hardwareConcurrency (), availableMiB, game.maxClients (), cv_graph_analyze_swarm_bots.as <int> ());
   m_load = {};
   m_load.limit = m_workerLimit;
   m_lastCpuTime = processCpuSeconds ();
   m_lastCpuSample = m_started;
   m_nextLoadSample = m_started + 1.0f;
   m_frameSum = 0.0f;
   m_frameCount = 0;
   logger.message ("[YaPB explore] capacity cpus=%d available_mib=%llu slots=%d selected=%d requested=%d",
      plat.hardwareConcurrency (), availableMiB, game.maxClients (), m_workerLimit, cv_graph_analyze_swarm_bots.as <int> ());
   if (!m_virtual && m_workerLimit == 0) {
      ctrl.msg ("Swarm analysis needs at least three client slots, including space for spectators.");
      m_failed = true;
   }
   for (int i = 0; i < 7; ++i) {
      m_physics[i] = engfuncs.pfnCVarGetFloat (kPhysicsCvars[i]);
      logger.message ("[YaPB explore] physics %s=%g", kPhysicsCvars[i], m_physics[i]);
   }
   m_virtualJob = -1;
   m_breakables.clear ();
   if (cv_graph_analyze_breakables && !m_pruning) {
      for (auto entity : game.getBreakables ()) {
         if (!game.isBreakableEntity (entity) || entity->v.classname.str () != "func_breakable" || !isGlass (entity)) continue;
         const auto &v = entity->v;
         m_breakables.emplace (BreakableState { entity, entity->serialnumber, v.solid, v.effects, v.takedamage,
            { v.absmin.x, v.absmin.y, v.absmin.z, v.absmax.x, v.absmax.y, v.absmax.z } });
         entity->v.solid = SOLID_NOT;
         entity->v.effects |= EF_NODRAW;
         entity->v.takedamage = DAMAGE_NO;
         engfuncs.pfnSetOrigin (entity, entity->v.origin);
      }
      logger.message ("[YaPB explore] temporarily excluded %d shootable breakables; state will be restored.", m_breakables.length ());
   }
   m_virtualVolumes.clear ();
   m_ladders.clear ();
   m_ladderEntity = m_ladderCandidate = 0;
   m_ladderUp = m_ladderDown = m_ladderTrial = false;
   if (m_virtual) {
      for (const char *classname : { "trigger_push", "trigger_teleport", "trigger_gravity", "trigger_multiple", "trigger_once", "func_conveyor" }) {
         edict_t *first = nullptr;
         game.searchEntities ("classname", classname, [&] (edict_t *ent) {
            if (ent == first) return EntitySearchResult::Break;
            if (!first) first = ent;
            if (m_virtualVolumes.length () >= 1024) { m_failed = true; return EntitySearchResult::Break; }
            if (ent->v.absmax.x > ent->v.absmin.x && ent->v.absmax.y > ent->v.absmin.y && ent->v.absmax.z > ent->v.absmin.z) {
               m_virtualVolumes.emplace (GraphHazardBounds { ent->v.absmin.x, ent->v.absmin.y, ent->v.absmin.z,
                  ent->v.absmax.x, ent->v.absmax.y, ent->v.absmax.z });
            }
            return EntitySearchResult::Continue;
         });
      }
      logger.message ("[YaPB explore] virtual prediction excludes %d movement/touch volumes.", m_virtualVolumes.length ());
      edict_t *first = nullptr;
      if (!m_incremental) game.searchEntities ("classname", "func_ladder", [&] (edict_t *ent) {
         if (ent == first) return EntitySearchResult::Break;
         if (!first) first = ent;
         m_ladders.emplace (ent);
         return EntitySearchResult::Continue;
      });
      logger.message ("[YaPB explore] found %d ladders for separate contact/climb/exit trials.", m_ladders.length ());
   }

   // Seeds are actual player spawns, not every item/goal across the map.
   auto seed = [this] (StringRef classname) {
      edict_t *first = nullptr;
      game.searchEntities ("classname", classname, [&] (edict_t *ent) {
         if (ent == first) return EntitySearchResult::Break;
         if (!first) first = ent;
         if (graph.length () >= 32) return EntitySearchResult::Break;
         TraceResult tr {};
         const auto origin = game.getEntityOrigin (ent);
         game.testHull (origin, origin - Vector (0.0f, 0.0f, 128.0f), TraceIgnore::Monsters, head_hull, nullptr, &tr);
         if (tr.fStartSolid || tr.fAllSolid || tr.flFraction >= 1.0f) return EntitySearchResult::Continue;
         const auto pos = tr.vecEndPos + Vector (0.0f, 0.0f, 19.0f);
         game.testHull (pos, pos, TraceIgnore::Monsters, human_hull, nullptr, &tr);
         if (graphTraceIsClear (tr) && !graph.exists (reusableNode (pos, false))) {
            graph.add (NodeAddFlag::Normal, pos);
         }
         return EntitySearchResult::Continue;
      });
   };
   if (!m_incremental) {
      seed ("info_player_start");
      seed ("info_player_deathmatch");
   }
   if (!graph.length ()) {
      ctrl.msg ("Swarm analysis has no safe player spawn; no graph was saved.");
      m_failed = true;
   }
   ctrl.msg ("Graph exploration: %s, %d nodes maximum. Spectators only.", m_virtual ? "virtual player simulation (no client slots)" : "physical bots", kExploreNodeLimit);
}

void GraphExplore::stop () {
   if (!m_active) return;
   m_active = false;
   for (auto &worker : m_workers) {
      auto bot = bots.findBotByIndex (worker.slot);
      if (bot && bot->m_graphExplorer && !bot->m_isStale) bot->kick (true);
      worker = {};
   }
   for (auto &job : m_jobs) job = {};
   int restored = 0;
   for (const auto &state : m_breakables) {
      auto entity = state.entity;
      if (game.isNullEntity (entity) || entity->serialnumber != state.serial) continue;
      // Restore only the fields still owned by this analysis session.
      if (entity->v.solid == SOLID_NOT) entity->v.solid = state.solid;
      if (entity->v.effects == (state.effects | EF_NODRAW)) entity->v.effects = state.effects;
      if (entity->v.takedamage == DAMAGE_NO) entity->v.takedamage = state.takedamage;
      engfuncs.pfnSetOrigin (entity, entity->v.origin);
      ++restored;
   }
   logger.message ("[YaPB explore] restored %d temporarily excluded breakables.", restored);
   // Keep bounds until saving finishes; no graph edge may lose its obstruction
   // provenance while its corresponding entity is restored.

}

bool GraphExplore::touchesBreakable (const Vector &from, const Vector &to) const {
   for (const auto &state : m_breakables) if (graphSegmentTouchesHazard (state.bounds, from, to)) return true;
   return false;
}

void GraphExplore::recordMaterial (edict_t *entity, int value) {
   if (!entity) return;
   for (auto &material : m_materials) {
      if (material.entity == entity) { material.serial = entity->serialnumber; material.value = value; return; }
   }
   m_materials.emplace (Material { entity, entity->serialnumber, value });
}

bool GraphExplore::isGlass (edict_t *entity) const {
   for (const auto &material : m_materials) {
      if (material.entity == entity && material.serial == entity->serialnumber) return material.value == 0;
   }
   return false; // Unknown materials stay solid.
}

bool GraphExplore::canReuse (const Vector &from, const Vector &to, bool crouch, bool otherCrouch) const {
   const int hull = crouch ? head_hull : human_hull;
   if (exploreCanReuse (from, to, crouch, otherCrouch, cv_graph_analyze_swarm_spacing.as <float> (),
      [hull] (const Vector &a, const Vector &b) {
         TraceResult tr {};
         game.testHull (a, b, TraceIgnore::Monsters, hull, nullptr, &tr);
         return graphTraceIsClear (tr) && !analyzer.pathTouchesHazard (a, b);
      }, [hull] (const Vector &point) {
         TraceResult tr {};
         game.testHull (point, point - Vector (0, 0, 6), TraceIgnore::Monsters, hull, nullptr, &tr);
         return !tr.fStartSolid && !tr.fAllSolid && tr.flFraction < 1 && tr.vecPlaneNormal.z >= 0.7f;
      })) return true;
   if (crouch != otherCrouch || from.distanceSq2d (to) > cr::sqrf (cv_graph_analyze_swarm_spacing.as <float> ())) return false;
   auto trace = [this] (VirtualVec a, VirtualVec b, bool duck) { return virtualTrace (a, b, duck); };
   // Reusing a stair/ramp sample must preserve travel in both directions.
   const VirtualVec a { from.x, from.y, from.z }, b { to.x, to.y, to.z };
   return virtualWalkCorridor (a, b, crouch, m_physics[5], trace)
      && virtualWalkCorridor (b, a, crouch, m_physics[5], trace);
}

int GraphExplore::reusableNode (const Vector &pos, bool crouch, int exclude, bool landmark) const {
   int result = kInvalidNodeIndex;
   float best = cr::sqrf (landmark ? 32.0f : cv_graph_analyze_swarm_spacing.as <float> ()) + 0.01f;
   for (int i = 0; i < graph.length (); ++i) {
      if (i == exclude) continue;
      const auto &node = graph[i];
      if (node.flags & NodeFlag::Ladder) continue;
      const float distance = node.origin.distanceSq (pos);
      if (distance < best && canReuse (pos, node.origin, crouch, (node.flags & NodeFlag::Crouch) != 0)) {
         best = distance;
         result = i;
      }
   }
   return result;
}

bool GraphExplore::candidate (Job &job) {
   const int count = graph.length ();
   if (count == 0 || count > kExploreNodeLimit) return false;
   // Rotate across frontier nodes instead of filling the queue with all 32
   // experiments from one spawn. This gives workers distinct regions to test.
   for (int n = 0; n < count; ++n) {
      m_source %= count;
      if (m_probes[m_source] < 32) break;
      m_source = (m_source + 1) % count;
      if (n + 1 == count) return false;
   }
   const int source = m_source;
   if (graph[source].flags & NodeFlag::Ladder) { m_probes[source] = 32; return false; }
   const int probe = m_probes[source]++;
   const int direction = probe % 8;
   const bool walkingProbe = probe < 8;
   const float distance = exploreProbeDistance (walkingProbe ? probe : probe - 8, cv_graph_analyze_swarm_spacing.as <float> ());
   m_source = (m_source + 1) % count;
   bool freeLink = false;
   for (const auto &link : graph[source].links) freeLink |= link.index == kInvalidNodeIndex;
   if (!freeLink) { m_probes[source] = 32; return false; }
   static const float dx[] = { 1, -1, 0, 0, 0.70710678f, -0.70710678f, 0.70710678f, -0.70710678f };
   static const float dy[] = { 0, 0, 1, -1, 0.70710678f, 0.70710678f, -0.70710678f, -0.70710678f };
   const auto baseline = graph[source].origin + Vector (dx[direction] * distance, dy[direction] * distance, 0);
   VirtualVec candidatePos {};
   bool crouch = (graph[source].flags & NodeFlag::Crouch) != 0;
   bool landmark = false;
   if (walkingProbe) {
      const auto &start = graph[source].origin;
      candidatePos = virtualWalkFrontier (VirtualVec { start.x, start.y, start.z },
         VirtualVec { baseline.x, baseline.y, baseline.z }, crouch, m_physics[5],
         [this] (VirtualVec a, VirtualVec b, bool duck) { return virtualTrace (a, b, duck); });
      const float progress = (candidatePos - VirtualVec { start.x, start.y, start.z }).horizontal ();
      if (progress < 32) return false;
      virtualPreferStanding (candidatePos, crouch,
         [this] (VirtualVec a, VirtualVec b, bool duck) { return virtualTrace (a, b, duck); });
      // The end of a straight run can be the only place to turn into the next
      // flight/corridor. Preserve it instead of merging it back into its source.
      landmark = progress < distance - 1;
   }
   else if (!virtualFloorCandidate ({ baseline.x, baseline.y, baseline.z }, candidatePos, crouch,
      [this] (VirtualVec a, VirtualVec b, bool duck) { return virtualTrace (a, b, duck); })) return false;
   Vector pos (candidatePos.x, candidatePos.y, candidatePos.z);
   if (analyzer.pathTouchesHazard (pos, pos)) return false;
   // Cluster open-ground samples instead of creating overlapping grids.
   // Preserve close endpoints where a gap, obstacle or stance change matters.
   int nearest = reusableNode (pos, crouch, source, landmark);
   if (graph.exists (nearest)) {
      const auto &start = graph[source].origin, &end = graph[nearest].origin;
      // Do not snap a corner candidate to a node across a wall or on another
      // stair flight, even if both can separately reach the candidate.
      const bool walkCrouched = crouch || (graph[source].flags & NodeFlag::Crouch);
      const float startOffset = walkCrouched && !(graph[source].flags & NodeFlag::Crouch) ? 18.0f : 0;
      const float endOffset = walkCrouched && !crouch ? 18.0f : 0;
      if (walkingProbe && !virtualWalkCorridor (VirtualVec { start.x, start.y, start.z - startOffset },
         VirtualVec { end.x, end.y, end.z - endOffset }, walkCrouched, m_physics[5],
         [this] (VirtualVec a, VirtualVec b, bool duck) { return virtualTrace (a, b, duck); })) nearest = kInvalidNodeIndex;
   }
   if (graph.exists (nearest)) pos = graph[nearest].origin;
   else if (!landmark && canReuse (pos, graph[source].origin, crouch, (graph[source].flags & NodeFlag::Crouch) != 0)) return false;
   else if (graph.length () >= kExploreNodeLimit) return false;
   if (pos.distanceSq2d (graph[source].origin) < cr::sqrf (32.0f)) return false;
   for (const auto &link : graph[source].links) {
      if (graph.exists (link.index) && graph[link.index].origin.distanceSq (pos) < cr::sqrf (12.0f)) return false;
   }
   for (const auto &other : m_jobs) {
      if (other.used && other.source == source && other.target.distanceSq (pos) < cr::sqrf (12.0f)) return false;
   }
   job = {};
   job.used = true;
   job.source = source;
   job.target = pos;
   job.crouch = crouch;
   job.landmark = landmark;
   job.mode = crouch || (graph[source].flags & NodeFlag::Crouch) ? 1 : 0;
   return true;
}

bool GraphExplore::hasFrontier () const {
   for (int i = 0; i < graph.length (); ++i) if (m_probes[i] < 32) return true;
   return false;
}

bool GraphExplore::update () {
   if (!m_active || m_failed) return true;
   m_frameSum += globals->frametime;
   ++m_frameCount;
   if (game.time () >= m_nextLoadSample) {
      const auto cpu = processCpuSeconds ();
      const float elapsed = game.time () - m_lastCpuSample;
      const float duty = elapsed > 0.0f ? static_cast <float> ((cpu - m_lastCpuTime) / elapsed) : 1.0f;
      m_workerLimit = m_virtual ? 0 : exploreWorkerCount (plat.hardwareConcurrency (), availableMemoryMiB (), game.maxClients (), cv_graph_analyze_swarm_bots.as <int> ());
      m_load.observe (m_frameSum / cr::max (1, m_frameCount), duty, m_workerLimit);
      m_lastCpuTime = cpu;
      m_lastCpuSample = game.time ();
      m_nextLoadSample = game.time () + 1.0f;
      m_frameSum = 0.0f;
      m_frameCount = 0;
   }
   // Observe humans before throttling: a bad frame must not delay cancellation.
   for (const auto &client : util.getClients ()) {
      auto bot = client.ent ? bots.findBotByEntity (client.ent) : nullptr;
      if ((client.flags & ClientFlags::Used) && client.ent && (!bot || !bot->m_graphExplorer)
         && (client.team == Team::CT || client.team == Team::Terrorist)) {
         ctrl.msg ("Swarm analysis stopped: another player joined a playing team. Partial graph was not saved.");
         m_failed = true;
         return true;
      }
   }
   if (!m_pruning && exploreTimeBudgetReached (game.time () - m_started, cv_graph_analyze_swarm_seconds.as <float> ())) {
      ctrl.msg ("Swarm analysis reached its budget: %d accepted links from %d trials.", m_accepted, m_trials);
      m_failed = m_accepted == 0 && !m_incremental;
      return true;
   }
   if (!m_virtual && game.time () < m_nextUpdate) return false;
   m_nextUpdate = game.time () + 0.05f;
   for (int i = 0; i < 7; ++i) {
      if (engfuncs.pfnCVarGetFloat (kPhysicsCvars[i]) != m_physics[i]) {
         ctrl.msg ("Swarm analysis stopped: movement settings changed. Partial graph was not saved.");
         m_failed = true;
         return true;
      }
   }
   if (m_pruning) return updatePrune ();
   if (game.time () >= m_nextProgress) {
      int active = 0;
      for (const auto &worker : m_workers) if (worker.job >= 0) ++active;
      int frontier = 0;
      for (int i = 0; i < graph.length (); ++i) if (m_probes[i] < 32) ++frontier;
      if (m_virtual) ctrl.msg ("Virtual exploration: %d nodes, %d predicted links, %d trials, %d frontier nodes, zero client bots.", graph.length (), m_accepted, m_trials, frontier);
      else ctrl.msg ("Graph exploration: %d nodes, %d verified links, %d trials, %d active / %d allowed explorers.",
         graph.length (), m_accepted, m_trials, active, m_load.limit);
      m_nextProgress = game.time () + 15.0f;
   }
   if (m_virtual) return updateVirtual ();
   // Throttle candidate creation under load; existing physical trials keep
   // receiving movement each server frame so they do not accumulate fake time.
   if (!exploreFrameAllowsWork (globals->frametime, m_load.busy)) return false;
   for (auto &worker : m_workers) {
      auto bot = bots.findBotByIndex (worker.slot);
      if (bot && !bot->m_isStale) continue;
      if (worker.job >= 0) { m_jobs[worker.job].owner = -1; worker.job = -1; }
      if (&worker - m_workers >= m_load.limit) continue;
      if (game.time () < m_nextCreate) break;
      // Slot ownership is discovered after creation, never by display name.
      if (bots.getBotCount () < m_load.limit) {
         const auto result = bots.create (strings.format ("Graph explorer %d", static_cast <int> (&worker - m_workers) + 1),
            Difficulty::Normal, Personality::Normal, -1, -1, true);
         m_nextCreate = game.time () + 0.25f;
         if (result == BotCreateResult::Success) {
            bots.forEach ([&] (Bot *created) {
               if (!created->m_graphExplorer || created->m_isStale) return false;
               for (const auto &owned : m_workers) if (owned.slot == created->index ()) return false;
               worker = {};
               worker.slot = created->index ();
               return true;
            });
         }
      }
      break;
   }
   if (hasFrontier ()) {
      int probes = 0;
      for (auto &job : m_jobs) {
         if (!job.used) {
            candidate (job);
            if (++probes == 4) break;
         }
      }
   }
   bool pending = false;
   for (const auto &job : m_jobs) pending |= job.used;
   if (!pending && !hasFrontier ()) {
      ctrl.msg ("Swarm exploration completed: %d verified links from %d trials.", m_accepted, m_trials);
      m_failed = m_accepted == 0 && !m_incremental;
      return true;
   }
   return false;
}

void GraphExplore::accept (const Job &job) {
   const auto sourceOrigin = graph[job.source].origin;
   const float sourceDuckOffset = job.mode == 1 && !(graph[job.source].flags & NodeFlag::Crouch) ? 18.0f : 0;
   const float targetDuckOffset = job.mode == 1 && !job.crouch ? 18.0f : 0;
   if (job.mode < 2 && !virtualWalkCorridor (
      VirtualVec { sourceOrigin.x, sourceOrigin.y, sourceOrigin.z - sourceDuckOffset },
      VirtualVec { job.target.x, job.target.y, job.target.z - targetDuckOffset }, job.mode == 1, m_physics[5],
      [this] (VirtualVec a, VirtualVec b, bool duck) { return virtualTrace (a, b, duck); })) return;
   bool free = false;
   for (const auto &link : graph[job.source].links) free |= link.index == kInvalidNodeIndex;
   if (!free) return;
   int target = graph.getNearestNoBuckets (job.target, 1.0f);
   if (!graph.exists (target)) {
      if (graph.length () >= kExploreNodeLimit) return;
      // Another physical worker may have added a nearby node while this trial
      // ran. Do not silently retarget its evidence or insert a duplicate.
      const int nearby = reusableNode (job.target, job.crouch, job.source, job.landmark);
      if (graph.exists (nearby)) {
         const auto &other = graph[nearby].origin;
         if (virtualWalkCorridor (VirtualVec { sourceOrigin.x, sourceOrigin.y, sourceOrigin.z - sourceDuckOffset },
            VirtualVec { other.x, other.y, other.z - targetDuckOffset }, job.mode == 1, m_physics[5],
            [this] (VirtualVec a, VirtualVec b, bool duck) { return virtualTrace (a, b, duck); })) return;
      }
      const int before = graph.length ();
      graph.add (NodeAddFlag::Normal, job.target);
      if (graph.length () != before + 1) return;
      target = before;
      if (job.crouch) graph[target].flags |= NodeFlag::Crouch;
   }
   if (target == job.source) return;
   // Never evict another verified route when the eight link slots are full.
   for (const auto &link : graph[job.source].links) {
      if (link.index == target) return;
   }
   graph.addPath (job.source, target, graph[job.source].origin.distance (job.target));
   for (auto &link : graph[job.source].links) {
      if (link.index != target) continue;
      link.flags = m_virtual ? PathFlag::InputSimulated : PathFlag::InputVerified;
      if (job.mode >= 2) link.flags |= PathFlag::Jump;
      if (job.mode % 2) link.flags |= PathFlag::InputCrouch;
      if (job.mode == 4) link.flags |= PathFlag::InputAirDuck;
      if (touchesBreakable (graph[job.source].origin, job.target)) link.flags |= PathFlag::InputBreakable;
      link.velocity.clear ();
      ++m_accepted;
      if (m_incremental && !m_opened[target] && !(graph[target].flags & NodeFlag::Ladder)) {
         m_opened[target] = true;
         m_probes[target] = 0;
      }

      logger.message ("[YaPB explore] accepted from=%d to=%d mode=%d attempts=%d successes=%d",
         job.source, target, job.mode, job.evidence.attempts, job.evidence.successes);
   }
}

VirtualTrace GraphExplore::virtualTrace (VirtualVec from, VirtualVec to, bool crouched, edict_t *ladder) const {
   const Vector start (from.x, from.y, from.z), end (to.x, to.y, to.z);
   TraceResult hit {};
   game.testHull (start, end, TraceIgnore::Monsters, crouched ? head_hull : human_hull, ladder, &hit);
   bool unsupported = hit.pHit && hit.pHit != ladder && game.indexOfEntity (hit.pHit) != 0;
   if (unsupported && hit.pHit->v.classname.str () == "func_wall") {
      const auto &v = hit.pHit->v;
      // Stationary brush platforms are ordinary geometry. Moving brushes and
      // interactive entities still need their own movement model.
      unsupported = v.solid != SOLID_BSP || v.velocity.lengthSq () != 0 || v.avelocity.lengthSq () != 0
         || v.takedamage != DAMAGE_NO;
   }
   for (float height : { -16.0f, 0.0f, 16.0f }) {
      const int contents = engfuncs.pfnPointContents (hit.vecEndPos + Vector (0, 0, height));
      unsupported |= contents <= CONTENTS_WATER && !(ladder && contents == CONTENTS_LADDER);
   }
   unsupported |= analyzer.pathTouchesHazard (start, hit.vecEndPos);
   for (const auto &volume : m_virtualVolumes) unsupported |= graphSegmentTouchesHazard (volume, start, hit.vecEndPos);
   bool centerSupported = true;
   if (hit.flFraction < 1 && hit.vecPlaneNormal.z >= 0.7f && !hit.fStartSolid && !hit.fAllSolid) {
      // A hull corner touching a ledge is not a supported waypoint. Account
      // for the uphill corner of a ramp when checking beneath the feet center.
      const float depth = virtualSupportDepth (
         { hit.vecPlaneNormal.x, hit.vecPlaneNormal.y, hit.vecPlaneNormal.z }, m_physics[5]);
      const auto feet = hit.vecEndPos - Vector (0, 0, crouched ? 18.0f : 36.0f);
      TraceResult support {};
      game.testLine (feet + Vector (0, 0, 1), feet - Vector (0, 0, depth), TraceIgnore::Monsters, ladder, &support);
      centerSupported = !support.fStartSolid && !support.fAllSolid && support.flFraction < 1 && support.vecPlaneNormal.z >= 0.7f;
   }
   return VirtualTrace { { hit.vecEndPos.x, hit.vecEndPos.y, hit.vecEndPos.z },
      { hit.vecPlaneNormal.x, hit.vecPlaneNormal.y, hit.vecPlaneNormal.z },
      hit.flFraction, hit.fStartSolid != 0 || hit.fAllSolid != 0, unsupported, centerSupported };
}

bool GraphExplore::ladderContact (VirtualVec pos, edict_t *ladder) const {
   TraceResult tr {};
   const Vector point (pos.x, pos.y, pos.z);
   engfuncs.pfnTraceModel (point, point, human_hull, ladder, &tr);
   return tr.fStartSolid || tr.fAllSolid || tr.flFraction < 1;
}

bool GraphExplore::prepareLadderTrial (edict_t *ladder, int candidate) {
   auto reject = [ladder, candidate] (const char *reason) {
      logger.message ("[YaPB explore] ladder setup entity=%d candidate=%d rejected=%s", game.indexOfEntity (ladder), candidate, reason);
      return false;
   };
   const bool up = candidate % 2 == 0;
   if ((up && m_ladderUp) || (!up && m_ladderDown)) return false;
   const auto &low = ladder->v.absmin, &high = ladder->v.absmax;
   const auto size = high - low;
   if (size.z < 48 || size.z > 8192) return false;
   const bool xFace = size.x < size.y;
   const int face = candidate / 48;
   Vector normal {};
   if (xFace) normal.x = face ? -1.0f : 1.0f;
   else normal.y = face ? -1.0f : 1.0f;
   auto column = (low + high) * 0.5f;
   // absmin/absmax already include the engine's padding. A 15-unit offset
   // merely touches the expanded brush and may never establish contact.
   if (xFace) column.x = (face ? low.x : high.x) + normal.x * 13;
   else column.y = (face ? low.y : high.y) + normal.y * 13;
   // Floor endpoints are checked independently. No assumption that both ends
   // are reachable, or that an upward climb proves the downward exit works.
   auto floorAt = [this, ladder] (Vector point, float depth, Vector &result) {
      const auto hit = virtualTrace ({ point.x, point.y, point.z }, { point.x, point.y, point.z - depth }, false, ladder);
      if (hit.solid || hit.unsupported || hit.fraction >= 1 || hit.normal.z < 0.7f) return false;
      result = Vector (hit.end.x, hit.end.y, hit.end.z + 1);
      const auto fit = virtualTrace ({ result.x, result.y, result.z }, { result.x, result.y, result.z }, false, ladder);
      return !fit.solid && !fit.unsupported && fit.fraction == 1;
   };
   Vector bottom {}, mount {};
   column.z = low.z + 40;
   if (!floorAt (column, 128, mount)) return reject ("mount-floor");
   auto bottomPoint = column + normal * 48;
   if (!floorAt (bottomPoint, 128, bottom) || cr::abs (bottom.z - mount.z) > m_physics[5]) return reject ("bottom-exit");
   const auto endColumn = Vector (column.x, column.y, high.z + 38);
   const auto sweep = virtualTrace ({ mount.x, mount.y, mount.z }, { endColumn.x, endColumn.y, endColumn.z }, false, ladder);
   if (sweep.solid || sweep.unsupported || sweep.fraction < 1) return reject ("climb-clearance");
   if (!ladderContact ({ mount.x, mount.y, mount.z + 4 }, ladder)
      || !ladderContact ({ mount.x, mount.y, high.z + 24 }, ladder)) return reject ("brush-contact");
   static const float dx[] = { 1, -1, 0, 0, 0.70710678f, -0.70710678f, 0.70710678f, -0.70710678f };
   static const float dy[] = { 0, 0, 1, -1, 0.70710678f, 0.70710678f, -0.70710678f, -0.70710678f };
   const int direction = (candidate / 2) % 8;
   const int distanceIndex = (candidate / 16) % 3;
   const float distance[] = { 24, 48, 64 };
   auto topPoint = Vector (column.x + dx[direction] * distance[distanceIndex], column.y + dy[direction] * distance[distanceIndex], high.z + 76);
   Vector top {};
   if (!floorAt (topPoint, 112, top) || top.z < high.z + 12 || top.z > high.z + 64) return reject ("top-exit");
   m_ladderBottom = bottom;
   m_ladderTop = top;
   // Aim into the ladder on approach so ordinary target-radius braking does
   // not stop the player just outside contact. Collision still bounds motion.
   m_ladderMount = up ? mount - normal * 16 : Vector (column.x, column.y, high.z + 24);
   m_ladderLow = mount.z;
   m_ladderHigh = high.z;
   m_ladderGoingUp = up;
   m_ladderEvidence = {};
   return true;
}

void GraphExplore::acceptLadderTrial () {
   // Never replace an existing node's movement evidence or evict its links.
   if (graph.length () + m_ladderPlayer.rungCount + 2 > kExploreNodeLimit) return;
   auto node = [this] (const Vector &pos, bool rung) {
      for (int existing = 0; existing < graph.length (); ++existing) {
         const auto &point = graph[existing];
         if (((point.flags & NodeFlag::Ladder) != 0) != rung || point.origin.distanceSq (pos) > cr::sqrf (24.0f)) continue;
         if (!rung && canReuse (pos, point.origin, false, (point.flags & NodeFlag::Crouch) != 0)) return existing;
         if (rung) {
            auto ladder = m_ladders[m_ladderEntity];
            const auto hit = virtualTrace ({ pos.x, pos.y, pos.z }, { point.origin.x, point.origin.y, point.origin.z }, false, ladder);
            if (hit.solid || hit.unsupported || hit.fraction < 1) continue;
            bool contact = true;
            for (int n = 0; n <= 8; ++n) {
               const auto sample = pos + (point.origin - pos) * (n / 8.0f);
               contact &= ladderContact ({ sample.x, sample.y, sample.z }, ladder);
            }
            if (contact) return existing;
         }
      }
      const int index = graph.length ();
      graph.add (NodeAddFlag::NoHostage, pos);
      if (graph.length () != index + 1) return kInvalidNodeIndex;
      if (rung) graph[index].flags |= NodeFlag::Ladder;
      return index;
   };
   auto link = [this] (int from, int to) {
      if (!graph.exists (from) || !graph.exists (to) || from == to) return;
      bool free = false;
      for (const auto &edge : graph[from].links) {
         if (edge.index == to) return;
         free |= edge.index == kInvalidNodeIndex;
      }
      if (!free) return;
      graph.addPath (from, to, graph[from].origin.distance (graph[to].origin));
      for (auto &edge : graph[from].links) if (edge.index == to) {
         edge.flags = PathFlag::InputSimulated | PathFlag::InputLadder;
         if (touchesBreakable (graph[from].origin, graph[to].origin)) edge.flags |= PathFlag::InputBreakable;
         edge.velocity.clear ();
         ++m_accepted;
      }
   };
   int previous = node (m_ladderGoingUp ? m_ladderBottom : m_ladderTop, false);
   Vector last {};
   for (int i = 0; i < m_ladderPlayer.rungCount; ++i) {
      const auto &sample = m_ladderPlayer.rungs[i];
      const Vector pos (sample.x, sample.y, sample.z);
      if (i > 0 && pos.distanceSq (last) < cr::sqrf (24.0f)) continue;
      const int next = node (pos, true);
      link (previous, next);
      previous = next;
      last = pos;
   }
   link (previous, node (m_ladderGoingUp ? m_ladderTop : m_ladderBottom, false));
   logger.message ("[YaPB explore] ladder accepted entity=%d direction=%s rungs=%d attempts=%d",
      game.indexOfEntity (m_ladders[m_ladderEntity]), m_ladderGoingUp ? "up" : "down", m_ladderPlayer.rungCount, m_ladderEvidence.attempts);
}

bool GraphExplore::updateLadderTrial (const VirtualPhysics &physics) {
   if (m_ladderEntity >= m_ladders.length ()) return false;
   auto ladder = m_ladders[m_ladderEntity];
   if (game.isNullEntity (ladder) || m_ladderCandidate >= 96 || (m_ladderUp && m_ladderDown)) {
      logger.message ("[YaPB explore] ladder finished entity=%d up=%d down=%d", game.isNullEntity (ladder) ? -1 : game.indexOfEntity (ladder), m_ladderUp, m_ladderDown);
      ++m_ladderEntity;
      m_ladderCandidate = 0;
      m_ladderTrial = m_ladderUp = m_ladderDown = false;
      return true;
   }
   if (!m_ladderTrial) {
      if (m_ladderEvidence.attempts == 0 && !prepareLadderTrial (ladder, m_ladderCandidate)) { ++m_ladderCandidate; return true; }
      m_ladderPlayer = {};
      const auto source = m_ladderGoingUp ? m_ladderBottom : m_ladderTop;
      const auto target = m_ladderGoingUp ? m_ladderTop : m_ladderBottom;
      m_ladderPlayer.player.origin = m_ladderPlayer.player.source = { source.x, source.y, source.z };
      m_ladderPlayer.player.grounded = true;
      m_ladderPlayer.mount = { m_ladderMount.x, m_ladderMount.y, m_ladderMount.z };
      m_ladderPlayer.destination = { target.x, target.y, target.z };
      m_ladderPlayer.up = m_ladderGoingUp;
      m_ladderPlayer.low = m_ladderLow;
      m_ladderPlayer.high = m_ladderHigh;
      m_ladderTrial = true;
   }
   auto trace = [this, ladder] (VirtualVec a, VirtualVec b, bool crouch) { return virtualTrace (a, b, crouch, ladder); };
   auto contact = [this, ladder] (VirtualVec point) { return ladderContact (point, ladder); };
   const auto result = m_ladderPlayer.step (physics, m_ladderEvidence.attempts == 0 ? 0.01f : 0.008f, trace, contact);
   if (result != VirtualResult::Running) {
      ++m_trials;
      const auto verdict = m_ladderEvidence.record (result == VirtualResult::Arrived);
      logger.message ("[YaPB explore] ladder trial entity=%d candidate=%d up=%d result=%d phase=%d pos=%.1f,%.1f,%.1f",
         game.indexOfEntity (ladder), m_ladderCandidate, m_ladderGoingUp, static_cast <int> (result), m_ladderPlayer.phase,
         m_ladderPlayer.player.origin.x, m_ladderPlayer.player.origin.y, m_ladderPlayer.player.origin.z);
      m_ladderTrial = false;
      if (verdict == ExploreVerdict::Accepted || verdict == ExploreVerdict::Rejected) {
         if (verdict == ExploreVerdict::Accepted) {
            acceptLadderTrial ();
            if (m_ladderGoingUp) m_ladderUp = true;
            else m_ladderDown = true;
         }
         m_ladderEvidence = {};
         ++m_ladderCandidate;
      }
   }
   return true;
}

bool GraphExplore::updatePrune () {
   const auto started = virtualBudgetSeconds ();
   while (m_pruneNode < graph.length ()) {
      if (virtualBudgetSeconds () - started >= cv_graph_analyze_virtual_budget.as <float> () * 0.001) return false;
      auto &source = graph[m_pruneNode];
      auto &link = source.links[m_pruneLink++];
      if (m_pruneLink == kMaxNodeLinks) { m_pruneLink = 0; ++m_pruneNode; }
      if (link.index == kInvalidNodeIndex) continue;
      bool blocked = !graph.exists (link.index) || link.index == source.number;
      if (!blocked) {
         const auto &target = graph[link.index];
         if ((link.flags & (PathFlag::Jump | PathFlag::InputLadder | PathFlag::InputBreakable))
            || ((source.flags | target.flags) & NodeFlag::Ladder)) { ++m_pruneSkipped; continue; }
         const bool duck = (link.flags & PathFlag::InputCrouch) || ((source.flags | target.flags) & NodeFlag::Crouch);
         auto from = source.origin, to = target.origin;
         if (duck && !(source.flags & NodeFlag::Crouch)) from.z -= 18;
         if (duck && !(target.flags & NodeFlag::Crouch)) to.z -= 18;
         bool uncertain = false;
         auto trace = [&] (VirtualVec a, VirtualVec b, bool crouch) {
            TraceResult hit {};
            const Vector begin (a.x, a.y, a.z), end (b.x, b.y, b.z);
            game.testHull (begin, end, TraceIgnore::Monsters, crouch ? head_hull : human_hull, nullptr, &hit);
            if (hit.pHit && game.indexOfEntity (hit.pHit) != 0) {
               const auto &v = hit.pHit->v;
               const auto classname = hit.pHit->v.classname.str ();
               // Opening doors and moving platforms are not permanent walls.
               uncertain |= (classname != "func_wall" && classname != "func_breakable")
                  || v.velocity.lengthSq () != 0 || v.avelocity.lengthSq () != 0;
            }
            uncertain |= engfuncs.pfnPointContents (hit.vecEndPos) <= CONTENTS_WATER;
            for (const auto &volume : m_virtualVolumes) uncertain |= graphSegmentTouchesHazard (volume, begin, hit.vecEndPos);
            return VirtualTrace { { hit.vecEndPos.x, hit.vecEndPos.y, hit.vecEndPos.z },
               { hit.vecPlaneNormal.x, hit.vecPlaneNormal.y, hit.vecPlaneNormal.z }, hit.flFraction,
               hit.fStartSolid != 0 || hit.fAllSolid != 0, uncertain };
         };
         blocked = !virtualWalkCorridor (VirtualVec { from.x, from.y, from.z }, VirtualVec { to.x, to.y, to.z }, duck, m_physics[5], trace);
         if (uncertain) { ++m_pruneSkipped; continue; }
      }
      if (blocked) {
         logger.message ("[YaPB prune] removed walking link %d -> %d", source.number, link.index);
         link = {};
         link.index = kInvalidNodeIndex;
         graph.markChanged ();
         ++m_pruned;
      }
   }
   ctrl.msg ("Prune complete: removed %d invalid walking links; retained %d special or unmodeled links for review. No nodes deleted.", m_pruned, m_pruneSkipped);
   return true;
}

bool GraphExplore::updateVirtual () {
   // Neither virtual time nor probe count consumes client slots. Keep all
   // engine collision callbacks on this thread, with a hard per-frame bound.
   const auto started = virtualBudgetSeconds ();
   VirtualPhysics physics { m_physics[0], cr::min (250.0f, m_physics[1]), m_physics[2],
      m_physics[3], m_physics[4], m_physics[5], m_physics[6] };
   auto trace = [this] (VirtualVec from, VirtualVec to, bool crouched) {
      return virtualTrace (from, to, crouched);
   };
   for (int ticks = 0; ticks < 65536; ++ticks) {
      if (virtualBudgetSeconds () - started >= cv_graph_analyze_virtual_budget.as <float> () * 0.001) break;
      if (ticks % 2 == 0 && updateLadderTrial (physics)) continue;
      if (m_virtualJob < 0) {
         for (int i = 0; i < kExploreJobs; ++i) if (m_jobs[i].used) { m_virtualJob = i; break; }
         if (m_virtualJob < 0 && hasFrontier ()) {
            // Candidate rejection also consumes budget, so sealed spawns
            // cannot make one frame scan an unbounded frontier.
            candidate (m_jobs[0]);
            if (m_jobs[0].used) m_virtualJob = 0;
            else continue;
         }
         if (m_virtualJob < 0) {
            if (m_ladderEntity < m_ladders.length ()) continue;
            ctrl.msg ("Virtual exploration completed: %d predicted links from %d trials. No client bots used.", m_accepted, m_trials);
            if (graph.length () >= kExploreNodeLimit) ctrl.msg ("Graph reached its %d-node format capacity; some areas may remain unexplored.", kExploreNodeLimit);
            m_failed = m_accepted == 0 && !m_incremental;
            return true;
         }
         const auto &job = m_jobs[m_virtualJob];
         const auto &source = graph[job.source];
         m_virtualPlayer = {};
         m_virtualPlayer.source = m_virtualPlayer.origin = { source.origin.x, source.origin.y, source.origin.z };
         m_virtualPlayer.target = { job.target.x, job.target.y, job.target.z };
         m_virtualPlayer.crouched = (source.flags & NodeFlag::Crouch) != 0;
         m_virtualPlayer.targetCrouch = job.crouch;
         m_virtualPlayer.grounded = true;
         m_virtualPlayer.mode = job.mode;
         m_virtualPlayer.constrainRoute = true;
      }
      auto &job = m_jobs[m_virtualJob];
      // Repeat at a different tick interval to reject numerically fragile
      // landings. These remain predictions, never InputVerified evidence.
      const float dt = job.evidence.attempts == 0 ? 0.01f : 0.008f;
      auto result = m_virtualPlayer.step (physics, dt, trace);
      if (!exploreInsideBounds (exploreBounds (graph[job.source].origin, job.target), m_virtualPlayer.origin)) result = VirtualResult::Rejected;
      if (result != VirtualResult::Running) {
         Worker completed {};
         completed.job = m_virtualJob;
         completed.jumped = m_virtualPlayer.jumped;
         completed.airborne = m_virtualPlayer.airborne;
         complete (completed, result == VirtualResult::Arrived);
         m_virtualJob = -1;
      }
   }
   return false;
}

void GraphExplore::complete (Worker &worker, bool success) {
   auto &job = m_jobs[worker.job];
   if (job.mode >= 2 && (!worker.jumped || !worker.airborne)) success = false;
   const auto verdict = job.evidence.record (success);
   ++m_trials;
   logger.message ("[YaPB explore] trial source=%d mode=%d success=%d attempts=%d successes=%d",
      job.source, job.mode, success ? 1 : 0, job.evidence.attempts, job.evidence.successes);
   if (verdict == ExploreVerdict::Accepted) {
      accept (job);
      job = {};
   }
   else if (verdict == ExploreVerdict::Rejected) {
      // Try walking, crouching, jumping, then crouch-jumping. Each mode gets
      // independent evidence; successes from different actions never combine.
      ++job.mode;
      if ((job.crouch || (graph[job.source].flags & NodeFlag::Crouch)) && job.mode == 2) ++job.mode;
      job.evidence = {};
      if (job.mode == 4 && (graph[job.source].flags & NodeFlag::Crouch)) ++job.mode;
      if (job.mode > 4) job = {};
   }
   if (job.used) job.owner = -1;
   worker.job = -1;
   worker.phase = Phase::Idle;
}

void GraphExplore::frame (Bot &bot) {
   if (!m_active || bot.m_isStale) return;
   Worker *selected = nullptr;
   for (auto &worker : m_workers) if (worker.slot == bot.index ()) selected = &worker;
   if (!selected) return;
   auto &worker = *selected;
   bot.m_isAlive = game.isAliveEntity (bot.ent ());
   bot.m_team = game.getPlayerTeam (bot.ent ());
   if (bot.m_notStarted) {
      if (worker.deadline <= game.time ()) {
         bot.updateTeamJoin ();
         worker.deadline = game.time () + 0.2f;
      }
      // The game DLL advances MOTD/team-menu state through player movement
      // ticks even while stationary. Returning without RPM strands fake clients
      // in UN on stock Steam CS; normal bots also tick movement while joining.
      bot.m_frameInterval = game.time () - bot.m_previousThinkTime;
      const auto msec = bot.computeMsec ();
      bot.m_previousThinkTime = game.time ();
      engfuncs.pfnRunPlayerMove (bot.ent (), bot.pev->v_angle, 0.0f, 0.0f, 0.0f, 0, 0, msec);
      return;
   }
   if (!bot.m_isAlive) {
      if (worker.job >= 0) complete (worker, false);
      if (game.time () >= worker.deadline) {
         // Dedicated analysis only: ask the game DLL to respawn normally.
         // Never fabricate health/flags or continue evidence across a death.
         MDLL_Spawn (bot.ent ());
         worker.deadline = game.time () + 2.0f;
      }
      return;
   }

   // Idle explorers are non-solid between trials, so they cannot block a
   // single-spawn map or accidentally serve as a jump boost. Active trials
   // restore normal player solidity before resetting to their source.
   if (worker.job < 0) bot.pev->solid = SOLID_NOT;

   const auto now = game.time ();
   const auto dt = cr::clamp (now - worker.lastTime, 0.0f, 0.1f);
   worker.lastTime = now;
   Vector angles = bot.pev->v_angle;
   float forward = 0.0f;
   int buttons = 0;
   if (worker.job < 0 && &worker - m_workers < m_load.limit
      && exploreFrameAllowsWork (globals->frametime, m_load.busy) && bot.pev->maxspeed >= 100.0f) {
      for (int i = 0; i < kExploreJobs; ++i) {
         auto &job = m_jobs[i];
         if (!job.used || job.owner >= 0) continue;
         // Other explorers must not stand on this trial's source or path.
         bool occupied = false;
         for (const auto &other : m_workers) {
            if (other.slot == worker.slot || other.job < 0) continue;
            const auto &otherJob = m_jobs[other.job];
            occupied |= exploreBoundsOverlap (exploreBounds (graph[otherJob.source].origin, otherJob.target),
               exploreBounds (graph[job.source].origin, job.target));
         }
         if (occupied) continue;
         worker.job = i;
         job.owner = worker.slot;
         worker.phase = Phase::Reset;
         worker.deadline = now + 2.0f;
         break;
      }
   }
   if (worker.job >= 0) {
      auto &job = m_jobs[worker.job];
      const auto source = graph[job.source].origin;
      if (worker.phase == Phase::Reset) {
         bot.pev->solid = SOLID_SLIDEBOX;
         TraceResult tr {};
         const bool sourceCrouch = (graph[job.source].flags & NodeFlag::Crouch) != 0;
         // Enter a low source with the actual crouching hull. For the reverse
         // transition, reset at crouching height and let PM_Duck stand up.
         if (sourceCrouch && !bot.isDucking ()) {
            buttons |= IN_DUCK;
            if (now >= worker.deadline) complete (worker, false);
         }
         else {
            game.testHull (source, source, TraceIgnore::None, sourceCrouch ? head_hull : human_hull, bot.ent (), &tr);
            if (!graphTraceIsClear (tr)) {
               complete (worker, false);
            }
            else {
               // Teleport/clear momentum ONLY between trials, never in flight.
               const auto resetPos = !sourceCrouch && bot.isDucking () ? source - Vector (0.0f, 0.0f, 18.0f) : source;
               engfuncs.pfnSetOrigin (bot.ent (), resetPos);
               bot.pev->velocity.clear ();
               bot.pev->basevelocity.clear ();
               angles = (job.target - source).angles ();
               angles.x = angles.z = 0.0f;
               worker.yawVelocity = 0.0f;
               worker.jumped = false;
               worker.airborne = false;
               worker.landing = {};
               worker.health = bot.pev->health;
               worker.spawnTime = bot.m_spawnTime;
               worker.phase = Phase::Settle;
               worker.deadline = now + 0.5f;
               if (sourceCrouch) buttons |= IN_DUCK;
            }
         }
      }
      else if (worker.phase == Phase::Settle) {
         if (graph[job.source].flags & NodeFlag::Crouch) buttons |= IN_DUCK;
         if (now >= worker.deadline) {
            if (!(bot.pev->flags & FL_ONGROUND) || bot.pev->origin.distanceSq2d (source) > cr::sqrf (4.0f)
               || bot.pev->health < worker.health || bot.pev->maxspeed < 100.0f) {
               complete (worker, false);
            }
            else {
               worker.phase = Phase::Move;
               worker.deadline = now + 3.0f;
            }
         }
      }
      else if (worker.phase == Phase::Move) {
         const bool grounded = (bot.pev->flags & FL_ONGROUND) != 0;
         const bool crouch = job.mode % 2 != 0 || (job.mode == 4 && worker.jumped && (!grounded || worker.airborne));
         if (worker.jumped && !grounded) worker.airborne = true;
         const float feet = bot.pev->origin.z + bot.pev->mins.z;
         const float targetFeet = job.target.z - (job.crouch ? 18.0f : 36.0f);
         const bool inside = bot.pev->origin.distanceSq2d (job.target) <= cr::sqrf (12.0f)
            && cr::abs (feet - targetFeet) < 8.0f;
         const bool safe = bot.pev->health >= worker.health && bot.pev->movetype == MOVETYPE_WALK
            && bot.m_spawnTime == worker.spawnTime
            && exploreInsideBounds (exploreBounds (source, job.target), bot.pev->origin)
            && !analyzer.pathTouchesHazard (bot.pev->origin, bot.pev->origin)
            && virtualRouteCorridor ({ source.x, source.y, source.z }, { job.target.x, job.target.y, job.target.z },
               { bot.pev->origin.x, bot.pev->origin.y, bot.pev->origin.z });
         const bool landed = worker.landing.update (now, grounded, inside, safe);
         if (!safe || now >= worker.deadline || bot.pev->origin.z < job.target.z - 192.0f) {
            complete (worker, false);
         }
         else if (landed) {
            complete (worker, true);
         }
         else {
            const auto heading = (job.target - bot.pev->origin).angles ().y;
            angles.y = cr::wrapAngle (angles.y + navigationTurnStep (heading - angles.y, worker.yawVelocity, dt));
            angles.x = angles.z = 0.0f;
            forward = inside ? 0.0f : bot.pev->maxspeed;
            if (crouch) buttons |= IN_DUCK;
            if (job.mode >= 2 && !worker.jumped && grounded
               && bot.pev->origin.distanceSq2d (source) >= cr::sqrf (kExploreJumpRunup)) {
               buttons |= IN_JUMP;
               worker.jumped = true;
            }
         }
      }
   }
   bot.m_frameInterval = now - bot.m_previousThinkTime;
   const auto msec = bot.computeMsec ();
   bot.m_previousThinkTime = now;
   // No normal sensing, weapons, bhop, jump-velocity assignment, or network AI.
   engfuncs.pfnRunPlayerMove (bot.ent (), angles, forward, 0.0f, 0.0f,
      static_cast <uint16_t> (buttons), 0, msec);
}
