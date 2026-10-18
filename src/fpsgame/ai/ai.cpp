#include "game.h"

extern int fog;

namespace ai
{
    using namespace game;

    avoidset obstacles;
    int updatemillis = 0, iteration = 0, itermillis = 0, forcegun = -1;
    vec aitarget(0, 0, 0);

    VAR(aidebug, 0, 0, 6);
    VAR(aiforcegun, -1, -1, NUMWEAPS-1);

    void requestAdd(AiType type, int skill)
    {
        addmsg(N_REQADDAI, "ri2", type, skill > 0 ? clamp(skill, 1, 101) : -1);
    }

    void requestDel(AiType type)
    {
        addmsg(N_REQDELAI, "ri", type);
    }

    float viewdist(int x)
    {
        return x <= 100 ? clamp((SIGHTMIN+(SIGHTMAX-SIGHTMIN))/100.f*float(x), float(SIGHTMIN), float(fog)) : float(fog);
    }

    float viewfieldx(int x)
    {
        return x <= 100 ? clamp((VIEWMIN+(VIEWMAX-VIEWMIN))/100.f*float(x), float(VIEWMIN), float(VIEWMAX)) : float(VIEWMAX);
    }

    float viewfieldy(int x)
    {
        return viewfieldx(x)*3.f/4.f;
    }

    VAR(aicanmove, 0, 1, 1);

    bool canmove(fpsent *d)
    {
        return aicanmove && d->state != CS_DEAD && !intermission;
    }

    float weapmindist(int weap)
    {
        return WEAP_IS_EXPLOSIVE(weap) ? weapons[weap].projradius : 2;
    }

    float weapmaxdist(int weap)
    {
        return weapons[weap].range + 4;
    }

    bool weaprange(fpsent *d, int weap, float dist)
    {
        float mindist = weapmindist(weap), maxdist = weapmaxdist(weap);
        return dist >= mindist*mindist && dist <= maxdist*maxdist;
    }

    bool targetable(fpsent *d, fpsent *e)
    {
        if(d == e || !canmove(d)) return false;
        return e->state == CS_ALIVE && !isteam(d->team, e->team);
    }

    bool getsight(vec &o, float yaw, float pitch, vec &q, vec &v, float mdist, float fovx, float fovy)
    {
        float dist = o.dist(q);

        if(dist <= mdist)
        {
            float x = fmod(fabs(asin((q.z-o.z)/dist)/RAD-pitch), 360);
            float y = fmod(fabs(-atan2(q.x-o.x, q.y-o.y)/RAD-yaw), 360);
            if(min(x, 360-x) <= fovx && min(y, 360-y) <= fovy) return raycubelos(o, q, v);
        }
        return false;
    }

    bool canshoot(fpsent *d, fpsent *e)
    {
        if(weaprange(d, d->gunselect, e->o.squaredist(d->o)) && targetable(d, e))
            return d->ammo[d->gunselect] > 0 && lastmillis - d->lastaction >= d->gunwait;
        return false;
    }

    void DummyAi::destroy(fpsent *d)
    {
        create(d);
        d->ai.local = false;
    }

    void DummyAi::create(fpsent *d)
    {
        d->ai.local = true;
        d->ai.type = ai::AI_TYPE_BOT;
        d->ai.data = NULL;
    }

    AiInterface * const aiTypes[AI_TYPE_NUM] = {
        new DummyAi(),
        new bot::BotAi(),
        new monster::MonsterAi(),
    };

    AiInterface * const getAiType(AiType type)
    {
        return aiTypes[type];
    }

    AiInterface * const getAiType(fpsent *d)
    {
        return getAiType(d->ai.type);
    }

    static void destroy(fpsent *d)
    {
        getAiType(d)->destroy(d);
    }

    static void create(fpsent *d, AiType type)
    {
        if(d->ai.type != type)
        {
            destroy(d);
            d->ai.type = type;
            getAiType(type)->create(d);
        }
    }


    char randletter(bool vowel, char exclude = 0)
    {
        static const int vpos[] = {0, 4, 8, 14, 20, 24};
        char letter = rnd(26);
        bool isv = false;
        exclude = (exclude)? exclude-'a': 40;
        loopi(sizeof(vpos)/sizeof(vpos[0])) if (vpos[i] == letter) { isv = true; break; }
        if (isv == vowel) return letter+'a';
        while (isv != vowel || letter == exclude)
        {
            letter = (letter+1)%26;
            isv = false;
            loopi(sizeof(vpos)/sizeof(vpos[0])) if (vpos[i] == letter) { isv = true; break; }
        }
        return letter+'a';
    }

    const char *generatename()
    {
        static const char *ntemplates[4] = { "cvcvc", "vcvc", "cvcvv", "cvcv" };
        const char *t = ntemplates[rnd(4)];
        static char name[6];
        for (int i=0, l=strlen(t); i <= l; i++) name[i] = (i==l)? '\0': randletter((t[i]=='v')? true: false, (i)? name[i-1]: 0);
        name[0] = toupper(name[0]);
        return name;
    }

    struct BotName
    {
        const char *name;
        int validFor;

        ~BotName()
        {
            DELETEA(name);
        }
    };

    vector<BotName> botnames;

    ICOMMAND(resetbotnames, "", (), {
        botnames.shrink(0);
    });

    ICOMMAND(registerbotname, "si", (const char *s, int *validFor), {
        BotName &botName = botnames.add();
        botName.name = newstring(s);

        botName.validFor = *validFor > 0 ? *validFor : ~0;
    });

    const char *getbotname(fpsent *state)
    {
        if (ai::botnames.length())
        {
            int seed = rnd(ai::botnames.length());

            int filter = (1 << clamp(state->playermodel, 0, 31));

            ai::BotName *best = NULL;
            int bestScore = __INT_MAX__;

            loopvjrandom(ai::botnames, seed)
            {
                ai::BotName &botName = ai::botnames[j];
                if(filter & botName.validFor)
                {
                    int duplicates = 0;
                    loopv(players)
                    {
                        if (!strcmp(players[i]->name, botName.name))
                        {
                            duplicates ++;
                        }
                    }
                    if(duplicates < bestScore)
                    {
                        bestScore = duplicates;
                        best = &botName;
                    }
                }
                if(bestScore == 0) break;
            }

            if(best) return best->name;

            DEBUG_ERROR("Could not find a name for %i (filter:%i)", state->clientnum, filter);
        }


        return generatename();
    }

    void init(fpsent *d, AiType aiType, int ocn, int sk, int bn, int pm, int pc, const char *name, const char *team)
    {
        loadwaypoints();

        fpsent *o = newclient(ocn);

        d->ownernum = ocn;
        d->plag = 0;
        d->ai.skill = max(1, sk);
        d->playermodel = (pm < 0) ? chooserandomplayermodel(rand()) : pm;
        d->playerclass = (pc < 0) ? rand()%NUMPCS : pc;

        if(!name[0])
        {
            name = getbotname(d);
            addmsg(N_SWITCHNAME, "rcs", d, name);
        }

        bool resetthisguy = false;
        if(!d->name[0])
        {
            if(aidebug) conoutf("%s assigned to %s at skill %d with type %i", colorname(d, name), o ? colorname(o) : "?", sk, aiType);
            else if(aiType == AI_TYPE_BOT) conoutf("\f0join:\f7 %s", colorname(d, name));
            resetthisguy = true;
        }
        else
        {
            if(d->ownernum != ocn)
            {
                if(aidebug) conoutf("%s reassigned to %s", colorname(d, name), o ? colorname(o) : "?");
                resetthisguy = true;
            }
            if(d->ai.skill != sk && aidebug) conoutf("%s changed skill to %d", colorname(d, name), sk);
        }

        copystring(d->name, name, MAXNAMELEN+1);
        copystring(d->team, team, MAXTEAMLEN+1);

        if(aidebug && (pm < 0 || pc < 0)) conoutf("server let us pick bot model/class for: %s (pm:%i pc:%i -> pm:%i pc:%i)", colorname(d, d->name), pm, pc, d->playermodel, d->playerclass);
        if(aidebug && *name) conoutf("server let us pick bot name: %s (before:%s)", colorname(d, d->name), colorname(d, name));

        if(resetthisguy) removeweapons(d);
        if(d->ownernum >= 0 && player1->clientnum == d->ownernum)
        {
            create(d, aiType);
        }
        else if(d->ai.local) destroy(d);
    }

    void update()
    {
        if(intermission) { loopv(players) if(players[i]->ai.local) players[i]->stopmoving(); }
        else // fixed rate logic done out-of-sequence at 1 frame per second for each ai
        {
            if(totalmillis-updatemillis > 1000)
            {
                avoid();
                forcegun = multiplayer(false) ? -1 : aiforcegun;
                updatemillis = totalmillis;
            }
            if(!iteration && totalmillis-itermillis > 1000)
            {
                iteration = 1;
                itermillis = totalmillis;
            }
            int count = 0;
            AI_MULTI_DISPATCH(ai->startThink());
            loopv(players) if(players[i]->ai.local) getAiType(players[i])->think(players[i], ++count == iteration ? true : false);
            AI_MULTI_DISPATCH(ai->endThink());
            if(++iteration > count) iteration = 0;
        }
    }

    bool badhealth(fpsent *d)
    {
        if(d->ai.skill <= 100) return d->health <= (111-d->ai.skill)/4;
        return false;
    }

    bool isgoodammo(int gun)
    {
        return WEAP_USABLE(gun) && !WEAP_IS_MELEE(gun);
    }

    bool hasgoodammo(fpsent *d)
    {
        //TODO: move to weapons.h
        static const int goodguns[] = { WEAP_MG, WEAP_ROCKETL, WEAP_FLAMEJET, WEAP_SLUGSHOT, WEAP_SNIPER, WEAP_CROSSBOW };
        loopi(sizeof(goodguns)/sizeof(goodguns[0])) if(d->hasammo(goodguns[0])) return true;
        if(d->ammo[WEAP_GRENADIER] > 5) return true;
        return false;
    }

    void damaged(fpsent *d, fpsent *e)
    {
        if(bot::damaged(d, e)) return;
    }

    void findorientation(vec &o, float yaw, float pitch, vec &pos)
    {
        vec dir;
        vecfromyawpitch(yaw, pitch, 1, 0, dir);
        if(raycubepos(o, dir, pos, 0, RAY_CLIPMAT|RAY_SKIPFIRST) == -1)
            pos = dir.mul(2*getworldsize()).add(o); //otherwise 3dgui won't work when outside of map
    }

    void spawned(fpsent *d)
    {
        getAiType(d)->respawn(d);
    }

    void killed(fpsent *d, fpsent *e)
    {
        getAiType(d)->killed(d, e);
        if(d != e) getAiType(e)->killed(d, e);
    }

    void itemspawned(int ent)
    {
        if(entities::ents.inrange(ent) && entities::ents[ent]->type >= I_AMMO && entities::ents[ent]->type <= I_QUAD)
        {
            loopv(players)
                if(players[i] && players[i]->ai.local && players[i]->ai.type == AI_TYPE_BOT && players[i]->canpickup(entities::ents[ent]->type))
                    getAiType(players[i])->itemSpawned(players[i], ent);
        }
    }

    void fixfullrange(float &yaw, float &pitch, float &roll, bool full)
    {
        if(full)
        {
            while(pitch < -180.0f) pitch += 360.0f;
            while(pitch >= 180.0f) pitch -= 360.0f;
            while(roll < -180.0f) roll += 360.0f;
            while(roll >= 180.0f) roll -= 360.0f;
        }
        else
        {
            if(pitch > 89.9f) pitch = 89.9f;
            if(pitch < -89.9f) pitch = -89.9f;
            if(roll > 89.9f) roll = 89.9f;
            if(roll < -89.9f) roll = -89.9f;
        }
        while(yaw < 0.0f) yaw += 360.0f;
        while(yaw >= 360.0f) yaw -= 360.0f;
    }

    void fixrange(float &yaw, float &pitch)
    {
        float r = 0.f;
        fixfullrange(yaw, pitch, r, false);
    }

    void getyawpitch(const vec &from, const vec &pos, float &yaw, float &pitch)
    {
        float dist = from.dist(pos);
        yaw = -atan2(pos.x-from.x, pos.y-from.y)/RAD;
        pitch = asin((pos.z-from.z)/dist)/RAD;
    }

    void scaleyawpitch(float &yaw, float &pitch, float targyaw, float targpitch, float frame, float scale)
    {
        if(yaw < targyaw-180.0f) yaw += 360.0f;
        if(yaw > targyaw+180.0f) yaw -= 360.0f;
        float offyaw = fabs(targyaw-yaw)*frame, offpitch = fabs(targpitch-pitch)*frame*scale;
        if(targyaw > yaw)
        {
            yaw += offyaw;
            if(targyaw < yaw) yaw = targyaw;
        }
        else if(targyaw < yaw)
        {
            yaw -= offyaw;
            if(targyaw > yaw) yaw = targyaw;
        }
        if(targpitch > pitch)
        {
            pitch += offpitch;
            if(targpitch < pitch) pitch = targpitch;
        }
        else if(targpitch < pitch)
        {
            pitch -= offpitch;
            if(targpitch > pitch) pitch = targpitch;
        }
        fixrange(yaw, pitch);
    }

    bool lockon(fpsent *d, fpsent *e, float maxdist)
    {
        if((WEAP_IS_MELEE(d->gunselect)) && !d->blocked && !d->timeinair)
        {
            vec dir = vec(e->o).sub(d->o);
            float xydist = dir.x*dir.x+dir.y*dir.y, zdist = dir.z*dir.z, mdist = maxdist*maxdist, ddist = d->radius*d->radius+e->radius*e->radius;
            if(zdist <= ddist && xydist >= ddist+4 && xydist <= mdist+ddist) return true;
        }
        return false;
    }

    void avoid()
    {
        // guess as to the radius of ai and other critters relying on the avoid set for now
        float guessradius = player1->radius;
        obstacles.clear();
        loopv(players)
        {
            dynent *d = players[i];
            if(d->state != CS_ALIVE) continue;
            obstacles.avoidnear(d, d->o.z + d->aboveeye + 1, d->feetpos(), guessradius + d->radius);
        }
        extern avoidset wpavoid;
        obstacles.add(wpavoid);
        avoidweapons(obstacles, guessradius);
    }

    VAR(showwaypoints, 0, 0, 1);
    VAR(showwaypointsradius, 0, 200, 10000);

    void render()
    {
        if(aidebug > 1)
        {
            int total = 0, alive = 0;
            loopv(players) if(players[i]->ai.local) total++;
            loopv(players) if(players[i]->state == CS_ALIVE && players[i]->ai.local)
            {
                alive++;
                getAiType(players[i])->render(players[i], float(alive)/float(total));
            }
            if(aidebug >= 4)
            {
                int cur = 0;
                loopv(obstacles.obstacles)
                {
                    const avoidset::obstacle &ob = obstacles.obstacles[i];
                    int next = cur + ob.numwaypoints;
                    for(; cur < next; cur++)
                    {
                        int ent = obstacles.waypoints[cur];
                        if(iswaypoint(ent))
                            regular_particle_splash(PART_EDIT, 2, 40, waypoints[ent].o, 0xFF6600, 1.5f);
                    }
                    cur = next;
                }
            }
        }
        if(showwaypoints || aidebug >= 6)
        {
            vector<int> close;
            int len = waypoints.length();
            if(showwaypointsradius)
            {
                findwaypointswithin(camera1->o, 0, showwaypointsradius, close);
                len = close.length();
            }
            loopi(len)
            {
                waypoint &w = waypoints[showwaypointsradius ? close[i] : i];
                loopj(MAXWAYPOINTLINKS)
                {
                     int link = w.links[j];
                     if(!link) break;
                     particle_flare(w.o, waypoints[link].o, 1, PART_STREAK, 0x0000FF);
                }
            }

        }
    }
}
