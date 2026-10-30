#include "game.h"
#include "rpg/rpg.h"

namespace ai
{
    namespace bot
    {
        using namespace game;

        ICOMMAND(addbot, "i", (int *skill), requestAdd(AI_TYPE_BOT, *skill));
        ICOMMAND(delbot, "", (), requestDel(AI_TYPE_BOT));
        ICOMMAND(botlimit, "i", (int *n), addmsg(N_BOTLIMIT, "ri", *n));
        ICOMMAND(botbalance, "i", (int *n), addmsg(N_BOTBALANCE, "ri", *n));

        extern vector<char *> botnames;

        BotAiInfo *getBotState(fpsent *d)
        {
            ASSERT(d->ai.local);
            ASSERT(d->ai.type == AI_TYPE_BOT);
            ASSERT(d->ai.data);
            return (BotAiInfo *)d->ai.data;
        }

        bool cansee(fpsent *d, vec &x, vec &y, vec &targ)
        {
            BotAiInfo *ai = getBotState(d);
            aistate &b = ai->getstate();
            if(canmove(d) && b.type != AI_S_WAIT)
            {
                return getsight(x, d->yaw, d->pitch, y, targ, ai->views[2], ai->views[0], ai->views[1]);
            }
            return false;
        }

        vec getaimpos(fpsent *d, fpsent *e)
        {
            vec o = e->o;
            if(d->gunselect == WEAP_ROCKETL) o.z += (e->aboveeye*0.2f)-(0.8f*d->eyeheight);
            else if(d->gunselect != WEAP_GRENADIER) o.z += (e->aboveeye-e->eyeheight)*0.5f;
            if(d->ai.skill <= 100)
            {
                BotAiInfo *ai = getBotState(d);
                if(lastmillis >= ai->lastaimrnd)
                {
                    //TODO: move to weapons.h
                    const int aiskew[NUMWEAPS] = { 1, 10, 50, 5, 20, 1, 100, 10, 10, 10, 1, 1 };
                    #define rndaioffset(r) ((rnd(int(r*aiskew[d->gunselect]*2)+1)-(r*aiskew[d->gunselect]))*(1.f/float(max(d->ai.skill, 1))))
                    loopk(3) ai->aimrnd[k] = rndaioffset(e->radius);
                    int dur = (d->ai.skill+10)*10;
                    ai->lastaimrnd = lastmillis+dur+rnd(dur);
                }
                loopk(3) o[k] += ai->aimrnd[k];
            }
            return o;
        }

        bool hasrange(fpsent *d, fpsent *e, int weap)
        {
            if(!e) return true;
            if(targetable(d, e))
            {
                vec ep = getaimpos(d, e);
                float dist = ep.squaredist(d->headpos());
                if(weaprange(d, weap, dist)) return true;
            }
            return false;
        }

        bool canshoot(fpsent *d)
        {
            return !getBotState(d)->becareful && d->ammo[d->gunselect] > 0 && lastmillis - d->lastaction >= d->gunwait;
        }
        
        bool hastarget(fpsent *d, aistate &b, fpsent *e, float yaw, float pitch, float dist)
        { // add margins of error
            if(weaprange(d, d->gunselect, dist) || (d->ai.skill <= 100 && !rnd(d->ai.skill)))
            {
                if(WEAP_IS_MELEE(d->gunselect)) return true;
                BotAiInfo *ai = getBotState(d);
                float skew = clamp(float(lastmillis-ai->enemymillis)/float((d->ai.skill*weapons[d->gunselect].attackdelay/200.f)), 0.f, weapons[d->gunselect].projspeed ? 0.25f : 1e16f),
                offy = yaw-d->yaw, offp = pitch-d->pitch;
                if(offy > 180) offy -= 360;
                else if(offy < -180) offy += 360;
                if(fabs(offy) <= ai->views[0]*skew && fabs(offp) <= ai->views[1]*skew) return true;
            }
            return false;
        }

        void BotAi::destroy(fpsent *d)
        {
            d->ai.local = false;
            ASSERT(d->ai.data);
            delete (BotAiInfo *)d->ai.data;
            d->ai.data = NULL;
        }

        void BotAi::create(fpsent *d)
        {
            d->ai.local = true;
            BotAiInfo *ai = new BotAiInfo;
            d->ai.data = ai;

            ai->views[0] = viewfieldx(d->ai.skill);
            ai->views[1] = viewfieldy(d->ai.skill);
            ai->views[2] = viewdist(d->ai.skill);
        }
        
        bool checkothers(vector<int> &targets, fpsent *d, int state, int targtype, int target, bool teams, int *members)
        { // checks the states of other ai for a match
            targets.setsize(0);
            loopv(players)
            {
                fpsent *e = players[i];
                if(targets.find(e->clientnum) >= 0) continue;
                if(teams && d && !isteam(d->team, e->team)) continue;
                if(members) (*members)++;
                if(e == d || !e->ai.local || e->ai.type != AI_TYPE_BOT || e->state != CS_ALIVE) continue;
                aistate &b = getBotState(e)->getstate();
                if(state >= 0 && b.type != state) continue;
                if(target >= 0 && b.target != target) continue;
                if(targtype >=0 && b.targtype != targtype) continue;
                targets.add(e->clientnum);
            }
            return !targets.empty();
        }
        
        bool makeroute(fpsent *d, aistate &b, int node, bool changed, int retries)
        {
            if(!iswaypoint(d->lastnode)) return false;
            BotAiInfo *ai = getBotState(d);
            if(changed && ai->route.length() > 1 && ai->route[0] == node) return true;
            if(route(d, d->lastnode, node, ai->route, obstacles, retries))
            {
                b.override = false;
                return true;
            }
            // retry fails: 0 = first attempt, 1 = try ignoring obstacles, 2 = try ignoring prevnodes too
            if(retries <= 1) return makeroute(d, b, node, false, retries+1);
            return false;
        }
        
        bool makeroute(fpsent *d, aistate &b, const vec &pos, bool changed, int retries)
        {
            int node = closestwaypoint(pos, SIGHTMIN, true);
            return makeroute(d, b, node, changed, retries);
        }
        
        bool randomnode(fpsent *d, aistate &b, const vec &pos, float guard, float wander)
        {
            static vector<int> candidates;
            candidates.setsize(0);
            findwaypointswithin(pos, guard, wander, candidates);
            
            while(!candidates.empty())
            {
                int w = rnd(candidates.length()), n = candidates.removeunordered(w);
                if(n != d->lastnode && !getBotState(d)->hasprevnode(n) && !obstacles.find(n, d) && makeroute(d, b, n)) return true;
            }
            return false;
        }
        
        bool randomnode(fpsent *d, aistate &b, float guard, float wander)
        {
            return randomnode(d, b, d->feetpos(), guard, wander);
        }

        bool enemy(fpsent *d, aistate &b, const vec &pos, float guard = SIGHTMIN, int pursue = 0)
        {
            fpsent *t = NULL;
            vec dp = d->headpos();
            float mindist = guard*guard, bestdist = 1e16f;
            loopv(players)
            {
                fpsent *e = players[i];
                if(e == d || !targetable(d, e)) continue;
                vec ep = getaimpos(d, e);
                float dist = ep.squaredist(dp);
                if(dist < bestdist && (cansee(d, dp, ep) || dist <= mindist))
                {
                    t = e;
                    bestdist = dist;
                }
            }
            if(t && violence(d, b, t, pursue)) return true;
            return false;
        }
        
        bool patrol(fpsent *d, aistate &b, const vec &pos, float guard, float wander, int walk, bool retry)
        {
            vec feet = d->feetpos();
            if(walk == 2 || b.override || (walk && feet.squaredist(pos) <= guard*guard) || !makeroute(d, b, pos))
            { // run away and back to keep ourselves busy
                if(!b.override && randomnode(d, b, pos, guard, wander))
                {
                    b.override = true;
                    return true;
                }
                else if(getBotState(d)->route.empty())
                {
                    if(!retry)
                    {
                        b.override = false;
                        return patrol(d, b, pos, guard, wander, walk, true);
                    }
                    b.override = false;
                    return false;
                }
            }
            b.override = false;
            return true;
        }
        
        bool defend(fpsent *d, aistate &b, const vec &pos, float guard, float wander, int walk)
        {
            bool hasenemy = enemy(d, b, pos, wander, WEAP_IS_MELEE(d->gunselect));
            if(!walk)
            {
                if(d->feetpos().squaredist(pos) <= guard*guard)
                {
                    b.idle = hasenemy ? 2 : 1;
                    return true;
                }
                
                walk++;
            }
            return patrol(d, b, pos, guard, wander, walk);
        }
        
        bool violence(fpsent *d, aistate &b, fpsent *e, int pursue)
        {
            if(e && targetable(d, e))
            {
                BotAiInfo *ai = getBotState(d);
                if(pursue)
                {
                    if((b.targtype != AI_T_AFFINITY || !(pursue%2)) && makeroute(d, b, e->lastnode))
                        ai->switchstate(b, AI_S_PURSUE, AI_T_PLAYER, e->clientnum);
                    else if(pursue >= 3) return false; // can't pursue
                }
                if(ai->enemy != e->clientnum)
                {
                    ai->enemyseen = ai->enemymillis = lastmillis;
                    ai->enemy = e->clientnum;
                }
                return true;
            }
            return false;
        }
        
        bool target(fpsent *d, aistate &b, int pursue = 0, bool force = false, float mindist = 0.f)
        {
            static vector<fpsent *> hastried; hastried.setsize(0);
            vec dp = d->headpos();
            while(true)
            {
                float dist = 1e16f;
                fpsent *t = NULL;
                loopv(players)
                {
                    fpsent *e = players[i];
                    if(e == d || hastried.find(e) >= 0 || !targetable(d, e)) continue;
                    vec ep = getaimpos(d, e);
                    float v = ep.squaredist(dp);
                    if((!t || v < dist) && (mindist <= 0 || v <= mindist) && (force || cansee(d, dp, ep)))
                    {
                        t = e;
                        dist = v;
                    }
                }
                if(t)
                {
                    if(violence(d, b, t, pursue)) return true;
                    hastried.add(t);
                }
                else break;
            }
            return false;
        }

        void assist(fpsent *d, aistate &b, vector<interest> &interests, bool all, bool force)
        {
            loopv(players)
            {
                fpsent *e = players[i];
                if(e == d || (!all && e->ai.type != AI_TYPE_NONE) || !isteam(d->team, e->team)) continue;
                interest &n = interests.add();
                n.state = AI_S_DEFEND;
                n.node = e->lastnode;
                n.target = e->clientnum;
                n.targtype = AI_T_PLAYER;
                n.score = e->o.squaredist(d->o)/(hasgoodammo(d) ? 1e8f : (force ? 1e4f : 1e2f));
            }
        }
        
        static void tryitem(fpsent *d, extentity &e, int id, aistate &b, vector<interest> &interests, bool force = false)
        {
            float score = 0;
            switch(e.type)
            {
                case I_HEALTH: case I_HEALTH2: case I_HEALTH3:
                    if(d->health < min(d->ai.skill, 75)) score = 1e3f;
                    break;
                case I_QUAD: score = 1e3f; break;
                case I_GREENARMOUR: case I_YELLOWARMOUR:
                {
                    int atype = A_GREEN + e.type - I_GREENARMOUR;
                    if(atype > d->armourtype) score = atype == A_YELLOW ? 1e2f : 1e1f;
                    else if(d->armour < 50) score = 1e1f;
                    break;
                }
                case I_MORTAR: score = clamp(MORTAR_MAX_AMMO-d->ammo[WEAP_MORTAR], 0, MORTAR_MAX_AMMO)*1e2f; break;
                default:
                {
                    if(e.type >= I_AMMO && e.type <= I_AMMO4 && !d->hasmaxammo(e.type))
                    {
                        score = (e.type-I_AMMO)*1e2f;
                        // go get a weapon upgrade
                    }
                    break;
                }
            }
            if(score != 0)
            {
                interest &n = interests.add();
                n.state = AI_S_INTEREST;
                n.node = closestwaypoint(e.o, SIGHTMIN, true);
                n.target = id;
                n.targtype = AI_T_ENTITY;
                n.score = d->feetpos().squaredist(e.o)/(force ? -1 : score);
            }
        }
        
        void items(fpsent *d, aistate &b, vector<interest> &interests, bool force = false)
        {
            loopv(entities::ents)
            {
                extentity &e = *(extentity *)entities::ents[i];
                if(!e.spawned || !d->canpickup(e.type)) continue;
                tryitem(d, e, i, b, interests, force);
            }
        }
        
        static vector<int> targets;
        
        bool parseinterests(fpsent *d, aistate &b, vector<interest> &interests, bool override, bool ignore)
        {
            while(!interests.empty())
            {
                int q = interests.length()-1;
                loopi(interests.length()-1) if(interests[i].score < interests[q].score) q = i;
                interest n = interests.removeunordered(q);
                bool proceed = true;
                if(!ignore) switch(n.state)
                {
                case AI_S_DEFEND: // don't get into herds
                {
                    int members = 0;
                    proceed = !checkothers(targets, d, n.state, n.targtype, n.target, true, &members) && members > 1;
                    break;
                }
                default: break;
                }
                if(proceed && makeroute(d, b, n.node))
                {
                    getBotState(d)->switchstate(b, n.state, n.targtype, n.target);
                    return true;
                }
            }
            return false;
        }
        
        bool find(fpsent *d, aistate &b, bool override = false)
        {
            static vector<interest> interests;
            interests.setsize(0);
            if(!m_noitems)
            {
                if((!m_noammo && !hasgoodammo(d)) || d->health < min(d->ai.skill - 15, 75))
                    items(d, b, interests);
                else
                {
                    static vector<int> nearby;
                    nearby.setsize(0);
                    findents(I_AMMO, I_QUAD, false, d->feetpos(), vec(32, 32, 24), nearby);
                    loopv(nearby)
                    {
                        int id = nearby[i];
                        extentity &e = *(extentity *)entities::ents[id];
                        if(d->canpickup(e.type)) tryitem(d, e, id, b, interests);
                    }
                }
            }

            BotGameMode *gm = cmode ? cmode->getBotGameMode() : NULL;
            if(gm) gm->aifind(d, b, interests);

            if(m_teammode) assist(d, b, interests);
            return parseinterests(d, b, interests, override);
        }
        
        bool findassist(fpsent *d, aistate &b, bool override = false)
        {
            static vector<interest> interests;
            interests.setsize(0);
            assist(d, b, interests);
            while(!interests.empty())
            {
                int q = interests.length()-1;
                loopi(interests.length()-1) if(interests[i].score < interests[q].score) q = i;
                interest n = interests.removeunordered(q);
                bool proceed = true;
                switch(n.state)
                {
                    case AI_S_DEFEND: // don't get into herds
                    {
                        int members = 0;
                        proceed = !checkothers(targets, d, n.state, n.targtype, n.target, true, &members) && members > 1;
                        break;
                    }
                    default: break;
                }
                if(proceed && makeroute(d, b, n.node))
                {
                    getBotState(d)->switchstate(b, n.state, n.targtype, n.target);
                    return true;
                }
            }
            return false;
        }

        bool damaged(fpsent *d, fpsent *e)
        {
            if(d->ai.local && d->ai.type == AI_TYPE_BOT && canmove(d) && targetable(d, e)) // see if this ai is interested in a grudge
            {
                aistate &b = getBotState(d)->getstate();
                if(violence(d, b, e, WEAP_IS_MELEE(d->gunselect))) return true;
            }
            if(checkothers(targets, d, AI_S_DEFEND, AI_T_PLAYER, d->clientnum, true))
            {
                loopv(targets)
                {
                    fpsent *t = getclient(targets[i]);
                    if(!t->ai.local || !canmove(t) || !targetable(t, e)) continue;
                    aistate &c = getBotState(t)->getstate();
                    if(violence(t, c, e, WEAP_IS_MELEE(d->gunselect))) return true;
                }
            }
            return false;
        }

        void BotAi::spawned(fpsent* d)
        {
            if(d->ai.local)
            {
                BotAiInfo *ai = getBotState(d);
                ai->clearsetup();
                ai->reset(true);
                ai->lastrun = lastmillis;
                if(m_insta) ai->weappref = WEAP_CROSSBOW;
                else
                {
                    if(forcegun >= 0 && forcegun < NUMWEAPS) ai->weappref = forcegun;
                    else if(m_noammo) ai->weappref = -1;
                    else
                    {
                        ai->weappref = getRandomWeapon(d);
                    }
                }
                vec dp = d->headpos();
                findorientation(dp, d->yaw, d->pitch, ai->target);
            }
        }

        void BotAi::killed(fpsent *d, fpsent *e)
        {
            if(d->ai.local && d->ai.type == AI_TYPE_BOT) getBotState(d)->reset();
        }

        void BotAi::itemSpawned(fpsent *d, int ent)
        {
            BotAiInfo *ai = getBotState(d);
            bool wantsitem = false;
            switch(entities::ents[ent]->type)
            {
                case I_HEALTH: case I_HEALTH2: case I_HEALTH3: wantsitem = badhealth(d); break;
                case I_GREENARMOUR: case I_YELLOWARMOUR: case I_QUAD: break;
                case I_MORTAR: wantsitem = d->ammo[WEAP_MORTAR]<MORTAR_MAX_AMMO; break;
                default:
                {
                    //itemstat &is = itemstats[entities::ents[ent]->type-I_AMMO];
                    wantsitem = !d->hasmaxammo();
                    break;
                }
            }
            if(wantsitem)
            {
                aistate &b = ai->getstate();
                if(b.targtype == AI_T_AFFINITY) return;
                if(b.type == AI_S_INTEREST && b.targtype == AI_T_ENTITY)
                {
                    if(entities::ents.inrange(b.target))
                    {
                        if(d->o.squaredist(entities::ents[ent]->o) < d->o.squaredist(entities::ents[b.target]->o))
                            ai->switchstate(b, AI_S_INTEREST, AI_T_ENTITY, ent);
                    }
                    return;
                }
                ai->switchstate(b, AI_S_INTEREST, AI_T_ENTITY, ent);
            }
        }

        bool check(fpsent *d, aistate &b)
        {
            BotGameMode *gm = cmode ? cmode->getBotGameMode() : NULL;
            if(gm && gm->aicheck(d, b)) return true;
            return false;
        }
        
        int dowait(fpsent *d, aistate &b)
        {
            BotAiInfo *ai = getBotState(d);
            ai->clear(true); // ensure they're clean
            if(check(d, b) || find(d, b)) return 1;
            if(target(d, b, 4, false)) return 1;
            if(target(d, b, 4, true)) return 1;
            if(randomnode(d, b, SIGHTMIN, 1e16f))
            {
                ai->switchstate(b, AI_S_INTEREST, AI_T_NODE, ai->route[0]);
                return 1;
            }
            return 0; // but don't pop the state
        }
        
        int dodefend(fpsent *d, aistate &b)
        {
            if(d->state == CS_ALIVE)
            {
                switch(b.targtype)
                {
                    case AI_T_NODE:
                        if(check(d, b)) return 1;
                        if(iswaypoint(b.target)) return defend(d, b, waypoints[b.target].o) ? 1 : 0;
                        break;
                    case AI_T_ENTITY:
                        if(check(d, b)) return 1;
                        if(entities::ents.inrange(b.target)) return defend(d, b, entities::ents[b.target]->o) ? 1 : 0;
                        break;
                    case AI_T_AFFINITY:
                    {
                        BotGameMode *gm = cmode ? cmode->getBotGameMode() : NULL;
                        if(gm) return gm->aidefend(d, b) ? 1 : 0;
                        break;
                    }
                    case AI_T_PLAYER:
                    {
                        if(check(d, b)) return 1;
                        fpsent *e = getclient(b.target);
                        if(e && e->state == CS_ALIVE) return defend(d, b, e->feetpos()) ? 1 : 0;
                        break;
                    }
                    default: break;
                }
            }
            return 0;
        }
        
        int dointerest(fpsent *d, aistate &b)
        {
            if(d->state != CS_ALIVE) return 0;
            switch(b.targtype)
            {
                case AI_T_NODE: // this is like a wait state without sitting still..
                    if(check(d, b) || find(d, b)) return 1;
                    if(target(d, b, 4, true)) return 1;
                    if(iswaypoint(b.target) && vec(waypoints[b.target].o).sub(d->feetpos()).magnitude() > CLOSEDIST)
                        return makeroute(d, b, waypoints[b.target].o) ? 1 : 0;
                    break;
                case AI_T_ENTITY:
                    if(entities::ents.inrange(b.target))
                    {
                        extentity &e = *(extentity *)entities::ents[b.target];
                        if(!e.spawned || e.type < I_AMMO || e.type > I_AMMO4 || d->hasmaxammo(e.type)) return 0;
                        //if(d->feetpos().squaredist(e.o) <= CLOSEDIST*CLOSEDIST)
                        //{
                        //    b.idle = 1;
                        //    return true;
                        //}
                        return makeroute(d, b, e.o) ? 1 : 0;
                    }
                    break;
            }
            return 0;
        }
        
        int dopursue(fpsent *d, aistate &b)
        {
            if(d->state == CS_ALIVE)
            {
                switch(b.targtype)
                {
                    case AI_T_NODE:
                    {
                        if(check(d, b)) return 1;
                        if(iswaypoint(b.target))
                            return defend(d, b, waypoints[b.target].o) ? 1 : 0;
                        break;
                    }
                    case AI_T_AFFINITY:
                    {
                        BotGameMode *gm = cmode ? cmode->getBotGameMode() : NULL;
                        if(gm) return gm->aipursue(d, b) ? 1 : 0;
                        break;
                    }
                    
                    case AI_T_PLAYER:
                    {
                        //if(check(d, b)) return 1;
                        fpsent *e = getclient(b.target);
                        if(e && e->state == CS_ALIVE)
                        {
                            float guard = SIGHTMIN, wander = weapons[d->gunselect].range;
                            if(WEAP_IS_MELEE(d->gunselect)) guard = 0.f;
                            return patrol(d, b, e->feetpos(), guard, wander) ? 1 : 0;
                        }
                        break;
                    }
                    default: break;
                }
            }
            return 0;
        }
        
        int closenode(fpsent *d)
        {
            vec pos = d->feetpos();
            int node1 = -1, node2 = -1;
            float mindist1 = CLOSEDIST*CLOSEDIST, mindist2 = CLOSEDIST*CLOSEDIST;
            BotAiInfo *ai = getBotState(d);
            loopv(ai->route) if(iswaypoint(ai->route[i]))
            {
                vec epos = waypoints[ai->route[i]].o;
                float dist = epos.squaredist(pos);
                if(dist > FARDIST*FARDIST) continue;
                int entid = obstacles.remap(d, ai->route[i], epos);
                if(entid >= 0)
                {
                    if(entid != i) dist = epos.squaredist(pos);
                    if(dist < mindist1) { node1 = i; mindist1 = dist; }
                }
                else if(dist < mindist2) { node2 = i; mindist2 = dist; }
            }
            return node1 >= 0 ? node1 : node2;
        }
        
        int wpspot(fpsent *d, int n, bool check = false)
        {
            if(iswaypoint(n)) loopk(2)
            {
                vec epos = waypoints[n].o;
                int entid = obstacles.remap(d, n, epos, k!=0);
                if(iswaypoint(entid))
                {
                    BotAiInfo *ai = getBotState(d);
                    ai->spot = epos;
                    ai->targnode = entid;
                    return !check || d->feetpos().squaredist(epos) > MINWPDIST*MINWPDIST ? 1 : 2;
                }
            }
            return 0;
        }
        
        int randomlink(fpsent *d, int n)
        {
            if(iswaypoint(n) && waypoints[n].haslinks())
            {
                waypoint &w = waypoints[n];
                static vector<int> linkmap; linkmap.setsize(0);
                loopi(MAXWAYPOINTLINKS)
                {
                    if(!w.links[i]) break;
                    BotAiInfo *ai = getBotState(d);
                    if(iswaypoint(w.links[i]) && !ai->hasprevnode(w.links[i]) && ai->route.find(w.links[i]) < 0)
                        linkmap.add(w.links[i]);
                }
                if(!linkmap.empty()) return linkmap[rnd(linkmap.length())];
            }
            return -1;
        }
        
        bool anynode(fpsent *d, aistate &b, int len = AiInfo::NUMPREVNODES)
        {
            if(iswaypoint(d->lastnode)) loopk(2)
            {
                BotAiInfo *ai = getBotState(d);
                ai->clear(k ? true : false);
                int n = randomlink(d, d->lastnode);
                if(wpspot(d, n))
                {
                    ai->route.add(n);
                    ai->route.add(d->lastnode);
                    loopi(len)
                    {
                        n = randomlink(d, n);
                        if(iswaypoint(n)) ai->route.insert(0, n);
                        else break;
                    }
                    return true;
                }
            }
            return false;
        }
        
        bool checkroute(fpsent *d, int n)
        {
            BotAiInfo *ai = getBotState(d);
            if(ai->route.empty() || !ai->route.inrange(n)) return false;
            int last = ai->lastcheck ? lastmillis-ai->lastcheck : 0;
            if(last < 500 || n < 3) return false; // route length is too short
            ai->lastcheck = lastmillis;
            int w = iswaypoint(d->lastnode) ? d->lastnode : ai->route[n], c = min(n-1, AiInfo::NUMPREVNODES);
            loopj(c) // check ahead to see if we need to go around something
            {
                int p = n-j-1, v = ai->route[p];
                if(ai->hasprevnode(v) || obstacles.find(v, d)) // something is in the way, try to remap around it
                {
                    int m = p-1;
                    if(m < 3) return false; // route length is too short from this point
                    loopirev(m)
                    {
                        int t = ai->route[i];
                        if(!ai->hasprevnode(t) && !obstacles.find(t, d))
                        {
                            static vector<int> remap; remap.setsize(0);
                            if(route(d, w, t, remap, obstacles))
                            { // kill what we don't want and put the remap in
                                while(ai->route.length() > i) ai->route.pop();
                                loopvk(remap) ai->route.add(remap[k]);
                                return true;
                            }
                            return false; // we failed
                        }
                    }
                    return false;
                }
            }
            return false;
        }
        
        bool hunt(fpsent *d, aistate &b)
        {
            BotAiInfo *ai = getBotState(d);
            if(!ai->route.empty())
            {
                int n = closenode(d);
                if(ai->route.inrange(n) && checkroute(d, n)) n = closenode(d);
                if(ai->route.inrange(n))
                {
                    if(!n)
                    {
                        switch(wpspot(d, ai->route[n], true))
                        {
                            case 2: ai->clear(false);
                            case 1: return true; // not close enough to pop it yet
                            case 0: default: break;
                        }
                    }
                    else
                    {
                        while(ai->route.length() > n+1) ai->route.pop(); // waka-waka-waka-waka
                        int m = n-1; // next, please!
                        if(ai->route.inrange(m) && wpspot(d, ai->route[m])) return true;
                    }
                }
            }
            b.override = false;
            return anynode(d, b);
        }

        void jumpto(fpsent *d, aistate &b, const vec &pos)
        {
            vec off = vec(pos).sub(d->feetpos()), dir(off.x, off.y, 0);
            BotAiInfo *ai = getBotState(d);
            bool sequenced = ai->blockseq || ai->targseq, offground = d->timeinair && !d->inwater,
            jump = !offground && lastmillis >= ai->jumpseed && (sequenced || off.z >= JUMPMIN || lastmillis >= ai->jumprand);
            if(jump)
            {
                vec old = d->o;
                d->o = vec(pos).add(vec(0, 0, d->eyeheight));
                if(collide(d, vec(0, 0, 1))) jump = false;
                d->o = old;
                if(jump)
                {
                    float radius = 18*18;
                    loopv(entities::ents) if(entities::ents[i]->type == JUMPPAD)
                    {
                        fpsentity &e = *(fpsentity *)entities::ents[i];
                        if(e.o.squaredist(pos) <= radius) { jump = false; break; }
                    }
                }
            }
            if(jump)
            {
                d->jumping = true;
                int seed = (111-d->ai.skill)*(d->inwater ? 3 : 5);
                ai->jumpseed = lastmillis+seed+rnd(seed);
                seed *= b.idle ? 50 : 25;
                ai->jumprand = lastmillis+seed+rnd(seed);
            }
        }

        int process(fpsent *d, aistate &b)
        {
            int result = 0, stupify = d->ai.skill <= 10+rnd(15) ? rnd(d->ai.skill*1000) : 0, skmod = 101-d->ai.skill;
            BotAiInfo *ai = getBotState(d);
            float frame = d->ai.skill <= 100 ? float(lastmillis-ai->lastrun)/float(max(skmod,1)*10) : 1;
            vec dp = d->headpos();
            
            bool idle = b.idle == 1 || (stupify && stupify <= skmod);
            ai->dontmove = false;
            if(idle)
            {
                ai->lastaction = ai->lasthunt = lastmillis;
                ai->dontmove = true;
                ai->spot = vec(0, 0, 0);
            }
            else if(hunt(d, b))
            {
                getyawpitch(dp, vec(ai->spot).add(vec(0, 0, d->eyeheight)), ai->targyaw, ai->targpitch);
                ai->lasthunt = lastmillis;
            }
            else
            {
                idle = ai->dontmove = true;
                ai->spot = vec(0, 0, 0);
            }
            
            if(!ai->dontmove) jumpto(d, b, ai->spot);
            
            fpsent *e = getclient(ai->enemy);
            bool enemyok = e && targetable(d, e);
            if(!enemyok || d->ai.skill >= 50)
            {
                fpsent *f = (fpsent *)intersectclosest(dp, ai->target, d);
                if(f)
                {
                    if(targetable(d, f))
                    {
                        if(!enemyok) violence(d, b, f, WEAP_IS_MELEE(d->gunselect));
                        enemyok = true;
                        e = f;
                    }
                    else enemyok = false;
                }
                else if(!enemyok && target(d, b, WEAP_IS_MELEE(d->gunselect), false, SIGHTMIN))
                    enemyok = (e = getclient(ai->enemy)) != NULL;
            }
            if(enemyok)
            {
                vec ep = getaimpos(d, e);
                float yaw, pitch;
                getyawpitch(dp, ep, yaw, pitch);
                fixrange(yaw, pitch);
                bool insight = cansee(d, dp, ep), hasseen = ai->enemyseen && lastmillis-ai->enemyseen <= (d->ai.skill*10)+3000,
                quick = ai->enemyseen && lastmillis-ai->enemyseen <= (d->gunselect == WEAP_MG ? 300 : skmod)+30;
                if(insight) ai->enemyseen = lastmillis;
                if(idle || insight || hasseen || quick)
                {
                    float sskew = insight || d->ai.skill > 100 ? 1.5f : (hasseen ? 1.f : 0.5f);
                    if(insight && lockon(d, e, 16))
                    {
                        ai->targyaw = yaw;
                        ai->targpitch = pitch;
                        if(!idle) frame *= 2;
                        ai->becareful = false;
                    }
                    scaleyawpitch(d->yaw, d->pitch, yaw, pitch, frame, sskew);
                    if(insight || quick)
                    {
                        if(ai::canshoot(d, e) && hastarget(d, b, e, yaw, pitch, dp.squaredist(ep)))
                        {
                            d->attacking = true;
                            ai->lastaction = lastmillis;
                            result = 3;
                        }
                        else result = 2;
                    }
                    else result = 1;
                }
                else
                {
                    if(!ai->enemyseen || lastmillis-ai->enemyseen > (d->ai.skill*50)+3000)
                    {
                        ai->enemy = -1;
                        ai->enemyseen = ai->enemymillis = 0;
                    }
                    enemyok = false;
                    result = 0;
                }
            }
            else
            {
                if(!enemyok)
                {
                    ai->enemy = -1;
                    ai->enemyseen = ai->enemymillis = 0;
                }
                enemyok = false;
                result = 0;
            }
            
            fixrange(ai->targyaw, ai->targpitch);
            if(!result) scaleyawpitch(d->yaw, d->pitch, ai->targyaw, ai->targpitch, frame*0.25f, 1.f);
            
            if(ai->becareful && d->physstate == PHYS_FALL)
            {
                float offyaw, offpitch;
                vec v = vec(d->vel).normalize();
                vectoyawpitch(v, offyaw, offpitch);
                offyaw -= d->yaw; offpitch -= d->pitch;
                if(fabs(offyaw)+fabs(offpitch) >= 135) ai->becareful = false;
                else if(ai->becareful) ai->dontmove = true;
            }
            else ai->becareful = false;
            
            if(ai->dontmove) d->move = d->strafe = 0;
            else
            { // our guys move one way.. but turn another?! :)
                const struct aimdir { int move, strafe, offset; } aimdirs[8] =
                {
                    {  1,  0,   0 },
                    {  1,  -1,  45 },
                    {  0,  -1,  90 },
                    { -1,  -1, 135 },
                    { -1,  0, 180 },
                    { -1, 1, 225 },
                    {  0, 1, 270 },
                    {  1, 1, 315 }
                };
                float yaw = ai->targyaw-d->yaw;
                while(yaw < 0.0f) yaw += 360.0f;
                while(yaw >= 360.0f) yaw -= 360.0f;
                int r = clamp(((int)floor((yaw+22.5f)/45.0f))&7, 0, 7);
                const aimdir &ad = aimdirs[r];
                d->move = ad.move;
                d->strafe = ad.strafe;
            }
            findorientation(dp, d->yaw, d->pitch, ai->target);
            return result;
        }

        bool request(fpsent *d, aistate &b)
        {
            BotAiInfo *ai = getBotState(d);
            fpsent *e = getclient(ai->enemy);
            if(!d->hasammo(d->gunselect) || !hasrange(d, e, d->gunselect) || (d->gunselect != ai->weappref && (!isgoodammo(d->gunselect) || d->hasammo(ai->weappref))))
            {
                static const int gunprefs[] = { WEAP_PREF_ORDER };
                int gun = -1;
                if(d->hasammo(ai->weappref) && hasrange(d, e, ai->weappref)) gun = ai->weappref;
                else
                {
                    loopi(sizeof(gunprefs)/sizeof(gunprefs[0])) if(d->hasammo(gunprefs[i]) && hasrange(d, e, gunprefs[i]))
                    {
                        gun = gunprefs[i];
                        break;
                    }
                }
                if(gun >= 0 && gun != d->gunselect) gunselect(gun, d);
            }
            return process(d, b) >= 2;
        }
        
        void timeouts(fpsent *d, aistate &b)
        {
            BotAiInfo *ai = getBotState(d);
            if(d->blocked)
            {
                ai->blocktime += lastmillis-ai->lastrun;
                if(ai->blocktime > (ai->blockseq+1)*1000)
                {
                    ai->blockseq++;
                    switch(ai->blockseq)
                    {
                        case 1: case 2: case 3:
                            if(entities::ents.inrange(ai->targnode)) ai->addprevnode(ai->targnode);
                            ai->clear(false);
                            break;
                        case 4: ai->reset(true); break;
                        case 5: ai->reset(false); break;
                        case 6: default: suicide(d); return; break; // this is our last resort..
                    }
                }
            }
            else ai->blocktime = ai->blockseq = 0;
            
            if(ai->targnode == ai->targlast)
            {
                ai->targtime += lastmillis-ai->lastrun;
                if(ai->targtime > (ai->targseq+1)*1000)
                {
                    ai->targseq++;
                    switch(ai->targseq)
                    {
                        case 1: case 2: case 3:
                            if(entities::ents.inrange(ai->targnode)) ai->addprevnode(ai->targnode);
                            ai->clear(false);
                            break;
                        case 4: ai->reset(true); break;
                        case 5: ai->reset(false); break;
                        case 6: default: suicide(d); return; break; // this is our last resort..
                    }
                }
            }
            else
            {
                ai->targtime = ai->targseq = 0;
                ai->targlast = ai->targnode;
            }
            
            if(ai->lasthunt)
            {
                int millis = lastmillis-ai->lasthunt;
                if(millis <= 1000) { ai->tryreset = false; ai->huntseq = 0; }
                else if(millis > (ai->huntseq+1)*1000)
                {
                    ai->huntseq++;
                    switch(ai->huntseq)
                    {
                        case 1: ai->reset(true); break;
                        case 2: ai->reset(false); break;
                        case 3: default: suicide(d); return; break; // this is our last resort..
                    }
                }
            }
        }
        
        // Battlegrounds (v0.1.40, Slice 3): bots cast RPG abilities,
        // never FPS guns, on battleground maps. Slice 3 bots are Magi:
        // Spark Bolt (id 1, 120 range, instant) at the sighted enemy.
        // The server re-validates everything (class, mana, cooldown,
        // range, line of sight, enemy team), so this is only a trigger:
        // a cast that would fail is dropped server-side. Throttled
        // locally so one bot does not spam a message every think tick.
        void bg_botcast(fpsent *d)
        {
            BotAiInfo *ai = getBotState(d);
            if(lastmillis - ai->lastbgcast < 1500) return;
            fpsent *e = ai->enemy >= 0 ? getclient(ai->enemy) : NULL;
            if(!e || e->state != CS_ALIVE || isteam(d->team, e->team)) return;
            if(e->o.squaredist(d->o) > 115*115) return; // Spark Bolt range 120, small margin
            ai->lastbgcast = lastmillis;
            addmsg(N_RPG_CAST, "rci3", d, 1, int(d->yaw), e->clientnum);
        }

        void logic(fpsent *d, aistate &b, bool run)
        {
            bool allowmove = canmove(d) && b.type != AI_S_WAIT;
            if(d->state != CS_ALIVE || !allowmove) d->stopmoving();
            if(d->state == CS_ALIVE)
            {
                if(allowmove)
                {
                    if(!request(d, b)) target(d, b, WEAP_IS_MELEE(d->gunselect), b.idle ? true : false);
                }
                // Battlegrounds (v0.1.40, stall fix): bots cast RPG abilities
                // whenever alive with a sighted in-range enemy, even in
                // AI_S_WAIT. Gating casts on allowmove silenced all combat
                // whenever all flags were home: on waypoint-less maps the
                // only route-free state is flag-driven PURSUE, so with flags
                // home every bot sat in WAIT (dowait still acquires enemies
                // via target(), but logic() never called bg_botcast). The
                // server still re-validates every cast (live match only,
                // class, mana, cooldown, range, line of sight, enemy team).
                BotAiInfo *ai = getBotState(d);
                if(m_ctf && rpg::mapactive(getclientmap())) bg_botcast(d);
                else if(allowmove) shoot(d, ai->target);
                if(!intermission)
                {
                    if(d->ragdoll) cleanragdoll(d);
                    moveplayer(d, 10, true);
                    if(allowmove && !b.idle) timeouts(d, b);
                    entities::checkitems(d);
                    if(cmode) cmode->checkitems(d);
                }
            }
            else if(d->state == CS_DEAD)
            {
                if(d->ragdoll) moveragdoll(d);
                else if(lastmillis-d->lastpain<2000)
                {
                    d->move = d->strafe = 0;
                    moveplayer(d, 10, false);
                }
            }
            d->attacking = d->jumping = false;
        }

        void BotAi::think(fpsent *d, bool run)
        {
            // the state stack works like a chain of commands, certain commands simply replace each other
            // others spawn new commands to the stack the ai reads the top command from the stack and executes
            // it or pops the stack and goes back along the history until it finds a suitable command to execute
            bool cleannext = false;
            BotAiInfo *ai = getBotState(d);
            if(ai->state.empty()) ai->addstate(AI_S_WAIT);
            loopvrev(ai->state)
            {
                aistate &c = ai->state[i];
                if(cleannext)
                {
                    c.millis = lastmillis;
                    c.override = false;
                    cleannext = false;
                }
                if(d->state == CS_DEAD && d->respawned!=d->lifesequence && (!cmode || cmode->respawnwait(d) <= 0) && lastmillis - d->lastpain >= 500)
                {
                    addmsg(N_TRYSPAWN, "rcii", d, d->playerclass, d->playermodel);
                    d->respawned = d->lifesequence;
                }
                else if(d->state == CS_ALIVE && run)
                {
                    int result = 0;
                    c.idle = 0;
                    switch(c.type)
                    {
                        case AI_S_WAIT: result = dowait(d, c); break;
                        case AI_S_DEFEND: result = dodefend(d, c); break;
                        case AI_S_PURSUE: result = dopursue(d, c); break;
                        case AI_S_INTEREST: result = dointerest(d, c); break;
                        default: result = 0; break;
                    }
                    if(result <= 0)
                    {
                        if(c.type != AI_S_WAIT)
                        {
                            switch(result)
                            {
                                case 0: default: ai->removestate(i); cleannext = true; break;
                                case -1: i = ai->state.length()-1; break;
                            }
                            continue; // shouldn't interfere
                        }
                    }
                }
                logic(d, c, run);
                break;
            }
            if(ai->trywipe) ai->wipe();
            ai->lastrun = lastmillis;
        }

        void drawroute(fpsent *d, float amt = 1.f)
        {
            int last = -1;
            
            BotAiInfo *ai = getBotState(d);
            loopvrev(ai->route)
            {
                if(ai->route.inrange(last))
                {
                    int index = ai->route[i], prev = ai->route[last];
                    if(iswaypoint(index) && iswaypoint(prev))
                    {
                        waypoint &e = waypoints[index], &f = waypoints[prev];
                        vec fr = f.o, dr = e.o;
                        fr.z += amt; dr.z += amt;
                        particle_flare(fr, dr, 1, PART_STREAK, 0xFFFFFF);
                    }
                }
                last = i;
            }
            if(aidebug >= 5)
            {
                vec pos = d->feetpos();
                if(ai->spot != vec(0, 0, 0)) particle_flare(pos, ai->spot, 1, PART_LIGHTNING, 0x00FFFF);
                if(iswaypoint(ai->targnode))
                    particle_flare(pos, waypoints[ai->targnode].o, 1, PART_LIGHTNING, 0xFF00FF);
                if(iswaypoint(d->lastnode))
                    particle_flare(pos, waypoints[d->lastnode].o, 1, PART_LIGHTNING, 0xFFFF00);
                loopi(AiInfo::NUMPREVNODES) if(iswaypoint(ai->prevnodes[i]))
                {
                    particle_flare(pos, waypoints[ai->prevnodes[i]].o, 1, PART_LIGHTNING, 0x884400);
                    pos = waypoints[ai->prevnodes[i]].o;
                }
            }
        }

        const char *stnames[AI_S_MAX] = {
            "wait", "defend", "pursue", "interest"
        }, *sttypes[AI_T_MAX+1] = {
            "none", "node", "player", "affinity", "entity"
        };
        
        void BotAi::render(fpsent *d, float i)
        {
            BotAiInfo *ai = getBotState(d);
            vec pos = d->abovehead();
            pos.z += 3;
            if(aidebug >= 4) drawroute(d, 4.f*i);
            if(aidebug >= 3)
            {
                defformatstring(q)("node: %d route: %d (%d)",
                                    d->lastnode,
                                    !ai->route.empty() ? ai->route[0] : -1,
                                    ai->route.length()
                );
                particle_textcopy(pos, q, PART_TEXT, 1);
                pos.z += 2;
            }
            bool top = true;
            loopvrev(ai->state)
            {
                aistate &b = ai->state[i];
                defformatstring(s)("%s%s (%d ms) %s:%d",
                                    top ? "\fg" : "\fy",
                                    stnames[b.type],
                                    lastmillis-b.millis,
                                    sttypes[b.targtype+1], b.target
                );
                particle_textcopy(pos, s, PART_TEXT, 1);
                pos.z += 2;
                if(top)
                {
                    if(aidebug >= 3) top = false;
                    else break;
                }
            }
            if(aidebug >= 3)
            {
                if(ai->weappref >= 0 && ai->weappref < NUMWEAPS)
                {
                    particle_textcopy(pos, weapons[ai->weappref].name, PART_TEXT, 1);
                    pos.z += 2;
                }
                fpsent *e = getclient(ai->enemy);
                if(e)
                {
                    particle_textcopy(pos, colorname(e), PART_TEXT, 1);
                    pos.z += 2;
                }
            }
        }

        //RR
        //TODO: remove
        void tryWipe(fpsent *d)
        {
            bot::getBotState(d)->trywipe = true;
        }
        
        void switchState(fpsent *d, bot::aistate &b, int t, int r, int v)
        {
            bot::getBotState(d)->switchstate(b, t, r, v);
        }
        ///RR
    }
}