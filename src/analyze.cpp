//
// YaPB, based on PODBot by Markus Klinge ("CountFloyd").
// Copyright © YaPB Project Developers <yapb@jeefo.net>.
//
// SPDX-License-Identifier: MIT
//

#include <yapb.h>
#include <graph_clearance.h>

ConVar cv_graph_analyze_auto_start ("graph_analyze_auto_start", "1", "Autostart analyzer if all other cases fail.");
ConVar cv_graph_analyze_auto_save ("graph_analyze_auto_save", "1", "Auto save results of analysis to graph file and re-add bots.");
ConVar cv_graph_analyze_distance ("graph_analyze_distance", "64", "The minimum distance to keep nodes from each other.", true, 42.0f, 128.0f);
ConVar cv_graph_analyze_max_jump_height ("graph_analyze_max_jump_height", "44", "Max jump height to test if the next node will be unreachable.", true, 44.0f, 64.0f);
ConVar cv_graph_analyze_fps ("graph_analyze_fps", "30.0", "The FPS at which the analyzer process is running. This keeps the game from freezing during analysis.", true, 25.0f, 99.0f);
ConVar cv_graph_analyze_clean_paths_on_finish ("graph_analyze_clean_paths_on_finish", "1", "Specifies if the analyzer should clean unnecessary paths upon finishing.");
ConVar cv_graph_analyze_optimize_nodes_on_finish ("graph_analyze_optimize_nodes_on_finish", "1", "Specifies if the analyzer should merge some near-placed nodes with many connections together.");
ConVar cv_graph_analyze_mark_goals_on_finish ("graph_analyze_mark_goals_on_finish", "1", "Specifies if the analyzer should mark nodes as map goals automatically upon finishing.");
ConVar cv_graph_analyze_swarm ("graph_analyze_swarm", "0", "Experimental dedicated-server graph exploration with CPU/RAM-sized input-only bots. Enable before loading a map without a graph.", true, 0.0f, 1.0f);

void GraphAnalyze::resumeWorkers () {
   if (m_pausedWorkers != 0) worker.startup (m_pausedWorkers);
   m_pausedWorkers = 0;
}

bool GraphAnalyze::repopulate (int seed) {
   if (m_isAnalyzing || graph.length () < kMaxNodeLinks
      || (seed != -1 && seed != -3 && (!graph.exists (seed) || (graph[seed].flags & NodeFlag::Ladder)))) {
      ctrl.msg ("Repopulate needs an idle server, an existing graph, and a valid floor seed (or all).");
      return false;
   }
   if (bots.getBotCount () != 0) {
      ctrl.msg ("Remove bots first with yb kickall. Repopulate keeps the analysis server empty.");
      return false;
   }
   for (const auto &client : util.getClients ()) {
      if ((client.flags & ClientFlags::Used) && client.ent && (client.team == Team::CT || client.team == Team::Terrorist)) {
         ctrl.msg ("Switch every player to spectator first (jointeam 6), then repopulate using the landing node index.");
         return false;
      }
   }
   const auto filename = bstor.buildPath (BotFile::Graph);
   if (plat.fileExists (filename.chars ())) {
      File original (filename, "rb");
      if (!original) { ctrl.msg ("Cannot read the existing graph for backup; repopulate cancelled."); return false; }
      String backupName {};
      for (int attempt = 0; attempt < 10000; ++attempt) {
         backupName = strings.format ("%s.before-repopulate-%d-%d.bak", filename.chars (), static_cast <int> (plat.seconds ()), attempt);
         if (!plat.fileExists (backupName.chars ())) break;
         backupName.clear ();
      }
      if (backupName.empty ()) return false;
      File backup (backupName, "wb");
      if (!backup) { ctrl.msg ("Cannot create graph backup; repopulate cancelled."); return false; }
      unsigned char buffer[4096] {};
      auto remaining = original.length ();
      while (remaining > 0) {
         const auto chunk = cr::min (remaining, sizeof (buffer));
         if (original.read (buffer, 1, chunk) != chunk || backup.write (buffer, 1, chunk) != chunk) {
            ctrl.msg ("Graph backup failed; repopulate cancelled.");
            return false;
         }
         remaining -= chunk;
      }
      if (backup.flush ()) { ctrl.msg ("Graph backup flush failed; repopulate cancelled."); return false; }
      ctrl.msg ("Previous graph backed up to %s.", backupName.chars ());
   }
   // Drain readers before changing the graph; save manual edits as the restart
   // point. Do not optimize, move nodes, or rewrite their narrow-place flags.
   m_pausedWorkers = worker.available () ? static_cast <int> (engfuncs.pfnCVarGetFloat ("yb_threadpool_workers")) : 0;
   worker.shutdown ();
   if (!graph.saveGraphData (true)) {
      resumeWorkers ();
      ctrl.msg ("Cannot save manual edits; repopulate cancelled.");
      return false;
   }
   graph.cancelLearnJump ();
   graph.clearEditFlag (GraphEdit::Auto);
   m_repopulating = true;
   m_repopulateSeed = seed;
   start ();
   if (seed == -3) ctrl.msg ("Prune started. Checking walking links; nodes and jump routes are retained. Spectators only.");
   else ctrl.msg ("Repopulate started from %s. Existing nodes and jump links are retained. Spectators only.",
      seed == -1 ? "all nodes" : strings.format ("node %d", seed));
   return m_isAnalyzing;
}

void GraphAnalyze::start () {
   if (m_isAnalyzing) {
      return;
   }

   m_exploring = m_repopulating || cv_graph_analyze_swarm.as <int> () != 0;
   m_isCrouch = false;
   if (m_exploring && ((!game.isDedicated () && !m_repopulating) || bots.getBotCount () != 0)) {
      ctrl.msg ("Swarm analysis requires a dedicated analysis server without existing bots.");
      return;
   }

   // start analyzer in few seconds after level initialized
   if (m_repopulating || cv_graph_analyze_auto_start) {
      m_updateInterval = game.time () + 3.0f;
      m_basicsCreated = false;
      m_hazards.clear ();
      edict_t *firstHurt = nullptr;

      game.searchEntities ("classname", "trigger_hurt", [&] (edict_t *ent) {
         // Some engine builds wrap FindEntityByString back to the first match.
         // Stop after one pass so analysis cannot grow this list indefinitely.
         if (ent == firstHurt) {
            return EntitySearchResult::Break;
         }
         if (!firstHurt) {
            firstHurt = ent;
         }
         // Ignore non-damaging/empty helper entities; map brush triggers have
         // usable world bounds even though movement traces pass through them.
         if (ent->v.dmg > 0.0f && ent->v.solid == SOLID_TRIGGER
            && ent->v.absmax.x > ent->v.absmin.x
            && ent->v.absmax.y > ent->v.absmin.y
            && ent->v.absmax.z > ent->v.absmin.z) {
            m_hazards.emplace (GraphHazardBounds {
               ent->v.absmin.x, ent->v.absmin.y, ent->v.absmin.z,
               ent->v.absmax.x, ent->v.absmax.y, ent->v.absmax.z
            });
         }
         return EntitySearchResult::Continue;
      });
      ctrl.msg ("Map analysis will avoid %d damage volumes.", m_hazards.length ());

      // set as we're analyzing
      m_isAnalyzing = true;

      // silence all graph messages
      graph.setMessageSilence (true);

      // set all nodes as not expanded
      for (auto &expanded : m_expandedNodes) {
         expanded = false;
      }

      // set all nodes as not optimized
      for (auto &optimized : m_optimizedNodes) {
         optimized = false;
      }
      ctrl.msg ("Starting map analysis.");
   }
   else {
      m_updateInterval = 0.0f;
   }
}

void GraphAnalyze::update () {
   if (cr::fzero (m_updateInterval) || !m_isAnalyzing) {
      return;
   }

   if (m_exploring) {
      if (!m_repopulating && !cv_graph_analyze_swarm) {
         suspend ();
         ctrl.msg ("Swarm analysis cancelled; partial graph was not saved.");
         return;
      }
      if (!m_basicsCreated) {
         if (game.time () < m_updateInterval) return;
         m_basicsCreated = true;
         explorer.start (m_repopulating ? m_repopulateSeed : -2);
      }
      if (explorer.update ()) {
         const bool failed = explorer.failed ();
         explorer.stop ();
         if (failed) {
            suspend ();
         }
         else {
            finish ();
         }
      }
      return;
   }

   if (m_updateInterval >= game.time ()) {
      return;
   }
   else {
      displayOverlayMessage ();
   }

   // add basic nodes
   if (!m_basicsCreated) {
      graph.addBasic ();
      m_basicsCreated = true;
   }

   for (int i = 0; i < graph.length (); ++i) {
      if (m_updateInterval >= game.time ()) {
         return;
      }

      if (!graph.exists (i)) {
         return;
      }

      if (m_expandedNodes[i]) {
         continue;
      }

      m_expandedNodes[i] = true;
      setUpdateInterval ();

      auto pos = graph[i].origin;
      const auto range = cv_graph_analyze_distance.as <float> ();

      for (int dir = 1; dir < kMaxNodeLinks; ++dir) {
         switch (dir) {
         case 1:
            flood (pos, { pos.x + range, pos.y, pos.z }, range);
            break;

         case 2:
            flood (pos, { pos.x - range, pos.y, pos.z }, range);
            break;

         case 3:
            flood (pos, { pos.x, pos.y + range, pos.z }, range);
            break;

         case 4:
            flood (pos, { pos.x, pos.y - range, pos.z }, range);
            break;

         case 5:
            flood (pos, { pos.x + range, pos.y, pos.z + 128.0f }, range);
            break;

         case 6:
            flood (pos, { pos.x - range, pos.y, pos.z + 128.0f }, range);
            break;

         case 7:
            flood (pos, { pos.x, pos.y + range, pos.z + 128.0f }, range);
            break;

         case 8:
            flood (pos, { pos.x, pos.y - range, pos.z + 128.0f }, range);
            break;
         }
      }
   }

   // finish generation if no updates occurred recently
   if (m_updateInterval + 2.0f < game.time ()) {
      finish ();
      return;
   }
}

void GraphAnalyze::suspend (bool restoreWorkers) {
   explorer.stop ();
   m_updateInterval = kInfiniteDistance;
   m_isAnalyzing = false;
   m_isAnalyzed = false;
   m_basicsCreated = false;
   m_hazards.clear ();
   m_exploring = false;
   m_repopulating = false;
   m_repopulateSeed = -2;
   if (restoreWorkers) resumeWorkers ();
   else m_pausedWorkers = 0;
   graph.setMessageSilence (false);
}

bool GraphAnalyze::pathTouchesHazard (const Vector &from, const Vector &to) const {
   for (const auto &hazard : m_hazards) {
      if (graphSegmentTouchesHazard (hazard, from, to)) {
         return true;
      }
   }
   return false;
}

void GraphAnalyze::finish () {
   // run optimization on finish
   if (!m_exploring) optimize (); // do not move physically verified endpoints

   // mark goal nodes
   if (!m_repopulating) markGoals ();

   m_isAnalyzed = true;
   m_isAnalyzing = false;
   m_updateInterval = 0.0f;

   // un-silence all graph messages
   graph.setMessageSilence (false);

   ctrl.msg ("Completed map analysis.");

   // auto save bots graph
   if (m_repopulating || cv_graph_analyze_auto_save) {
      auto editor = graph.getEditor ();
      int editFlags = 0;
      for (int flag : { GraphEdit::On, GraphEdit::Noclip }) if (graph.hasEditFlag (flag)) editFlags |= flag;
      if (!graph.saveGraphData (m_repopulating)) {
         ctrl.msg ("Can't save analyzed graph. Internal error.");
         resumeWorkers ();
         return;
      }
      if (!graph.loadGraphData ()) {
         ctrl.msg ("Can't load analyzed graph. Internal error.");
         resumeWorkers ();
         return;
      }
      if (m_repopulating && !game.isNullEntity (editor)) {
         graph.setEditor (editor);
         graph.setEditFlag (editFlags);
      }
      vistab.startRebuild ();
      ctrl.enableDrawModels (false);

      if (!m_exploring) cv_quota.revert ();
   }
   if (m_repopulating) ctrl.msg (m_repopulateSeed == -3 ? "Pruned graph saved; resume play when ready." : "Repopulate complete. Expanded graph saved; resume play when ready.");
   m_repopulating = false;
   m_repopulateSeed = -2;
   resumeWorkers ();
}

void GraphAnalyze::optimize () {
   if (graph.length () == 0) {
      return;
   }

   if (!cv_graph_analyze_optimize_nodes_on_finish) {
      return;
   }
   cleanup ();

   auto smooth = [] (const Array <int> &nodes) {
      Vector result {};

      for (const auto &node : nodes) {
         result += graph[node].origin;
      }
      result /= kMaxNodeLinks;
      result.z = graph[nodes.first ()].origin.z;

      return result;
   };

   // set all nodes as not optimized
   for (auto &optimized : m_optimizedNodes) {
      optimized = false;
   }

   for (int i = 0; i < graph.length (); ++i) {
      if (m_optimizedNodes[i]) {
         continue;
      }
      const auto &path = graph[i];
      const bool crouch = (path.flags & NodeFlag::Crouch) != 0;
      Array <int> indexes {};

      for (const auto &link : path.links) {
         if (graph.exists (link.index) && !m_optimizedNodes[link.index]
            && ((graph[link.index].flags & NodeFlag::Crouch) != 0) == crouch
            && !AStarAlgo::cantSkipNode (path.number, link.index, true)) {

            indexes.emplace (link.index);
         }
      }

      // we're have max out node links
      if (indexes.length () >= kMaxNodeLinks) {
         const Vector &pos = smooth (indexes);

         if (pathTouchesHazard (pos, pos)) {
            continue;
         }

         // Averaging nodes around a corner can put the replacement in a thin
         // wall. Validate its volume and routes before deleting any originals.
         TraceResult clearance {};
         game.testHull (pos, pos, TraceIgnore::Monsters, crouch ? head_hull : human_hull, nullptr, &clearance);
         if (!graphTraceIsClear (clearance)) {
            continue;
         }
         bool canMerge = graph.isNodeReacheable (path.origin, pos)
            && graph.isNodeReacheable (pos, path.origin);
         for (const auto &index : indexes) {
            if (!graph.isNodeReacheable (graph[index].origin, pos)
               || !graph.isNodeReacheable (pos, graph[index].origin)) {
               canMerge = false;
               break;
            }
         }
         if (!canMerge) {
            continue;
         }

         for (const auto &index : indexes) {
            graph.erase (index);
         }
         m_isCrouch = crouch;
         graph.add (NodeAddFlag::Normal, pos);
      }
   }

   // clear the useless connections
   if (cv_graph_analyze_clean_paths_on_finish) {
      for (auto i = 0; i < graph.length (); ++i) {
         graph.clearConnections (i);
      }
   }
}

void GraphAnalyze::cleanup () {
   int connections = 0; // clean bad paths

   for (auto i = 0; i < graph.length (); ++i) {
      connections = 0;

      for (const auto &link : graph[i].links) {
         if (link.index != kInvalidNodeIndex) {
            if (link.index > graph.length ()) {
               graph.erase (i);
            }
            ++connections;
         }
      }

      // no connections
      if (!connections) {
         graph.erase (i);
      }

      // path number differs
      if (graph[i].number != i) {
         graph.erase (i);
      }

      for (const auto &link : graph[i].links) {
         if (link.index != kInvalidNodeIndex) {
            if (link.index >= graph.length () || link.index < -kInvalidNodeIndex) {
               graph.erase (i);
            }
            else if (link.index == i) {
               graph.erase (i);
            }
         }
      }

      if (!graph.isConnected (i)) {
         graph.erase (i);
      }
   }
}

void GraphAnalyze::displayOverlayMessage () const {
   auto listenserverEdict = game.getLocalEntity ();

   if (game.isNullEntity (listenserverEdict) || !m_isAnalyzing) {
      return;
   }
   constexpr StringRef analyzeHudMesssage =
      "+-----------------------------------------------------------------+\n"
      "         Map analysis for bots is in progress. Please Wait..       \n"
      "+-----------------------------------------------------------------+\n";

   static hudtextparms_t textParams {};

   textParams.channel = 1;
   textParams.x = -1.0f;
   textParams.y = -1.0f;
   textParams.effect = 1;

   textParams.r1 = textParams.r2 = static_cast <uint8_t> (255);
   textParams.g1 = textParams.g2 = static_cast <uint8_t> (31);
   textParams.b1 = textParams.b2 = static_cast <uint8_t> (75);
   textParams.a1 = textParams.a2 = static_cast <uint8_t> (0);

   textParams.fadeinTime = 0.0078125f;
   textParams.fadeoutTime = 0.0078125f;
   textParams.holdTime = m_updateInterval;
   textParams.fxTime = 0.25f;

   game.sendHudMessage (listenserverEdict, textParams, analyzeHudMesssage);
}

void GraphAnalyze::flood (const Vector &pos, const Vector &next, float range) {
   range *= 0.75f;

   TraceResult tr {};
   game.testHull (pos, { next.x, next.y, next.z + 19.0f }, TraceIgnore::Monsters, head_hull, nullptr, &tr);

   // we're can't reach next point
   if (tr.fStartSolid || tr.fAllSolid
      || (!graphTraceIsClear (tr) && !game.isBreakableEntity (tr.pHit))) {
      return;
   }

   // we're have something in around, skip
   if (graph.exists (graph.getForAnalyzer (tr.vecEndPos, range))) {
      return;
   }
   game.testHull (tr.vecEndPos, { tr.vecEndPos.x, tr.vecEndPos.y, tr.vecEndPos.z - 999.0f }, TraceIgnore::Monsters, head_hull, nullptr, &tr);

   // ground is away for a break
   if (tr.fStartSolid || tr.fAllSolid || tr.flFraction >= 1.0f) {
      return;
   }
   const Vector &nextPos = { tr.vecEndPos.x, tr.vecEndPos.y, tr.vecEndPos.z + 19.0f };

   const int endIndex = graph.getForAnalyzer (nextPos, range);
   const int targetIndex = graph.getNearestNoBuckets (nextPos, 250.0f);

   if (graph.exists (endIndex) || !graph.exists (targetIndex)) {
      return;
   }
   const auto &targetPos = graph[targetIndex].origin;

   // re-check there's nothing nearby, and add something we're want
   if (!graph.exists (graph.getNearestNoBuckets (nextPos, range))) {
      // Check the whole player volume, including ceiling edges beside the
      // center line. GoldSrc hulls are 72 units standing and 36 crouching.
      game.testHull (nextPos, nextPos, TraceIgnore::Monsters, human_hull, nullptr, &tr);
      m_isCrouch = !graphTraceIsClear (tr);
      const auto nodePos = m_isCrouch ? nextPos - Vector (0.0f, 0.0f, 18.0f) : nextPos;
      if (m_isCrouch) {
         game.testHull (nodePos, nodePos, TraceIgnore::Monsters, head_hull, nullptr, &tr);
         if (!graphTraceIsClear (tr)) {
            return;
         }
      }

      if ((graph.isNodeReacheable (targetPos, nodePos)
         && graph.isNodeReacheable (nodePos, targetPos)) || (graph.isNodeReacheableWithJump (nodePos, targetPos)
            && graph.isNodeReacheableWithJump (targetPos, nodePos))) {

         graph.add (NodeAddFlag::Normal, nodePos);
      }
   }
}

void GraphAnalyze::setUpdateInterval () {
   const auto frametime = globals->frametime;

   if ((cv_graph_analyze_fps.as <float> () + frametime) <= 1.0f / frametime) {
      m_updateInterval = game.time () + frametime * 0.06f;
   }
}

void GraphAnalyze::markGoals () {
   if (!cv_graph_analyze_mark_goals_on_finish) {
      return;
   }

   auto updateNodeFlags = [] (int type, StringRef classname) {
      game.searchEntities ("classname", classname, [&] (edict_t *ent) {
         for (auto &path : graph) {
            const auto &bb = path.origin + Vector (1.0f, 1.0f, 1.0f);

            if (ent->v.absmin.x > bb.x || ent->v.absmin.y > bb.y) {
               continue;
            }

            if (ent->v.absmax.x < bb.x || ent->v.absmax.y < bb.y) {
               continue;
            }
            path.flags |= type;
         }
         return EntitySearchResult::Continue;
      });
   };

   if (game.mapIs (MapFlags::Demolition)) {
      updateNodeFlags (NodeFlag::Goal, "func_bomb_target"); // bombspot zone
      updateNodeFlags (NodeFlag::Goal, "info_bomb_target"); // bombspot zone (same as above)
   }
   else if (game.mapIs (MapFlags::HostageRescue)) {
      updateNodeFlags (NodeFlag::Rescue, "func_hostage_rescue"); // hostage rescue zone
      updateNodeFlags (NodeFlag::Rescue, "info_hostage_rescue"); // hostage rescue zone (same as above)
      updateNodeFlags (NodeFlag::Rescue, "info_player_start"); // then add ct spawnpoints

      updateNodeFlags (NodeFlag::Goal, "hostage_entity"); // hostage entities
      updateNodeFlags (NodeFlag::Goal, "monster_scientist"); // hostage entities (same as above)
   }
   else if (game.mapIs (MapFlags::Assassination)) {
      updateNodeFlags (NodeFlag::Goal, "func_vip_safetyzone"); // vip rescue (safety) zone
      updateNodeFlags (NodeFlag::Goal, "func_escapezone"); // terrorist escape zone
   }
}
