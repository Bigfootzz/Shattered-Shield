#include "game.h"
#include "rpg/rpg.h"
#include "rpg/abilities.h"
#include "rpg/items.h"
#include "engine.h"
#include "version.h"

namespace game
{
    bool intermission = false;
    int maptime = 0, maprealtime = 0, maplimit = -1;
    int respawnent = -1;
    int lasthit = 0, lastspawnattempt = 0;

    int following = -1, followdir = 0;
    int pmillis = 0;

    fpsent *player1 = NULL;         // our client
    vector<fpsent *> players;       // other clients
    int savedammo[NUMWEAPS];

    bool clientoption(const char *arg) { return false; }

    SVAR(TEAM_1_NAME, TEAM_0);
    SVAR(TEAM_2_NAME, TEAM_1);
    VAR(MAX_NAME_LENGTH, MAXNAMELEN, MAXNAMELEN, MAXNAMELEN);

    void taunt()
    {
        if(player1->state!=CS_ALIVE || player1->physstate<PHYS_SLOPE) return;
        if(lastmillis-player1->lasttaunt<1000) return;
        player1->lasttaunt = lastmillis;
        addmsg(N_TAUNT, "rc", player1);
    }
    COMMAND(taunt, "");

    ICOMMAND(getfollow, "", (),
    {
        fpsent *f = followingplayer();
        intret(f ? f->clientnum : -1);
    });

    void follow(char *arg)
    {
        if(arg[0] ? player1->state==CS_SPECTATOR : following>=0)
        {
            following = arg[0] ? parseplayer(arg) : -1;
            if(following==player1->clientnum) following = -1;
            followdir = 0;
            conoutf("follow %s", following>=0 ? "on" : "off");
            if(following >= 0) setisburning(getclient(following)->onfire!=0, true);
        }
    }
    COMMAND(follow, "s");

    void nextfollow(int dir)
    {
        if(player1->state!=CS_SPECTATOR || clients.empty())
        {
            stopfollowing();
            return;
        }
        int cur = following >= 0 ? following : (dir < 0 ? clients.length() - 1 : 0);
        loopv(clients)
        {
            cur = (cur + dir + clients.length()) % clients.length();
            if(clients[cur] && clients[cur]->state!=CS_SPECTATOR)
            {
                if(following<0) conoutf("follow on");
                following = cur;
                followdir = dir;
                setisburning(getclient(following)->onfire!=0, true);
                return;
            }
        }
        stopfollowing();
    }
    ICOMMAND(nextfollow, "i", (int *dir), nextfollow(*dir < 0 ? -1 : 1));

    uint binomialCoef(uint n, uint const k)
    {
        uint r = 1;
        if(k > n) return 0;
        for(uint d = 1; d <= k; d++)
        {
            r *= n--;
            r /= d;
        }

        return r;
    }

    bool inputenabled = true;

    struct CutsceneCamera
    {
        int id;
        vec o;
        float weight;
    };

    struct Cutscene
    {
        int id;
        const char *name;
        bool computed;
        int startTime;
        float timePerPoint;
        vector<CutsceneCamera *> cameras;
        vector<vec> computedPoints;

        Cutscene() : id(-1), name(NULL), computed(false), startTime(-1), timePerPoint(3000), cameras(), computedPoints() {}

        ~Cutscene()
        {
            DELETEA(name);
        }
    };

    static inline bool sortCutsceneCamerasFunction(CutsceneCamera *&c1, CutsceneCamera *&c2)
    {
        return c1->id > c2->id;
    }

    vec getComputedPointIndexed(Cutscene *cs, uint index)
    {
        return cs->computedPoints[index % cs->computedPoints.length()];
    }

    FVAR(cutscenePrecision, 0.f, 100.f, 100.f);

    float bernsteinposition(float value, int currentelement, float position, int elementcount, float weight)
    {
        if(position >= 1.0f) position= 0.999f;

        // bonomial coefficient
        uint coef = binomialCoef(elementcount, currentelement);

        return coef
                * pow(position, currentelement)
                * pow((1-position), (elementcount-currentelement)) // interpolation value1
                * value // actual value
                * weight; // weight
    }

    vec calculatePosition(Cutscene *cs, float position)
    {
        // computed point
        vec finished_point;

        int elementCount = cs->cameras.length()-1;

        // the number of camera points given
        loopi(elementCount+1)
        {
            // compute bezier coordinates using bernstein polynoms
            finished_point.x += bernsteinposition(cs->cameras[i]->o.x, i, position, elementCount, cs->cameras[i]->weight);
            finished_point.y += bernsteinposition(cs->cameras[i]->o.y, i, position, elementCount, cs->cameras[i]->weight);
            finished_point.z += bernsteinposition(cs->cameras[i]->o.z, i, position, elementCount, cs->cameras[i]->weight);
        }

        return finished_point;
    }
    
    void calculateCurveBernsteinPolynom(Cutscene *cs)
    {
        // Calculate precision
        float fStep = 1.0f / cutscenePrecision;

        cs->computedPoints.shrink(0);

        // go along our curve in fPos steps
        for (float fPos = 0.0f; fPos <= 1.0f; fPos += fStep)
        {
            // add computed point
            vec point = calculatePosition(cs, fPos);
            cs->computedPoints.add(point);
        }

        cs->computed = true;
    }

    vector <Cutscene> cutscenes;

    void resetCutscenes()
    {
        cutscenes.shrink(0);
    }
    COMMAND(resetCutscenes, "");

    void defineCutscene(int *id, const char *name)
    {
        Cutscene &cs = cutscenes.add();
        cs.id = *id;
        cs.name = newstring(name);
    }
    COMMAND(defineCutscene, "is");

    void recomputeCutscenes()
    {
        vector<extentity *> &ents = entities::getents();

        loopv(cutscenes)
        {
            cutscenes[i].cameras.shrink(0);
            cutscenes[i].computed = false;
        }

        loopv(ents)
        {
            if(ents[i]->type == CAMERA)
            {
                loopvj(cutscenes)
                {
                    if(cutscenes[j].id == ents[i]->attr1)
                    {
                        CutsceneCamera *c = new CutsceneCamera;
                        c->id = ents[i]->attr2;
                        c->o = ents[i]->o;
                        c->weight = 1.0f;
                        cutscenes[j].cameras.add(c);
                        break;
                    }
                }
            }
        }

        loopv(cutscenes)
        {
            cutscenes[i].cameras.sort(sortCutsceneCamerasFunction);
            calculateCurveBernsteinPolynom(&cutscenes[i]);
        }
    }
    COMMAND(recomputeCutscenes, "");

    VAR(currentCutscene, -1, -1, __INT_MAX__);
    VAR(dbgcutscenes, 0, 0, 1);

    void drawCutscenePaths()
    {
        loopv(cutscenes)
        {
            Cutscene &cs = cutscenes[i];

            if(!cs.computed) recomputeCutscenes();

            vec lastPos; bool haveLastPos = false;
            loopvj(cs.computedPoints)
            {
                if(!haveLastPos)
                {
                    haveLastPos = true;
                }
                else
                {
                    particle_flare(lastPos, cs.computedPoints[j], 0, PART_DEBUG_LINE, j % 2 == 0 ? 0x00AA00 : 0xAA0000, 1.f);
                }
                lastPos = cs.computedPoints[j];
            }
        }
    }

    void updateCutsceneCamera()
    {
        if(dbgcutscenes) drawCutscenePaths();
        if(!cutscenes.inrange(currentCutscene)) return;

        Cutscene &cs = cutscenes[currentCutscene];

        if(!cs.computed) recomputeCutscenes();

        if(cs.startTime < 0) cs.startTime = totalmillis;
        int elapsedTime = totalmillis - cs.startTime;
        float progress = float(elapsedTime)/(float(cs.timePerPoint)/cutscenePrecision);
        int pointIndex = floor(progress);

        if(pointIndex + 1 >= cs.computedPoints.length())
        {
            defformatstring(cutsceneFinishName)("cutscene_finished_%i", currentCutscene);
            currentCutscene = -1;
            cs.startTime = -1;
            if(identexists(cutsceneFinishName)) execute(cutsceneFinishName);
            if(identexists("cutscene_finished")) execute("cutscene_finished");
            return;
        }

        float currentPointProgress = progress - float(pointIndex);

        vec currentPosition = cs.computedPoints[pointIndex];
        vec nextPosition = cs.computedPoints[pointIndex+1];

        vec delta (nextPosition);
        delta.sub(currentPosition);

        float yaw, pitch;
        vectoyawpitch( delta, yaw, pitch);

        delta.mul(currentPointProgress);

        //TODO: create camera entity if player is in first person
        camera1->o = currentPosition.add(delta);
        camera1->yaw = yaw;
        camera1->pitch = pitch;
    }

    ICOMMAND(iszombiegame, "", (), intret(m_sp||m_survival));

    VAR(dbgisinfected, 0, 0, 1);
    bool isinfected()
    {
        return dbgisinfected || hudplayer()->isInfected();
    }
    bool ismoving(bool move, bool strafe) { return (move && player1->move!=0) || (strafe && player1->strafe!=0); }

    SVARP(voicedir, "male");

    struct RadioSound
    {
        string name;
        string path;
        string text;
    };

    vector<RadioSound *> radioSounds;

    void registerRadioSound(const char *name, const char *path, const char *text)
    {
        RadioSound *sound = new RadioSound();
        copystring(sound->name, name);
        copystring(sound->path, path);
        copystring(sound->text, text);
        radioSounds.add(sound);
    }

    void unRegisterRadioSound(const char *name)
    {
        loopv(radioSounds)
        {
            if(0 == strcmp(name, radioSounds[i]->name))
            {
                radioSounds.remove(i);
                break;
            }
        }
    }

    const RadioSound *getRadioSound(const char *name)
    {
        loopv(radioSounds)
        {
            if(0 == strcmp(name, radioSounds[i]->name))
            {
                return radioSounds[i];
            }
        }

        return NULL;
    }

    void sayradio(char *s, bool teamonly = false, fpsent *speaker = NULL)
    {
        if (speaker == player1) return;

        string soundName;

        if(!speaker)
        {
            formatstring(soundName)("%s.%s", voicedir, s);
        }
        else
        {
            copystring(soundName, s);
        }

        const RadioSound *sound = getRadioSound(soundName);
        if(!sound) return;

        playsoundname(sound->path /*, speaker != NULL ? &speaker->o : NULL*/);

        if (speaker == NULL)
        {
            defformatstring(text)("%s", sound->text);
            if(!teamonly)
            {
                toserver(text);
            }
            else
            {
                sayteam(text);
            }

            addmsg(teamonly ? N_RADIOTEAM: N_RADIOALL, "rs", soundName);
        }
    }

    ICOMMAND(radioregister, "sss", (const char *name, const char *path, const char *text), registerRadioSound(name, path, text));
    ICOMMAND(radioall, "s", (char *s), sayradio(s));
    ICOMMAND(radioteam, "s", (char *s), sayradio(s, true));

    void applyeffect(fpsent *d, int item)
    {
        const char *iname = NULL;
        const playerclassinfo &pci = game::getplayerclassinfo(d);

        switch (item)
        {
            case BA_AMMO:
                loopi(WEAPONS_PER_CLASS) d->ammo[pci.weap[i]] = min(d->ammo[pci.weap[i]] + GUN_AMMO_MAX(pci.weap[i])/2, GUN_AMMO_MAX(pci.weap[i]));
                iname = "$ Ammo $";
                break;

            case BA_AMMOD:
                loopi(WEAPONS_PER_CLASS) d->ammo[pci.weap[i]] = GUN_AMMO_MAX(pci.weap[i]);
                iname = "$ Double Ammo $";
                break;

            case BA_HEALTH:
                d->health = min(d->health + d->maxhealth/2, d->maxhealth);
                iname = "$ Heatlh $";
                break;
            case BA_HEALTHD:
                d->health = d->maxhealth;
                iname = "$ Double Heatlh $";
                break;

            case BA_ARMOURG:
            case BA_ARMOURY:
                iname = (item==BA_ARMOURY)? "$ Yellow Armour $": "$ Green Armour $";
                d->armourtype = (item==BA_ARMOURY)? A_YELLOW: A_GREEN;
                d->armour = (item==BA_ARMOURY)? 200: 100;
                break;

            case BA_QUAD:
            case BA_QUADD:
                iname = (item==BA_QUADD)? "$ Double Quad $": "$ Quad $";
                d->quadmillis = (item==BA_QUADD)? 60000: 30000;
                break;

            case BA_SUPPORT:
            case BA_SUPPORTD:
                iname = (item==BA_SUPPORTD)? "$ Double Support $": "$ Support $";
                break;

            case BA_DAMAGE:
                iname = "$ Damage +10% $";
                break;
            case BA_EXTRALIFE:
                iname = "$ Extra Life banked $";
                break;
            case BA_QUADR:
                iname = "$ Quad armed for next round $";
                d->quadmillis = 30000;
                break;
        }

        if(!iname) iname = "$ <INVALID> $";

        particle_textcopy(d->abovehead(), iname, PART_TEXT, 3000, 0x007755, 4.0f, -8);
    }

    void applyitem(fpsent *d, int item)
    {
        switch (item)
        {
            case BA_AMMO:
            {
                const playerclassinfo &pci = game::getplayerclassinfo(d);
                loopi(WEAPONS_PER_CLASS) d->ammo[pci.weap[i]] = min(d->ammo[pci.weap[i]] + GUN_AMMO_MAX(pci.weap[i])/2, GUN_AMMO_MAX(pci.weap[i]));
                break;
            }
            case BA_AMMOD:
            {
                const playerclassinfo &pci = game::getplayerclassinfo(d);
                loopi(WEAPONS_PER_CLASS) d->ammo[pci.weap[i]] = GUN_AMMO_MAX(pci.weap[i]);
                break;
            }

            case BA_HEALTH:
                d->health = min(d->health + d->maxhealth/2, d->maxhealth);
                break;
            case BA_HEALTHD:
                d->health = d->maxhealth;
                break;

            case BA_ARMOURG:
            case BA_ARMOURY:
                d->armourtype = (item==BA_ARMOURY)? A_YELLOW: A_GREEN;
                d->armour = (item==BA_ARMOURY)? 200: 100;
                break;

            case BA_QUAD:
            case BA_QUADD:
                d->quadmillis = (item==BA_QUADD)? 60000: 30000;
                break;

            case BA_SUPPORT:
            case BA_SUPPORTD:
                //if (M_DMSP) spawnsupport((item==BA_SUPPORTD)? 6: 3); //TODO
                break;
        }
    }

    int shopend = 0; // client millis when the intermission shop closes (0 = closed)

    bool shopopen() { return shopend && lastmillis < shopend; }

    // called by the N_SHOPOPEN server message: the buy menu opens automatically
    void openshop(int millis)
    {
        shopend = lastmillis + millis;
        showgui("buy");
        conoutf(CON_GAMEINFO, "\f2Shop open! Spend your guts - press B or click a button.");
    }

    // cubescript getter so the buy menu can show the live guts balance
    ICOMMAND(getguts, "", (), intret(player1->guts));

    void buy(char *s)
    {
        if ((player1->state != CS_ALIVE && !shopopen()) || intermission) return;

        loopi(BA_NUM) if (!strcmp(buyablesnames[i], s))
        {
            if (player1->guts < buyablesprices[i])
            {
                conoutf(CON_INFO, "\f2not enough gut points to buy item");
                break;
            }
            addmsg(N_BUY, "rci", player1, i);
            break;
        }
    }
    COMMAND(buy, "s");

    void endsp(bool allkilled)
    {
        conoutf(CON_GAMEINFO, allkilled ? "\f2you have cleared the map!" : "\f2you reached the exit!");
        addmsg(N_FORCEINTERMISSION, "r");
    }
    ICOMMAND(endsp, "", (), endsp(false));

    void spsummary(int accuracy)
    {
        //TODO
//         conoutf(CON_GAMEINFO, "\f2--- single player time score: ---");
//         int pen, score = 0;
//         pen = ((lastmillis-maptime)*100)/(1000*gamespeed)); score += pen; if(pen) conoutf(CON_GAMEINFO, "\f2time taken: %d seconds (%d simulated seconds)", pen, (lastmillis-maptime)/1000);
//         pen = player1->deaths*60; score += pen; if(pen) conoutf(CON_GAMEINFO, "\f2time penalty for %d deaths (1 minute each): %d seconds", player1->deaths, pen);
//         pen = remain*10;          score += pen; if(pen) conoutf(CON_GAMEINFO, "\f2time penalty for %d monsters remaining (10 seconds each): %d seconds", remain, pen);
//         pen = (10-skill)*20;      score += pen; if(pen) conoutf(CON_GAMEINFO, "\f2time penalty for lower skill level (20 seconds each): %d seconds", pen);
//         pen = 100-accuracy;       score += pen; if(pen) conoutf(CON_GAMEINFO, "\f2time penalty for missed shots (1 second each %%): %d seconds", pen);
//         defformatstring(aname)("bestscore_%s", getclientmap());
//         const char *bestsc = getalias(aname);
//         int bestscore = *bestsc ? parseint(bestsc) : score;
//         if(score<bestscore) bestscore = score;
//         defformatstring(nscore)("%d", bestscore);
//         alias(aname, nscore);
//         conoutf(CON_GAMEINFO, "\f2TOTAL SCORE (time + time penalties): %d seconds (best so far: %d seconds)", score, bestscore);
    }

    const char *getclientmap() { return clientmap; }

    void resetgamestate()
    {
        if(m_dmsp)
        {
            entities::resettriggers();
        }
        if(m_classicsp)
        {
            clearmovables();
            AI_MULTI_DISPATCH(ai->reset());                 // all monsters back at their spawns for editing
            entities::resettriggers();
        }
        player1->guts = 0;
        clearprojectiles();
        clearbouncers();
    }

    FVAR(lasthp, 0, 0, 1000);
    FVAR(lastap, 0, 0, 1000);

    fpsent *spawnstate(fpsent *d)              // reset player state not persistent accross spawns
    {
        d->respawn(gamemode);
        d->spawnstate(gamemode);
        if (d == player1)
        {
            lasthp = 0.f;
            lastap = 0.f;
        }
        return d;
    }

    void respawnself(bool isMapstart = false)
    {
        extern int connectionState;
        if((!isMapstart && ispaused()) || connectionState != CONNECTION_STATE_CONNECTED) return;
        resetdamagescreen();
        if(isMapstart || m_mp(gamemode))
        {
            if(isMapstart || player1->respawned!=player1->lifesequence)
            {
                addmsg(N_TRYSPAWN, "rcii", player1, playerclass, playermodel);
                player1->respawned = player1->lifesequence;
            }
        }
        else
        {
            spawnplayer(player1);
            showscores(false);
            lasthit = 0;
            if(cmode) cmode->respawned(player1);
        }
    }

    fpsent *pointatplayer()
    {
        loopv(players) if(players[i] != player1 && intersect(players[i], player1->o, worldpos)) return players[i];
        return NULL;
    }

    void stopfollowing()
    {
        if(following<0) return;
        following = -1;
        followdir = 0;
        conoutf("follow off");
    }

    fpsent *followingplayer()
    {
        if(player1->state!=CS_SPECTATOR || following<0) return NULL;
        fpsent *target = getclient(following);
        if(target && target->state!=CS_SPECTATOR) return target;
        return NULL;
    }

    fpsent *hudplayer()
    {
        if(thirdperson) return player1;
        fpsent *target = followingplayer();
        return target ? target : player1;
    }

    void setupcamera()
    {
        fpsent *target = followingplayer();
        if(target)
        {
            player1->yaw = target->yaw;
            player1->pitch = target->state==CS_DEAD ? 0 : target->pitch;
            player1->o = target->o;
            player1->resetinterp();
        }
    }

    bool detachcamera()
    {
        fpsent *d = hudplayer();
        return d->state==CS_DEAD;
    }

    bool collidecamera()
    {
        switch(player1->state)
        {
            case CS_EDITING: return false;
            case CS_SPECTATOR: return followingplayer()!=NULL;
        }
        return true;
    }

    VARP(smoothmove, 0, 75, 100);
    VARP(smoothdist, 0, 32, 64);

    void predictplayer(fpsent *d, bool move)
    {
        d->o = d->newpos;
        d->yaw = d->newyaw;
        d->pitch = d->newpitch;
        d->roll = d->newroll;
        if(move)
        {
            moveplayer(d, 1, false);
            d->newpos = d->o;
        }
        float k = 1.0f - float(lastmillis - d->smoothmillis)/smoothmove;
        if(k>0)
        {
            d->o.add(vec(d->deltapos).mul(k));
            d->yaw += d->deltayaw*k;
            if(d->yaw<0) d->yaw += 360;
            else if(d->yaw>=360) d->yaw -= 360;
            d->pitch += d->deltapitch*k;
            d->roll += d->deltaroll*k;
        }
    }

    void otherplayers(int curtime)
    {
        loopv(players)
        {
            fpsent *d = players[i];
            if(d == player1 || d->ai.local) continue;

            if(d->state==CS_DEAD && d->ragdoll) moveragdoll(d);
            else if(!intermission)
            {
                if(lastmillis - d->lastaction >= d->gunwait) d->gunwait = 0;
                if(d->quadmillis) entities::checkquad(curtime, d);
            }

            const int lagtime = totalmillis-d->lastupdate;
            if(!lagtime || intermission) continue;
            else if(lagtime>1000 && d->state==CS_ALIVE)
            {
                d->state = CS_LAGGED;
                continue;
            }
            if(d->state==CS_ALIVE || d->state==CS_EDITING)
            {
                if(smoothmove && d->smoothmillis>0) predictplayer(d, true);
                else moveplayer(d, 1, false);
            }
            else if(d->state==CS_DEAD && !d->ragdoll && lastmillis-d->lastpain<2000) moveplayer(d, 1, true);
        }
    }

    VARFP(slowmosp, 0, 0, 1, { if(m_sp && !slowmosp) server::forcegamespeed(100); }); 

    void checkslowmo()
    {
        static int lastslowmohealth = 0;
        server::forcegamespeed(intermission ? 100 : clamp(player1->health, 25, 200));
        if(player1->health<player1->maxhealth && lastmillis-max(maptime, lastslowmohealth)>player1->health*player1->health/2)
        {
            lastslowmohealth = lastmillis;
            player1->health++;
        }
    }

    int lastinterm = 0;
    bool shownvotes = false;
    void updateworld()        // main game update loop
    {
        extern int connectionState;
        if(connectionState != CONNECTION_STATE_CONNECTED) { gets2c(); return; }
        if(!maptime) { maptime = lastmillis; maprealtime = totalmillis; return; }
        if(!curtime) { currentCutscene = 0; updateCutsceneCamera(); gets2c(); if(player1->clientnum>=0) c2sinfo(); return; }

        physicsframe();
        ai::navigate();
        if(player1->state != CS_DEAD && !intermission)
        {
            entities::checkquad(curtime, player1);
        }
        updateweapons(curtime);
        otherplayers(curtime);
        ai::update();
        moveragdolls();
        gets2c();
        updatemovables(curtime);
        if (cmode) cmode->update(curtime);
        if(shopend && lastmillis >= shopend) { shopend = 0; cleargui(); }
        if(intermission)
        {
            currentCutscene = 0;
            updateCutsceneCamera();
        }
        else if(player1->state == CS_DEAD)
        {
            if(player1->ragdoll) moveragdoll(player1);
            else if(lastmillis-player1->lastpain<2000)
            {
                player1->move = player1->strafe = 0;
                moveplayer(player1, 10, true);
            }
        }
        else
        {
            if(player1->ragdoll) cleanragdoll(player1);
            if (inputenabled) moveplayer(player1, 10, true);
            else { currentCutscene = 0; updateCutsceneCamera(); }

            swayhudgun(curtime);
            entities::checkitems(player1);
            if(m_sp)
            {
                if(slowmosp) checkslowmo();
                entities::checktriggers();
            }
            else if(cmode) cmode->checkitems(player1);
        }
        // --- RPG: Shattered Shield inventory (milestone step 4, additive; see src/rpg) ---
        // Loot drops render a sparkle + name label, RPG maps only.
        if(rpg::mapactive(getclientmap()))
        {
            rpg::lootdrop drops[64];
            int nd = 0;
            rpg::getlootdrops(drops, 64, nd);
            loopi(nd)
            {
                vec o(drops[i].x, drops[i].y, drops[i].z);
                regular_particle_splash(PART_SPARK, 1, 60, o, 0xFFD24A, 2.5f, 50, -150);
                particle_textcopy(o, rpg::itemname(drops[i].item), PART_TEXT, 1, 0xFFD24A, 2.0f, 0);
                particle_flare(o, o, 0, PART_LFLARE, 0xFFD24A, 1.2f);
            }
        }
        if(player1->clientnum>=0) c2sinfo();   // do this last, to reduce the effective frame lag
        if(intermission && !shownvotes && (totalmillis-lastinterm) > 10000) { showscores(false); showgui("votesetup"); shownvotes = true; }
        else if(!intermission) { if (shownvotes) { cleargui(); } shownvotes = false; }
    }

    void spawnplayer(fpsent *d)   // place at random spawn
    {
        if(cmode) cmode->pickspawn(d);
        else findplayerspawn(d, d==player1 && respawnent>=0 ? respawnent : -1);
        spawnstate(d);
        if(d==player1)
        {
            if(editmode) d->state = CS_EDITING;
            else if(d->state != CS_SPECTATOR) d->state = CS_ALIVE;
        }
        else d->state = CS_ALIVE;
    }

    VARP(spawnwait, 0, 2000, 5000);

    bool respawn()
    {
        if(player1->state==CS_DEAD)
        {
            player1->attacking = false;
            int wait = cmode ? cmode->respawnwait(player1) : 0;
            if(wait>0)
            {
                lastspawnattempt = lastmillis;
                return false;
            }
            if(lastmillis < player1->lastpain + spawnwait) return false;
            resetfade();
            respawnself();
            if(m_classicsp)
            {
                conoutf(CON_GAMEINFO, "\f2You wasted another life! The monsters stole your armour and some ammo...");
                loopi(NUMWEAPS) if(i!=WEAP_PISTOL && (player1->ammo[i] = savedammo[i]) > 5) player1->ammo[i] = max(player1->ammo[i]/3, 5);
            }
            return true;
        }
        return false;
    }

    // inputs

    int nextchasee(int last)
    {
        int ret = last;
        int l = players.length();
        for (int i=1; i<l; i++)
        {
            ret = (last+i)%l;
            if (players[ret]->state==CS_ALIVE) return ret;
        }
        return last%l;
    }

    void doattack(bool on, bool altfire)
    {
        if(intermission) return;
        player1->altfire = on && altfire;
        if((player1->attacking = on) && !respawn() && killcamera)
        {
            static int cpo = 0;
            cpo = nextchasee(cpo);
            player1->follow = players[cpo]->clientnum;
            return;
        }
    }

    bool canjump()
    {
        if(!intermission) respawn();
        return !mainmenu && player1->state!=CS_DEAD && !intermission;
    }

    bool allowmove(physent *d)
    {
        if(d->type!=ENT_PLAYER) return true;
        return !mainmenu && (!((fpsent *)d)->lasttaunt || lastmillis-((fpsent *)d)->lasttaunt>=1000);
    }

    VARP(hitsound, 0, 0, 1);

    void damaged(int damage, fpsent *d, fpsent *actor, bool local, int gun)
    {
        if(d->state!=CS_ALIVE || intermission) return;

        if(local) damage = d->dodamage(damage);
        else if(actor==player1)
        {
            damageeffect(damage,d,true,true);
            return;
        }

        fpsent *h = hudplayer();
        bool pl = false;
        if(h!=player1 && actor==h && d!=actor)
        {
            if(hitsound && lasthit != lastmillis) playsound(S_HIT);
            lasthit = lastmillis;
            pl = true;
        }
        if(d==h)
        {
            damageblend(damage*max(WEAP(gun,quakemul),1.f)*(1.0-(float)d->health/(float)d->maxhealth));
            float qu = (float)quakemillis*(float)WEAP(gun,quakemul)*min((float)damage/140.f, 2.f);
            d->addquake((int)qu);
            damagecompass(damage, actor->o);
        }
        damageeffect(damage, d, true, pl);

        ai::damaged(d, actor);

        if(m_sp && slowmosp && d==player1 && d->health < 1) d->health = 1;

        if(d->health<=0) { if(local) killed(d, actor, gun); }
        else if(d==h) playsound(min<int>(S_PAIN_ALAN+d->playermodel, S_PAIN_ADVENT));
        else /*if (d->ai.type != ai::AI_TYPE_MONSTER)*/ playsound(S_PAIN1+rnd(5), &d->o); //TODO: zombie pain sounds
    }

    VARP(deathscore, 0, 1, 1);
    void showdeathscores(){    if(deathscore) showscores(true);}

    void deathstate(fpsent *d, bool restore)
    {
        d->state = CS_DEAD;
        d->lastpain = lastmillis;
        if(!restore) gibeffect(max(-d->health, 0), d->vel, d);
        if(d==player1)
        {
            if(deathscore) showscores(true);
            disablezoom();
            if(!restore) loopi(NUMWEAPS) savedammo[i] = player1->ammo[i];
            d->attacking = false;
            if(!restore) d->deaths++;
            //d->pitch = 0;
            d->roll = 0;
            playsound(S_DIE1+rnd(2));
        }
        else
        {
            d->move = d->strafe = 0;
            d->resetinterp();
            d->smoothmillis = 0;
            /*if (d->aitype != AI_ZOMBIE)*/ playsound(S_DIE1+rnd(2), &d->o); //TODO: zombie die sounds
        }
    }

    VARP(teamcolorfrags, 0, 1, 1);

    void killed(fpsent *d, fpsent *actor, int gun, int special)
    {
        if(d->state==CS_EDITING)
        {
            d->editstate = CS_DEAD;
            if(d==player1) d->deaths++;
            else d->resetinterp();
            return;
        }
        else if((d->state!=CS_ALIVE && d->state != CS_LAGGED && d->state != CS_SPAWNING) || intermission) return;

        fpsent *h = followingplayer();
        if(!h) h = player1;
        if(special != -1)
        {
            if(actor == h && d != h && d->ai.type != ai::AI_TYPE_MONSTER)
            {
                hudevents.add(hudevent(special ? (WEAP_IS_EXPLOSIVE(gun) ? HET_DIRECTHIT : HET_HEADSHOT): HET_KILL, lastmillis));
            }

            int contype = d==h || actor==h ? CON_FRAG_SELF : CON_FRAG_OTHER;
            const char *dname = "", *aname = "";
            if(m_teammode && teamcolorfrags)
            {
                dname = teamcolorname(d, "you");
                aname = teamcolorname(actor, "you");
            }
            else
            {
                dname = colorname(d, NULL, "", "", "you");
                aname = colorname(actor, NULL, "", "", "you");
            }
            if(actor->type==ENT_AI)
                conoutf(contype, "\f2%s got killed by %s!", dname, aname);
            else if(d==actor || actor->type==ENT_INANIMATE)
            {
                if (gun < -1) conoutf(contype, "\f2%s %s%s", dname, GUN_SUICIDE_MESSAGE_INSERT_YOU(-2-gun, d == player1 ? "were " : "was "), GUN_SUICIDE_MESSAGE(-2-gun));
                else conoutf(contype, "\f2%s %s %s", dname, GUN_FRAG_MESSAGE(gun, actor->isInfected()), d==player1 ? "yourself!" : "thyself");
            }
            else if(isteam(d->team, actor->team))
            {
                contype |= CON_TEAMKILL;
                if(actor==player1) conoutf(contype, "\f3you %s a teammate (%s)%s", GUN_FRAG_MESSAGE(gun, actor->isInfected()), dname, special? GUN_SPECIAL_MESSAGE(gun, actor->isInfected()): "");
                else if(d==player1) conoutf(contype, "\f3you were %s by a teammate (%s)%s", GUN_FRAGBY_MESSAGE(gun, actor->isInfected()), aname, special? GUN_SPECIAL_MESSAGE(gun, actor->isInfected()): "");
                else conoutf(contype, "\f2%s %s a teammate (%s)%s", aname, GUN_FRAG_MESSAGE(gun, actor->isInfected()), dname, special? GUN_SPECIAL_MESSAGE(gun, actor->isInfected()): "");
            }
            else
            {
                if(d==player1) conoutf(contype, "\f2you were %s by %s%s", GUN_FRAGBY_MESSAGE(gun, actor->isInfected()), aname, special? GUN_SPECIAL_MESSAGE(gun, actor->isInfected()): "");
                else conoutf(contype, "\f2%s %s %s%s", aname, GUN_FRAG_MESSAGE(gun, actor->isInfected()), dname, special? GUN_SPECIAL_MESSAGE(gun, actor->isInfected()): "");
            }
        }

        deathstate(d);
        ai::killed(d, actor);
        if(d == player1) d->follow = actor->clientnum;
    }

    dynent *followcam()
    {
        fpsent *d = followingplayer();
        fpsent *follow = player1->follow != -1 ? getclient(player1->follow) : NULL;
        if(!follow)
        {
            player1->follow = -1;
        }
        else if (!d && player1->lastpain < lastmillis-1000 && d != player1)
            return (player1->state==CS_DEAD && follow && follow->state>=CS_ALIVE && follow->state<=CS_SPECTATOR)? follow: player1;
        return d;
    }

    void timeupdate(int secs)
    {
        if(secs > 0)
        {
            maplimit = lastmillis + secs*1000;
        }
        else
        {
            lastinterm = totalmillis;
            intermission = true;
            player1->attacking = false;
            if(cmode) cmode->gameover();
            conoutf(CON_GAMEINFO, "\f2intermission:");
            conoutf(CON_GAMEINFO, "\f2game has ended!");
            if(m_ctf) conoutf(CON_GAMEINFO, "\f2player frags: %d, flags: %d, deaths: %d", player1->frags, player1->flags, player1->deaths);
            else conoutf(CON_GAMEINFO, "\f2player frags: %d, deaths: %d", player1->frags, player1->deaths);
            int accuracy = (player1->totaldamage*100)/max(player1->totalshots, 1);
            conoutf(CON_GAMEINFO, "\f2player total damage dealt: %d, damage wasted: %d, accuracy(%%): %d", player1->totaldamage, player1->totalshots-player1->totaldamage, accuracy);
            if(cmode) cmode->printScores();
            if(m_sp) spsummary(accuracy);

            showscores(true);
            disablezoom();

            if(identexists("intermission")) execute("intermission");
        }
    }

    ICOMMAND(getfrags, "", (), intret(player1->frags));
    ICOMMAND(getflags, "", (), intret(player1->flags));
    ICOMMAND(getdeaths, "", (), intret(player1->deaths));
    ICOMMAND(getaccuracy, "", (), intret((player1->totaldamage*100)/max(player1->totalshots, 1)));
    ICOMMAND(gettotaldamage, "", (), intret(player1->totaldamage));
    ICOMMAND(gettotalshots, "", (), intret(player1->totalshots));

    vector<fpsent *> clients;

    fpsent *newclient(int cn)   // ensure valid entity
    {
        if(cn < 0 || cn > max(0xFF, MAXCLIENTS + MAXBOTS))
        {
            neterr("clientnum", false);
            return NULL;
        }

        if(cn == player1->clientnum) return player1;

        while(cn >= clients.length()) clients.add(NULL);
        if(!clients[cn])
        {
            fpsent *d = new fpsent;
            d->clientnum = cn;
            clients[cn] = d;
            players.add(d);
        }
        return clients[cn];
    }

    fpsent *getclient(int cn)   // ensure valid entity
    {
        if(cn == player1->clientnum) return player1;
        return clients.inrange(cn) ? clients[cn] : cmode? cmode->getclient(cn): NULL;
    }

    void clientdisconnected(int cn, bool notify)
    {
        if(!clients.inrange(cn)) return;
        if(following==cn)
        {
            if(followdir) nextfollow(followdir);
            else stopfollowing();
        }
        fpsent *d = clients[cn];
        if(!d) return;
        if(notify && d->name[0]) conoutf("\f4leave:\f7 %s", colorname(d));
        removeweapons(d);
        removetrackedparticles(d);
        removetrackeddynlights(d);
        if(cmode) cmode->removeplayer(d);
        players.removeobj(d);
        DELETEP(clients[cn]);
        cleardynentcache();
    }

    void clearclients(bool notify)
    {
        loopv(clients) if(clients[i]) clientdisconnected(i, notify);
    }

    void initclient()
    {
        player1 = spawnstate(new fpsent);
        players.add(player1);
    }

    VARP(showmodeinfo, 0, 1, 1);

    VARP(playerclass, 0, 0, NUMPCS-1);

    void startgame()
    {
        if(identexists("map_changed")) execute("map_changed");

        clearmovables();
        AI_MULTI_DISPATCH(ai->reset());

        clearprojectiles();
        clearbouncers();
        clearragdolls();

        // reset perma-state
        loopv(players)
        {
            fpsent *d = players[i];
            d->state = CS_DEAD;
            d->frags = d->flags = 0;
            d->deaths = 0;
            d->totaldamage = 0;
            d->totalshots = 0;
            d->lifesequence = -1;
            d->respawned = d->suicided = -2;
            d->guts = 0;
            d->infectedType = 0;
            if (m_oneteam) strcpy(d->team, TEAM_0);
        }

        setclientmode();

        intermission = false;
        maptime = maprealtime = 0;
        maplimit = -1;

        if(cmode)
        {
            cmode->preload();
            cmode->setup();
        }

        conoutf(CON_GAMEINFO, "\f2game mode is %s", server::modename(gamemode));

        if(m_classicsp)
        {
            defformatstring(scorename)("bestscore_%s", getclientmap());
            const char *best = getalias(scorename);
            if(*best) conoutf(CON_GAMEINFO, "\f2try to beat your best score so far: %s", best);
        }

        showscores(false);
        disablezoom();
        lasthit = 0;
        inputenabled = true;
        cameracap = false;

        resetfade();

        if(identexists("mapstart")) execute("mapstart");
    }

    void startmap(const char *name)   // called just after a map load
    {
        ai::savewaypoints();
        ai::clearwaypoints(true);

        respawnent = -1; // so we don't respawn at an old spot
        respawnself(true);
        entities::resetspawns();
        copystring(clientmap, name ? name : "");

        sendmapinfo();
    }

    vector<const char*> sptips;
    ICOMMAND(addtip, "s", (char *s), {
        sptips.add(newstring(s));
    });
    ICOMMAND(cleartips, "s", (), {
        const char *tip = NULL;
        while(sptips.length() > 0 && (tip = sptips.pop()) != NULL)
        {
            DELETEA(tip);
        }
    });
    ICOMMAND(gettip, "", (), {
        result(sptips[rnd(sptips.length())]);
    });

    const char *getmapinfo()
    {
        static char sinfo[1000];
        sinfo[0] = '\0';

        const ENetAddress *address = connectedpeer();
        if(address)
        {
            if(servinfo[0]) formatstring(sinfo)("\fs\feServer: \f6%s\fS\n\n", servinfo);
            else if(connectname && connectname[0]) formatstring(sinfo)("\fs\feServer: \f6%s:%d\fS\n\n", connectname, address->port);
        }

        const GameMode *mode = getGameMode(gamemode);

        if(mode)
        {
            if(mode->description) strcat(sinfo, mode->description);

            vector<const Mutator *> mutators;
            getMutators(mode->flags, mutators);

            loopv(mutators)
            {
                strcat(sinfo, mutators[i]->description);
            }
        }

        if (!sptips.empty())
        {
                if (sinfo[0]) strcat(sinfo, "\n");
                strcat(sinfo, "\fs\feTip: \fS");
                strcat(sinfo,  sptips[rnd(sptips.length())]);
        }

        return sinfo[0]? sinfo: NULL;
    }

    void physicstrigger(physent *d, bool local, int floorlevel, int waterlevel, int material)
    {
        if(d->type==ENT_INANIMATE) return;
        if     (waterlevel>0) { if(material!=MAT_LAVA) playsound(S_SPLASH1, d==player1 ? NULL : &d->o); }
        else if(waterlevel<0) playsound(material==MAT_LAVA ? S_BURN : S_SPLASH2, d==player1 ? NULL : &d->o);
        if     (floorlevel>0) { if(d==player1 || d->type!=ENT_PLAYER || ((fpsent *)d)->ai.local) msgsound(S_JUMP, d); }
        else if(floorlevel<0) { if(d==player1 || d->type!=ENT_PLAYER || ((fpsent *)d)->ai.local) msgsound(S_LAND, d); }
        if     (d->state == CS_DEAD && floorlevel<0) playsound(S_SPLOSH+(rand()%3), &d->o);
    }

    void dynentcollide(physent *d, physent *o, const vec &dir)
    {
        switch(d->type)
        {
            case ENT_PLAYER:
            case ENT_AI:
                ai::getAiType((fpsent *)d)->collide((fpsent *)d, o, dir);
                break;
            case ENT_INANIMATE: if(dir.z > 0) stackmovable((movable *)d, o); break;
        }
    }

    void msgsound(int n, physent *d)
    {
        if(!d || d==player1)
        {
            addmsg(N_SOUND, "ci", d, n);
            playsound(n);
        }
        else
        {
            if(d->type==ENT_PLAYER && ((fpsent *)d)->ai.local)
                addmsg(N_SOUND, "ci", d, n);
            playsound(n, &d->o);
        }
    }

    int numdynents() { return players.length()+movables.length()+(cmode? cmode->getdynents().length(): 0); }

    ICOMMAND(numdynents, "", (), intret(numdynents()));

    dynent *iterdynents(int i)
    {
        ASSERT(i >= -1 && "Dynent id can not be negative!");
        if(i<players.length()) return players[i];
        i -= players.length();
        if(i<movables.length()) return (dynent *)movables[i];
        if (!cmode) return NULL;
        i -= movables.length();
        if(i<cmode->getdynents().length()) return cmode->getdynents()[i];
        return NULL;
    }

    bool duplicatename(fpsent *d, const char *name = NULL, const char *alt = NULL)
    {
        if(!name) name = d->name;
        if(alt && d != player1 && !strcmp(name, alt)) return true;
        loopv(players) if(d!=players[i] && !strcmp(name, players[i]->name)) return true;
        return false;
    }

    static string cname[3];
    static int cidx = 0;

    const char *colorname(fpsent *d, const char *name, const char *prefix, const char *suffix, const char *alt)
    {
        if(!name) name = alt && d == player1 ? alt : d->name; 
        bool dup = !name[0] || duplicatename(d, name, alt) || d->ai.type != ai::AI_TYPE_NONE;
        if(dup || prefix[0] || suffix[0])
        {
            cidx = (cidx+1)%3;
            if(dup) formatstring(cname[cidx])(d->ai.type == ai::AI_TYPE_NONE ? "%s%s \fs\f5(%d)\fS%s" : "%s%s \fs\f5[%d]\fS%s", prefix, name, d->clientnum, suffix);
            else formatstring(cname[cidx])("%s%s%s", prefix, name, suffix);
            return cname[cidx];
        }
        return name;
    }

    int dteamnum(fpsent *d) { return !m_teammode ? 3 : (d->isInfected() ? 2 : (!strcmp(d->team, TEAM_0) ? 0 : 1)); }
    float colorteam[4][3] = { { 0.47f, 0.63f, 1.f }, { 1.f, 0.47f, 0.47f }, { 0.47f, 1.f, 0.47f }, { 1.f, 1.f, 1.f } };

    VARP(teamcolortext, 0, 1, 1);

    const char *teamcolorname(fpsent *d, const char *alt)
    {
        if(!teamcolortext || !m_teammode) return colorname(d, NULL, "", "", alt);
        return colorname(d, NULL, TEAM_COLOR_TEXT2(d->team, "\fs", ), "\fS", alt); 
    }

    const char *teamcolor(const char *name, const char *team, const char *alt)
    {
        if(!teamcolortext || !m_teammode) return isteam(player1->team, team) || !alt ? name : alt;
        cidx = (cidx+1)%3;
        formatstring(cname[cidx])(TEAM_COLOR_TEXT2(team, "\fs", "%s\fS"), isteam(player1->team, team) || !alt ? name : alt);
        return cname[cidx];
    }

    void suicide(physent *d, int type)
    {
        if(d==player1 || ((d->type==ENT_PLAYER || d->type==ENT_AI) && ((fpsent *)d)->ai.local))
        {
            if(d->state!=CS_ALIVE) return;
            fpsent *pl = (fpsent *)d;
            if(!m_mp(gamemode)) killed(pl, pl, -1-type);
            else if(pl->suicided!=pl->lifesequence)
            {
                addmsg(N_SUICIDE, "rci", pl, type);
                pl->suicided = pl->lifesequence;
            }
        }
        else if(d->type==ENT_INANIMATE) suicidemovable((movable *)d);
    }
    ICOMMAND(suicide, "", (), suicide(player1));

    VARP(showradar, 0, 1, 1);
    bool needminimap(bool cond) { return showradar && cond && ((cmode)? cmode->needsminimap(): m_teammode) /*m_ctf || m_protect || m_hold || m_capture*/; }

    void drawicon(int icon, float x, float y, float sz)
    {
        settexture("data/hud/items.png");
        glBegin(GL_TRIANGLE_STRIP);
        float tsz = 0.125f, tx = tsz*(icon%8), ty = tsz*(icon/8);
        glTexCoord2f(tx,     ty);     glVertex2f(x,    y);
        glTexCoord2f(tx+tsz, ty);     glVertex2f(x+sz, y);
        glTexCoord2f(tx,     ty+tsz); glVertex2f(x,    y+sz);
        glTexCoord2f(tx+tsz, ty+tsz); glVertex2f(x+sz, y+sz);
        glEnd();
    }

    void drawSprite(int sprite, int x, int y, int w, int h)
    {
        drawicon(sprite, x, y, w);
    }

    void drawquad(float x, float y, float w, float h, float tx1, float ty1, float tx2, float ty2, bool flipx, bool flipy)
    {
        glBegin(GL_TRIANGLE_STRIP);
        glTexCoord2f(flipx ? tx2 : tx1, flipy ? ty2 : ty1); glVertex2f(x, y);
        glTexCoord2f(flipx ? tx1 : tx2, flipy ? ty2 : ty1); glVertex2f(x+w, y);
        glTexCoord2f(flipx ? tx2 : tx1, flipy ? ty1 : ty2); glVertex2f(x, y+h);
        glTexCoord2f(flipx ? tx1 : tx2, flipy ? ty1 : ty2); glVertex2f(x+w, y+h);
        glEnd();
    }
    void drawtexture(float x, float y, float w, float h, bool flipx, bool flipy) { drawquad(x, y, w, h, 0, 0, 1, 1, flipx, flipy); }
    void drawsized(float x, float y, float s, bool flipx, bool flipy) { drawquad(x, y, s, s, 0, 0, 1, 1, flipx, flipy); }
    void drawblend(int x, int y, int w, int h, float r, float g, float b, bool blend)
    {
        if(!blend) glEnable(GL_BLEND);
        glBlendFunc(GL_ZERO, GL_SRC_COLOR);
        glColor3f(r, g, b);
        glBegin(GL_TRIANGLE_STRIP);
        glVertex2f(x, y);
        glVertex2f(x+w, y);
        glVertex2f(x, y+h);
        glVertex2f(x+w, y+h);
        glEnd();
        if(!blend) glDisable(GL_BLEND);
        else glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    float abovegameplayhud(int w, int h)
    {
        switch(hudplayer()->state)
        {
            case CS_EDITING:
            case CS_SPECTATOR:
                return 1;
            default:
                return 1650.0f/1800.0f;
        }
    }

    void drawometer(float progress, float size, vec pos, vec color)
    {
        glPushMatrix();
        glColor3fv(color.v);
        glTranslatef(pos.x, pos.y, 0);
        glScalef(size, size, 1);

        int angle = progress*360.f;
        int loops = angle/45;
        angle %= 45;
        float x = 0, y = -.5;
        settexture("data/hud/meter.png");

        glBegin(GL_TRIANGLE_FAN);
        glTexCoord2f(.5, .5);    glVertex2f(0, 0);
        glTexCoord2f(.5, 0);    glVertex2f(x, y);
        loopi(loops)
        {
            y==0||y==x? y -= x: x += y; // y-x==0
            glTexCoord2f(x+.5, y+.5);    glVertex2f(x, y);
        }

        if (angle)
        {
            float ang = sinf(angle*RAD)*(sqrtf(powf(x, 2)+powf(y, 2)));
            if (y==0||y==x) { (x < 0)? y += ang : y -= ang; } // y-x==0
            else { (y < 0)? x -= ang: x += ang; }
            glTexCoord2f(x+.5, y+.5);    glVertex2f(x, y);
        }
        glEnd();

        glPopMatrix();
    }

    void drawspinner(const char *tex, float yaw, int x, int y, float s, float blend, const vec &colour)
    {
        float tx = sinf(RAD*yaw), ty = -cosf(RAD*yaw), ts = s*sqrt(2);
        vec pos = vec(x+tx, y+ty, 0);
        settexture(tex, 3);
        glColor4f(colour.x, colour.y, colour.z, blend);
        glBegin(GL_TRIANGLE_STRIP);
        loopk(4)
        {
            vec norm;
            switch(k)
            {
                case 0: vecfromyawpitch(yaw, 0, 1, -1, norm);   glTexCoord2f(0, 1); break;
                case 1: vecfromyawpitch(yaw, 0, 1, 1, norm);    glTexCoord2f(1, 1); break;
                case 2: vecfromyawpitch(yaw, 0, -1, -1, norm);  glTexCoord2f(0, 0); break;
                case 3: vecfromyawpitch(yaw, 0, -1, 1, norm);   glTexCoord2f(1, 0); break;
            }
            norm.z = 0; norm.normalize().mul(ts).add(pos);
            glVertex2f(norm.x, norm.y);
        }
        glEnd();
    }

    vector<hudevent> hudevents;

    void drawhudevents(fpsent *d, int w, int h)
    {
        if (d != player1) return;
        if (d->state != CS_ALIVE)
        {
            hudevents.shrink(0);
            return;
        }
        float rw = ((float)w / ((float)h/1800.f)) /*, rh = 1800*/;
        float iw = rw+512.f, ix = 256;

        loopi(hudevents.length())
        {
            if (lastmillis-hudevents[i].millis > 2000)
            {
                hudevents.remove(i);
                if (hudevents.length()) hudevents[0].millis = lastmillis;
                continue;
            }

            float x = clamp((lastmillis-hudevents[i].millis)-1000.f, -1000.f, 1000.f);
            x = ((x<0? min(x+500.f, 0.f): max(x-500.f, 0.f))+500)/1000*iw -ix;
            float y = 350;

            Texture *i_special = NULL;
            switch (hudevents[i].type)
            {
                case HET_HEADSHOT:
                    i_special = textureload("data/hud/blip_blue_skull.png", 0, true, false);
                    break;
                case HET_DIRECTHIT:
                    i_special = textureload("data/hud/blip_blue_flag.png", 0, true, false);
                    break;
                default:
                case HET_KILL:
                    i_special = textureload("data/hud/blip_blue_dead.png", 0, true, false);
                    break;
            }
            float sw = i_special->xs/2,
                  sh = i_special->ys;

            glBindTexture(GL_TEXTURE_2D, i_special->id);
            glColor4f(1.f, 1.f, 1.f, 1.f);

            glBegin(GL_TRIANGLE_STRIP);
            glTexCoord2f(0, 0); glVertex2f(x-sw,       y);
            glTexCoord2f(1, 0); glVertex2f(x+sw,       y);
            glTexCoord2f(0, 1); glVertex2f(x-sw,       y+sh);
            glTexCoord2f(1, 1); glVertex2f(x+sw,       y+sh);
            glEnd();
            break;
        }
    }

    FVARP(a_scale, 0, 1.2f, 100);
    FVARP(i_round_scale, 0, 1, 100);

    void drawroundicon(int w,int h)
    {
        static Texture *i_round = NULL;
        if (!i_round) i_round = textureload("data/hud/round.png", 0, true, false);
        static Texture *i_roundnum = NULL;
        if (!i_roundnum) i_roundnum = textureload("data/hud/roundnum.png", 0, true, false);

        glPushMatrix();
        glScalef(a_scale, a_scale, 1);

        float x = 230,
              y = 1700/a_scale - i_round->ys/a_scale;
        float sw = 200*i_round_scale, sh = 100*i_round_scale;

        drawometer(min((float)(cmode ? cmode->getRemain() : 0)/(float)(max(1, cmode ? cmode->getTotal() : 1)), 1.f), 150, vec(x+120, y-80, 0), vec(1, 0, 0));

        glColor4f(1.f, 1.f, 1.f, 1.f);

        // draw "round" icon
        glBindTexture(GL_TEXTURE_2D, i_round->id);

        glBegin(GL_TRIANGLE_STRIP);
        glTexCoord2f(0, 0); glVertex2f(x,          y);
        glTexCoord2f(1, 0); glVertex2f(x+sw,       y);
        glTexCoord2f(0, 1); glVertex2f(x,          y+sh);
        glTexCoord2f(1, 1); glVertex2f(x+sw,       y+sh);
        glEnd();

        // draw round number (fingers)
        x += sw+10;
        y -= sh/2.5;

        float rxscale = min(740.f/max(1, (cmode ? cmode->getRound() : 0)*40-((cmode ? cmode->getRound() : 0)/5)*15), 1.f);
        x /= rxscale;
        glScalef(rxscale, 1, 1);

        glBindTexture(GL_TEXTURE_2D, i_roundnum->id);
        sw = 75*i_round_scale;
        sh = 150*i_round_scale;

        for(int i=0; i<(cmode ? cmode->getRound() : 0); i++)
        {
            if(i%5 == 4)
            {
                float tempx = x-(50*3);
                float tempy = y-20;
                glBegin(GL_TRIANGLE_STRIP);
                glTexCoord2f(0, 0); glVertex2f(tempx+(180.f),   tempy);
                glTexCoord2f(1, 0); glVertex2f(tempx+(220.f),   tempy+(sh*.6f));
                glTexCoord2f(0, 1); glVertex2f(tempx,           tempy+(sh*.8f));
                glTexCoord2f(1, 1); glVertex2f(tempx+(40.f),    tempy+(sh*1.3f));
                glEnd();
                x += 25;
            }
            else
            {
                glBegin(GL_TRIANGLE_STRIP);
                glTexCoord2f(0, 0); glVertex2f(i%5 < 2 ? x+sw : x, y);
                glTexCoord2f(1, 0); glVertex2f(i%5 < 2 ? x : x+sw, y);
                glTexCoord2f(0, 1); glVertex2f(i%5 < 2 ? x+sw : x, y+sh);
                glTexCoord2f(1, 1); glVertex2f(i%5 < 2 ? x : x+sw, y+sh);
                glEnd();
                x += 40;
            }
        }
        glPopMatrix();
    }

    VARP(ammohud, 0, 1, 1);
    FVARP(ammo_bar_scale, 0, 1.9f, 100);

    int lastslot = -1, flip[10] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    void drawammohud(fpsent *d, int w, int h)
    {
        float rw = ((float)w / ((float)h/1800.f)), rh = 1800, sz = HICON_SIZE;
        glColor4f(1.f, 1.f, 1.f, 1.f);

        // draw ammo bar

        static Texture *ammo_bar = NULL;
        if (d->isInfected()) ammo_bar = textureload("data/hud/ammo_bar_inf.png", 0, true, false);
        else ammo_bar = textureload("data/hud/ammo_bar.png", 0, true, false);
        glBindTexture(GL_TEXTURE_2D, ammo_bar->id);

        float aw = 512*ammo_bar_scale, ah = 256*ammo_bar_scale, ax = rw - aw, ay = rh - ah;

        if(!d->isInfected()) glColor4f(colorteam[dteamnum(d)][0], colorteam[dteamnum(d)][1], colorteam[dteamnum(d)][2], 1.f);
        else glColor4f(1.f, 1.f, 1.f, 0.5f);
        glBegin(GL_TRIANGLE_STRIP);
        glTexCoord2f(0, 0); glVertex2f(ax    ,    ay);
        glTexCoord2f(1, 0); glVertex2f(ax+aw,    ay);
        glTexCoord2f(0, 1); glVertex2f(ax    ,    ay+ah);
        glTexCoord2f(1, 1); glVertex2f(ax+aw,    ay+ah);
        glEnd();

        glPushMatrix();
        glScalef(2.f, 2.f, 1.f);

        // draw ammo count

        char tcx[10];
        int tcw, tch;
        sprintf(tcx, "%d", d->ammo[d->gunselect]);
        text_bounds(tcx, tcw, tch);
        if(d->ammo[d->gunselect] > 0) draw_text(tcx, (int)rw/2 - 80 - tcw/2, (rh-260-tch)/2);

        // draw bullets

        if (d->gunselect != WEAP_FIST && d->gunselect < WEAP_BITE)
        {
            const char *ammotex[10][2] = {
                { "data/hud/ammo_sg_a.png", "data/hud/ammo_sg_b.png" }, // SLUGSHOT
                { "data/hud/ammo_mg_a.png", "data/hud/ammo_mg_b.png" }, // MG
                { "data/hud/ammo_rl_a.png", "data/hud/ammo_rl_b.png" }, // ROCKETL
                { "data/hud/ammo_sr_a.png", "data/hud/ammo_sr_b.png" }, // SNIPER
                { "data/hud/ammo_ft_a.png", "data/hud/ammo_ft_b.png" }, // FLAMEJET
                { "data/hud/ammo_cb_a.png", "data/hud/ammo_cb_b.png" }, // CROSSBOW
                { "data/hud/ammo_gl_a.png", "data/hud/ammo_gl_b.png" }, // GRENADIER
                { "data/hud/ammo_hl_a.png", "data/hud/ammo_hl_b.png" }, // HEALER
                { "data/hud/ammo_rl_a.png", "data/hud/ammo_rl_b.png" }, // MORTAR
                { "data/hud/ammo_pt_a.png", "data/hud/ammo_pt_b.png" } // PISTOL
            };

            static Texture *ammo_bullet = NULL;
            int wmax = GUN_AMMO_MAX(d->gunselect), curslot = max(d->gunselect-1, 0), ammopx = 315,
                step[10] = { 20, 15, 35, 15, 21, 15, 45, 21, 35, 15 }, loopamt = floor(ammopx/step[curslot]);
            float size = 64, sx = (int)rw/2 - 480+(ammopx-min(step[curslot]*loopamt, ammopx)), sy = (rh-260-tch)/2, col[2] = { 1.f, 1.f };
            bool spent = false;

            if(curslot != lastslot) loopi(10) { flip[i] = rnd(2); }
            loopi(min(loopamt, wmax)) // loop 21 times or max ammo if lower
            {
                spent = i < loopamt-d->ammo[d->gunselect];
                ammo_bullet = textureload(ammotex[curslot][spent ? 1 : 0], 0, true, false);
                loopk(2) col[k] = spent ? (k ? 0.75f : 0.5f) : 1.f;
                glColor4f(col[0], col[0], col[0], col[1]);
                glBindTexture(GL_TEXTURE_2D, ammo_bullet->id);
                glBegin(GL_TRIANGLE_STRIP);
                glTexCoord2f(0, 0); glVertex2f(flip[i%10] ? sx+size : sx, sy);
                glTexCoord2f(1, 0); glVertex2f(flip[i%10] ? sx : sx+size,    sy);
                glTexCoord2f(0, 1); glVertex2f(flip[i%10] ? sx+size : sx, sy+size);
                glTexCoord2f(1, 1); glVertex2f(flip[i%10] ? sx : sx+size,    sy+size);
                glEnd();
                sx += step[curslot];
            }
            lastslot = curslot;
        }
        glPopMatrix();

        // draw weapon icons

        glPushMatrix();
        float xup = (rw-1000+sz)*1.5f, yup = HICON_Y*1.5f+8;
        glScalef(1/1.5f, 1/1.5f, 1);
        loopi(NUMWEAPS)
        {
            int gun = (i+1)%NUMWEAPS, index = weapons[gun].icon;
            if (!WEAP_USABLE(gun)) continue;
            if (gun == d->gunselect)
            {
                drawicon(index, xup - 30, yup - 60, 180);
                xup += sz * 1.5;
                continue;
            }
            if (!d->ammo[gun]) continue;

            drawicon(index, xup, yup, sz);
            xup += sz * 1.5;
        }
        glPopMatrix();
    }

    void drawhudbody(float x, float y, float sx, float sy, float ty)
    {
        glBegin(GL_TRIANGLE_STRIP);
        glTexCoord2f(0, ty); glVertex2f(x    ,    y);
        glTexCoord2f(1, ty); glVertex2f(x+sx,    y);
        glTexCoord2f(0, 1);  glVertex2f(x    ,    y+sy);
        glTexCoord2f(1, 1);  glVertex2f(x+sx,    y+sy);
        glEnd();
    }

    VARP(gunwaithud, 0, 1, 1);
    FVARP(gunwaitft_scale, 0, 1.f, 100);

    void drawcrosshairhud(fpsent *d, int w, int h)
    {
        if (!gunwaithud || d->gunwait <= 300 || d->isInfected()) return;

        float mwait = ((float)(lastmillis-d->lastaction)*(float)(lastmillis-d->lastaction))/((float)d->gunwait*(float)d->gunwait);
        mwait = clamp(mwait, 0.f, 1.f);

        //static Texture *gunwaitt = NULL;
        //if (!gunwaitt) gunwaitt = textureload("data/hud/gunwait.png", 0, true, false);
        static Texture *gunwaitft = NULL;
        if (!gunwaitft) gunwaitft = textureload("data/hud/gunwait_filled.png", 0, true, false);
        int size = 128*gunwaitft_scale;

        float rw = ((float)w / ((float)h/1800.f)), rh = 1800;
        float scale = a_scale * 1.6;
        float x = (rw/scale)/2.f+100, y = (rh-size*2.f)/(scale*2.f);

        glPushMatrix();
        glScalef(scale, scale, 1);
        glColor4f(1.0f, 1.0f, 1.0f, 0.5);

        //glBindTexture(GL_TEXTURE_2D, gunwaitt->id);
        //drawhudbody(x, y, gunwaitt->xs, gunwaitt->ys, 0);

        glBindTexture(GL_TEXTURE_2D, gunwaitft->id);
        drawhudbody(x, y + (1-mwait) * size, size, mwait * size, 1-mwait);

        glPopMatrix();
    }

    VARP(hudplayers, 0, 1, 1);

    void drawhudplayers(fpsent *d, int w, int h)
    {
        #define loopscoregroup(o, b) \
        loopv(sg->players) \
        { \
            fpsent *o = sg->players[i]; \
            b; \
        }

        int numgroups = groupplayers();
        if (numgroups <= 1) return;

        if (!hudplayers) return;

        scoregroup *sg = NULL;
        loopi(numgroups) if (!strcmp(groups[i]->team, d->team)) sg = groups[i];
        if (!sg) return;

        pushfont();
        settextscale(0.6);

        int numplayers = min(sg->players.length(), 9);
        int x = 1800*w/h - 500, y = 540;
        
        string bufs[numplayers];

        int stepw, steph;
        text_bounds("", stepw, steph);
        int endh = steph * numplayers;
        
        loopi(numplayers)
        {
            fpsent *o = sg->players[i];
            formatstring(bufs[i])("%s: %s%s", colorname(o), TEAM_COLOR_TEXT(o->team), game::getplayerclassinfo(o).name);
            int boundw, boundh;
            text_bounds(bufs[i], boundw, boundh);
            stepw = max(stepw, boundh);
        }
        
        static Texture *teamhudbg = NULL;
        if (!teamhudbg) teamhudbg = textureload("data/hud/teamhudbg.png", 0, true, false);
        glBindTexture(GL_TEXTURE_2D, teamhudbg->id);

        glColor4f(colorteam[dteamnum(d)][0], colorteam[dteamnum(d)][1], colorteam[dteamnum(d)][2], 0.5f);

        glBegin(GL_TRIANGLE_STRIP);

        glTexCoord2f(0.f, 0.f); glVertex2f(x-10,  y-10);
        glTexCoord2f(1.f, 0.f); glVertex2f(x+460, y-10);
        glTexCoord2f(0.f, 1.f); glVertex2f(x-10,  y+endh);
        glTexCoord2f(1.f, 1.f); glVertex2f(x+460, y+endh);

        glEnd();

        loopi(numplayers)
        {
            fpsent *o = sg->players[i];
            draw_textx("%d", x, y, 255, 255, 255, 244, TEXT_LEFT_JUSTIFY, -1, -1, o->frags);
            draw_text(bufs[i], x+stepw, y, 255, 255, 255, 244, -1, -1);
            y += steph;
        }

        popfont();
    }

    int lastguts=0, lastgutschangemillis=0, lastgutschange=0;
    FVARP(h_body_scale, 0, 1, 100);

    // --- RPG: Shattered Shield inventory (milestone step 4, additive; see src/rpg) ---
    // RPG HUD overlay, drawn on RPG maps only: health + mana bars skinned with
    // the kit art, the two ability icons with live cooldown feedback. This
    // replaces the FPS ammo/health readouts while in RPG play (those are gated
    // off on RPG maps below); the FPS HUD is untouched everywhere else.
    static Texture *rpg_loadtex(const char *path) { return textureload(path, 3, true, false); }
    static void rpg_fillrect(float x, float y, float w, float h, float r, float g, float b, float a = 1.0f)
    {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glColor4f(r, g, b, a);
        glBegin(GL_QUADS);
        glVertex2f(x, y); glVertex2f(x+w, y); glVertex2f(x+w, y+h); glVertex2f(x, y+h);
        glEnd();
    }
    // TEST-ONLY: direct walk control (the `forward` console command does not move the headless player; this sets the physics flag directly).
    ICOMMAND(rpgwalk, "i", (int *d), { if(player1) { player1->k_up = *d!=0; player1->move = *d!=0 ? 1 : 0; } });

    void drawrpghud(fpsent *d, int w, int h)
    {
        if(!rpg::mapactive(getclientmap())) return;
        if(d->state == CS_DEAD || d->state == CS_SPECTATOR) return;
        int mana = 0, maxmana = 0;
        rpg::getclientmana(mana, maxmana);
        float hppct = d->maxhealth > 0 ? float(d->health) / float(d->maxhealth) : 0;
        float mpct = maxmana > 0 ? float(mana) / float(maxmana) : 0;
        // bar_frame.png is 492x32: draw at native aspect (was 420x30)
        float bx = 60, bw = 492, bh = 32;
        float by = 1800 - 260;
        // health bar
        Texture *frame = rpg_loadtex("packages/ui/rpg/hud/bar_frame.png");
        glBindTexture(GL_TEXTURE_2D, frame->id);
        glColor4f(1, 1, 1, 1);
        drawtexture(bx, by, bw, bh);
        if(hppct > 0) rpg_fillrect(bx + 12, by + 6, (bw - 24) * hppct, bh - 12, 0.85f, 0.15f, 0.15f);
        defformatstring(hps)("HP %d/%d", d->health, d->maxhealth);
        draw_text(hps, int(bx), int(by - 36));
        // mana bar
        float my = by + bh + 44;
        glBindTexture(GL_TEXTURE_2D, frame->id);
        glColor4f(1, 1, 1, 1);
        drawtexture(bx, my, bw, bh);
        if(mpct > 0) rpg_fillrect(bx + 12, my + 6, (bw - 24) * mpct, bh - 12, 0.2f, 0.4f, 0.95f);
        defformatstring(mps)("MP %d/%d", mana, maxmana);
        draw_text(mps, int(bx), int(my - 36));
        // ability icons, bottom-right, with cooldown feedback
        float ix = w*1800/h - 260, iy = 1800 - 220, isz = 96;
        Texture *ps = rpg_loadtex("packages/ui/rpg/hud/icon_powerstrike.png");
        glBindTexture(GL_TEXTURE_2D, ps->id);
        glColor4f(1, 1, 1, 1);
        drawtexture(ix, iy, isz, isz);
        int cd0 = rpg::cooldownleft(0, totalmillis);
        if(cd0 > 0)
        {
            rpg_fillrect(ix, iy, isz, isz, 0.0f, 0.0f, 0.0f, 0.65f);
            defformatstring(cds)("%.1f", cd0/1000.0f);
            draw_text(cds, int(ix + 28), int(iy + 30));
        }
        else draw_text("1", int(ix + 4), int(iy - 30));
        Texture *sb = rpg_loadtex("packages/ui/rpg/hud/icon_sparkbolt.png");
        glBindTexture(GL_TEXTURE_2D, sb->id);
        glColor4f(1, 1, 1, 1);
        drawtexture(ix + isz + 20, iy, isz, isz);
        int cd1 = rpg::cooldownleft(1, totalmillis);
        if(cd1 > 0)
        {
            rpg_fillrect(ix + isz + 20, iy, isz, isz, 0.0f, 0.0f, 0.0f, 0.65f);
            defformatstring(cds2)("%.1f", cd1/1000.0f);
            draw_text(cds2, int(ix + isz + 48), int(iy + 30));
        }
        else draw_text("2", int(ix + isz + 24), int(iy - 30));
        draw_text("[U] inventory  [K] character", int(w*1800/h/2 - 260), int(1800 - 70));
    }

    void drawhudicons(fpsent *d, int w, int h)
    {
        if(d->state!=CS_DEAD)
        {
            // draw guts (RPG: hidden on RPG maps - the RPG HUD bars replace
            // the FPS readouts there; FPS/battleground modes are untouched)
            if(!rpg::mapactive(getclientmap()))
            {
                int guts = (d==player1)? d->guts: lastguts;
                defformatstring(gutss)("\f1Guts: \f3%d", guts);
                draw_text(gutss, w*1800/h - 420, 1350);

                if (guts != lastguts)
                {
                    lastgutschange = guts-lastguts;
                    lastgutschangemillis = lastmillis;
                }

                if (lastgutschange && lastmillis-lastgutschangemillis<2550)
                {
                    formatstring(gutss)(lastgutschange>0? "\fg+%d": "\fe%d", lastgutschange);
                    draw_text(gutss, w*1800/h - 262, 1300, 255, 255, 255, 255-((lastmillis-lastgutschangemillis)/10));
                }

                lastguts = guts;
            }

            // draw ammo
            if(d->quadmillis) drawicon(HICON_QUAD, 80, 1200);
            if(ammohud && !rpg::mapactive(getclientmap())) drawammohud(d, w, h);
            if(cmode && cmode->getRound() > 0) drawroundicon(w,h);
            if(shopopen())
            {
                defformatstring(shoptext)("SHOP: %ds", (shopend-lastmillis+999)/1000);
                draw_text(shoptext, w*1800/h - 262, 150, 255, 220, 80);
            }
            if(!d->isInfected()) drawcrosshairhud(d, w, h);

            // --- RPG: Shattered Shield inventory (milestone step 4): the FPS health body is hidden on RPG maps (the RPG bars replace it) ---
            if (!m_insta && !rpg::mapactive(getclientmap()))
            {
                static Texture *h_body_fire = NULL, *h_body = NULL, *h_body_white = NULL;
                if (d->isInfected())
                {
                    h_body_fire = textureload("data/hud/body_fire_inf.png", 0, true, false);
                    h_body = textureload("data/hud/body_inf.png", 0, true, false);
                    h_body_white = textureload("data/hud/body_white_inf.png", 0, true, false);
                }
                else
                {
                    h_body_fire = textureload("data/hud/body_fire.png", 0, true, false);
                    h_body = textureload("data/hud/body.png", 0, true, false);
                    h_body_white = textureload("data/hud/body_white.png", 0, true, false);
                }
                int width = 150*h_body_scale, height = 300*h_body_scale;

                glPushMatrix();
                glScalef(a_scale, a_scale, 1);

                int bx = HICON_X * 2,
                    by = HICON_Y/a_scale - height/a_scale;
                int bt = lastmillis - pmillis;

                // draw fire
                float falpha = d->onfire? (float)(abs(((lastmillis-d->burnmillis)%900)-450)+50)/500: 0.f;
                if (falpha)
                {
                    glBindTexture(GL_TEXTURE_2D, h_body_fire->id);
                    glColor4f(1.f, 1.f, 1.f, falpha);
                    // -20 works for "some reason"
                    drawhudbody(bx, HICON_Y/a_scale - height/a_scale - 20, width, height, 0);
                }

                // draw health
                lasthp += float((d->health>lasthp)? 1: (d->health<lasthp)? -1: 0) * min(curtime/10.f, float(abs(d->health-lasthp)));
                lasthp = min(lasthp, (float)d->maxhealth);
                float b = d->isInfected() ? 1.f : max(lasthp, 0.f) / d->maxhealth, ba = 1;
                if (b <= .4f)
                {
                    ba = ((float)bt-500) / 500 + 0.2;
                    if (ba < 0) ba *= -1;
                    if (bt >= 1000) pmillis = lastmillis;
                }

                glBindTexture(GL_TEXTURE_2D, h_body->id);
                drawhudbody(bx, by, width, height, 0);

                glBindTexture(GL_TEXTURE_2D, h_body_white->id);
                if (!d->isInfected()) glColor4f(1-b, max(b-0.6f, 0.f), 0.0, ba);
                else glColor4f(1.f, 1.f, 1.f, 0.5f);
                drawhudbody(bx, by + (1-b) * height, width, b * height, 1-b);

                // draw armour
                if (d->armour && !d->isInfected())
                {
                    vec col(1, 1, 1);
                    int maxarmour = 0;
                    if (d->armourtype == A_BLUE)
                    {
                        maxarmour = 50;
                        col = vec(0.5f, 0.5f, 1.f);
                    }
                    else if (d->armourtype == A_GREEN)
                    {
                        maxarmour = 100;
                        col = vec(0.5f, 1.f, 0.5f);
                    }
                    else if (d->armourtype == A_YELLOW)
                    {
                        maxarmour = 200;
                        col = vec(1.f, 0.75f, 0.f);
                    }
                    lastap += ((d->armour>lastap)? 1: (d->armour<lastap)? -1: 0) * min(curtime/10.f, float(abs(d->armour-lastap)));
                    lastap = min(lastap, (float)maxarmour);
                    float c = max(lastap, 0.f) / (float)maxarmour, aheight = 188*h_body_scale;
                    #define blend(a) min(c < a ? c*(1.f/a) : 1.f, 1.f)

                    static Texture *h_body_armour = NULL;
                    if (!h_body_armour) h_body_armour = textureload("data/hud/body_armour.png", 0, true, false);
                    glBindTexture(GL_TEXTURE_2D, h_body_armour->id);
                    glColor4f(0.f, 0.f, 0.f, blend(0.5f));
                    drawhudbody(bx, by, width, aheight, 0);
                    glColor4f(1.f, 1.f, 1.f, blend(0.25f));
                    drawhudbody(bx, by+((1.f-c)*aheight), width, c*aheight, 1.f-c);

                    static Texture *h_body_armour_col = NULL;
                    if (!h_body_armour_col) h_body_armour_col = textureload("data/hud/body_armour_col.png", 0, true, false);
                    glBindTexture(GL_TEXTURE_2D, h_body_armour_col->id);
                    glColor4f(col.x, col.y, col.z, blend(0.25f));
                    drawhudbody(bx, by+((1.f-c)*aheight), width, c*aheight, 1.f-c);
                }

                glPopMatrix();

                char tst[20];
                int tw, th;
                if (d->armour > 0)
                    sprintf(tst, "%d/%d", d->health, d->armour);
                else
                    sprintf(tst, "%d", d->health);
                text_bounds(tst, tw, th);
                draw_text(tst, bx + (width*a_scale)/2 - tw/2, by*a_scale + height*a_scale + 6);
            }
            drawhudplayers(d, w, h);
            drawhudevents(d, w, h);
       }
    }

    VARP(minradarscale, 0, 384, 10000);
    VARP(maxradarscale, 1, 1024, 10000);
    FVARP(minimapalpha, 0, 1, 1);

    float calcradarscale()
    {
        //return radarscale<=0 || radarscale>maxradarscale ? maxradarscale : max(radarscale, float(minradarscale));
        return clamp(max(minimapradius.x, minimapradius.y)/3, float(minradarscale), float(maxradarscale));
    }

    void drawminimap(fpsent *d, float x, float y, float s)
    {
        vec pos = vec(d->o).sub(minimapcenter).mul(minimapscale).add(0.5f), dir;
        vecfromyawpitch(camera1->yaw, 0, 1, 0, dir);
        float scale = calcradarscale();
        glBegin(GL_TRIANGLE_FAN);
        loopi(16)
        {
            vec tc = vec(dir).rotate_around_z(i/16.0f*2*M_PI);
            glTexCoord2f(pos.x + tc.x*scale*minimapscale.x, pos.y + tc.y*scale*minimapscale.y);
            vec v = vec(0, -1, 0).rotate_around_z(i/16.0f*2*M_PI);
            glVertex2f(x + 0.5f*s*(1.0f + v.x), y + 0.5f*s*(1.0f + v.y));
        }
        glEnd();
    }

    void drawradar(float x, float y, float s)
    {
        glBegin(GL_TRIANGLE_STRIP);
        glTexCoord2f(0.0f, 0.0f); glVertex2f(x,   y);
        glTexCoord2f(1.0f, 0.0f); glVertex2f(x+s, y);
        glTexCoord2f(0.0f, 1.0f); glVertex2f(x,   y+s);
        glTexCoord2f(1.0f, 1.0f); glVertex2f(x+s, y+s);
        glEnd();
    }

    void drawblip(fpsent *d, float x, float y, float s, const vec &pos, bool flagblip)
    {
        float scale = calcradarscale();
        vec dir = d->o;
        dir.sub(pos).div(scale);
        float size = flagblip ? 0.1f : 0.05f,
              xoffset = flagblip ? -2*(3/32.0f)*size : -size,
              yoffset = flagblip ? -2*(1 - 3/32.0f)*size : -size,
              dist = dir.magnitude2(), maxdist = 1 - 0.05f - 0.05f;
        if(dist >= maxdist) dir.mul(maxdist/dist);
        dir.rotate_around_z(-camera1->yaw*RAD);
        drawradar(x + s*0.5f*(1.0f + dir.x + xoffset), y + s*0.5f*(1.0f + dir.y + yoffset), size*s);
    }

    bool needscorebar(fpsent *d) { return d->state!=CS_EDITING && !editmode && m_teammode; }

    FVARP(scorebarsize, 0, 1, 1);
    void drawscorebar(fpsent *d, int w, int h)
    {
        if(!scorebarsize || !needscorebar(d)) return;
        
        int numgroups = groupplayers();
        if(numgroups < 2) return;
        
        float screenhalf = (1800*w/h)/2.f, pad = 10, barw = 1536*scorebarsize, barh = 128*scorebarsize, barx = screenhalf-barw/2, bary = pad, blend = 1.f,
            iconw = 88*scorebarsize, icony = bary+(barh-iconw)/2, icongap = 4*scorebarsize, ioverw = 384*scorebarsize, ioverx = screenhalf-ioverw/2,
            wingbgw = 576*scorebarsize, textlogow = 486*scorebarsize, centeroverw = 192*scorebarsize, centeroverx = screenhalf-centeroverw/2,
            textscale = scorebarsize/0.75f;
        const char *teamname[3] = { "survivors", "scavengers", "infected" };

        // scorebar itself
        Texture *t = textureload("data/hud/scorebar.png", 3);
        glBindTexture(GL_TEXTURE_2D, t->id);
        glColor4f(1.f, 1.f, 1.f, blend);
        drawtexture(barx, bary, barw, barh);

        scoregroup *winning = NULL, *runnerup = NULL;
        loopi(numgroups) if(winning == NULL || winning->team == NULL || groups[i]->score > winning->score) winning = groups[i];
        loopi(numgroups) if(groups[i] != winning && (runnerup == NULL || groups[i]->score > runnerup->score)) runnerup = groups[i];
        if(winning && runnerup)
        {
            bool winissurviver = !strcmp(winning->team, TEAM_0);
            int tone = winissurviver ? 0 : 1, ttwo = winissurviver ? 1 : 0;
            if(m_infection)
            {
                tone *= 2;
                ttwo *= 2;
            }

            // team textlogo backdrops
            t = textureload("data/hud/scorebar_wingacc.png", 3);
            glBindTexture(GL_TEXTURE_2D, t->id);
            glColor4f(colorteam[tone][0], colorteam[tone][1], colorteam[tone][2], blend);
            drawtexture(barx, bary, wingbgw, barh);
            glBindTexture(GL_TEXTURE_2D, t->id);
            glColor4f(colorteam[ttwo][0], colorteam[ttwo][1], colorteam[ttwo][2], blend);
            drawtexture(barx+barw, bary, -wingbgw, barh);

            if(d->state!=CS_SPECTATOR)
            {
                bool iswinteam = winissurviver ? !strcmp(d->team, TEAM_0) : !strcmp(d->team, TEAM_1);
                int teamd = iswinteam ? tone : ttwo;
                t = textureload("data/hud/scorebar_curteam.png", 3);
                glBindTexture(GL_TEXTURE_2D, t->id);
                glColor4f(colorteam[teamd][0], colorteam[teamd][1], colorteam[teamd][2], blend);
                drawtexture(barx+(iswinteam ? 0 : barw), bary, (iswinteam ? 1 : -1)*barh, barh);
            }

            // team textlogos
            defformatstring(textlogoone)("data/hud/scorebar_%s.png", teamname[tone]);
            t = textureload(textlogoone, 3);
            glBindTexture(GL_TEXTURE_2D, t->id);
            glColor4f(colorteam[tone][0], colorteam[tone][1], colorteam[tone][2], blend);
            drawtexture(barx, bary, textlogow, barh);
            defformatstring(textlogotwo)("data/hud/scorebar_%s.png", teamname[ttwo]);
            t = textureload(textlogotwo, 3);
            glBindTexture(GL_TEXTURE_2D, t->id);
            glColor4f(colorteam[ttwo][0], colorteam[ttwo][1], colorteam[ttwo][2], blend);
            drawtexture(barx+barw-textlogow, bary, textlogow, barh);

            // team numerical scores will go here
            bool teamwon = winning->score>=10000;
            float lscorex = ioverx, rscorex = ioverx+ioverw, scorescale = 1.5f, scorey = bary+barh*0.5f+barh*0.25f*(0.875f+0.125f*textscale);
            pushfont();
            setfont(teamwon ? "default" : "digit_white");
            settextscale((teamwon ? 0.7 : scorescale)*textscale);
            if(teamwon)
            {
                draw_textx("WIN", lscorex, scorey, colorteam[tone][0]*255.f, colorteam[tone][1]*255.f, colorteam[tone][2]*255.f, 255.f*blend, TEXT_RIGHT_UP);
            }
            else
            {
                draw_textx("%d", lscorex, scorey, colorteam[tone][0]*255.f, colorteam[tone][1]*255.f, colorteam[tone][2]*255.f, 255.f*blend, TEXT_RIGHT_UP, -1, -1, winning->score);
            }
            settextscale(1.f);
            popfont();
            pushfont();
            setfont("digit_white");
            settextscale(scorescale*textscale);
            draw_textx("%d", rscorex, scorey, colorteam[ttwo][0]*255.f, colorteam[ttwo][1]*255.f, colorteam[ttwo][2]*255.f, 255.f*blend, TEXT_LEFT_UP, -1, -1, runnerup->score);
            settextscale(1.f);
            popfont();

            // team icons
            defformatstring(teamiconone)("data/hud/team_%s.png", teamname[tone]);
            t = textureload(teamiconone, 3);
            glBindTexture(GL_TEXTURE_2D, t->id);
            glColor4f(colorteam[tone][0], colorteam[tone][1], colorteam[tone][2], blend);
            drawtexture(ioverx+icongap, icony-icongap, iconw, iconw);
            defformatstring(teamicontwo)("data/hud/team_%s.png", teamname[ttwo]);
            t = textureload(teamicontwo, 3);
            glBindTexture(GL_TEXTURE_2D, t->id);
            glColor4f(colorteam[ttwo][0], colorteam[ttwo][1], colorteam[ttwo][2], blend);
            drawtexture(ioverx-icongap+ioverw-iconw, icony-icongap, iconw, iconw);
        }

        // team icons overlay
        t = textureload("data/hud/scorebar_iconover.png", 3);
        glBindTexture(GL_TEXTURE_2D, t->id);
        glColor4f(1.f, 1.f, 1.f, blend);
        drawtexture(ioverx, bary, ioverw, barh);

        // gametype image
        float dispwait = 2000, fadeinmillis = 1000, admillis = 2000+fadeinmillis+dispwait, shiftmillis = 1000, overmillis = 500,
            timeframe = admillis+shiftmillis, timemillis = lastmillis-maptime,
            blendin = (timemillis < fadeinmillis+dispwait ? (timemillis-dispwait)/float(fadeinmillis) : 1.f),
            blendout = timemillis < admillis ? 1.f : (timemillis > admillis+shiftmillis/2.f ? 0.f : 1.f-((timemillis-admillis)/float(shiftmillis/2.f))),
            scale = timemillis > timeframe ? 0.f : (timemillis < admillis ? 1.f : 1.f-((timemillis-admillis)/float(shiftmillis))),
            logow = 192*scorebarsize, logoh = 64*scorebarsize, gamelogoy = bary+((1800/3.f)*scale), gamelogoh = logoh+logoh*scale,
            gamelogox = centeroverx-(logow/2)*scale, gamelogow = logow+logow*scale;

        if(blendout != 0)
        {
            t = textureload("data/hud/gm_back.png", 3);
            glBindTexture(GL_TEXTURE_2D, t->id);
            glColor4f(1.f, 1.f, 1.f, blendin*blendout*blend);
            drawtexture(gamelogox, gamelogoy, gamelogow, gamelogoh);
        }

        defformatstring(gamelogo)("data/hud/gamemode_%s.png", getGameMode(gamemode)->shortName);
        t = textureload(gamelogo, 3);
        glBindTexture(GL_TEXTURE_2D, t->id);
        glColor4f(1.f, 1.f, 1.f, blendin*blend);
        drawtexture(gamelogox, gamelogoy, gamelogow, gamelogoh);

        if(blendout > 0)
        {
            t = textureload("data/hud/gm_over.png", 3);
            glBindTexture(GL_TEXTURE_2D, t->id);
            glColor4f(1.f, 1.f, 1.f, blendin*blendout*blend);
            drawtexture(gamelogox, gamelogoy, gamelogow, gamelogoh);
        }

        // center display & overlay
        if(timemillis > timeframe)
        {
            float centerscale = min((timemillis-timeframe)/float(overmillis), 1.f);
            int secs = max(maplimit-lastmillis, 0)/1000, mins = secs/60;
            secs %= 60;

            pushfont();
            setfont("digit_white");
            settextscale(textscale*0.9f);
            draw_textx("%d:%02d", screenhalf, bary+barh/2.f, 255, 255, 255, 255.f*centerscale*blend, TEXT_CENTERED, -1, -1, mins, secs);
            settextscale(1.f);
            popfont();

            t = textureload("data/hud/scorebar_centerover.png", 3);
            glBindTexture(GL_TEXTURE_2D, t->id);
            glColor4f(1.f, 1.f, 1.f, centerscale*blend);
            drawtexture(centeroverx, bary, centeroverw, barh);
        }
    }

    FVARP(radartexalpha, 0, 1, 1);
    void drawradarfull(fpsent *d, int w, int h)
    {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        int s = 1800/4, x = 1800*w/h - s - s/10, y = s/10;
        glColor4f(1, 1, 1, minimapalpha);
        if(minimapalpha >= 1) glDisable(GL_BLEND);
        bindminimap();
        drawminimap(d, x, y, s);
        if(minimapalpha >= 1) glEnable(GL_BLEND);
        float margin = 0.04f, roffset = s*margin, rsize = s + 2*roffset, blend = clamp((lastmillis%5000)/5000.f, 0.f, 1.f), fade = 1.f-blend, rpoffset = rsize*0.5f*(1.f-blend);
        settexture("data/hud/radarping.png", 3);
        glColor4f(1, 1, 1, fade > 0.75f ? 0.75f : fade);
        drawradar(x - roffset + rpoffset, y - roffset + rpoffset, rsize*blend);
        settexture("data/hud/radar.png", 3);
        glColor4f(1, 1, 1, radartexalpha);
        drawradar(x - roffset, y - roffset, rsize);
        loopi(2)
        {
            if(i) settexture("data/hud/compass.png", 3);
            else settexture("data/hud/compass_accent.png");
            glPushMatrix();
            glTranslatef(x - roffset + 0.5f*rsize, y - roffset + 0.5f*rsize, 0);
            glRotatef(camera1->yaw + 180, 0, 0, -1);
            if(i) glColor4f(1, 1, 1, radartexalpha);
            else glColor4f(colorteam[dteamnum(d)][0], colorteam[dteamnum(d)][1], colorteam[dteamnum(d)][2], 1.f);
            drawradar(-0.5f*rsize, -0.5f*rsize, rsize);
            glPopMatrix();
        }

        glColor4f(1.f, 1.f, 1.f, 1.f);

        settexture(!strcmp(d->team, TEAM_0) ? "data/hud/blip_blue.png" : "data/hud/blip_red.png");
        loopv(players) if (players[i] != d && isteam(players[i]->team, d->team) && players[i]->state == CS_ALIVE)
        {
            drawblip(d, x, y, s, players[i]->o, false);
        }

        extentity *ent;
        int lastc = 0;
        float rscale = calcradarscale();
        rscale *= rscale;
        loopv(entities::ents)
        {
            ent = entities::ents[i];
            if (!ent->spawned || ent->o.squaredist(d->o) > rscale) continue;

            if (ent->type >= I_HEALTH && ent->type <= I_HEALTH3)
            {
                if (lastc != 1) settexture("data/hud/blip_health.png");
                else lastc = 1;
            }
            else if (ent->type >= I_AMMO && ent->type <= I_AMMO4)
            {
                if (lastc != 2) settexture("data/hud/blip_ammo.png");
                else lastc = 2;
            }
            else if (ent->type == I_GREENARMOUR)
            {
                if (lastc != 3) settexture("data/hud/blip_garmour.png");
                else lastc = 3;
            }
            else if (ent->type == I_YELLOWARMOUR)
            {
                if (lastc != 4) settexture("data/hud/blip_yarmour.png");
                else lastc = 4;
            }
            else if (ent->type == I_MORTAR)
            {
                if (lastc != 5) settexture("data/hud/blip_red.png");
                else lastc = 5;
            }
            else
            {
                if (lastc != 6) settexture("data/hud/blip_quad.png");
                else lastc = 6;
            }

            drawblip(d, x, y, s, ent->o, false);
        }

        if (cmode) cmode->drawblips(d, w, h, x, y, s, rscale);
    }

    void drawprogress(int x, int y, float start, float length, float size, bool left, bool sec, bool millis, const vec &colour, float fade, float skew, float textscale, const char *text, ...)
    {
        if(skew <= 0.f) return;
        float q = clamp(skew, 0.f, 1.f), s = size*skew, cs = int(s)/2, cx = left ? x+cs : x-cs, cy = y-cs;
        vec col(colour.x*q, colour.y*q, colour.z*q);
        settexture("data/hud/progressbg.png", 3);
        glColor4f(col.x, col.y, col.z, fade*0.25f);
        drawslice(0, 1, cx, cy, cs*2/3);
        glColor4f(col.x, col.y, col.z, fade);
        if(sec) drawspinner("data/hud/progresssec.png", (((millis ? SDL_GetTicks() : lastmillis)%10000)/10000.f)*360.f, cx, cy, cs, fade/2, col); // only lastmillis is affected by game pausing
        settexture("data/hud/progress.png", 3);
        drawslice(start, length, cx, cy, cs*2/3);
        if(text && *text)
        {
            glPushMatrix();
            glScalef(skew, skew, 1);
            if(textscale) settextscale(textscale);
            int tx = int(cx*(1.f/skew)), ty = int((cy-FONTH/2*skew)*(1.f/skew)), ti = int(255.f*fade);
            defvformatstring(str, text, text);
            draw_textx("%s", tx, ty+FONTH/8.f, 255, 255, 255, ti, TEXT_CENTERED, -1, -1, str);
            if(textscale) settextscale(1);
            glPopMatrix();
        }
    }

    VARP(showtimers, 0, 1, 1);
    void drawtimers(fpsent *d, int w, int h)
    {
        float cs = 0.06f*1800*w/h, blend = 1;
        int rs = 1800/4, cx = 1800*w/h-(needminimap(!d->isInfected()) ? rs+cs/2 : 0)-rs/10, cy = cs+rs/10;
        vec colour(colorteam[dteamnum(d)][0], colorteam[dteamnum(d)][1], colorteam[dteamnum(d)][2]);
        if(m_timed)
        {
            if(totalmillis-lastinterm > 10000)
            { // game vote
                if(!game::intermission) lastinterm = 0;
                else
                {
                    int votelimit = 30000, millis = votelimit-(totalmillis-(lastinterm+10000));
                    float amt = float(millis)/float(votelimit);
                    const char *col = "\fw";
                    if(amt > 0.75f) col = "\fg";
                    else if(amt > 0.5f) col = "\fy";
                    else if(amt > 0.25f) col = "\fo";
                    else col = "\fr";
                    drawprogress(cx, cy, 0, 1, cs, false, false, false, colour, blend*0.25f, 1);
                    drawprogress(cx, cy, 1-amt, amt, cs, false, true, false, colour, blend, 1, 0.7f, "%s%d", col, int(millis/1000.f));
                }
            }
            else if(game::intermission)
            {
                drawprogress(cx, cy, 0, 0, cs, false, true, true, vec(0.5f, 0.5f, 0.5f), blend*0.5f, 1.f, 1.f, "");
                drawprogress(cx, cy, 0, 1, cs, false, false, false, colour, blend, 1.f, 0.7f, "\frEND"); // intermission
            }
        }
        else if(ispaused()) drawprogress(cx, cy, 0, 1, cs, false, true, true, colour, blend*0.75f, 1.f, 0.5f, "PAUSED"); // paused
    }

    void gameplayhud(int w, int h)
    {
        fpsent *d = hudplayer();

        if (d->state == CS_DEAD)
        {
            fpsent *t = (fpsent *)followcam();
            if (t) d = t;
        }

        glPushMatrix();
        glScalef(h/1800.0f, h/1800.0f, 1);

        if(player1->state==CS_SPECTATOR)
        {
            int pw, ph, tw, th, fw, fh;
            text_bounds("  ", pw, ph);
            text_bounds("SPECTATOR", tw, th);
            th = max(th, ph);
            fpsent *f = followingplayer();
            text_bounds(f ? colorname(f) : " ", fw, fh);
            fh = max(fh, ph);
            draw_text("SPECTATOR", w*1800/h - tw - pw, 1200 - th - fh);
            if(f) draw_text(colorname(f), w*1800/h - fw - pw, 1200 - fh);
        }

        if(needscorebar(player1)) drawscorebar(d, w, h); // draw scorebar

        if(needminimap(!d->isInfected())) drawradarfull(d, w, h); // draw minimap
        if(showtimers) drawtimers(d, w, h);

        if(d->state!=CS_EDITING)
        {
            if(d->state!=CS_SPECTATOR) drawhudicons(d, w, h);
            if(d->state!=CS_SPECTATOR) drawrpghud(d, w, h);
            if(cmode) cmode->drawhud(d, w, h);
        }

        glPopMatrix();
    }

    int clipconsole(int w, int h)
    {
        return (h*(1 + 1 + 10))/(4*10);
    }

    VARP(weapcrosshair, 0, 1, 1);
    VARP(teamcrosshair, 0, 1, 1);
    VARP(hitcrosshair, 0, 425, 1000);

    SVARP(crosshair_default, "dot");
    SVARP(crosshair_team, "teammate");
    SVARP(crosshair_chainsaw, "brace");
    SVARP(crosshair_slugshot, "bounds");
    SVARP(crosshair_mg, "quadc");
    SVARP(crosshair_launcher_rocket, "laun");
    SVARP(crosshair_sniper, "radar");
    SVARP(crosshair_flamejet, "eclipse");
    SVARP(crosshair_crossbow, "circ");
    SVARP(crosshair_launcher_grenade, "underc");
    SVARP(crosshair_healer, "sonic");
    SVARP(crosshair_mortar, "boundo");
    SVARP(crosshair_pistol, "dot");

    const char *getCrosshairName(int index)
    {
        switch(index)
        {
            default: 
            case 0:     return crosshair_default;               break; // default
            case 1:     return crosshair_team;                  break; // teammate
            case 2:     return crosshair_chainsaw;              break; // chainsaw
            case 3:     return crosshair_slugshot;              break; // Slugshot
            case 4:     return crosshair_mg;                    break; // MG
            case 5:     return crosshair_launcher_rocket;       break; // RocketL
            case 6:     return crosshair_sniper;                break; // Sniper
            case 7:     return crosshair_flamejet;              break; // FlameJet
            case 8:     return crosshair_crossbow;              break; // CrossBow
            case 9:     return crosshair_launcher_grenade;      break; // Grenadier
            case 10:    return crosshair_healer;                break; // Healer
            case 11:    return crosshair_mortar;                break; // Mortar
            case 12:    return crosshair_pistol;                break; // Pistol
        }
    }

    VARP(hidescopech, 0, 1, 1);

    int selectcrosshair(float &r, float &g, float &b, float &f)
    {
        fpsent *d = hudplayer();
        if(d->state==CS_SPECTATOR || d->state==CS_DEAD) return -1;
        if(d->state!=CS_ALIVE) return 0;
        int index = 0;
        if(!editmode && !m_insta)
        {
            float p = float(float(d->health)/float(d->maxhealth));
            r = 1.0f; g = p; b = p - ((d->maxhealth - d->health)*0.01);
        }
        if(d->gunwait) { /*r *= 0.5f; g *= 0.5f; b *= 0.5f;*/ f *= (game::mwaiti() && !zoom ? 0.5f : 1.f)*(game::mwaiti() && zoom ? game::mwaiti() : 1.f); }
        if(hidescopech && zoom && isscopedweap()) index = -1;
        else
        {
            if(weapcrosshair) index = d->hudgun+2;
            if(lasthit && lastmillis - lasthit < hitcrosshair) index *= -1;
        }
        if((m_teammode || m_oneteam) && teamcrosshair)
        {
            dynent *o = intersectclosest(d->o, worldpos, d);
            if(o && o->type==ENT_PLAYER && !((fpsent *)o)->isInfected() && weapons[d->hudgun].damage > 0 && 0 == strcmp(((fpsent *)o)->team, d->team))
            {
                index = 1;
                r = !strcmp(d->team, TEAM_0) ? 0 : 1;
                g = 0;
                b = !strcmp(d->team, TEAM_0) ? 1 : 0;
            }
        }
        return index;
    }

    void lighteffects(dynent *e, vec &color, vec &dir)
    {
        int irsm = hudplayer()->irsm;
        if ((e->type==ENT_PLAYER || e->type==ENT_AI) && irsm)
        {
            if(e->type==ENT_PLAYER && isteam(TEAM_0, ((fpsent*)e)->team))
            {
                color.z = max(float(irsm)/(float)WEAP(WEAP_SNIPER,attackdelay), 1.0f);
                color.y *= 1.0f-color.z;
                color.x = color.y;
            }
            else
            {
                color.x = max(float(irsm)/(float)WEAP(WEAP_SNIPER,attackdelay), 1.0f);
                color.y *= 1.0f-color.x;
                color.z = color.y;                
            }
        }
#if 0
        fpsent *d = (fpsent *)e;
        if(d->state!=CS_DEAD && d->quadmillis)
        {
            float t = 0.5f + 0.5f*sinf(2*M_PI*lastmillis/1000.0f);
            color.y = color.y*(1-t) + t;
        }
#endif
    }

    // any data written into this vector will get saved with the map data. Must take care to do own versioning, and endianess if applicable. Will not get called when loading maps from other games, so provide defaults.
    void writegamedata(vector<char> &extras) {}
    void readgamedata(vector<char> &extras) {}

    const char *savedconfig() { return "config/config.cfg"; }
    const char *restoreconfig() { return "config/restore.cfg"; }
    const char *defaultconfig() { return "data/defaults.cfg"; }
    const char *autoexec() { return "config/autoexec.cfg"; }

    void loadconfigs()
    {
    }

    const char *getentname(dynent *d) // debugging helper function
    {
        return (d->type==ENT_PLAYER)? ((fpsent*)d)->name: "n/a";
    }

    //SVAR(curmaps, "");

    //void genmaplist()
    //{
    //    char *maplist = curmaps;
    //    string curmap;
    //    while (*maplist)
    //    {
    //        int len = 0;
    //        while (maplist[len] != ' ' && maplist[len] != '\0') len++;
    //        strncpy(curmap, maplist, len);
    //        curmap[len] = '\0';
    //        maplist += len;
    //        if (*maplist) maplist += 1;

    //        cgui->text("\fgmaps: ", 0xFFFFFF);
    //        conoutf("%s", curmap);
    //    }
    //}
    //COMMAND(genmaplist, "");
}

