// SPDX-License-Identifier: MIT
#pragma once

#include <graph_exploration.h>
#include <virtual_player.h>
#include <graph_hazard.h>

class GraphExplore : public Singleton <GraphExplore> {
   enum class Phase { Idle, Reset, Settle, Move };
   struct Job {
      bool used {};
      int source { -1 };
      Vector target {};
      bool crouch {};
      bool landmark {};
      int mode {};
      int owner { -1 };
      ExploreEvidence evidence {};
   };
   struct Worker {
      int slot { -1 };
      int job { -1 };
      Phase phase { Phase::Idle };
      float deadline {};
      float health {};
      float yawVelocity {};
      float lastTime {};
      bool jumped {};
      bool airborne {};
      float spawnTime {};
      ExploreLanding landing {};
   };
   struct BreakableState {
      edict_t *entity {};
      int serial {}, solid {}, effects {};
      float takedamage {};
      GraphHazardBounds bounds {};
   };
   Job m_jobs[kExploreJobs] {};
   Worker m_workers[kExploreWorkers] {};
   int m_source {};
   int m_probes[kExploreNodeLimit] {};
   int m_workerLimit {};
   ExploreLoad m_load {};
   float m_nextLoadSample {};
   float m_lastCpuSample {};
   double m_lastCpuTime {};
   float m_frameSum {};
   int m_frameCount {};
   int m_trials {};
   int m_accepted {};
   float m_started {};
   float m_nextUpdate {};
   float m_nextCreate {};
   float m_nextProgress {};
   bool m_active {};
   bool m_failed {};
   float m_physics[7] {};
   bool m_virtual {};
   bool m_incremental {};
   bool m_pruning {};
   int m_pruneNode {}, m_pruneLink {}, m_pruned {}, m_pruneSkipped {};
   bool m_opened[kExploreNodeLimit] {};
   int m_virtualJob { -1 };
   VirtualPlayer m_virtualPlayer {};
   SmallArray <GraphHazardBounds> m_virtualVolumes {};
   SmallArray <BreakableState> m_breakables {};
   struct Material { edict_t *entity; int serial, value; };
   SmallArray <Material> m_materials {};
   bool isGlass (edict_t *entity) const;
   bool touchesBreakable (const Vector &from, const Vector &to) const;
   SmallArray <edict_t *> m_ladders {};
   int m_ladderEntity {}, m_ladderCandidate {};
   bool m_ladderUp {}, m_ladderDown {}, m_ladderTrial {};
   Vector m_ladderBottom {}, m_ladderTop {}, m_ladderMount {};
   float m_ladderLow {}, m_ladderHigh {};
   bool m_ladderGoingUp {};
   ExploreEvidence m_ladderEvidence {};
   VirtualLadderPlayer m_ladderPlayer {};
   bool updateVirtual ();
   bool updatePrune ();
   VirtualTrace virtualTrace (VirtualVec from, VirtualVec to, bool crouched, edict_t *ladder = nullptr) const;
   bool ladderContact (VirtualVec pos, edict_t *ladder) const;
   bool prepareLadderTrial (edict_t *ladder, int candidate);
   bool updateLadderTrial (const VirtualPhysics &physics);
   void acceptLadderTrial ();
   bool canReuse (const Vector &from, const Vector &to, bool crouch, bool otherCrouch) const;
   int reusableNode (const Vector &pos, bool crouch, int exclude = -1, bool landmark = false) const;

   bool candidate (Job &job);
   bool hasFrontier () const;
   void complete (Worker &worker, bool success);
   void accept (const Job &job);

public:
   void recordMaterial (edict_t *entity, int value);
   void clearMaterials () { m_materials.clear (); }
   void start (int seed = -2);
   void stop ();
   bool update ();
   void frame (Bot &bot);
   bool active () const { return m_active; }
   bool failed () const { return m_failed; }

};

CR_EXPOSE_GLOBAL_SINGLETON (GraphExplore, explorer);
