#ifndef PARSEMESSAGES

#define infteamflag(s) (!strcmp(s, TEAM_0) ? 1 : (!strcmp(s, TEAM_1) ? 2 : 0))
#define infflagteam(i) (i==1 ? TEAM_0 : (i==2 ? TEAM_1 : NULL))

#ifdef SERVMODE
struct infectionservmode : servmode
#else
FVARP(zombieradarscale, 0.f, 1.f, 100.f);
VARP(zombieradaroffset, 0, 200, 1000);

struct infectionclientmode : clientmode, ::ai::bot::BotGameMode
#endif
{
    static const int RESPAWNSECS = 5;

    int getteamscore(const char *team)
    {
        int score = 0;
#ifdef SERVMODE
    #define PLAYERS server::clients
    #define GS state.
#else
    #define PLAYERS game::players
    #define GS
#endif
        loopv(PLAYERS)
        {
            if(isteam(PLAYERS[i]->team, team))
                score += PLAYERS[i]-> GS frags;
        }
#undef GS
#undef PLAYERS
        
        return score;
    }

    void getteamscores(vector<teamscore> &tscores)
    {
        tscores.add(teamscore(TEAM_0, getteamscore(TEAM_0)));
        tscores.add(teamscore(TEAM_1, getteamscore(TEAM_1)));
    }

#ifdef SERVMODE

    infectionservmode() : timeLeft(0), nextInsanity(0), specialComming(false) {}

    bool specialComming;
    vector<clientinfo *> spawning;
    
    bool isSpawning(clientinfo *ci)
    {
        int i = spawning.find(ci);
        if(i < 0) return false;
        spawning.remove(i);
        return true;
    }
    
    bool canspawn(clientinfo *ci, bool connecting = false)
    {
        return m_juggernaut ? (isteam(ci->team, TEAM_0) && !isSpawning(ci) ? false : true) : lastmillis-ci->state.lastdeath > RESPAWNSECS;
    }

    bool canchangeteam(clientinfo *ci, const char *oldteam, const char *newteam)
    {
        return false;
    }

    void sendZombieState(clientinfo *victim, bool zombie = true, int type = 0)
    {
        if(specialComming && zombie && type == 0)
        {
            specialComming = false;
            makeInfected(victim, zombie ? monster::getRandomTypeWithTrait(monster::MONSTER_TYPE_TRAIT_BOSS) : -1);
        }
        else
        {
            makeInfected(victim, (type == 0 && zombie ? monster::getRandomType() : type -1));
        }
    }

    void makeZombie(clientinfo *victim, bool zombie = true, int type = 0)
    {
        ASSERT(victim != NULL && "Victim cannot be null");
        if(!victim) return;
        copystring(victim->team, zombie ? TEAM_1 : TEAM_0);
        aiman::changeteam(victim);
        sendZombieState(victim, zombie, type);
        sendf(-1, 1, "riisi", N_SETTEAM, victim->clientnum, victim->team, -1);
        sendf(-1, 1, "ri6", N_DIED, victim->clientnum, victim->clientnum, victim->state.frags, 0 /* reason */, -1);

        instantRespawn(victim);

        spawning.add(victim);
    }

    bool isZombieAvailable(const char *team = TEAM_1, clientinfo *exclude = NULL) //is any zombie still connected
    {
        //Check if any zombies left
        loopv(clients)
        {
            if(exclude != clients[i] && (clients[i]->state.state == CS_ALIVE || (!m_juggernaut && (clients[i]->state.state == CS_DEAD || clients[i]->state.state == CS_SPAWNING))))
            {
                if(isteam(clients[i]->team, team))
                {
                    return true;
                }
            }
        }
        
        return false;
    }

    void initclient(clientinfo *ci, packetbuf &p, bool connecting)
    {
        loopv(clients)
        {
            if(clients[i]->state.isInfected())
            {
                putint(p, N_INFECT);
                putint(p, clients[i]->clientnum);
                putint(p, clients[i]->state.infectedType);
            }
        }
    }
    
    void entergame(clientinfo *ci)
    {
        //Restored from an earlier state
        if(ci->state.isInfected())
        {
            makeZombie(ci, true, ci->state.infectedType);
        }
        else if(isZombieAvailable(TEAM_1, ci))
        {
            makeZombie(ci, false);
        }
        else
        {
            makeZombie(ci);
        }
    }

    int timeLeft;
    int nextInsanity;

    void updateRaw()
    {
        if(timeLeft > 0)
        {
            if(timeLeft < totalmillis)
            {
                timeLeft = 0;
                pausegame(false);
            }
            else if(timeLeft - 1900 < totalmillis)
            {
                pausegame(true);
            }
        }

        if(nextInsanity < totalmillis)
        {
            loopv(clients)
            {
                if(isteam(clients[i]->team, TEAM_0))
                {
                    dodamage(clients[i], clients[i], WEAP(WEAP_INSANITY, damage), WEAP_INSANITY, vec(0, 0, 0), false);
                }
            }
            nextInsanity = totalmillis + 20000 + rnd(20000);
            sendservmsgf(-1, "Insanity is upon us!");
            specialComming = rnd(5)==1;
        }
    }

    void newRound()
    {
        if(!clients.length()) return;
        spawning.setsize(0);

        int aiNum = 0;

        loopv(clients)
        {
            makeZombie(clients[i], false);
            if(clients[i]->state.ai.type == ai::AI_TYPE_BOT)
            {
                aiNum ++;
            }
        }


        if(aiNum > 0) 
        {
            int numZombies = 1;

            if(!m_juggernaut)
            {
                numZombies += min(
                    5,
                    min(
                        (int)floor(
                            float(clients.length())/float(4)
                        ),
                        (int)ceil(
                            float(aiNum)/float(4)
                        )
                    )
                );
            }

            // pick first nomZombies amount of bots
            loopv(bots)
            {
                if(bots[i])
                {
                    numZombies--;

                    makeZombie(bots[i]);
                    if(numZombies <= 0)
                    {
                        break;
                    }
                }
            }
        }
        else
        {
            //No ai, pick random player
            //No zombies found, pick random new one
            int x = rnd(clients.length());

            do
            {
                loopv(clients)
                {
                    if(clients[i]->state.state == CS_ALIVE || clients[i]->state.state == CS_DEAD || clients[i]->state.state == CS_SPAWNING)
                    {
                        if(x <= 0) //We have found our new zombie
                        {
                            makeZombie(clients[i]);
                            x = -1;
                            break;
                        }

                        x--;
                    }
                }
            }
            while (x >= 0);
        }

        sendservmsgf(-1, "New round!");
        timeLeft = totalmillis + 2000;
        nextInsanity = totalmillis + 20000 + rnd(20000);
    }
    
    void reset(bool empty)
    {
        newRound();
    }

    void cleanup()
    {
        specialComming = false;
        spawning.shrink(0);
    }

    void leavegame(clientinfo *ci, bool disconnecting)
    {
        if(isZombieAvailable(TEAM_1, ci))
        {
            if(!isZombieAvailable(TEAM_0, ci))
            {
                newRound();
                return;
            }
        }
        else
        {
            newRound();
        }
    }

    void onSpawn(clientinfo *victim)
    {
        if (!m_juggernaut && isteam(victim->team, TEAM_1))
        {
            sendZombieState(victim);
        }
    }

    bool died(clientinfo *victim, clientinfo *actor)
    {
        bool didRespawn = false;
        // Non zombie killed by zombie or suicide
        if(actor == NULL || victim == actor || (isteam(victim->team, TEAM_0) && actor && isteam(actor->team, TEAM_1)))
        {
            //We do not respawn humans as zombies in juggernaut mode
            if(!m_juggernaut)
            {
                makeZombie(victim);
                didRespawn = true;
            }
        }

        //No humans left or juggernaut zombie is dead
        if(!isZombieAvailable(TEAM_0, victim) || (m_juggernaut && isteam(victim->team, TEAM_1)))
        {
            newRound();
        }
        else if(isteam(victim->team, TEAM_1) && ::monster::shouldSpawnRat())
        {
            makeZombie(victim, true, ::monster::getRandomTypeWithTrait(::monster::MONSTER_TYPE_TRAIT_RAT)+1);
            didRespawn = true;
        }

        return didRespawn;
    }

    virtual int fragvalue(clientinfo *victim, clientinfo *actor)
    {
        return (victim == actor) ? 0 : servmode::fragvalue(victim, actor);
    }

    bool shouldBalanceBots(bool botbalance)
    {
        return false;
    }
};
#else
    ::ai::bot::BotGameMode *getBotGameMode() { return this; }

    infectionclientmode()
    {
    }

    void preload()
    {
    }

    void drawblip(vec pos, int w, int h, float s, float maxdist, float mindist, float scale, int team)
    {
        pos.z = 0;
        float dist = pos.magnitude();
        if (dist > maxdist) return;
        float balpha = 1.f-max(.5f, (dist/maxdist));

        settexture(team==0? "data/hud/blip_grey.png": team==1? "data/hud/blip_blue.png": "data/hud/blip_red.png");

        vec po(pos);
        po.normalize().mul(mindist);

        pos.mul(scale).add(po);
        pos.x += (float)w/(float)h*900.f;
        pos.y += 900;
        //pos.add(120.f);
        glColor4f(1, 1, 1, balpha);

        glBegin(GL_TRIANGLE_STRIP);
        glTexCoord2f(0.0f, 0.0f); glVertex2f(pos.x-s,    pos.y-s);
        glTexCoord2f(1.0f, 0.0f); glVertex2f(pos.x+s,    pos.y-s);
        glTexCoord2f(0.0f, 1.0f); glVertex2f(pos.x-s,    pos.y+s);
        glTexCoord2f(1.0f, 1.0f); glVertex2f(pos.x+s,    pos.y+s);
        glEnd();
    }

    void drawhud(fpsent *d, int w, int h)
    {
        if(d->isInfected())
        {
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            float s = 24/2, maxdist = 400, mindist = 50+zombieradaroffset, scale = zombieradarscale;

            loopv(players) if (players[i]!=d && players[i]->isInfected()!=d->isInfected() && players[i]->state == CS_ALIVE)
                drawblip(vec(d->o).sub(players[i]->o).rotate_around_z((-d->yaw)*RAD), w, h, s, maxdist, mindist, scale,
                            players[i]->isInfected()? 0: isteam(players[i]->team, d->team)? 1: 2);
        }

        if (d->state == CS_DEAD)
        {
            int wait = respawnwait(d);
            if (wait > 0)
            {
                int x = (float)w/(float)h*1600.f, y = 200;
                glPushMatrix();
                glScalef(2, 2, 1);
                bool flash = wait>0 && d==player1 && lastspawnattempt>=d->lastpain && lastmillis < lastspawnattempt+100;
                draw_textf("%s%d", x/2-(wait>=10 ? 28 : 16), y/2-32, flash ? "\f3" : "", wait);
                glPopMatrix();
            }
        }
    }

    int respawnwait(fpsent *d)
    {
        return max(0, RESPAWNSECS-(lastmillis-d->lastpain)/1000);
    }

    int respawnmillis(fpsent *d)
    {
        return max(0, RESPAWNSECS*1000-(lastmillis-d->lastpain));
    }

    const char *prefixnextmap() { return "infection_"; }

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
#endif

#elif SERVMODE

#else

#endif

