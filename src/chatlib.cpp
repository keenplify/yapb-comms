//
// YaPB, based on PODBot by Markus Klinge ("CountFloyd").
// Copyright © YaPB Project Developers <yapb@jeefo.net>.
//
// SPDX-License-Identifier: MIT
//

#include <yapb.h>

ConVar cv_chat ("chat", "1", "Enables or disables bot chat functionality.");
ConVar cv_chat_percent ("chat_percent", "30", "Bot's chance to send random dead chat when killed.", true, 0.0f, 100.0f);
ConVar cv_chat_llm_port ("chat_llm_port", "0", "Local LLM bridge TCP port. 0 disables LLM chat.", true, 0.0f, 65535.0f);

static String escapeChatJson (StringRef value) {
   String escaped {};

   for (size_t i = 0; i < value.length () && i < 240; ++i) {
      const auto c = value.chars ()[i];

      if (c == '"' || c == '\\') {
         escaped.appendf ("\\%c", c);
      }
      else if (static_cast <unsigned char> (c) < 32) {
         escaped.append (" ");
      }
      else {
         escaped.appendf ("%c", c);
      }
   }
   return escaped;
}

static String requestLocalChat (StringRef body, int port) {
#if defined(CR_WINDOWS)
   WSADATA wsa {};
   if (WSAStartup (MAKEWORD (2, 2), &wsa) != 0) {
      return {};
   }
   const SOCKET socket = ::socket (AF_INET, SOCK_STREAM, IPPROTO_TCP);
   if (socket == INVALID_SOCKET) {
      WSACleanup ();
      return {};
   }
#else
   const int socket = ::socket (AF_INET, SOCK_STREAM, IPPROTO_TCP);
   if (socket < 0) {
      return {};
   }
#endif
   auto closeSocket = [&] () {
#if defined(CR_WINDOWS)
      closesocket (socket);
      WSACleanup ();
#else
      close (socket);
#endif
   };
   sockaddr_in address {};
   address.sin_family = AF_INET;
   address.sin_port = htons (static_cast <uint16_t> (port));
   address.sin_addr.s_addr = htonl (INADDR_LOOPBACK);

#if defined(CR_WINDOWS)
   DWORD timeout = 6000;
#else
   timeval timeout { 6, 0 };
#endif
   setsockopt (socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast <const char *> (&timeout), sizeof (timeout));
   setsockopt (socket, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast <const char *> (&timeout), sizeof (timeout));

   if (::connect (socket, reinterpret_cast <sockaddr *> (&address), sizeof (address)) != 0) {
      closeSocket ();
      return {};
   }
   const String request = strings.format (
      "POST /reply HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: application/json\r\nContent-Length: %u\r\nConnection: close\r\n\r\n%s",
      static_cast <unsigned> (body.length ()), body.chars ());

   size_t sent = 0;

   while (sent < request.length ()) {
#if defined(CR_WINDOWS)
      const int flags = 0;
#else
      const int flags = MSG_NOSIGNAL;
#endif
      const int amount = ::send (socket, request.chars () + sent, static_cast <int> (request.length () - sent), flags);

      if (amount <= 0) {
         closeSocket ();
         return {};
      }
      sent += static_cast <size_t> (amount);
   }
   String response {};
   char buffer[512] {};

   while (response.length () < 4096) {
      const int amount = ::recv (socket, buffer, sizeof (buffer), 0);

      if (amount <= 0) {
         break;
      }
      response.append (String (buffer, static_cast <size_t> (amount)));
   }
   closeSocket ();
   if (!response.startsWith ("HTTP/1.1 200")) {
      return {};
   }
   const auto bodyStart = response.find ("\r\n\r\n");

   if (bodyStart == String::InvalidIndex) {
      return {};
   }
   const String raw = response.substr (bodyStart + 4, 160);
   String safe {};

   for (size_t i = 0; i < raw.length (); ++i) {
      const auto c = raw[i];

      // A reply is inserted into a game console command: reject delimiters.
      if (c == '"' || c == '\\' || c == ';' || c == '%' || static_cast <unsigned char> (c) < 32) {
         safe.append (" ");
      }
      else {
         safe.appendf ("%c", c);
      }
   }
   safe.trim ();
   return safe;
}

BotChatManager::BotChatManager () {
   m_clanTags = {
      { "[[", "]]" }, { "-=", "=-" }, { "-[", "]-" }, { "-]", "[-" },
      { "-}", "{-" }, { "-{", "}-" }, { "<[", "]>" }, { "<]", "[>" },
      { "[-", "-]" }, { "]-", "-[" }, { "{-", "-}" }, { "}-", "-{" },

      { "[", "]" }, { "{", "}" }, { "<", "[" }, { ">", "<" }, { ")", "(" },
      { "-", "-" }, { "|", "|" }, { "=", "=" }, { "+", "+" }, { "(", ")" },
   };
}

bool BotChatManager::hasHumanOnTeam (int team) const {
   for (const auto &client : util.getClients ()) {
      if ((client.flags & ClientFlags::Used) && client.team == team
         && client.ent && !game.isFakeClientEntity (client.ent)) {
         return true;
      }
   }
   return false;
}

void BotChatManager::beginRound () {
   ++m_roundNumber;
   for (auto &kills : m_roundKills) {
      kills = 0;
   }
   m_lastKiller = 0;
   m_lastVictim = 0;
   m_recentEvents.clear ();
   for (auto &count : m_startTeamPlayers) {
      count = 0;
   }
   for (const auto &client : util.getClients ()) {
      if ((client.flags & ClientFlags::Used) && client.team >= 0 && client.team < kGameTeamNum) {
         ++m_startTeamPlayers[client.team];
      }
   }
}

void BotChatManager::recordEvent (StringRef event) {
   if (event.empty ()) {
      return;
   }
   if (m_recentEvents.length () > 180) {
      m_recentEvents.clear ();
   }
   if (!m_recentEvents.empty ()) {
      m_recentEvents.append ("|");
   }
   m_recentEvents.append (event);
}

void BotChatManager::recordDeath (edict_t *killer, edict_t *victim) {
   const int killerIndex = game.indexOfPlayer (killer);
   const int victimIndex = game.indexOfPlayer (victim);

   if (killerIndex < 1 || killerIndex > kGameMaxPlayers || victimIndex < 1 || victimIndex > kGameMaxPlayers) {
      return;
   }
   m_killStreak[victimIndex] = 0;
   m_lastKiller = killerIndex;
   m_lastVictim = victimIndex;

   if (game.getRealPlayerTeam (killer) == game.getRealPlayerTeam (victim)) {
      recordEvent (strings.format ("teamkill:%s>%s", killer->v.netname.chars (), victim->v.netname.chars ()));
      return;
   }
   ++m_roundKills[killerIndex];
   ++m_killStreak[killerIndex];
   recordEvent (strings.format ("kill:%s>%s,k:%d,streak:%d",
      killer->v.netname.chars (), victim->v.netname.chars (), m_roundKills[killerIndex], m_killStreak[killerIndex]));
}

void BotChatManager::recordRoundWin (int team) {
   if (game.is (GameFlags::CSDM | GameFlags::FreeForAll) || team < 0 || team >= kGameTeamNum) {
      return;
   }
   recordEvent (strings.format ("round_winner:%s", team == Team::CT ? "CT" : "T"));
   const int enemyTeam = team == Team::CT ? Team::Terrorist : Team::CT;
   const int enemyCount = m_startTeamPlayers[enemyTeam];

   if (enemyCount < 2) {
      return;
   }
   for (int index = 1; index <= kGameMaxPlayers; ++index) {
      if (m_roundKills[index] != enemyCount) {
         continue;
      }
      auto player = game.entityOfIndex (index);

      if (!game.isNullEntity (player) && game.getRealPlayerTeam (player) == team) {
         recordEvent (strings.format ("ace:%s", player->v.netname.chars ()));
         return;
      }
   }
}

String BotChatManager::recentContext () const {
   String context = strings.format ("round:%d", m_roundNumber);
   int alive[kGameTeamNum] {};

   for (const auto &client : util.getClients ()) {
      if ((client.flags & ClientFlags::Used) && (client.flags & ClientFlags::Alive)
         && client.team >= 0 && client.team < kGameTeamNum) {
         ++alive[client.team];
      }
   }
   context.appendf (";alive:T%d,CT%d", alive[Team::Terrorist], alive[Team::CT]);

   if (gameState.isBombPlanted ()) {
      context.append (";bomb:planted");
   }

   if (!m_recentEvents.empty ()) {
      context.appendf (";%s", m_recentEvents.chars ());
   }
   for (int i = 0; i < 4; ++i) {
      const auto &line = m_recentChat[(m_chatCursor + i) % 4];

      if (!line.empty ()) {
         context.appendf (";chat:%s", line.chars ());
      }
   }
   return context;
}

void BotChatManager::recordChat (int speakerIndex, StringRef message) {
   if (speakerIndex < 1 || speakerIndex > kGameMaxPlayers || message.empty ()) {
      return;
   }
   auto speaker = game.entityOfIndex (speakerIndex);

   if (game.isNullEntity (speaker)) {
      return;
   }
   m_recentChat[m_chatCursor] = strings.format ("%.24s:%.90s", speaker->v.netname.chars (), message.chars ());
   m_chatCursor = (m_chatCursor + 1) % 4;
}

bool BotChatManager::queueLlmReply (int botIndex, int speakerIndex, int depth, StringRef incoming, StringRef fallback) {
   const int port = cv_chat_llm_port.as <int> ();

   if (port == 0 || m_llmPending.length () >= 4 || incoming.empty ()) {
      return false;
   }
   for (const auto &request : m_llmPending) {
      if (request->botIndex == botIndex) {
         return false;
      }
   }
   auto bot = bots.findBotByIndex (botIndex);
   auto speaker = game.entityOfIndex (speakerIndex);

   if (!bot || game.isNullEntity (speaker)) {
      return false;
   }
   auto pending = cr::makeUnique <LlmPending> ();
   pending->botIndex = botIndex;
   pending->speakerIndex = speakerIndex;
   pending->depth = depth;
   pending->round = m_roundNumber;
   pending->botName = bot->ent ()->v.netname.str ();
   pending->fallback = fallback;

   const String botName = escapeChatJson (pending->botName);
   const String speakerName = escapeChatJson (speaker->v.netname.str ());
   const String line = escapeChatJson (incoming);
   const String context = escapeChatJson (recentContext ());
   const String body = strings.format ("{\"bot\":\"%s\",\"speaker\":\"%s\",\"line\":\"%s\",\"context\":\"%s\"}",
      botName.chars (), speakerName.chars (), line.chars (), context.chars ());

   auto *request = pending.get ();
   pending->thread = cr::makeUnique <Thread> ([request, body, port] () {
      const String reply = requestLocalChat (body, port);
      MutexScopedLock lock (request->lock);
      request->reply = reply;
      request->ready = true;
   });

   if (!pending->thread->ok ()) {
      return false;
   }
   m_llmPending.push (cr::move (pending));
   return true;
}

bool BotChatManager::hasLlmPending (int botIndex) const {
   for (const auto &request : m_llmPending) {
      if (request->botIndex == botIndex) {
         return true;
      }
   }
   return false;
}

void BotChatManager::pollLlmReplies () {
   for (size_t i = 0; i < m_llmPending.length (); ) {
      auto &request = m_llmPending[i];
      bool ready = false;

      {
         MutexScopedLock lock (request->lock);
         ready = request->ready;
      }
      if (!ready) {
         ++i;
         continue;
      }
      auto complete = cr::move (request);
      m_llmPending.erase (i, 1);
      complete->thread->join ();

      if (complete->round != m_roundNumber) {
         continue;
      }
      auto bot = bots.findBotByIndex (complete->botIndex);

      if (!bot || bot->ent ()->v.netname.str () != complete->botName.str ()) {
         continue;
      }
      const StringRef reply = complete->reply.empty () ? complete->fallback : complete->reply;

      if (reply.empty ()) {
         continue;
      }
      bot->m_sayTextBuffer.entityIndex = complete->speakerIndex;
      bot->m_sayTextBuffer.replyDepth = complete->depth;
      bot->prepareChatMessage (reply);
      bot->pushMsgQueue (BotMsg::Say);
   }
}

void BotChatManager::shutdownLlm () {
   for (auto &request : m_llmPending) {
      request->thread->join ();
   }
   m_llmPending.clear ();
}

void BotChatManager::stripTags (String &line) {
   if (line.empty ()) {
      return;
   }

   for (const auto &tag : m_clanTags) {
      const size_t start = line.find (tag.first, 0);

      if (start != String::InvalidIndex) {
         const size_t end = line.find (tag.second, start);
         const size_t diff = end - start;

         if (end != String::InvalidIndex && end > start && diff < 32 && diff > 1) {
            line.erase (start, diff + tag.second.length ());
            continue;
         }
      }
   }
}

void BotChatManager::humanizePlayerName (String &playerName) {
   if (playerName.empty ()) {
      return;
   }

   // drop tag marks, 80 percent of time
   if (rg.chance (80)) {
      stripTags (playerName);
   }
   else {
      playerName.trim ();
   }

   // sometimes switch name to lower characters, only valid for the english languge
   if (rg.chance (8) && cv_language.as <StringRef> () == "en") {
      playerName.lowercase ();
   }
}

void BotChatManager::addChatErrors (String &line) {
   // sometimes switch name to lower characters, only valid for the english languge
   if (rg.chance (8) && cv_language.as <StringRef> () == "en") {
      line.lowercase ();
   }
   const auto length = static_cast <int32_t> (line.length ());

   if (length > 15) {
     const auto percentile = length / 2;
     // prevent garbled text in Chinese chat messages
      if (cv_language.as <StringRef>() == "chs" || cv_language.as <StringRef>() == "cht") {
         // "length / 2" percent of time drop a character
         if (rg.chance(percentile)) {
            for (int i = 0; i < 6; ++i) { // try several times
               auto pos = rg(length / 8, length - length / 8);
               auto c = static_cast<unsigned char>(line[pos]);
               if (c < 0x80 && isalnum(c)) { // apply for alphas and nums only
                  line.erase(static_cast <size_t>(pos));
                  break;
               }
            }
         }

         // "length" / 4 precent of time swap character
         if (rg.chance(percentile / 2)) {
            for (int i = 0; i < 6; ++i) { // try several times
               auto pos = static_cast <size_t>(rg(length / 8, 3 * length / 8)); // choose random position in string
               auto c1 = static_cast<unsigned char>(line[pos]);
               auto c2 = static_cast<unsigned char>(line[pos + 1]);
               if (c1 < 0x80 && c2 < 0x80 && isalnum(c1) && isalnum(c2)) {
                  cr::swap(line[pos], line[pos + 1]);
                  break;
               }
            }
         }
      }
      else {
         // "length / 2" percent of time drop a character
         if (rg.chance(percentile)) {
            auto pos = rg(length / 8, length - length / 8);
            line.erase(static_cast <size_t> (pos));
         }

         // "length" / 4 precent of time swap character
         if (rg.chance(percentile / 2)) {
            auto pos = static_cast <size_t> (rg(length / 8, 3 * length / 8)); // choose random position in string
            cr::swap(line[pos], line[pos + 1]);
         }
      }
   }
}

bool BotChatManager::checkKeywords (StringRef line, String &reply) {
   // this function checks is string contain keyword, and generates reply to it

   if (!cv_chat || line.empty ()) {
      return false;
   }

   for (auto &factory : conf.getReplies ()) {
      for (const auto &keyword : factory.keywords) {

         // check is keyword has occurred in message
         if (line.find (keyword) != String::InvalidIndex) {
            auto &usedReplies = factory.usedReplies;

            if (usedReplies.length () >= factory.replies.length () / 4) {
               usedReplies.clear ();
            }

            if (!factory.replies.empty ()) {
               bool replyUsed = false;
               StringRef choosenReply = factory.replies.random ();

               // don't say this twice
               for (auto &used : usedReplies) {
                  if (used.contains (choosenReply)) {
                     replyUsed = true;
                     break;
                  }
               }

               // reply not used, so use it
               if (!replyUsed) {
                  reply.assign (choosenReply); // update final buffer
                  usedReplies.push (choosenReply); // add to ignore list

                  return true;
               }
            }
         }
      }
   }
   // didn't find a keyword? 70% of the time use some universal reply
   if (rg.chance (70) && conf.hasChatBank (Chat::NoKeyword)) {
      reply.assign (conf.pickRandomFromChatBank (Chat::NoKeyword));
      return true;
   }
   return false;
}

void Bot::prepareChatMessage (StringRef message) {
   // this function parses messages from the botchat, replaces keywords and converts names into a more human style

   if (!cv_chat || message.empty ()) {
      return;
   }
   m_chatBuffer = message;

   // must be called before return or on the end
   auto addChatErrors = [&] () {
      if (!m_chatBuffer.empty ()) {
         chatlib.addChatErrors (m_chatBuffer);
         String safe {};

         for (size_t i = 0; i < m_chatBuffer.length () && i < 180; ++i) {
            const auto c = m_chatBuffer[i];

            if (c == '"' || c == '\\' || c == ';' || static_cast <unsigned char> (c) < 32) {
               safe.append (" ");
            }
            else {
               safe.appendf ("%c", c);
            }
         }
         m_chatBuffer = cr::move (safe);
      }
   };

   // need to check if we're have special symbols
   size_t pos = message.find ('%');

   // nothing found, bail out
   if (pos == String::InvalidIndex || pos >= message.length ()) {
      addChatErrors ();
      return;
   }

   // get the humanized name out of client
   auto humanizedName = [] (int index) -> String {
      auto ent = game.playerOfIndex (index);

      if (!game.isPlayerEntity (ent)) {
         return "unknown";
      }
      String playerName = ent->v.netname.chars ();
      chatlib.humanizePlayerName (playerName);

      return playerName;
   };

   // find highfrag player
   auto getHighfragPlayer = [&] () -> String {
      int highestFrags = -1;
      int index = 0;

      for (int i = 0; i < game.maxClients (); ++i) {
         const Client &client = util.getClient (i);

         if (!(client.flags & ClientFlags::Used) || client.ent == ent ()) {
            continue;
         }
         const auto frags = static_cast <int> (client.ent->v.frags);

         if (frags > highestFrags) {
            highestFrags = frags;
            index = i;
         }
      }
      return humanizedName (index);
   };

   // get roundtime
   auto getRoundTime = [] () -> String {
      const auto roundTimeSecs = static_cast <int> (gameState.getRoundEndTime () - game.time ());

      String roundTime {};
      roundTime.assignf ("%02d:%02d", cr::clamp (roundTimeSecs / 60, 0, 59), cr::clamp (cr::abs (roundTimeSecs % 60), 0, 59));

      return roundTime;
   };

   // get bot's victim
   auto getMyVictim = [&] () -> String {
      return humanizedName (game.indexOfPlayer (m_lastVictim));
   };

   // get the game name alias
   auto getGameName = [] () -> String {
      String gameName {};

      if (game.is (GameFlags::ConditionZero)) {
         if (::rg.chance (30)) {
            gameName = "CZ";
         }
         else {
            gameName = "Condition Zero";
         }
      }
      else if (game.is (GameFlags::Modern) || game.is (GameFlags::Legacy)) {
         if (::rg.chance (30)) {
            gameName = "CS";
         }
         else {
            gameName = "Counter-Strike";
         }
      }
      return gameName;
   };

   // get enemy or teammate alive
   auto getPlayerAlive = [&] (bool needsEnemy) -> String {
      for (const auto &client : util.getClients ()) {
         if (!(client.flags & ClientFlags::Used) || !(client.flags & ClientFlags::Alive) || client.ent == ent ()) {
            continue;
         }
         const auto playerIndex = game.indexOfPlayer (client.ent);

         if (needsEnemy && m_team != client.team) {
            return humanizedName (playerIndex);
         }
         else if (!needsEnemy && m_team == client.team) {
            if (game.isPlayerEntity (pev->dmg_inflictor)
               && game.getRealPlayerTeam (pev->dmg_inflictor) == m_team) {

               return humanizedName (game.indexOfPlayer (pev->dmg_inflictor));
            }
            return humanizedName (playerIndex);
         }
      }
      return getHighfragPlayer ();
   };
   size_t replaceCounter = 0;

   while (replaceCounter < 6 && (pos = m_chatBuffer.find ('%')) != String::InvalidIndex) {
      const auto replacePosition = pos + 1;

      if (replacePosition >= m_chatBuffer.length ()) {
         break;
      }

      // found one, let's do replace
      switch (m_chatBuffer[replacePosition]) {

         // the highest frag player
      case 'f':
         m_chatBuffer.replace ("%f", getHighfragPlayer ());
         break;

         // current map name
      case 'm':
         m_chatBuffer.replace ("%m", game.getMapName ());
         break;

         // round time
      case 'r':
         m_chatBuffer.replace ("%r", getRoundTime ());
         break;

         // chat reply
      case 's':
         m_chatBuffer.replace ("%s", m_sayTextBuffer.entityIndex != -1 ? humanizedName (m_sayTextBuffer.entityIndex) : getHighfragPlayer ());
         break;

         // last bot victim
      case 'v':
         m_chatBuffer.replace ("%v", getMyVictim ());
         break;

         // game name
      case 'd':
         m_chatBuffer.replace ("%d", getGameName ());
         break;

         // teammate alive
      case 't':
         m_chatBuffer.replace ("%t", getPlayerAlive (false));
         break;

         // enemy alive
      case 'e':
         m_chatBuffer.replace ("%e", getPlayerAlive (true));
         break;

      case 'g':
         m_chatBuffer.replace ("%g", graph.getAuthor ());
         break;
      };
      ++replaceCounter;
   }
   addChatErrors ();
}

bool Bot::checkChatKeywords (String &reply) {
   // this function parse chat buffer, and prepare buffer to keyword searching

   return chatlib.checkKeywords (utf8tools.strToUpper (m_sayTextBuffer.sayText), reply);
}

bool Bot::receiveChat (int speakerIndex, StringRef message, int depth, bool fromBot) {
   if (!cv_chat || speakerIndex < 1 || speakerIndex > kGameMaxPlayers || message.empty () || depth > 2) {
      return false;
   }
   if (fromBot && !chatlib.hasHumanOnTeam (m_team)) {
      return false;
   }

   // A named bot owns the reply. Plain team chat can receive several answers.
   String upper = utf8tools.strToUpper (message);
   upper.trim ().trim ("\"");
   bool directed = false;
   for (const auto &other : bots) {
      String name = utf8tools.strToUpper (other->pev->netname.str ());

      if (name.empty ()) {
         continue;
      }
      if (upper.contains (strings.format ("@%s", name.chars ()))
         || upper.contains (strings.format ("%s:", name.chars ()))
         || upper.startsWith (strings.format ("%s,", name.chars ()))) {

         if (other->entindex () != entindex ()) {
            return false;
         }
         directed = true;
         break;
      }
   }
   if (m_sayTextBuffer.pending.length () >= 3) {
      m_sayTextBuffer.pending.shift ();
   }
   PendingChat incoming {};
   incoming.speakerIndex = speakerIndex;
   incoming.depth = depth;
   incoming.fromBot = fromBot;
   incoming.directed = directed;
   incoming.readyAt = game.time () + rg (1.5f, 3.5f);
   incoming.text = message;
   m_sayTextBuffer.pending.push (cr::move (incoming));
   return true;
}

bool Bot::isReplyingToChat () {
   if (m_sayTextBuffer.pending.empty () || m_sayTextBuffer.timeNextChat > game.time ()
      || chatlib.hasLlmPending (index ())) {
      return false;
   }
   if (m_sayTextBuffer.pending[0].readyAt > game.time ()) {
      return false;
   }
   auto incoming = cr::move (m_sayTextBuffer.pending[0]);
   m_sayTextBuffer.pending.shift ();

   if (incoming.fromBot && !chatlib.hasHumanOnTeam (m_team)) {
      return false;
   }
   m_sayTextBuffer.entityIndex = incoming.speakerIndex;
   m_sayTextBuffer.sayText = incoming.text;
   m_sayTextBuffer.replyDepth = incoming.depth + 1;
   const auto forget = [&] () {
      m_sayTextBuffer.entityIndex = -1;
      m_sayTextBuffer.sayText.clear ();
      m_sayTextBuffer.replyDepth = 0;
   };

   const int style = m_personality == Personality::Rusher ? 10 : m_personality == Personality::Careful ? -15 : 0;
   const int chance = cr::clamp (m_sayTextBuffer.chatProbability / 2 + style
      + (incoming.directed ? 40 : incoming.fromBot ? -20 : 15), 5, 90);
   if (!rg.chance (chance)) {
      forget ();
      return false;
   }
   String replyText {};
   const bool hasFallback = checkChatKeywords (replyText);

   if (rg.chance (incoming.fromBot ? 25 : 65)
      && chatlib.queueLlmReply (index (), incoming.speakerIndex, m_sayTextBuffer.replyDepth,
         incoming.text, replyText)) {

      m_lastChatTime = game.time ();
      m_sayTextBuffer.timeNextChat = game.time () + m_sayTextBuffer.chatDelay;
      forget ();
      return true;
   }
   if (!hasFallback) {
      forget ();
      return false;
   }
   prepareChatMessage (replyText);
   pushMsgQueue (BotMsg::Say);
   m_lastChatTime = game.time ();
   m_sayTextBuffer.timeNextChat = game.time () + m_sayTextBuffer.chatDelay;
   forget ();
   return true;
}

void Bot::checkForChat () {
   // say a text every now and then

   if (!cv_chat) {
      return;
   }
   if (m_adaptAnnouncePending && m_isAlive && m_buyingFinished
      && chatlib.hasHumanOnTeam (m_team)
      && bots.getLastChatTimestamp () + 5.0f < game.time ()) {
      m_adaptAnnouncePending = false;
      prepareChatMessage ("That route got me twice. Taking a safer path this round.");
      pushMsgQueue (BotMsg::SayTeam);
      bots.setLastChatTimestamp (game.time ());
      return;
   }
   if (isReplyingToChat ()) {
      return;
   }
   if (chatlib.hasLlmPending (index ())) {
      return;
   }
   if (m_isAlive && m_nextSocialChat <= game.time ()
      && (game.time () < gameState.getRoundStartTime () || gameState.isRoundOver ())) {

      m_nextSocialChat = game.time () + rg (20.0f, 40.0f);

      if (chatlib.hasHumanOnTeam (m_team) && bots.getLastChatTimestamp () + 8.0f < game.time ()
         && rg.chance (m_personality == Personality::Careful ? 15 : m_personality == Personality::Rusher ? 40 : 28)) {

         Array <edict_t *> teammates {};

         for (const auto &client : util.getClients ()) {
            if ((client.flags & ClientFlags::Used) && client.team == m_team && client.ent != ent ()) {
               teammates.push (client.ent);
            }
         }
         if (!teammates.empty ()) {
            const auto teammate = teammates.random ();
            const String opener = gameState.isRoundOver ()
               ? strings.format ("%s, ready for the next round?", teammate->v.netname.chars ())
               : strings.format ("%s, ready to push?", teammate->v.netname.chars ());

            m_sayTextBuffer.replyDepth = 0;
            prepareChatMessage (opener);
            pushMsgQueue (BotMsg::SayTeam);
            m_lastChatTime = game.time ();
            bots.setLastChatTimestamp (game.time ());
            return;
         }
      }
   }
   if (m_isAlive || game.is (GameFlags::CSDM)) {
      return;
   }

   // bot chatting turned on?
   if (rg.chance (cv_chat_percent.as <int> ())
      && m_lastChatTime + rg (6.0f, 10.0f) < game.time ()
      && bots.getLastChatTimestamp () + rg (2.5f, 5.0f) < game.time ()
      && chatlib.hasHumanOnTeam (m_team)) {

      if (conf.hasChatBank (Chat::Dead)) {
         StringRef phrase = conf.pickRandomFromChatBank (Chat::Dead);
         bool sayBufferExists = false;

         // search for last messages, sayed
         for (auto &sentence : m_sayTextBuffer.lastUsedSentences) {
            if (phrase.startsWith (sentence)) {
               sayBufferExists = true;
               break;
            }
         }

         if (!sayBufferExists) {
            prepareChatMessage (phrase);
            pushMsgQueue (BotMsg::Say);

            m_lastChatTime = game.time ();
            bots.setLastChatTimestamp (game.time ());

            // add to ignore list
            m_sayTextBuffer.lastUsedSentences.push (phrase);
         }
      }

      // clear the used line buffer every now and then
      if (static_cast <int> (m_sayTextBuffer.lastUsedSentences.length ()) > rg (4, 6)) {
         m_sayTextBuffer.lastUsedSentences.clear ();
      }
   }
}

void Bot::sendToChat (StringRef message, bool teamOnly) {
   // this function prints saytext message to all players

   if (m_isCreature || message.empty () || !cv_chat) {
      return;
   }

   // special handling for legacy games
   if (game.is (GameFlags::Legacy)) {
      sendToChatLegacy (message, teamOnly);
   }
   else {
      issueCommand ("%s \"%s\"", teamOnly ? "say_team" : "say", message);
   }
}

void Bot::sendToChatLegacy (StringRef message, bool teamOnly) {
   // this function prints saytext message to all players for legacy games (< cs 1.6)

   // note: for some reason using regular say & say_team for sending chat messages on hlds on a legacy games
   // causes buffer overruns somewhere in gamedll Host_Say function, thus crashing the game randomly.
   // so this function mimics what legacy gamedll is doing in their Host_Say.

   bool dedicatedSend = false;

   auto sendChatMsg = [&] (const Client &client, String chatMsg) {
      if (game.isDedicated () && !dedicatedSend) {
         dedicatedSend = true;

         game.print ("%s", chatMsg.trim ());
      }
      auto rcv = bots[client.ent];

      // Bots already receive this through pushMsgQueue; avoid duplicate turns.
      if (rcv == nullptr) {
         return; // do not send to controlled bots
      }

      if (((client.flags & ClientFlags::Alive) && m_isAlive)
         || (!(client.flags & ClientFlags::Alive) && m_isAlive)
         || (!(client.flags & ClientFlags::Alive) && !m_isAlive)) {

         MessageWriter (MSG_ONE, msgs.id (NetMsg::SayText), nullptr, client.ent)
            .writeByte (m_index)
            .writeString (chatMsg.chars ());
      }
   };

   if (teamOnly) {
      StringRef teamName {};

      if (m_team == Team::Terrorist) {
         teamName = "(Terrorist)";
      }
      else if (m_team == Team::CT) {
         teamName = "(Counter-Terrorist)";
      }

      for (const auto &client : util.getClients ()) {
         if (!(client.flags & ClientFlags::Used) || client.team2 != m_team || client.ent == ent ()) {
            continue;
         }
         String chatMsg {};

         if (m_isAlive) {
            chatMsg.appendf ("%c%s %c%s%c :  %s\n", 0x01, teamName, 0x03, pev->netname.chars (), 0x01, message);
         }
         else {
            chatMsg.appendf ("%c*DEAD*%s %c%s%c :  %s\n", 0x01, teamName, 0x03, pev->netname.chars (), 0x01, message);
         }
         sendChatMsg (client, chatMsg);
      }
      return;
   }

   for (const auto &client : util.getClients ()) {
      if (!(client.flags & ClientFlags::Used) || client.ent == ent ()) {
         continue;
      }
      String chatMsg {};

      if (m_isAlive) {
         chatMsg.appendf ("%c%s :  %s\n", 0x02, pev->netname.chars (), message);
      }
      else {
         chatMsg.appendf ("%c*DEAD* %c%s%c :  %s\n", 0x01, 0x03, pev->netname.chars (), 0x01, message);
      }
      sendChatMsg (client, chatMsg);
   }
}
