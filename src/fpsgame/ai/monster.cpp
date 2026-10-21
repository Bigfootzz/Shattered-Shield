// monster.h: implements AI for single player monsters, currently client only
#include "game.h"
#include "rpg/rpg.h" // Shattered Shield: RPG map gate + mob-behaviour constants

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
            { {WEAP_BITE, WEAP_NONE, WEAP_NONE, WEAP_NONE}, 90,     0,          0,      100,        "imp",      0},
               0,       0,      100,    400,    1,          S_PAINB,    S_DEATHB, 0,
            { "playermodels/imp",               NULL,       NULL,       NULL,       NULL,   NULL,   { NULL, NULL, NULL },   "imp",          NULL,       NULL,       "hudguns/zombie",   false,      true,       3.2f,   14.0f,      1.0f,       0 },
        },
        {
            // Bestiary (Shattered Shield): slow brute, tankier than a zombie,
            // below the champion. freq 0 for the same reason as the imp.
            { {WEAP_BITE, WEAP_NONE, WEAP_NONE, WEAP_NONE}, 300,    0,          0,      58,         "puglin",   0},
               0,       0,      100,    400,    1,          S_PAINH,    S_DEATHH, 0,
            { "playermodels/puglin",            NULL,       NULL,       NULL,       NULL,   NULL,   { NULL, NULL, NULL },   "puglin",       NULL,       NULL,       "hudguns/zombie",   false,      true,       4.2f,   13.0f,      1.0f,       0 },
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
    // 10. Every current world type is <= 10, so all ambient mobs (and the
    // Rotting Champion, a zombie variant) are passive until engaged under
    // the Oct 20 mob-behaviour rules. Data-only: stock modes never read it.
    static const int monsterlevels[] = { 2, 2, 2, 2, 2, 2, 2, 1, 10, 4, 6 };

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
        loopi(NUMMONSTERTYPES)
        {
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
            trigger = lastmillis+n-d->ai.skill*(n/16)+rnd(r+1);
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
        // so attacks land through the exact same shoot() plumbing. No
        // waypoint routing here: the world fields are open ground, and
        // the blocked-hop below covers lips and steps.
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
                    // The anchor z is a feet level; the origin rides at eye
                    // height. Placing at the raw anchor would embed the mob
                    // in the terrain and it would fall through the world.
                    d->o = vec(ai->rpghome.x, ai->rpghome.y, ai->rpghome.z + d->eyeheight + 2.0f);
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
            if(ai->enemy && (RPGDH(d->o) > rpg::MOB_LEASH_RANGE || RPGDH(ai->enemy->o) > rpg::MOB_LEASH_RANGE))
            {
                conoutf("rpgbehav: '%s' leashes (mob %.0f / foe %.0f from home, limit %.0f)", d->name, RPGDH(d->o), RPGDH(ai->enemy->o), rpg::MOB_LEASH_RANGE);
                ai->enemy = NULL;
                ai->rpgfoe = NULL;
                ai->transition(d, M_SEARCH, 1, 100, 100); // M_SEARCH here = returning home
            }

            // Steering: face the foe, or face home when returning.
            vec dest;
            bool havedest = false;
            if(ai->enemy) { dest = ai->enemy->o; havedest = true; }
            else if(RPGDH(d->o) > rpg::MOB_ROAM_RADIUS) { dest = ai->rpghome; havedest = true; }
            if(havedest)
            {
                ai->targetyaw = -atan2(dest.x - d->o.x, dest.y - d->o.y)/RAD;
                normalizeYaw(d->yaw, ai->targetyaw);
                if(ai->targetyaw > d->yaw) { d->yaw += curtime*0.5f; if(ai->targetyaw < d->yaw) d->yaw = ai->targetyaw; }
                else { d->yaw -= curtime*0.5f; if(ai->targetyaw > d->yaw) d->yaw = ai->targetyaw; }
                float dd = d->o.dist(dest);
                if(dd > 0.01f) d->pitch = asin(clamp((dest.z - d->o.z)/dd, -1.0f, 1.0f))/RAD;
            }

            if(d->blocked) // ran into scenery: hop it (returning or chasing)
            {
                d->blocked = false;
                if(!rnd(20000/max(monsterType.classInfo.maxspeed/4, 1))) d->jumping = true;
                else if(!ai->enemy && !rnd(3)) d->jumping = true;
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
                    d->move = dist > 20 ? 1 : 0;
                    if(ai->trigger < lastmillis)
                    {
                        vec target;
                        if(raycubelos(d->o, ai->enemy->o, target))
                        {
                            if(dist < 20 && ai->enemy->state == CS_ALIVE) // bite trigger, same gate as stock
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
                default: // idle at home: stand within the roam radius
                    d->move = 0;
                    if(ai->enemy) ai->transition(d, M_HOME, 1, 100, 100);
                    else if(RPGDH(d->o) > rpg::MOB_ROAM_RADIUS) ai->transition(d, M_SEARCH, 1, 100, 100);
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
                        if(!rnd(20000/(monsterType.classInfo.maxspeed/4)))                            // try to jump over obstackle (rare)
                        {
                            d->jumping = true;
                        }
                        else if(!ai->wproute.empty())
                        {
                            // following a waypoint route: the blocker is usually a step or lip the graph crosses, so hop again rather than spinning off course
                            if(!rnd(3)) d->jumping = true;
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
                            if(dist<32                   // the better the angle to the player, the further the monster can see/hear
                                ||(dist<64 && angle<135)
                                ||(dist<128 && angle<90)
                                ||(dist<256 && angle<45)
                                || angle<10
                                || (monsterhurt && d->o.dist(monsterhurtpos)<128))
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

                                    // the closer the monster is the more likely he wants to shoot, 
                                    if((!melee || dist<20) && !rnd(longrange ? (int)dist/12+1 : min((int)dist/12+1,6)) && ai->enemy->state==CS_ALIVE)      // get ready to fire
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