//
// YaPB, based on PODBot by Markus Klinge ("CountFloyd").
// Copyright © YaPB Project Developers <yapb@jeefo.net>.
//
// SPDX-License-Identifier: MIT
//

#pragma once

#include <team_order.h>

// bot creation tab
struct BotRequest {
   bool manual {};
   int difficulty {};
   int team {};
   int skin {};
   int personality {};
   String name {};
};

struct MapCallout {
   String name {};
   Vector position {};
};

struct TeamAlias {
   String phrase {};
   TeamOrder order { TeamOrder::None };
};

struct TeamOrderAck {
   int team {};
   int excludeSlot { -1 };
   bool accepted {};
   float due {};
};

// manager class
class BotManager final : public Singleton <BotManager> {
public:
   using ForEachBot = const Lambda <bool (Bot *)> &;
   using UniqueBot = UniquePtr <Bot>;

private:
   float m_difficultyBalanceTime {}; // time to balance difficulties ?
   float m_autoKillCheckTime {}; // time to kill all the bots ?
   float m_maintainTime {}; // time to maintain bot creation
   float m_quotaMaintainTime {}; // time to maintain bot quota
   float m_plantSearchUpdateTime {}; // time to update for searching planted bomb
   float m_lastChatTime {}; // global chat time timestamp
   float m_lastAddressedReplyTime {}; // global direct-reply rate limit
   float m_lastHumanTeamChat[kGameTeamNum] {};
   bool m_humanCaptain[kGameTeamNum] {};
   bool m_botCaptainCalled[kGameTeamNum] {};
   int m_botCaptainIndex[kGameTeamNum] {};
   float m_economyCallTime[kGameTeamNum] {};
   bool m_economyCallSent[kGameTeamNum] {};
   float m_nextCommsDebugTime {};
   Vector m_debugBotOrigin[kGameMaxPlayers] {};
   float m_debugBotMovedTime[kGameMaxPlayers] {};
   float m_lastEnemyReportTime[kGameTeamNum][3] {};
   float m_lastQueuedAckTime[kGameTeamNum] {};
   Array <TeamOrderAck> m_pendingOrderAcks {};
   float m_preRoundChatTime[kGameTeamNum] {};
   float m_postRoundChatTime[kGameTeamNum] {};
   bool m_preRoundChatSent[kGameTeamNum] {};
   bool m_postRoundChatSent[kGameTeamNum] {};
   float m_deadChatTime {};
   float m_postPlantChatTime {};
   bool m_postPlantChatSent {};
   int m_roundKills[kGameMaxPlayers] {};
   int m_pendingKillReaction[kGameMaxPlayers] {};
   int m_killReactionTeam[kGameMaxPlayers] {};
   float m_killReactionTime[kGameMaxPlayers] {};
   int m_dropOfferPhase[kGameTeamNum] {};
   int m_dropOfferRich[kGameTeamNum] {};
   int m_dropOfferPoor[kGameTeamNum] {};
   bool m_dropOfferInitiatedByPoor[kGameTeamNum] {};
   float m_dropOfferTime[kGameTeamNum] {};
   int m_humanDropBot[kGameTeamNum] {};
   int m_humanDropPlayer[kGameTeamNum] {};
   int m_humanDropWeapon[kGameTeamNum] {};
   int m_humanDropPhase[kGameTeamNum] {};
   float m_humanDropTime[kGameTeamNum] {};
   float m_humanDropDeadline[kGameTeamNum] {};
   bool m_humanDropBuyIssued[kGameTeamNum] {};
   int m_roundChatNumber {};
   String m_calloutMap {};
   Array <MapCallout> m_callouts {};
   Array <TeamAlias> m_teamAliases {};
   bool m_teamAliasesLoaded {};

   int m_lastWinner {}; // the team who won previous round
   int m_lastDifficulty {}; // last bots difficulty
   int m_bombSayStatus {}; // some bot is issued whine about bomb
   int m_numPreviousPlayers {}; // number of players in game im previous player check

   bool m_botsCanPause {}; // bots can do a little pause ?

   Deque <String> m_saveBotNames {}; // bots names that persist upon changelevel
   Deque <BotRequest> m_addRequests {}; // bot creation tab
   SmallArray <BotTask> m_filters {}; // task filters
   SmallArray <UniqueBot> m_bots {}; // all available bots

   edict_t *m_killerEntity {}; // killer entity for bots
   BotTeamData  m_teamData[kGameTeamNum] {}; // teams shared data

   CountdownTimer m_holdQuotaManagementTimer {}; // prevent from running quota management for some time

protected:
   BotCreateResult create (StringRef name, int difficulty, int personality, int team, int skin);

public:
   BotManager ();
   ~BotManager () = default;

public:
   Twin <int, int> countTeamPlayers ();

   Bot *findBotByIndex (int index);
   Bot *findBotByEntity (edict_t *ent);

   Bot *findAliveBot ();
   Bot *findHighestFragBot (int team);

   int getHumansCount (bool ignoreSpectators = false);
   int getAliveHumansCount ();
   int getPlayerPriority (edict_t *ent);

   float getConnectionTimes (StringRef name, float original);
   float getAverageTeamKPD (bool calcForBots);

   void frame ();
   bool aiBridgeReady () const;
   void logAiEvent (Bot *bot, edict_t *player, StringRef text, bool teamOnly, bool canned);
   void createKillerEntity ();
   void destroyKillerEntity ();
   void touchKillerEntity (Bot *bot);
   void destroy ();
   void addbot (StringRef name, int difficulty, int personality, int team, int skin, bool manual);
   void addbot (StringRef name, StringRef difficulty, StringRef personality, StringRef team, StringRef skin, bool manual);
   void serverFill (int selection, int personality = Personality::Normal, int difficulty = -1, int numToAdd = -1);
   void kickEveryone (bool instant = false, bool zeroQuota = true);
   void kickBot (int index);
   void kickFromTeam (Team team, bool removeAll = false);
   void killAllBots (int team = Team::Invalid, bool silent = false);
   void maintainQuota ();
   void maintainAutoKill ();
   void maintainLeaders ();
   void maintainCaptains ();
   void maintainEnemyCallouts ();
   void maintainRoundChat ();
   void maintainKillReactions ();
   void maintainDropOffers ();
   void acknowledgeTeamOrder (int team, bool accepted);
   void processTeamOrderAcks ();
   void maintainRoundRestart ();
   void initQuota ();
   void initRound ();
   void decrementQuota (int by = 1);
   void selectLeaders (int team, bool reset);
   void listBots ();
   void setWeaponMode (int selection);
   void updateTeamEconomics (int team, bool setTrue = false);
   void updateBotDifficulties ();
   void balanceBotDifficulties ();
   void reset ();
   void initFilters ();
   void resetFilters ();
   void captureChatRadio (StringRef cmd, StringRef arg, edict_t *ent);
   void loadMapCallouts ();
   bool findMapCallout (StringRef name, Vector &position);
   void loadTeamAliases ();
   TeamOrder parseTeamChatOrder (const char *raw);
   void notifyBombDefuse ();
   void execGameEntity (edict_t *ent);
   void forEach (ForEachBot handler);
   void disconnectBot (Bot *bot);
   void handleDeath (edict_t *killer, edict_t *victim);
   void setLastWinner (int winner);
   void checkBotModel (edict_t *ent, char *infobuffer);
   void checkNeedsToBeKicked ();
   void refreshCreatureStatus ();

   bool isTeamStacked (int team);
   bool kickRandom (bool decQuota = true, Team fromTeam = Team::Unassigned);
   bool balancedKickRandom (bool decQuota);
   bool hasCustomCSDMSpawnEntities ();
   bool isFrameSkipDisabled ();

public:
   bool getTeamEconomics (int team) const {
      return m_teamData[team].positiveEco;
   }

   int32_t getLastWinner () const {
      return m_lastWinner;
   }

   int32_t getBotCount () const {
      return m_bots.length <int32_t> ();
   }

   // get the list of filters
   SmallArray <BotTask> &getFilters () {
      return m_filters;
   }

   void createRandom (bool manual = false) {
      addbot ("", -1, -1, -1, -1, manual);
   }

   bool canPause () const {
      return m_botsCanPause;
   }

   void setCanPause (const bool pause) {
      m_botsCanPause = pause;
   }

   bool hasBombSay (int type) const {
      return (m_bombSayStatus & type) == type;
   }

   void clearBombSay (int type) {
      m_bombSayStatus &= ~type;
   }

   void setPlantedBombSearchTimestamp (const float timestamp) {
      m_plantSearchUpdateTime = timestamp;
   }

   float getPlantedBombSearchTimestamp () const {
      return m_plantSearchUpdateTime;
   }

   void setLastRadioTimestamp (const int team, const float timestamp) {
      if (team == Team::CT || team == Team::Terrorist) {
         m_teamData[team].lastRadioTimestamp = timestamp;
      }
   }

   float getLastRadioTimestamp (const int team) const {
      if (team == Team::CT || team == Team::Terrorist) {
         return m_teamData[team].lastRadioTimestamp;
      }
      return 0.0f;
   }

   void setLastRadio (const int team, const int radio) {
      m_teamData[team].lastRadioSlot = radio;
   }

   int getLastRadio (const int team) const {
      return m_teamData[team].lastRadioSlot;
   }

   void setLastChatTimestamp (const float timestamp) {
      m_lastChatTime = timestamp;
   }

   float getLastChatTimestamp () const {
      return m_lastChatTime;
   }

   // some bots are online ?
   bool hasBotsOnline () const {
      return getBotCount () > 0;
   }

public:
   Bot *operator [] (int index) {
      return findBotByIndex (index);
   }

   Bot *operator [] (edict_t *ent) {
      return findBotByEntity (ent);
   }

public:
   UniqueBot *begin () {
      return m_bots.begin ();
   }

   UniqueBot *begin () const {
      return m_bots.begin ();
   }

   UniqueBot *end () {
      return m_bots.end ();
   }

   UniqueBot *end () const {
      return m_bots.end ();
   }
};

// bot async worker wrapper
class BotThreadWorker final : public Singleton <BotThreadWorker> {
private:
   UniquePtr <ThreadPool> m_pool {};

public:
   explicit BotThreadWorker () = default;
   ~BotThreadWorker () = default;

public:
   void shutdown ();
   void startup (int workers);

public:
   template <typename F> void enqueue (F &&fn) {
      if (!available ()) {
         fn (); // no threads, no fun, just run task in current thread
         return;
      }
      m_pool->enqueue (cr::move (fn));
   }

public:
   bool available () {
      return m_pool && m_pool->threadCount () > 0;
   }
};

// expose global
CR_EXPOSE_GLOBAL_SINGLETON (BotManager, bots);

// expose async worker
CR_EXPOSE_GLOBAL_SINGLETON (BotThreadWorker, worker);
