//
// YaPB, based on PODBot by Markus Klinge ("CountFloyd").
// Copyright © YaPB Project Developers <yapb@jeefo.net>.
//
// SPDX-License-Identifier: MIT
//

#pragma once

// links keywords and replies together
struct ChatKeywords {
   StringArray keywords {};
   StringArray replies {};
   StringArray usedReplies {};

public:
   ChatKeywords () = default;

   ChatKeywords (const StringArray &keywords, const StringArray &replies) {
      this->keywords.clear ();
      this->replies.clear ();
      this->usedReplies.clear ();

      this->keywords.insert (0, keywords);
      this->replies.insert (0, replies);
   }
};

struct PendingChat {
   int speakerIndex {};
   int depth {};
   float readyAt {};
   bool fromBot {};
   bool directed {};
   String text {};
};

struct QueuedChat {
   String text {};
   int replyDepth {};
   bool teamOnly {};
};

// Per-bot conversation state. Keep a small queue so overlapping speakers do not
// overwrite one another while a bot is waiting to answer.
struct ChatCollection {
   int chatProbability {};
   float chatDelay {};
   float timeNextChat {};
   int entityIndex {};
   String sayText {};
   int replyDepth {};
   Array <PendingChat> pending {};
   StringArray lastUsedSentences {};
};


// bot's chat manager
class BotChatManager : public Singleton <BotChatManager> {
private:
   struct LlmPending {
      Mutex lock {};
      UniquePtr <Thread> thread {};
      String reply {};
      String fallback {};
      String botName {};
      int botIndex {};
      int speakerIndex {};
      int depth {};
      int round {};
      bool ready {};
   };

   SmallArray <Twin <String, String>> m_clanTags {}; // strippable clan tags
   Array <UniquePtr <LlmPending>> m_llmPending {};
   String m_recentChat[4] {};
   int m_chatCursor {};
   int m_roundKills[kGameMaxPlayers + 1] {};
   int m_killStreak[kGameMaxPlayers + 1] {};
   int m_startTeamPlayers[kGameTeamNum] {};
   int m_lastKiller {};
   int m_lastVictim {};
   int m_roundNumber {};
   String m_recentEvents {};

public:
   BotChatManager ();
   ~BotChatManager () = default;

public:
   // chat helper to strip the clantags out of the string
   void stripTags (String &line);

   // chat helper to make player name more human-like
   void humanizePlayerName (String &playerName);

   // chat helper to add errors to the bot chat string
   void addChatErrors (String &line);

   // chat helper to find keywords for given string
   bool checkKeywords (StringRef line, String &reply);
   bool hasHumanOnTeam (int team) const;
   void beginRound ();
   void recordDeath (edict_t *killer, edict_t *victim);
   void recordEvent (StringRef event);
   void recordRoundWin (int team);
   String recentContext () const;
   void recordChat (int speakerIndex, StringRef message);
   bool queueLlmReply (int botIndex, int speakerIndex, int depth, StringRef incoming, StringRef fallback);
   bool hasLlmPending (int botIndex) const;
   void pollLlmReplies ();
   void shutdownLlm ();
};

// expose global
CR_EXPOSE_GLOBAL_SINGLETON (BotChatManager, chatlib);
