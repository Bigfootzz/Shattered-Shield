
#ifndef PARSEMESSAGES

#ifndef SERVMODE
struct dmspclientmode : clientmode, ::ai::bot::BotGameMode
{
    int round, remain, total, level, roundTime;

    dmspclientmode() : round(0), level(0), roundTime(0)
    {
    }

    int getRound()
    {
        return round+1;
    }

    int getRemain()
    {
        return remain;
    }

    int getTotal()
    {
        return total;
    }

    void printScores()
    {
        int lscore = round*round*level;
        int tscore = max(((lastmillis-roundTime)/20000), 1);
        conoutf("\f2your score is %d", max(player1->frags*lscore/tscore, 0));
    }

    ::ai::bot::BotGameMode *getBotGameMode()
    {
        return this;
    }
    
    bool aicheck(fpsent *d, ai::bot::aistate &b)
    {
        float mindist = 1e16f;
        fpsent *closest = NULL;
        loopv(players) if (players[i]->isInfected() != d->isInfected() && players[i]->state == CS_ALIVE)
        {
            if (!closest || players[i]->o.dist(closest->o) < mindist) closest = players[i];
        }
        if (!closest) return false;
        b.millis = lastmillis;
        ai::bot::switchState(d, b, ai::bot::AI_S_PURSUE, ai::bot::AI_T_PLAYER, closest->clientnum);
        return true;
    }
    
    void aifind(fpsent *d, ai::bot::aistate &b, vector<ai::bot::interest> &interests)
    {
        bool inf = d->isInfected();
        loopv(players)
        {
            fpsent *f = players[i];
            if (inf != f->isInfected() && f->state == CS_ALIVE)
            {
                ai::bot::interest &n = interests.add();
                n.state = ai::bot::AI_S_PURSUE;
                n.node = ai::closestwaypoint(f->o, ai::SIGHTMIN, true);
                n.target = f->clientnum;
                n.targtype = ai::bot::AI_T_PLAYER;
                n.score = d->o.squaredist(f->o)/100.f;
            }
        }
    }
    
    bool aipursue(fpsent *d, ai::bot::aistate &b)
    {
        fpsent *e = getclient(b.target);
        if(!e) return false;
        if (d->isInfected() && !e->isInfected() && e->state == CS_ALIVE)
        {
            return ai::bot::defend(d, b, e->o, 5, 5, 2);
        }
        else if (!d->isInfected() && e->isInfected() && e->state == CS_ALIVE)
        {
            return ai::bot::violence(d, b, e, true);
        }
        return false;
    }
};
#else
void startintermission(); // defined later in server.cpp (game-over trigger)

struct dmspservmode : servmode
{

    /**
     * Maximum amount of monsters that are remaining when the round countdown is started.
     */
    static const int REMAIN_COUNTDOWN       = 6;

    /**
     * Amount of time to wait before starting the next round when only <code>REMAIN_COUNTDOWN</code> monsters are left.
     */
    static const int REMAIN_COUNTDOWN_TIME  = 150 * 1000;

    /**
     * Seconds the intermission shop stays open between rounds.
     * Fixed timer on purpose: no ready-up, so an idle or disconnected player
     * can never deadlock the server or hold up the other players.
     */
    static const int SHOP_SECS = 25;

    /**
     * Level: difficulty (1 <= x <= 4)
     * spawnLeft: how many monsters left to spawn
     * monsterTotal: total amount of monsters in this round
     * remain: amount of monsters left to be killed
     * nextMonster: time when we should spawn the next monster
     * roundTime: time when the coundown started or 0
     * shopopen: intermission shop is open (no fighting, buying allowed)
     * shopend: server millis when the shop closes and the next round starts
     */
    int level, round, spawnLeft, monsterTotal, remain, nextMonster, roundTime;
    bool shopopen;
    int shopend;

    dmspservmode()
    {
        reset(true);
    }

    void sendRoundInfo(int to = -1)
    {
        sendf(to, 1, "i6", N_ROUNDINFO, round, remain, monsterTotal, level, roundTime);
    }

    void startRound()
    {
        roundTime = 0;
        shopopen = false;
        nextMonster = 0;//lastmillis+10000+ (round * 1000);
        remain = monsterTotal = spawnLeft = ((level)*3) + (((round-1)*(level*2)) + int(round*round));

        sendRoundInfo();

        // deferred: startRound can be entered from inside event processing
        // (see scheduleresetai), where freeing AI clientinfo is unsafe
        aiman::scheduleresetai();

        loopv(clients)
        {
            // Clear all corpses
            if(clients[i]->state.ai.type != ai::AI_TYPE_MONSTER)
            {
                // Job 1: everyone is back in for the new round
                clients[i]->deadout = false;
                if(clients[i]->state.isInfected()) makeInfected(clients[i], -1);
                if(clients[i]->state.state == CS_DEAD) instantRespawn(clients[i]);
                // Job 3: a banked quad arms NOW, at round start, so none of its
                // duration is wasted in the shop. N_BUY with price 0 reuses the
                // existing client sync path (applyitem sets quadmillis locally).
                if(clients[i]->state.ai.type == ai::AI_TYPE_NONE && clients[i]->pendingquad > 0)
                {
                    clients[i]->pendingquad--;
                    clients[i]->state.quadmillis = 30000;
                    sendf(-1, 1, "ri4", N_BUY, clients[i]->clientnum, game::BA_QUADR, 0);
                }
            }
        }
    }

    void nextRound(bool clear = false)
    {
        round++;
        shopopen = false;
        sendservmsgf("\f2Round%s clear!", (clear)? "": " not");
        //TODO playsound(S_V_BASECAP);
        startRound();
    }

    // Job 3: intermission between rounds — fighting pauses and the buy menu opens.
    void startShop()
    {
        shopopen = true;
        shopend = lastmillis + SHOP_SECS*1000;
        sendservmsgf(-1, "\f2Round %d clear! Shop open for %d seconds - press B to buy.", round, SHOP_SECS);
        sendf(-1, 1, "ri2", N_SHOPOPEN, SHOP_SECS*1000);
        sendRoundInfo();
    }

    bool checkRound(clientinfo *exclude = NULL)
    {
        if(spawnLeft) return false;

        int monstersLeft = 0, humansLeft = 0;

        loopv(clients) if(clients[i] != exclude && clients[i]->state.state == CS_ALIVE )
        {
            if(clients[i]->state.ai.type == ai::AI_TYPE_MONSTER) monstersLeft++;
            else humansLeft ++;
        }

        if(monstersLeft <= 0 /*|| humansLeft <= 0*/)
        {
            startShop();
            return true;
        }

        return false;
    }

    void spawnMonster()
    {
        ASSERT(spawnLeft > 0);
        spawnLeft--;
        clientinfo *ci = aiman::addai(ai::monster::aiLevelToSkill(level), -1, ai::AI_TYPE_MONSTER);
        if(ci)
        {
            makeInfected(ci, monster::getRandomType());
            ci->state.state = CS_ALIVE;
            copystring(ci->name, monster::getMonsterType(ci->state.getMonsterType()).classInfo.name, MAXNAMELEN+1);
        }
        else DEBUG_ERROR("Could not create client while spawning monster.");
    }

    void update()
    {
        if(interm) return; // match is over (game over): freeze the round machine
        if(shopopen)
        {
            // fixed timer only: the next round starts when the shop closes, no waiting on players
            if(lastmillis >= shopend) nextRound();
            return;
        }
        if(spawnLeft && lastmillis>nextMonster)
        {
            if(monsterTotal==spawnLeft)
            {
                sendservmsgf("\f2ROUND %d: %d monsters. Fight!", round, monsterTotal);
                //TODO playsound(S_V_FIGHT);
            }
            nextMonster = lastmillis+1000;
            spawnMonster();
        }

        if (spawnLeft == 0 && remain <= REMAIN_COUNTDOWN && roundTime == 0)
        {
            roundTime = lastmillis;
            sendRoundInfo();
        }
        if (roundTime && lastmillis-roundTime > REMAIN_COUNTDOWN_TIME) nextRound();
    }

    void reset(bool empty)
    {
        level = 1;
        round = 0;
        shopopen = false;
        shopend = 0;
        if(!empty) startRound();
    }

    void setup()
    {
        // start round 1 when the match begins: previously the round machine
        // only ever advanced on the first death (see died/checkRound), so a
        // fresh match never spawned any monsters
        reset(true);
        round++;
        startRound();
    }

    // Job 1: a dead player cannot respawn until the next round (unless an
    // extra life is consumed). The client asks to respawn via N_TRYSPAWN,
    // which is gated here; instantRespawn (round start / extra life) bypasses it.
    bool canspawn(clientinfo *ci, bool connecting = false)
    {
        if(!connecting && ci->state.ai.type == ai::AI_TYPE_NONE && ci->deadout) return false;
        return true;
    }

    bool died(clientinfo *victim, clientinfo *actor)
    {
        // AI monster killed: count down the round
        if(victim->state.ai.type == ai::AI_TYPE_MONSTER)
        {
            remain--;
            sendRoundInfo();
            checkRound(victim);
            return false;
        }

        // ---- human (or bot) victim ----
        // Job 2: the old code turned a dead HUMAN into a rat 1/8 of the time
        // (makeInfected as MONSTER_TYPE_TRAIT_RAT via shouldSpawnRat).
        // Removed: a dead player never comes back as a rat.

        // Job 3: a banked extra life is consumed instead of dying out
        if(victim->extralives > 0)
        {
            victim->extralives--;
            if(victim->state.isInfected()) makeInfected(victim, -1);
            victim->deadout = false;
            sendservmsgf(victim->clientnum, "\f2Extra life used! %d banked.", victim->extralives);
            instantRespawn(victim);
            return true;
        }

        // Job 1: death ends your round — you are out until the next round
        victim->deadout = true;
        sendservmsgf(victim->clientnum, "\f2You are out for this round.");

        // Job 1: game over when every human player is out
        // (single player: your death ends the match). Bots do not count:
        // your death ends the run even if your bots are still standing.
        bool anyonein = false;
        loopv(clients)
            if(clients[i]->state.ai.type == ai::AI_TYPE_NONE && !clients[i]->deadout) { anyonein = true; break; }
        if(!anyonein) startintermission();
        return false;
    }
};
#endif

#else // PARSEMESSAGES
#ifndef SERVMODE
case N_ROUNDINFO:
    {
        dmspmode.round          = getint(p);
        dmspmode.remain         = getint(p);
        dmspmode.total          = getint(p);
        dmspmode.level          = getint(p);
        dmspmode.roundTime      = getint(p);
    }
    break;
#else

#endif
#endif