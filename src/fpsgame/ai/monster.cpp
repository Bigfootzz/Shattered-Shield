// monster.h: implements AI for single player monsters, currently client only
#include "game.h"
#include <cstring>
#include "rpg/rpg.h" // Shattered Shield: RPG map gate + mob-behaviour constants
#include "../diaglog.h" // TEMP diagnostics (Nov 18 .141): bite/spawn logging, remove when bugs are closed

extern int physsteps;

namespace monster
{
    static const MonsterType monstertypes[] =
    {
        {
            //PlayerClassInfo:
            //weap[4]                                       health, armourtype, armour, maxspeed,   name,       abilities
            { {WEAP_BITE, WEAP_NONE, WEAP_NONE, WEAP_NONE}, 180,    0,          0,      72,         "zombie 1", 0},
            // freq,    lag,    rate,   pain,   loyalty,    painsound,  diesound, traits, 
               3,       0,      100,    400,    1,          S_PAINB,    S_DEATHB, 0,
            //ModelInfo:
            // ffa,                             blueteam,   redteam,    hudguns,    vwep,   quad,   armour[3],              ffaicon,        blueicon,   redicon,    zombieHands,        ragdoll,    selectable, radius, eyeheight,  aboveeye,   weight
            { "playermodels/zombies/zombie1",   NULL,       NULL,       NULL,       NULL,   NULL,   { NULL, NULL, NULL },   "classicb_64",  NULL,       NULL,       "hudguns/zombie",   false,      true,       3.5f,   16.5f,      1.0f,       0},
        },
        {
            { {WEAP_BITE, WEAP_NONE, WEAP_NONE, WEAP_NONE}, 180,    0,          0,      72,         "zombie 2", 0},
               3,       0,      100,    400,    1,          S_PAINB,    S_DEATHB, 0,
            { "playermodels/zombies/zombie2",   NULL,       NULL,       NULL,       NULL,   NULL,   { NULL, NULL, NULL },   "zclassic",     NULL,       NULL,       "hudguns/zombie",   false,      true,       3.8f,   14.0f,      1.0f,       0 },
        },
        { 
            { {WEAP_BITE, WEAP_NONE, WEAP_NONE, WEAP_NONE}, 180,    0,          0,      72,         "zombie 3", 0},
               3,       0,      100,    400,    1,          S_PAINR,    S_DEATHR, 0,
            { "playermodels/zombies/zombie3",   NULL,       NULL,       NULL,       NULL,   NULL,   { NULL, NULL, NULL },   "classicd_64",  NULL,       NULL,       "hudguns/zombie",   false,      true,       3.8f,   15.8f,      1.0f,       0 },
        },
        { 
            { {WEAP_BITE, WEAP_NONE, WEAP_NONE, WEAP_NONE}, 180,    0,          0,      72,         "zombie 4", 0},
               3,       0,      100,    400,    1,          S_PAINR,    S_DEATHR, 0,
            { "playermodels/zombies/zombie4",   NULL,       NULL,       NULL,       NULL,   NULL,   { NULL, NULL, NULL },   "zjhon",        NULL,       NULL,       "hudguns/zombie",   false,      true,       3.8f,   17.0f,      1.0f,       0 },
        },
        { 
            { {WEAP_BITE, WEAP_NONE, WEAP_NONE, WEAP_NONE}, 180,    0,          0,      72,         "zombie 5", 0},
               3,       0,      100,    400,    1,          S_PAINH,    S_DEATHH, 0,
            { "playermodels/zombies/zombie5",   NULL,       NULL,       NULL,       NULL,   NULL,   { NULL, NULL, NULL },   "classicc",     NULL,       NULL,       "hudguns/zombie",   false,      true,       3.8f,   15.5f,      1.0f,       0 },
        },
        {
            { {WEAP_BITE, WEAP_NONE, WEAP_NONE, WEAP_NONE}, 180,    0,          0,      72,         "zombie 6", 0},
               3,       0,      100,    400,    1,          S_PAINH,    S_DEATHH, 0,
            { "playermodels/zombies/zombie6",   NULL,       NULL,       NULL,       NULL,   NULL,   { NULL, NULL, NULL },   "skeleton_64",  NULL,       NULL,       "hudguns/zombie",   false,      true,       3.8f,   15.5f,      1.0f,       0 },
        },
        {
            { {WEAP_BITE, WEAP_NONE, WEAP_NONE, WEAP_NONE}, 180,    0,          0,      72,         "zombie 7", 0},
               3,       0,      100,    400,    1,          S_PAIND,    S_DEATHD, 0,
            { "playermodels/zombies/zombie7",   NULL,       NULL,       NULL,       NULL,   NULL,   { NULL, NULL, NULL },   "heavy_64",     NULL,       NULL,       "hudguns/zombie",   false,      true,       4.5f,   17.0f,      1.0f,       0 },
        },
        {
            { {WEAP_BITE, WEAP_NONE, WEAP_NONE, WEAP_NONE}, 40,     0,          0,      130,        "rat",      0},
               0,       0,      100,    400,    1,          S_PAINR,    S_DEATHR,  MONSTER_TYPE_TRAIT_RAT,
            { "playermodels/zombies/rat",       NULL,       NULL,       NULL,       NULL,   NULL,   { NULL, NULL, NULL },   "rat",          NULL,       NULL,       NULL,               false,      true,       3.0f,   3.0f,       0.5f,       0 },
        },
        {
            { {WEAP_SLIME, WEAP_BITE, WEAP_NONE, WEAP_NONE}, 400,    A_YELLOW,   100,    28,         "juggernaut", 0},
               0,       0,      100,    400,    1,          S_PAINH,    S_DEATHH, MONSTER_TYPE_TRAIT_BOSS,
            { "playermodels/zombies/juggernaut",NULL,       NULL,       NULL,       NULL,   NULL,   { NULL, NULL, NULL },   "juggernaut",   NULL,       NULL,       "hudguns/zombie",   false,      true,       7.0f,   22.0f,      3.0f,       0 },
        },
        {
            // Bestiary (Shattered Shield): fast skirmisher, between a zombie
            // (180 HP) and the Rotting Champion (400 HP). freq 0: named types
            // never enter the random "zombie" pool; they spawn only where the
            // RPG spawn table names them (rpg_resolvespawntype).
            // TUNING Oct 21 (user-approved): 90 HP, not 200 - the
            // weakest ambient mob and the q1 hunt target was unwinnable
            // for a level-1 character.
            { {WEAP_BITE, WEAP_NONE, WEAP_NONE, WEAP_NONE}, 90,     0,          0,      116,        "imp",      0},
               0,       0,      100,    400,    1,          S_PAINB,    S_DEATHB, 0,
            { "playermodels/imp",               NULL,       NULL,       NULL,       NULL,   NULL,   { NULL, NULL, NULL },   "imp",          NULL,       NULL,       "hudguns/zombie",   false,      true,       3.2f,   14.0f,      1.0f,       0 },
        },
        {
            // Bestiary (Shattered Shield): slow brute, tankier than a zombie,
            // below the champion. freq 0 for the same reason as the imp.
            { {WEAP_BITE, WEAP_NONE, WEAP_NONE, WEAP_NONE}, 300,    0,          0,      90,         "puglin",   0},
               0,       0,      100,    400,    1,          S_PAINH,    S_DEATHH, 0,
            { "playermodels/puglin",            NULL,       NULL,       NULL,       NULL,   NULL,   { NULL, NULL, NULL },   "puglin",       NULL,       NULL,       "hudguns/zombie",   false,      true,       4.2f,   13.0f,      1.0f,       0 },
        },
        {
            // Forest Giant (Shattered Shield): "Forest Monster" by CDmir
            // (OpenGameArt, CC0). WORLD mob, 3x player size (Ed, Nov 17):
            // native height 41.24u incl. tree, mdlscale 468 -> ~193u tall.
            // Lives in the clearing just outside the village (4 spawns in
            // the RPG spawn table, GPS from Ed Nov 17); level 14, so the
            // stock AI hunts on sight (AGGRESSIVE, Ed's call). freq 0:
            // named types never enter the random pool; it spawns only
            // where the RPG spawn table names it. Radius/eyeheight follow
            // the codebase's own ratios (juggernaut-scaled); tune after
            // playtest.
            { {WEAP_BITE, WEAP_NONE, WEAP_NONE, WEAP_NONE}, 300,    0,          0,      45,         "forest giant", 0},
               0,       0,      100,    400,    1,          S_PAINH,    S_DEATHH, 0,
            { "playermodels/forest_giant",      NULL,       NULL,       NULL,       NULL,   NULL,   { NULL, NULL, NULL },   NULL,           NULL,       NULL,       "hudguns/zombie",   false,      true,       15.0f,  50.0f,      1.0f,       0 },
        }
        /* TODO
        { 
            //  gun,        speed   health, freq, lag, rate, pain, loyalty, bscale, weight, painsound, diesound, name, *mdlname, *vwepname uchar traits; playermodelinfo
            WEAP_SLUGSHOT,  13,     200, 0, 0,      2,      400,    0,      13,     75, S_PAIN4, S_DIE2,    "support trooper sg",   "ogro2",                        "ogro/vwep",    0,
            {}
        },
        { 
            //  gun,        speed   health, freq, lag, rate, pain, loyalty, bscale, weight, painsound, diesound, name, *mdlname, *vwepname uchar traits; playermodelinfo
            WEAP_ROCKETL,   13,     200, 0, 0,      2,      400,    0,      13,     75, S_PAIN4, S_DIE2,    "support trooper rl",   "ogro2",                        "ogro/vwep",    0,
            {}
        },*/
    };

    const int NUMMONSTERTYPES = sizeof(monstertypes)/sizeof(monstertypes[0]);

    const MonsterType &getMonsterType(int id)
    {
        return monstertypes[id % NUMMONSTERTYPES];
    }

    bool isValidMonsterType(int id)
    {
        return id >= 0 && id < NUMMONSTERTYPES;
    }

    // Shattered Shield RPG levels, parallel to monstertypes[] above. The
    // ladder follows max HP: rat 1, zombies 2, imp 4, puglin 6, juggernaut
    // 10, forest giant 14. All ambient types are <= 10, so all ambient mobs (and the
    // Rotting Champion, a zombie variant) are passive until engaged under
    // the Oct 20 mob-behaviour rules. Data-only: stock modes never read it.
    static const int monsterlevels[] = { 2, 2, 2, 2, 2, 2, 2, 1, 10, 4, 6, 14 };

    int getMonsterLevel(int id)
    {
        if(id < 0 || id >= NUMMONSTERTYPES) return 1;
        return monsterlevels[id];
    }

    int getRandomType()
    {
        int n = rnd(::monster::TOTMFREQ), type;
        for(int i = rnd(::monster::NUMMONSTERTYPES); ; i = (i+1) % ::monster::NUMMONSTERTYPES)
        {
            if((n -= ::monster::getMonsterType(i).freq)<0)
            {
                type = i; break;
            }
        }

        return type;
    }

    int getRandomTypeWithTrait(int traits)
    {
        vector<int> types;
        loopi(NUMMONSTERTYPES)
        {
            if(getMonsterType(i).traits & traits)
            {
                types.add(i);
            }
        }

        int amount = types.length(); 
        switch(amount)
        {
            case 0: return -1;
            case 1: return types[0];
            default:

                int n = rnd(::monster::TOTMFREQ), type;
                for(int i = rnd(amount); ; i = (i+1) % amount)
                {
                    if((n -= ::monster::getMonsterType(types[i]).freq)<0)
                    {
                        type = types[i]; break;
                    }
                }

            return type;
        }
    }

    bool shouldSpawnRat()
    {
        return rnd(8) == 0;
    }

#ifndef STANDALONE
    void preloadMonsters()
    {
        // Shattered Shield (load-time fix, Oct 22): on RPG maps only warm
        // the monster types that can actually appear. The quest champion
        // draws from the random (freq>0) pool and ambient spawns resolve by
        // name against the RPG spawn table (rpg_resolvespawntype); anything
        // else (e.g. the juggernaut: freq 0, FPS-mode-only) can never spawn
        // on an RPG map. FPS maps unchanged.
        bool rpgmap = rpg::mapactive(game::getincomingmapname());
        loopi(NUMMONSTERTYPES)
        {
            if(rpgmap)
            {
                const MonsterType &mt = monstertypes[i];
                bool needed = mt.freq > 0;
                if(!needed)
                {
                    loopj(rpg::spawnpointcount())
                    {
                        const rpg::spawndef *sp = rpg::spawnpoint(j);
                        if(sp && sp->type && !strcmp(sp->type, mt.classInfo.name)) { needed = true; break; }
                    }
                }
                if(!needed) continue;
            }
            preloadmodel(monstertypes[i].modelInfo.ffa);
        }
    }
#endif

    bool providesGuts(fpsstate *state)
    {
        return state->ai.type==ai::AI_TYPE_MONSTER;
    }
}

#ifndef STANDALONE
VAR(dbgmonster, 0, 0, 2);
namespace ai
{
    namespace monster
    {
        using namespace game;

        void MonsterAiInfo::transition(fpsent *d, int _state, int _moving, int n, int r) // n = at skill 0, n/2 = at skill 10, r = added random factor
        {
            monsterstate = _state;
            d->move = _moving;
            n = n*130/100;
            // FIX (Nov 17, mob AI proposal 2): the timing formula is only
            // defined for skill 0..10 (n at skill 0, ~n/2 at skill 10).
            // RPG ambient mobs spawn at skill 50, where n - 50*(n/16) goes
            // NEGATIVE, so the trigger lands in the past and every state
            // ends the same tick it starts (machine-gun state flips).
            // Clamp the skill the formula sees to its designed max of 10;
            // skill 50 then behaves like skill 10 (fastest intended).
            int skill = min(d->ai.skill, 10);
            trigger = lastmillis+n-skill*(n/16)+rnd(r+1);
        }

        inline MonsterAiInfo *getMonsterState(fpsent *d)
        {
            ASSERT(d->ai.local);
            ASSERT(d->ai.type == AI_TYPE_MONSTER);
            ASSERT(d->ai.data);
            return (MonsterAiInfo *)d->ai.data;
        }

        void MonsterAi::destroy(fpsent *d)
        {
            d->ai.local = false;
            ASSERT(d->ai.data);
            delete (MonsterAiInfo *)d->ai.data;
            d->ai.data = NULL;
        }

        void MonsterAi::create(fpsent *d)
        {
            d->ai.local = true;
            d->ai.data = new MonsterAiInfo;
        }

        void MonsterAi::collide(fpsent *d, physent *o, const vec &dir)
        {
            if(dir.z > 0)
            {
                MonsterAiInfo *ai = getMonsterState(d);
                ai->stacked = o;
                ai->stackpos = o->o;
            }
        }

        void MonsterAi::hit(fpsent *d, fpsent *actor, int damage, const vec &vel, int gun)
        {
            MonsterAiInfo *ai = getMonsterState(d);
            const ::monster::MonsterType &monsterType = ::monster::getMonsterType(d->getMonsterType());
            if(actor)
            {
                if(actor->isInfected())     // a monster hit us
                {
                    if(d!=actor && monsterType.loyalty)            // guard for RL guys shooting themselves :)
                    {
                        ai->anger++;     // don't attack straight away, first get angry
                        int _anger = actor->ai.type == AI_TYPE_MONSTER && actor->isInfected() ? ai->anger/2 : ai->anger;
                        if(_anger>=monsterType.loyalty) ai->enemy = actor;     // monster infight if very angry
                    }
                }
                else
                {
                    ai->anger = 0;
                    if (monsterType.loyalty)
                    {
                        ai->enemy = d;
                        if (!bestenemy || bestenemy->state==CS_DEAD) bestenemy = d;
                    }
                    monsterhurt = true;
                    monsterhurtpos = d->o;
                }
            }
            if(d->health > 0)
            {
                ai->transition(d, M_PAIN, 0, monsterType.pain, 200);      // in this state monster won't attack
                playsound(monsterType.painsound, &d->o);
            }
        }

        void MonsterAi::killed(fpsent *d, fpsent *e)
        {
            if(d->ai.type == AI_TYPE_MONSTER)
            {
                MonsterAiInfo *ai = getMonsterState(d);
                const ::monster::MonsterType &monsterType = ::monster::getMonsterType(d->getMonsterType());
                if (monsterType.freq) e->guts += (3/monsterType.freq) * (5*d->maxhealth/10);

                defformatstring(id)("monster_dead_%d", ai->tag);
                if(identexists(id)) execute(id);
            }
        }

        // --- RPG: Shattered Shield world-mob behaviour (Oct 20 rules) ---
        // game::damaged records the human attacker here; think() picks it
        // up as the mob's retaliation target. This is the ONLY way a world
        // mob acquires a target on RPG maps (no aggro on sight).
        void notedamage(fpsent *d, fpsent *actor)
        {
            if(!d || !actor || d == actor) return;
            if(d->ai.type != AI_TYPE_MONSTER || !d->ai.local || !d->ai.data) return;
            if(actor->isInfected()) return; // monster-on-monster damage is dropped server-side on RPG maps anyway
            MonsterAiInfo *ai = (MonsterAiInfo *)d->ai.data;
            ai->rpgfoe = actor;
        }

        // Replaces stock think() for level <= 10 monsters on RPG maps only.
        // Design: mobs are PASSIVE (never aggro on sight), ANCHORED to
        // their spawn point (idle roam radius rpg::MOB_ROAM_RADIUS),
        // RETALIATE only against the player who damaged them, and LEASH at
        // rpg::MOB_LEASH_RANGE from home: mob or foe past the line breaks
        // the fight, the mob walks home, and the server resets the fight
        // (full health, threat wiped) once it is back. Movement and the
        // bite flow deliberately mirror the stock M_HOME / M_AIMING path
        // so attacks land through the exact same shoot() plumbing.
        // Waypoint routing (Nov 17, proposal 5) IS used when chasing a foe
        // with no line of sight, reusing the stock Level 2 routing; it
        // falls back to the beeline when no .wpt graph exists for the map.
        // The blocked-hop + wall-slide below covers lips and steps.
        void MonsterAi::rpgthink(fpsent *d, MonsterAiInfo *ai, const ::monster::MonsterType &monsterType)
        {
            // Leash / roam distances are horizontal: the anchor z is a feet
            // level while d->o rides at eye height, so a 3D distance would
            // carry a constant eye-height bias.
            #define RPGDH(v) (vec((v).x - ai->rpghome.x, (v).y - ai->rpghome.y, 0).magnitude())
            // (Re)spawn or server reset: a new lifesequence means a fresh
            // life, anchored wherever the server placed the mob.
            if(ai->rpglifeseq != d->lifesequence)
            {
                ai->rpglifeseq = d->lifesequence;
                if(!ai->rpghomeknown) ai->rpghome = d->o;
                else if(RPGDH(d->o) > rpg::MOB_ROAM_RADIUS)
                {
                    // The spawnstate pipeline parks remote ents at a player
                    // spawn; the server anchor (N_RPG_MOBHOME) is the truth.
                    // As the movement authority for this mob, place it home.
                    // Nov 18 (.139, Note 196): the anchor z is the EYE
                    // position, not a feet level (the server sends sp->z,
                    // which is ground + eyeheight, and its own reset adds
                    // only +2.0f). The old code added eyeheight again, so
                    // the mob floated eyeheight above the ground (the giant
                    // floated ~52u). Only the +2.0f lift stays.
                    d->o = vec(ai->rpghome.x, ai->rpghome.y, ai->rpghome.z + 2.0f);
                    d->resetinterp(); // moveplayer(local) snaps o to newpos: keep them in sync
                    d->vel = vec(0, 0, 0);
                    d->falling = vec(0, 0, 0);
                }
                ai->rpgfoe = NULL;
                ai->enemy = NULL;
                ai->anger = 0;
                ai->wproute.setsize(0);
                ai->transition(d, M_SLEEP, 0, 100, 100);
            }

            // FIX (Nov 17, mob AI proposal 4): stuck detection + wall-slide.
            // If the mob was trying to move (d->move, set by the previous
            // tick's state machine) but its position barely changed over
            // the last second, it is wedged on terrain or a wall. Set a
            // steer offset so it angles along the obstacle instead of
            // pushing straight into it, hop to clear a lip, and (for an
            // idle mob) pick a fresh wander direction. The offset decays
            // while the mob moves freely, so it straightens out once
            // clear; if it is still blocked the blocked handler re-sets it.
            if(d->move && lastmillis >= ai->rpgstucktimer)
            {
                if(ai->rpgstucktimer > 0 && ai->rpgstuckpos.dist(d->o) < 2.0f)
                {
                    ai->rpgsteeroffset = rnd(2) ? 60 : -60;
                    ai->rpgwanderyaw = rnd(360);
                    // Nov 18 (.139, Note 198): no jumping on RPG maps (Ed:
                    // mobs should not jump at all). The steer offset alone
                    // turns the mob off the obstacle; the hop is dropped.
                    if(!rpg::mapactive(getclientmap())) d->jumping = true;
                }
                ai->rpgstuckpos = d->o;
                ai->rpgstucktimer = lastmillis + 1000;
            }
            if(!d->blocked && ai->rpgsteeroffset != 0)
            {
                float decay = curtime*0.06f; // ~60 deg/sec back to straight
                if(ai->rpgsteeroffset > 0) { ai->rpgsteeroffset -= decay; if(ai->rpgsteeroffset < 0) ai->rpgsteeroffset = 0; }
                else { ai->rpgsteeroffset += decay; if(ai->rpgsteeroffset > 0) ai->rpgsteeroffset = 0; }
            }

            // Retaliation: consume the recorded attacker as the target.
            if(!ai->enemy && ai->rpgfoe)
            {
                if(ai->rpgfoe->state == CS_ALIVE)
                {
                    ai->enemy = ai->rpgfoe;
                    ai->transition(d, M_HOME, 1, 100, 100);
                    conoutf("rpgbehav: '%s' engages '%s' (damaged)", d->name, ai->enemy->name);
                }
                ai->rpgfoe = NULL;
            }
            if(ai->enemy && ai->enemy->state != CS_ALIVE) { ai->enemy = NULL; ai->transition(d, M_SLEEP, 0, 100, 100); }

            // Leash: either side past the line from home breaks the fight.
            // Silent (Nov 18, Note 190): the leash message used conoutf and
            // spammed the screen every tick on load, because the foe (player)
            // is far from the mob's home even when the mob never left it.
            // Other mobs' leashes are silent; this one is too now.
            if(ai->enemy && (RPGDH(d->o) > rpg::MOB_LEASH_RANGE || RPGDH(ai->enemy->o) > rpg::MOB_LEASH_RANGE))
            {
                ai->enemy = NULL;
                ai->rpgfoe = NULL;
                ai->transition(d, M_SEARCH, 1, 100, 100); // M_SEARCH here = returning home
            }

            // Steering: face the foe, or face home when returning.
            vec dest;
            bool havedest = false;
            if(ai->enemy) { dest = ai->enemy->o; havedest = true; }
            else if(RPGDH(d->o) > rpg::MOB_ROAM_RADIUS) { dest = ai->rpghome; havedest = true; }
            // Waypoint routing (proposal 5): when chasing a foe with no
            // line of sight, steer along the waypoint graph instead of
            // beelining into the wall between the mob and the foe. This
            // reuses the stock Level 2 routing (ai::route / closestwaypoint
            // / wproute, replanned at most once a second). With no .wpt
            // graph for this map, closestwaypoint returns -1 and it falls
            // back to the beeline, so behaviour is unchanged until
            // rpg_01.wpt exists. The graph itself is the remaining piece.
            if(ai->enemy)
            {
                vec lostarget;
                if(!raycubelos(d->o, ai->enemy->o, lostarget))
                {
                    bool routed = false;
                    if(!ai->wproute.empty())
                    {
                        int node = ai->wproute.last();
                        if(iswaypoint(node))
                        {
                            if(d->o.squaredist(waypoints[node].o) < (WAYPOINTRADIUS*1.5f)*(WAYPOINTRADIUS*1.5f))
                            {
                                ai->wproute.pop(); // reached this node, aim for the next one
                                routed = !ai->wproute.empty();
                            }
                            else routed = true;
                            if(routed) { node = ai->wproute.last(); dest = waypoints[node].o; havedest = true; }
                        }
                        else ai->wproute.setsize(0);
                    }
                    if(!routed && lastmillis-ai->lastrouteplan >= 1000)
                    {
                        ai->lastrouteplan = lastmillis;
                        int from = closestwaypoint(d->o, SIGHTMAX, true);
                        int goal = closestwaypoint(ai->enemy->o, SIGHTMAX, true);
                        if(iswaypoint(from) && iswaypoint(goal) && from != goal)
                        {
                            if(route(d, from, goal, ai->wproute, obstacles)) { int node = ai->wproute.last(); dest = waypoints[node].o; havedest = true; }
                            else ai->wproute.setsize(0);
                        }
                        else ai->wproute.setsize(0);
                    }
                }
                else ai->wproute.setsize(0); // line of sight: beeline, drop any stale route
            }
            if(havedest)
            {
                ai->targetyaw = -atan2(dest.x - d->o.x, dest.y - d->o.y)/RAD;
                // Wall-slide (proposal 4): angle off the direct line by the
                // steer offset, so a mob sliding along a wall keeps moving
                // instead of pushing straight into it.
                ai->targetyaw += ai->rpgsteeroffset;
                normalizeYaw(d->yaw, ai->targetyaw);
                if(ai->targetyaw > d->yaw) { d->yaw += curtime*0.5f; if(ai->targetyaw < d->yaw) d->yaw = ai->targetyaw; }
                else { d->yaw -= curtime*0.5f; if(ai->targetyaw > d->yaw) d->yaw = ai->targetyaw; }
                float dd = d->o.dist(dest);
                if(dd > 0.01f) d->pitch = asin(clamp((dest.z - d->o.z)/dd, -1.0f, 1.0f))/RAD;
            }

            if(d->blocked) // ran into scenery: hop it (returning or chasing)
            {
                d->blocked = false;
                // Wall-slide (proposal 4): angle along the wall instead of
                // pushing straight into it. Set the steer offset if none is
                // active; the direction is random so it does not always
                // turn the same way into a corner.
                if(!ai->rpgsteeroffset) ai->rpgsteeroffset = rnd(2) ? 45 : -45;
                if(!rpg::mapactive(getclientmap()) && !rnd(20000/max(monsterType.classInfo.maxspeed/4, 1))) d->jumping = true;
                // v0.1.26 (item 5): idle wander hopped "like on drugs".
                // Was !rnd(3): while blocked on uneven ground, an idle
                // imp jumped on ~1 in 3 think ticks — machine-gun hopping
                // against every terrain lip, tree and rock. Now 1 in 30:
                // still clears a genuine step within about a second of
                // trying, but no longer bounces constantly. Combat hop
                // (first branch, chasing a foe) is untouched.
                else if(!ai->enemy && !rpg::mapactive(getclientmap()) && !rnd(30)) d->jumping = true;
            }

            float dist = ai->enemy ? ai->enemy->o.dist(d->o) : 0;
            switch(ai->monsterstate)
            {
                case M_AIMING: // delay between wanting to bite and biting (stock flow)
                    if(ai->enemy && ai->trigger < lastmillis && lastmillis-d->lastaction >= d->gunwait)
                    {
                        d->attacking = true;
                        shoot(d, ai->attacktarget);
                        ai->transition(d, M_ATTACKING, 0, 600, 0);
                        ai->lastshot = lastmillis;
                    }
                    else if(!ai->enemy) ai->transition(d, M_SLEEP, 0, 100, 100);
                    break;
                case M_ATTACKING:
                case M_PAIN:
                    if(ai->trigger < lastmillis) ai->transition(d, ai->enemy ? M_HOME : M_SLEEP, ai->enemy ? 1 : 0, 100, 100);
                    break;
                case M_HOME: // chasing the foe
                    if(!ai->enemy) { ai->transition(d, M_SLEEP, 0, 100, 100); break; }
                    // FIX (Nov 17, mob AI proposal 1): close the bite gap.
                    // The mob used to stop at dist <= 20 and try to bite at
                    // dist < 20, but WEAP_BITE's ray only reaches 12, so it
                    // stood 13-20u away whiffing forever. Now it closes to
                    // within 10u (inside the 12u bite ray, with margin for
                    // the 3D origin distance) and only bites once there.
                    d->move = dist > 10 ? 1 : 0;
                    if(ai->trigger < lastmillis)
                    {
                        vec target;
                        if(raycubelos(d->o, ai->enemy->o, target))
                        {
                            if(dist <= 10 && ai->enemy->state == CS_ALIVE) // bite trigger, inside bite range now
                            {
                                ai->attacktarget = rnd(100) < 20*aiSkillToLevel(d->ai.skill) ? ai->enemy->headpos() : target;
                                ai->transition(d, M_AIMING, 0, monsterType.lag, 10);
                            }
                            else ai->transition(d, M_HOME, 1, monsterType.rate, 0);
                        }
                        else ai->transition(d, M_HOME, 1, 800, 500); // no line of sight: keep homing (open fields)
                    }
                    break;
                case M_SEARCH: // returning home after a leash break
                    if(ai->enemy) { ai->transition(d, M_HOME, 1, 100, 100); break; }
                    if(RPGDH(d->o) <= rpg::MOB_ROAM_RADIUS) { d->move = 0; ai->transition(d, M_SLEEP, 0, 100, 100); }
                    else d->move = 1;
                    break;
                case M_SLEEP:
                default: // idle at home: slow wander within roam radius (v0.1.18, Ed: slower)
                    if(ai->enemy) { ai->transition(d, M_HOME, 1, 100, 100); break; }
                    if(RPGDH(d->o) > rpg::MOB_ROAM_RADIUS) { ai->transition(d, M_SEARCH, 1, 100, 100); break; }
                    if(lastmillis >= ai->rpgwandertimer)
                    {
                        ai->rpgwanderyaw = rnd(360);
                        ai->rpgwandertimer = lastmillis + 6000 + rnd(4000);
                        // start a fresh move phase with each new direction
                        ai->rpgmoving = true;
                        ai->rpgmovetimer = lastmillis + 2000;
                    }
                    // v0.1.18: intermittent move/pause cycle - 2s walk, 3s stand (Ed: still too fast)
                    // v0.1.26 (item 5): 2s walk, 5s stand — idle imps spend
                    // more time standing and less time running around,
                    // calming the patrol without touching combat movement.
                    if(lastmillis >= ai->rpgmovetimer)
                    {
                        ai->rpgmoving = !ai->rpgmoving;
                        ai->rpgmovetimer = lastmillis + (ai->rpgmoving ? 2000 : 5000);
                    }
                    float yawdiff = ai->rpgwanderyaw - d->yaw;
                    while(yawdiff > 180) yawdiff -= 360;
                    while(yawdiff < -180) yawdiff += 360;
                    d->yaw += yawdiff * min(1.0f, curtime*2.0f/1000.0f);
                    d->move = ai->rpgmoving ? 1 : 0;
                    break;
            }

            // movement tail, same as stock think()
            if(d->move || d->maymove() || (ai->stacked && (ai->stacked->state!=CS_ALIVE || ai->stackpos != ai->stacked->o)))
            {
                if(physsteps > 0) ai->stacked = NULL;
                moveplayer(d, 1, true);
                entities::checkitems(d);
                if(cmode) cmode->checkitems(d);
            }
        }

        void MonsterAi::think(fpsent *d, bool run)
        {
            MonsterAiInfo *ai = getMonsterState(d);
            const ::monster::MonsterType &monsterType = ::monster::getMonsterType(d->getMonsterType());

            // --- RPG: Shattered Shield world-mob behaviour (Oct 20 rules) ---
            // On RPG maps, level <= 10 monsters run the anchored/passive/
            // leashed behaviour instead of the stock hunt. Everywhere else
            // (and for any future higher-level type) stock think() is untouched.
            if(d->state==CS_ALIVE && rpg::mapactive(getclientmap()) && ::monster::getMonsterLevel(d->getMonsterType()) <= 10)
            {
                rpgthink(d, ai, monsterType);
                return;
            }

            // RPG leash for higher-level mobs (Nov 18, Notes 176/179):
            // the Forest Giant (level 14) runs the stock hunt, which has
            // no leash, so it chased the player ~1500 units to the
            // starting area and only the server backstop (75) yanked it
            // home, then it re-engaged and ran again. Apply the same
            // MOB_LEASH_RANGE break as rpgthink: mob or foe past the
            // line from home clears the fight and sends the mob home
            // (M_SEARCH). Aggression (aggro on sight) is untouched here;
            // the M_SLEEP visual-contact gate below stops it re-engaging
            // while the player is outside its perimeter.
            // Silent leash (Nov 18, Note 190): no conoutf. The old message
            // spammed the screen every tick on load: the giant sits at home
            // (mob 0) while the player spawns ~1700 away, so the foe-side
            // check fired on every tick and the text lived on screen.
            // Also skipped while the mob is asleep (M_SLEEP): it is not in
            // a fight, the player is only its potential target, so there is
            // nothing to leash. Without this the stock think re-acquires the
            // player as enemy every tick and the leash fires again.
            if(d->state==CS_ALIVE && ai->monsterstate != M_SLEEP && rpg::mapactive(getclientmap()) && ai->rpghomeknown && ai->enemy)
            {
                float mobdh = vec(d->o.x - ai->rpghome.x, d->o.y - ai->rpghome.y, 0).magnitude();
                float foedh = vec(ai->enemy->o.x - ai->rpghome.x, ai->enemy->o.y - ai->rpghome.y, 0).magnitude();
                if(mobdh > rpg::MOB_LEASH_RANGE || foedh > rpg::MOB_LEASH_RANGE)
                {
                    ai->enemy = NULL;
                    ai->rpgfoe = NULL;
                    ai->transition(d, M_SEARCH, 1, 100, 100);
                }
            }

            if(d->state==CS_ALIVE)
            {
                if (!ai->enemy || ai->enemy->state==CS_DEAD)
                {
                    if (bestenemy && bestenemy->state == CS_ALIVE) ai->enemy = bestenemy;
                    else
                    {
                        float bestdist = 9999999999, dist = 0;
                        // Find best monsters
                        if(!monsterType.loyalty)
                        {
                            loopv(players)
                            {
                                if(players[i] && players[i]->isInfected())
                                {
                                    const ::monster::MonsterType &otherMonsterType = ::monster::getMonsterType(players[i]->getMonsterType());
                                    if (players[i]->state == CS_ALIVE && otherMonsterType.loyalty && (dist = d->o.squaredist(players[i]->o)) < bestdist)
                                    {
                                        ai->enemy = players[i];
                                        bestdist = dist;
                                    }
                                }
                            }
                        }
                        // Find best players
                        else
                        {
                            loopv(players)
                            {
                                bool isHuman = !players[i]->isInfected();
                                if(!isHuman) isHuman = !::monster::getMonsterType(players[i]->getMonsterType()).loyalty;
                                if (isHuman && players[i]->state == CS_ALIVE && (dist = d->o.squaredist(players[i]->o)) < bestdist)
                                {
                                    ai->enemy = players[i];
                                    bestdist = dist;
                                }
                            }
                        }
                        return;
                    }
                }

                if(ai->enemy && ai->enemy->state==CS_DEAD)
                {
                    ai->enemy = monsterType.loyalty ? (player1->isInfected() ? NULL : player1) : bestenemy;
                    ai->anger = 0;
                }

                if(ai->enemy)
                {
                    normalizeYaw(d->yaw, ai->targetyaw);
                    if(ai->targetyaw > d->yaw)             // slowly turn monster towards his target
                    {
                        d->yaw += curtime*0.5f;
                        if(ai->targetyaw < d->yaw) d->yaw = ai->targetyaw;
                    }
                    else
                    {
                        d->yaw -= curtime*0.5f;
                        if(ai->targetyaw > d->yaw) d->yaw = ai->targetyaw;
                    }
                    float dist = ai->enemy->o.dist(d->o);
                    if(ai->monsterstate != M_SLEEP) d->pitch = asin((ai->enemy->o.z - d->o.z) / dist) / RAD; 

                    if(d->blocked)                                                              // special case: if we run into scenery
                    {
                        d->blocked = false;
                        if(!rpg::mapactive(getclientmap()) && !rnd(20000/(monsterType.classInfo.maxspeed/4)))  // try to jump over obstackle (rare; never on RPG maps, Note 198)
                        {
                            d->jumping = true;
                        }
                        else if(!ai->wproute.empty())
                        {
                            // following a waypoint route: the blocker is usually a step or lip the graph crosses, so hop again rather than spinning off course
                            // (never on RPG maps, Note 198: mobs do not jump)
                            if(!rpg::mapactive(getclientmap()) && !rnd(3)) d->jumping = true;
                        }
                        else if(ai->trigger < lastmillis && (ai->monsterstate != M_HOME || !rnd(5)))  // search for a way around (common)
                        {
                            ai->targetyaw += 90+rnd(180);                                        // patented "random walk" AI pathfinding (tm) ;)
                            ai->transition(d, M_SEARCH, 1, 100, 1000);
                        }
                    }

                    float enemyyaw = -atan2(ai->enemy->o.x - d->o.x, ai->enemy->o.y - d->o.y)/RAD;

                    switch(ai->monsterstate)
                    {
                        case M_PAIN:
                        case M_ATTACKING:
                        case M_SEARCH:
                            if(ai->trigger < lastmillis) ai->transition(d, M_HOME, 1, 100, 200);
                            break;

                        case M_SLEEP:                       // state classic sp monster start in, wait for visual contact
                        {
                            if(editmode) break;
                            normalizeYaw(d->yaw, enemyyaw);
                            float angle = (float)fabs(enemyyaw-d->yaw);
                            // RPG perimeter gate (Nov 18, Notes 176/179): on
                            // RPG maps, a mob only aggros on sight while the
                            // player is inside its perimeter (MOB_LEASH_RANGE
                            // from its home). Without this the giant re-engages
                            // the moment the backstop sends it home, because
                            // the player is still visible from the clearing,
                            // and it runs the whole way back to the village.
                            // A mob that was damaged (monsterhurt) still
                            // aggros, so retaliation is untouched.
                            bool inperimeter = true;
                            if(rpg::mapactive(getclientmap()) && ai->rpghomeknown && ai->enemy && !monsterhurt)
                            {
                                float foedh = vec(ai->enemy->o.x - ai->rpghome.x, ai->enemy->o.y - ai->rpghome.y, 0).magnitude();
                                inperimeter = foedh <= rpg::MOB_LEASH_RANGE;
                            }
                            if(inperimeter && (dist<32                   // the better the angle to the player, the further the monster can see/hear
                                ||(dist<64 && angle<135)
                                ||(dist<128 && angle<90)
                                ||(dist<256 && angle<45)
                                || angle<10
                                || (monsterhurt && d->o.dist(monsterhurtpos)<128)))
                            {
                                vec target;
                                if(raycubelos(d->o, ai->enemy->o, target))
                                {
                                    ai->transition(d, M_HOME, 1, 500, 200);
                                    playsound(S_GRUNT1+rnd(2), &d->o);
                                }
                            }
                            break;
                        }

                        case M_AIMING:                      // this state is the delay between wanting to shoot and actually firing
                            if(ai->trigger < lastmillis && lastmillis-d->lastaction >= d->gunwait)
                            {
                                d->attacking = true;
                                if(rnd(100) < 20*aiSkillToLevel(d->ai.skill)) ai->attacktarget = ai->enemy->headpos();
                                // TEMP diagnostic (Nov 18 .141, Note 229): log
                                // melee bites so the giant fix can be verified
                                // from diaglog.txt. Throttled to 1/sec globally
                                // so a biting mob does not flood the log.
                                if(d->gunselect == WEAP_BITE || d->gunselect == WEAP_FIST)
                                {
                                    static int diag_bite_last = 0;
                                    if(totalmillis - diag_bite_last >= 1000)
                                    {
                                        diag_bite_last = totalmillis;
                                        float hd = ai->enemy ? vec(d->o.x - ai->enemy->o.x, d->o.y - ai->enemy->o.y, 0).magnitude() : -1;
                                        diaglog("rpg: bite '%s' hdist=%.1f enemy='%s'", monsterType.classInfo.name ? monsterType.classInfo.name : "?", hd, ai->enemy ? ai->enemy->name : "?");
                                    }
                                }
                                shoot(d, ai->attacktarget);
                                ai->transition(d, M_ATTACKING, !monsterType.loyalty, 600, 0);
                                ai->lastshot = lastmillis;
                            }
                            break;

                        case M_HOME:                        // monster has visual contact, heads straight for player and may want to shoot at any time
                        {
                            // Level 2 waypoint routing: with no line of sight to the enemy, steer along the
                            // waypoint graph instead of beelining into the wall between monster and player.
                            bool routed = false;
                            if(!ai->wproute.empty())
                            {
                                int node = ai->wproute.last();
                                if(iswaypoint(node))
                                {
                                    if(d->o.squaredist(waypoints[node].o) < (WAYPOINTRADIUS*1.5f)*(WAYPOINTRADIUS*1.5f))
                                    {
                                        ai->wproute.pop();          // reached this node, aim for the next one
                                        routed = !ai->wproute.empty();
                                    }
                                    else routed = true;
                                    if(routed)
                                    {
                                        node = ai->wproute.last();
                                        ai->targetyaw = -atan2(waypoints[node].o.x - d->o.x, waypoints[node].o.y - d->o.y)/RAD;
                                    }
                                }
                                else ai->wproute.setsize(0);
                            }
                            if(!routed) ai->targetyaw = enemyyaw;
                            if(ai->trigger < lastmillis)
                            {
                                vec target;
                                if(!raycubelos(d->o, ai->enemy->o, target))    // no visual contact anymore, route through the waypoint graph and keep homing
                                {
                                    if(lastmillis-ai->lastrouteplan >= 1000)    // replan at most once a second per monster
                                    {
                                        ai->lastrouteplan = lastmillis;
                                        int from = closestwaypoint(d->o, SIGHTMAX, true);
                                        int goal = closestwaypoint(ai->enemy->o, SIGHTMAX, true);
                                        if(iswaypoint(from) && iswaypoint(goal) && from != goal)
                                        {
                                            if(!route(d, from, goal, ai->wproute, obstacles)) ai->wproute.setsize(0);
                                        }
                                        else ai->wproute.setsize(0);    // no graph coverage here: classic beeline/search behaviour below
                                    }
                                    ai->transition(d, M_HOME, 1, 800, 500);
                                }
                                else 
                                {
                                    ai->wproute.setsize(0);
                                    bool melee = false, longrange = false;
                                    switch(d->gunselect)
                                    {
                                        case WEAP_BITE:
                                        case WEAP_FIST: melee = true; break;
                                        case WEAP_SNIPER: longrange = true; break;
                                    }

                                    // Nov 18 (.141, Note 229): melee range is
                                    // horizontal, not 3D. The Forest Giant's
                                    // eye is 50u up vs the player's 15u, so
                                    // the 3D eye-to-eye distance is >=35u even
                                    // when the bodies touch (19.1u apart at
                                    // collision: giant radius 15 + player
                                    // 4.1) — the dist<20 gate could never fire
                                    // and the bite ray could never reach. The
                                    // horizontal gap is the real closing
                                    // distance; the bite ray (range 48) covers
                                    // the 3D eye-to-eye span.
                                    float hdist = vec(d->o.x - ai->enemy->o.x, d->o.y - ai->enemy->o.y, 0).magnitude();
                                    // the closer the monster is the more likely he wants to shoot, 
                                    if((!melee || hdist<20) && !rnd(longrange ? (int)dist/12+1 : min((int)dist/12+1,6)) && ai->enemy->state==CS_ALIVE)      // get ready to fire
                                    { 
                                        ai->attacktarget = target;
                                        ai->transition(d, M_AIMING, 0, monsterType.lag, 10);
                                    }
                                    else                                                        // track player some more
                                    {
                                        ai->transition(d, M_HOME, 1, monsterType.rate, 0);
                                    }
                                }
                            }
                        }
                        break;
                    }
                }
#if 1
                // TODO - Done elsewhere now monsters are players?
                if(d->move || d->maymove() || (ai->stacked && (ai->stacked->state!=CS_ALIVE || ai->stackpos != ai->stacked->o)))
                {
                    vec pos = d->feetpos();
//                     loopv(teleports) // equivalent of player entity touch, but only teleports are used
//                     {
//                         entity &e = *entities::ents[teleports[i]];
//                         float dist = e.o.dist(pos);
//                         if(dist<16) entities::teleport(teleports[i], d);
//                     }

                    if(physsteps > 0) ai->stacked = NULL;
                    moveplayer(d, 1, true);        // use physics to move monster
                    entities::checkitems(d);
                    if(cmode) cmode->checkitems(d);
                }

                if (d->onfire && lastmillis-d->lastburnpain >= 1000)
                {
                    d->lastburnpain = lastmillis;
                    float mdagamemul = 1.5; // does more damage to monsters than players
                    // TODO d->monsterpain((int)((float)weapons[WEAP_FLAMEJET].damage*(mdagamemul*1000.f/max((float)(lastmillis-d->burnmillis), 1000.f))), (fpsent*)d->fireattacker);
                }
#endif
            }
            else if(d->state==CS_DEAD)
            {
                if(lastmillis-d->lastpain<2000)
                {
                    d->move = d->strafe = 0;
                    moveplayer(d, 1, true);
                }
                else
                {
                    if (bestenemy == d) bestenemy = NULL;
                    return;
                }
            }
            //TODO: move to better location
            if (d->onfire && (lastmillis-d->burnmillis > 4000 || d->inwater))
            {
                d->onfire = false;
            }
        }

        void MonsterAi::clear()     // called after map start or when toggling edit mode to reset/spawn all monsters to initial state
        {
            removetrackedparticles();
            removetrackeddynlights();
            cleardynentcache();
            monsterhurt = false;

            if(m_classicsp)
            {
                loopv(entities::ents)
                {
                    extentity &e = *entities::ents[i];
                    if(e.type!=MONSTER) continue;
                    //monster *m = new monster(e.attr2, e.attr1, e.attr3, M_SLEEP, 100, 0);  
                    // TODO
                    // monsters.add(m);
//                     m->o = e.o;
//                     entinmap(m);
//                     updatedynentcache(m);
                }
            }
        }
        
        void MonsterAi::startThink()
        {
            monsterwashurt = monsterhurt;
        }

        void MonsterAi::endThink()
        {
            if(monsterwashurt)
            {
                monsterhurt = false;
                monsterwashurt = false;
            }
        }

        static const char * const stnames [] = {
            "M_NONE", "M_SEARCH", "M_HOME", "M_ATTACKING", "M_PAIN", "M_SLEEP", "M_AIMING"
        };
        
        void MonsterAi::render(fpsent *d, float i)
        {
            MonsterAiInfo *ai = getMonsterState(d);
            vec pos = d->abovehead();
            pos.z += 3;

            defformatstring(s)(
                "%s%s (%d ms %d) (%i)",
                "\fg",
                stnames[ai->monsterstate],
                lastmillis - ai->trigger,
                lastmillis - ai->lastshot,
                d->ai.skill
            );

            particle_textcopy(pos, s, PART_TEXT, 1);
            pos.z += 2;
        }
    }
}
#endif