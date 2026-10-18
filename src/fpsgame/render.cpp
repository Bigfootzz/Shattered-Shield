#include "game.h"

#if defined(WITH_BULLET) && WITH_BULLET
//extern vec ballPosition;
vec ballPosition (0,0,0);
#endif

namespace game
{
    vector<fpsent *> bestplayers;
    vector<const char *> bestteams;

    VARP(ragdoll, 0, 1, 1);
    VARP(ragdollmillis, 0, 10000, 300000);
    VARP(ragdollfade, 0, 1000, 300000);
    extern int playermodel;
#ifdef _DEBUG
    VARP(forceplayermodels, 0, 0, 1);
    VARP(allplayermodels, 0, 0, 1);
#else
    static const int forceplayermodels = 0;
    static const int allplayermodels = 0;
#endif
    VARP(hidedead, 0, 0, 1);

    vector<fpsent *> ragdolls;

    void saveragdoll(fpsent *d)
    {
        if(!d->ragdoll || !ragdollmillis || (!ragdollfade && lastmillis > d->lastpain + ragdollmillis)) return;
        fpsent *r = new fpsent(*d);
        r->lastupdate = ragdollfade && lastmillis > d->lastpain + max(ragdollmillis - ragdollfade, 0) ? lastmillis - max(ragdollmillis - ragdollfade, 0) : d->lastpain;
        r->edit = NULL;
        r->attackchan = r->idlechan = -1;
        ragdolls.add(r);
        d->ragdoll = NULL;
    }

    void clearragdolls()
    {
        ragdolls.deletecontents();
    }

    void moveragdolls()
    {
        loopv(ragdolls)
        {
            fpsent *d = ragdolls[i];
            if(lastmillis > d->lastupdate + ragdollmillis)
            {
                delete ragdolls.remove(i--);
                continue;
            }
            moveragdoll(d);
        }
    }

    void changedplayermodel()
    {
        if(player1->ragdoll) cleanragdoll(player1);
        loopv(ragdolls)
        {
            fpsent *d = ragdolls[i];
            if(!d->ragdoll) continue;
            if(!forceplayermodels)
            {
                const playermodelinfo *mdl = getplayermodelinfo(d->playermodel);
                if(mdl && (mdl->selectable || allplayermodels)) continue;
            }
            cleanragdoll(d);
        }
        loopv(players)
        {
            fpsent *d = players[i];
            if(d == player1 || !d->ragdoll) continue;
            if(!forceplayermodels)
            {
                const playermodelinfo *mdl = getplayermodelinfo(d->playermodel);
                if(mdl && (mdl->selectable || allplayermodels)) continue;
            }
            cleanragdoll(d);
        }
    }

    void preloadplayermodel()
    {
        loopi(NUMPLAYERMODELS)
        {
            const playermodelinfo *mdl = getplayermodelinfo(i);
            if(!mdl) break;
            if(i != playermodel && (!multiplayer(false) || forceplayermodels || (!mdl->selectable && !allplayermodels))) continue;
            if(m_teammode)
            {
                preloadmodel(mdl->blueteam);
                preloadmodel(mdl->redteam);
            }
            else preloadmodel(mdl->ffa);
            if(mdl->vwep) preloadmodel(mdl->vwep);
            if(mdl->quad) preloadmodel(mdl->quad);
            loopj(3) if(mdl->armour[j]) preloadmodel(mdl->armour[j]);
        }
        ::monster::preloadMonsters();
    }

    VAR(testquad, 0, 0, 1);
    VAR(testarmour, 0, 0, 1);
    VAR(testteam, 0, 0, 3);

    void renderplayer(fpsent *d, const playermodelinfo &mdl, int team, float fade, bool mainpass)
    {
        ASSERT(d->gunselect >= 0 && d->gunselect < NUMWEAPS);
        int lastaction = d->lastaction,
            hold = mdl.vwep || d->gunselect==WEAP_PISTOL ? 0
                                                         : clamp<int, int>(ANIM_HOLD1+d->gunselect, ANIM_HOLD1, ANIM_HOLD7)|ANIM_LOOP,
            attack = clamp<int, int>(ANIM_ATTACK1+d->gunselect, ANIM_ATTACK1, ANIM_ATTACK7),
            delay = mdl.vwep ? 300 : weapons[d->gunselect].attackdelay+50;

        //Zombies use attack indexes based on the zombie class
        if(d->isInfected())
        {
            const playerclassinfo &info = ::game::getplayerclassinfo(d);
            loopi(WEAPONS_PER_CLASS)
            {
                if(info.weap[i] == d->gunselect)
                {
                    hold = clamp<int, int>(ANIM_HOLD1+d->gunselect, ANIM_HOLD1, ANIM_HOLD7)|ANIM_LOOP;
                    attack = clamp<int, int>(ANIM_ATTACK1+d->gunselect, ANIM_ATTACK1, ANIM_ATTACK7);
                    break;
                }
            }
        }

        if(intermission && d->state!=CS_DEAD)
        {
            lastaction = 0;
            hold = attack = ANIM_LOSE|ANIM_LOOP;
            delay = 0;
            if(m_teammode ? bestteams.htfind(d->team)>=0 : bestplayers.find(d)>=0) hold = attack = ANIM_WIN|ANIM_LOOP;
        }
        else if(d->state==CS_ALIVE && d->lasttaunt && lastmillis-d->lasttaunt<1000 && lastmillis-d->lastaction>delay)
        {
            lastaction = d->lasttaunt;
            hold = attack = ANIM_TAUNT;
            delay = 1000;
        }
        modelattach a[5];
        static const char * const vweps[] = {"playermodels/vwep/fist", "playermodels/vwep/shotg", "playermodels/vwep/chaing", "playermodels/vwep/rocket", "playermodels/vwep/rifle", "playermodels/vwep/flameg", "playermodels/vwep/cbow", "playermodels/vwep/gl", "playermodels/vwep/healer", "playermodels/vwep/mortar", "playermodels/vwep/pistol", "playermodels/vwep/hand"};
        int ai = 0;
        if((!mdl.vwep || d->gunselect!=WEAP_FIST) && d->gunselect<=WEAP_PISTOL && !d->isInfected())
        {
            int vanim = ANIM_VWEP_IDLE|ANIM_LOOP, vtime = 0;
            if(lastaction && d->lastattackgun==d->gunselect && lastmillis < lastaction + delay)
            {
                vanim = ANIM_VWEP_SHOOT;
                vtime = lastaction;
            }
            a[ai++] = modelattach("tag_weapon", mdl.vwep ? mdl.vwep : vweps[
                d->gunselect + (d->gunselect == WEAP_FIST && d->ammo[d->gunselect] < 1 ? sizeof(vweps)/sizeof(vweps[0]) - 1 : 0)
            ], vanim, vtime);
        }
        if(d->state==CS_ALIVE)
        {
            if((testquad || d->quadmillis) && mdl.quad)
                a[ai++] = modelattach("tag_powerup", mdl.quad, ANIM_POWERUP|ANIM_LOOP, 0);
            if(testarmour || d->armour)
            {
                int type = clamp(d->armourtype, (int)A_BLUE, (int)A_YELLOW);
                if(mdl.armour[type])
                    a[ai++] = modelattach("tag_shield", mdl.armour[type], ANIM_SHIELD|ANIM_LOOP, 0);
            }
        }
        if(mainpass)
        {
            d->muzzle = vec(-1, -1, -1);
            a[ai++] = modelattach("tag_muzzle", &d->muzzle);
        }
        const char *mdlname = mdl.ffa;
        switch(testteam ? testteam-1 : team)
        {
            case 1: mdlname = mdl.blueteam; break;
            case 2: mdlname = mdl.redteam; break;
        }
        renderclient(d, mdlname, a[0].tag ? a : NULL, hold, attack, delay, lastaction, intermission && d->state!=CS_DEAD ? 0 : d->lastpain, fade, ragdoll && mdl.ragdoll, d==player1);

        const playerclassinfo &pci = game::getplayerclassinfo(hudplayer());
        if(pci.abilities & ABILITY_SEE_HEALTH && d->state == CS_ALIVE && !d->isInfected() && !hudplayer()->isInfected())
        {
            vec above(d->o);
            above.z += 4;

            float distance = above.squaredist(hudplayer()->o);
            if(distance < 10000.f)
            {
                particle_meter(above, float(d->health)/float(d->maxhealth), PART_METER, 1, strcmp(d->team, TEAM_0) ? 0xFF4B19 : 0x3219FF, 0, 2.0f);
            }
        }
#if 0
        if(d->state!=CS_DEAD && d->quadmillis)
        {
            entitylight light;
            rendermodel(&light, "quadrings", ANIM_MAPMODEL|ANIM_LOOP, vec(d->o).sub(vec(0, 0, d->eyeheight/2)), 360*lastmillis/1000.0f, 0, MDL_DYNSHADOW | MDL_CULL_VFC | MDL_CULL_DIST);
        }
#endif
    }

    VARP(teamskins, 0, 0, 1);
    VARDBG(dbghitbox, 0);

    void renderhitbox(fpsent *d)
    {
        if(dbghitbox && d->state!=CS_DEAD)
        {
            vec bottom(d->o), top(d->o);
            bottom.z -= d->eyeheight;
            top.z += d->aboveeye;
            
            vec direction(d->yaw, d->pitch);
            direction.z = 0;
            direction.normalize().mul(d->radius);
            
            int colors[] = {
                0xFF0000,
                0xAA0000,
                0xAAAA00,
                0x00AA00,
                0x00AAAA,
                0x0000AA,
                0xAA00AA,
                0x0000FF,
            };
            
            loopi(8)
            {
                float angle = float(i)/8.f*float(2)*M_PI;
                vec spoke(direction);
                spoke.rotate_around_z(angle);
                
                vec bottomSpoke(spoke);
                bottomSpoke.add(bottom);
                
                vec topSpoke(spoke);
                topSpoke.add(top);
                
                spoke.add(d->o);
                
                #define PARTICLE_OPTS 5, PART_DEBUG_LINE, colors[i], 1.0f
                particle_flare(bottom, bottomSpoke, PARTICLE_OPTS);
                particle_flare(bottomSpoke, topSpoke, PARTICLE_OPTS);
                particle_flare(topSpoke, top, PARTICLE_OPTS);
                
                if(i == 0)
                {
                    vec eyeSpoke(direction);
                    eyeSpoke.normalize()
                    .mul(d->radius * 2)
                    .add(d->o);
                    particle_flare(d->o, eyeSpoke, PARTICLE_OPTS);
                    
                    vec headSpoke (direction);
                    headSpoke.normalize()
                    .mul(d->radius * 2)
                    .add(d->o);
                    
                    headSpoke.z = (d->eyeheight + d->aboveeye) * 0.8f + (d->o.z - d->eyeheight);
                    
                    vec head(d->o);
                    head.z = headSpoke.z;
                    
                    particle_flare(head, headSpoke, PARTICLE_OPTS);
                }
                #undef PARTICLE_OPTS
            }
        }
    }

    #if defined(WITH_BULLET) && WITH_BULLET
    VAR(renderBall, 0, 1, 1);
    #endif
    
    void rendergame(bool mainpass)
    {
        if(mainpass) ai::render();

        if(intermission)
        {
            bestteams.shrink(0);
            bestplayers.shrink(0);
            if(m_teammode) getbestteams(bestteams);
            else getbestplayers(bestplayers);
        }

        startmodelbatches();

        fpsent *exclude = isthirdperson() ? NULL : followingplayer();
        loopv(players)
        {
            fpsent *d = players[i];
            if(d == player1 || d->state==CS_SPECTATOR || d->state==CS_SPAWNING || d->lifesequence < 0 || d == exclude || (d->state==CS_DEAD && hidedead)) continue;
            int team = 0;
            if((teamskins || m_teammode) && !(m_infection && d->isInfected())) team = !strcmp(d->team, TEAM_0) ? 1 : 2; // player1->team experimental
            renderplayer(d, getplayermodelinfo(d), team, 1, mainpass);
            copystring(d->info, colorname(d));
            if (d->isInfected()) concatstring(d->info, "  infected");
            if(d->state!=CS_DEAD) particle_text(d->abovehead(), d->info, PART_TEXT, 1, team ? (team==1 ? 0x6496FF : 0xFF4B19) : 0x1EC850, 2.0f);
            renderhitbox(d);
        }
        loopv(ragdolls)
        {
            fpsent *d = ragdolls[i];
            int team = 0;
            if(teamskins || m_teammode) team = d->isInfected() ? 0 : !strcmp(hudplayer()->team, TEAM_0) ? 1 : 2;
            float fade = 1.0f;
            if(ragdollmillis && ragdollfade)
                fade -= clamp(float(lastmillis - (d->lastupdate + max(ragdollmillis - ragdollfade, 0)))/min(ragdollmillis, ragdollfade), 0.0f, 1.0f);
            renderplayer(d, getplayermodelinfo(d), team, fade, mainpass);
        }
        if(isthirdperson() && !followingplayer() && (player1->state!=CS_DEAD || !hidedead)) renderplayer(player1, getplayermodelinfo(player1), (teamskins || m_teammode) && !player1->isInfected() ? 1 : 0, player1->state==CS_DEAD? 1: 0.3, mainpass);
        rendermovables();
        entities::renderentities();
        renderbouncers();
        renderprojectiles();
        if(cmode) cmode->rendergame();
        #if defined(WITH_BULLET) && WITH_BULLET
        if(renderBall) rendermodel(NULL, "mapmodels/cid/ball", ANIM_IDLE, ballPosition);
        #endif

        endmodelbatches();
    }

    VARP(hudgun, 0, 1, 1);
    VARP(hudgunsway, 0, 1, 1);
    VARP(teamhudguns, 0, 1, 1);
    VARP(chainsawhudgun, 0, 1, 1);
    VAR(testhudgun, 0, 0, 1);

    FVAR(swaystep, 1, 35.0f, 100);
    FVAR(swayside, 0, 0.04f, 1);
    FVAR(swayup, 0, 0.05f, 1);

    float swayfade = 0, swayspeed = 0, swaydist = 0;
    vec swaydir(0, 0, 0);

    void swayhudgun(int curtime)
    {
        fpsent *d = hudplayer();
        if(d->state != CS_SPECTATOR)
        {
            if(d->physstate >= PHYS_SLOPE)
            {
                swayspeed = min(sqrtf(d->vel.x*d->vel.x + d->vel.y*d->vel.y), d->maxspeed);
                swaydist += swayspeed*curtime/1000.0f;
                swaydist = fmod(swaydist, 2*swaystep);
                swayfade = 1;
            }
            else if(swayfade > 0)
            {
                swaydist += swayspeed*swayfade*curtime/1000.0f;
                swaydist = fmod(swaydist, 2*swaystep);
                swayfade -= 0.5f*(curtime*d->maxspeed)/(swaystep*1000.0f);
            }

            float k = pow(0.7f, curtime/10.0f);
            swaydir.mul(k);
            vec vel(d->vel);
            vel.add(d->falling);
            swaydir.add(vec(vel).mul((1-k)/(15*max(vel.magnitude(), d->maxspeed))));
        }
    }

    struct hudent : dynent
    {
        hudent() { type = ENT_CAMERA; }
    } guninterp;

    SVARP(hudgunsdir, "");

    int hudgunfm = 0;
    VARP(hudgunfade, 0, 250, 10000);

    //FVARP(firstpersondist, -10000, 0.01f, 10000);
    //FVARP(firstpersonshift, -10000, -0.1f, 10000);
    //FVARP(firstpersonadjust, -10000, -0.02f, 10000);
    FVARP(firstpersonyaw, -10000, 0.0625f, 10000);
    FVARP(firstpersonpitch, -10000, 0.0625f, 10000);
    VARP(firstpersonidle, 0, 7, 1000);
    FVARP(firstpersonidleamt, -10000, 0.0625f, 10000);
    FVARP(firstpersonidlemargin, -10000, 0.95f, 10000);
    //FVARP(addyaw, -10000, 0.0f, 10000);
    //FVARP(addpitch, -10000, 0.0f, 10000);

    float ironsight[NUMWEAPS+1][4][3] = { // ironsight[weap][0 = normal, 1 = sighted, +2 = vec(yaw, pitch, roll)]
        { {0.1f, 0.02f, 0},          {-0.1f, -0.025f, -0.2f},     {0, 0, 0},       {0, 15.f, 0} },      // 00: Fist
        { {0.1f, 0.02f, 0},          {-0.1425f, 0.05f, 0.025f},   {0, 0, 0},       {4.75f, -5.f, 0} },  // 01: Slugshot
        { {0.1f, 0.01f, 0},          {-0.105f, 0.005f, -0.4f},    {0, 0, 0},       {2.f, 0, 0} },       // 02: MG
        { {0.09f, 0.06f, -0.2f},     {-0.07f, 0.0291f, -0.52f},   {0, 0, 0},       {0, 0, 0} },         // 03: RocketL
        { {0.09f, -0.0125f, 0},      {-0.126f, 0.021f, -0.11f},   {0, 0, 0},       {0, 0, 0} },         // 04: Sniper
        { {0.05f, 0.01f, 0.05f},     {-0.105f, 0.025f, -0.1f},    {-10.f, 0, 0},   {-8.f, 0, 0} },      // 05: FlameJet
        { {0, 0, 0},                 {-0.157f, 0.0315f, -0.1f},   {0, 0, 0},       {10.f, -5.f, 0} },   // 06: CrossBow
        { {0.1f, 0.01f, 0},          {0.1f, 0, -0.3f},            {0, 0, 0},       {0, 0, 0} },         // 07: Grenadier
        { {0.04f, 0, -0.075f},       {-0.0825f, 0.025f, -0.2f},   {-7.5f, 5.f, 0}, {-7.5f, 5.f, 0} },   // 08: Healer
        { {0, 0, 0},                 {0, 0, 0},                   {0, 0, 0},       {0, 0, 0} },         // 09: Mortar
        { {0.07f, 0.02f, -0.1f},     {-0.2155f, 0.0775f, -0.35f}, {0, 0, 0},       {7.f, -4.5f, 0} },   // 10: Pistol
        { {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0} },     // 11: Bite (Bite & Barrel implemented as forced variables if ever called)
        { {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0} },     // 12: Barrel
        { {0.001f, -0.01f, -0.02},   {0.001f, -0.01f, -0.02},     {0, 0, 0},       {0, 0, 0} }          // 13: Fist (Infected)
    }; // {shift (x), adjust (y), dist (z)}

    float mwait(fpsent* d = player1, int weap = -1)
    {
        if(d->isInfected()) return 0;
        float mwait = clamp((((float)(lastmillis-d->lastaction)*(float)(lastmillis-d->lastaction))/((float)d->gunwait*(float)d->gunwait)), 0.f, 1.f),
            mweap[NUMWEAPS] = { 0, 0.2f, 0.0025f, 0.03f, 0.06f, 0, 0.03f, 0.02f, 0, 0, 0.01f, 0, 0 };
        return weap >= 0 ? mweap[weap] : (mwait < 0.5f ? mwait*2.f : 1.f-((mwait-0.5f)*2.f));
    }
    float mwaiti() { return mwait(player1, -1); }

    float idlepulse(float scale)
    {
        int timestep = firstpersonidle ? (lastmillis%(360*firstpersonidle))/firstpersonidle : 0;
        return sinf(RAD*timestep)*scale;
    }

    void drawhudmodel(fpsent *d, int anim, float speed = 0, int base = 0)
    {
        if(d->gunselect>WEAP_BITE) return;
        vec dir, orig = d->o;
        int fmillis = lastmillis-hudgunfm;
        extern int dbgisinfected;
        bool isInfected = dbgisinfected || d->isInfected();

        float yaw = d->yaw, pitch = d->pitch, hfade = min((((float)abs(fmillis-(hudgunfade/2.f)))/(hudgunfade/2.f)), 1.f), zfadespeed = isscopedweap(d->gunselect) ? 2.f : 1.f,
            zfadeamt = 1.f-min(scopev*zfadespeed, 1.f), zfade = !isInfected && zoom ? zfadeamt : 1.f, mfade = hfade*zfade, wfade = 0.06f*(1.f-hfade);

        if (d->gunselect != d->hudgun)
        {
            //todo: fix fps related bug where hudgun doesn't change
            if (fmillis>hudgunfade) { hudgunfm = lastmillis; fmillis = 0; }
            if (fmillis>=(hudgunfade/2.f)) d->hudgun = d->gunselect;
        }
        if (fmillis>hudgunfade) { hudgun = 1; hfade *= zoom && isscopedweap(d->gunselect) ? zfadeamt : 1.f; }
        int hudindex = isInfected ? WEAP_INFECTED : d->hudgun;
        float firstpersonshift = ironsight[hudindex][0][0]*mfade+ironsight[hudindex][1][0]*(1.f-mfade)+mwait(d, hudindex)*mwait(d)*0.75f,
            firstpersonadjust = ironsight[hudindex][0][1]*mfade+ironsight[hudindex][1][1]*(1.f-mfade)-mwait(d, hudindex)*mwait(d)-wfade,
            firstpersondist = ironsight[hudindex][0][2]*mfade+ironsight[hudindex][1][2]*(1.f-mfade),
            addyaw = ironsight[hudindex][2][0]*mfade+ironsight[hudindex][3][0]*(1.f-mfade),
            addpitch = ironsight[hudindex][2][1]*mfade+ironsight[hudindex][3][1]*(1.f-mfade);

        zfade = zfade*100.f > 1 ? zfade : 0.0f;
        if(hudgunsway && !intermission && zfade > 0.f)
        {
            vecfromyawpitch(d->yaw, 0, 0, 1, dir);
            float steps = swaydist/swaystep*M_PI;
            dir.mul(swayside*cosf(steps)*zfade);
            dir.z = swayup*(fabs(sinf(steps)) - 1)*zfade;
            orig.add(dir).add(swaydir);
        }
        else orig = d->o;
        float infval = isInfected?0.25f:1;
        if(firstpersonshift != 0.f)
        {
            vecfromyawpitch(yaw, pitch, 0, -1, dir);
            dir.mul(d->radius*firstpersonshift-(cosf(RAD*d->yaw)*d->radius*firstpersonyaw/2)*infval*zfade);
            orig.add(dir);
        }
        if(firstpersonadjust != 0.f)
        {
            vecfromyawpitch(yaw, pitch+90.f, 1, 0, dir);
            dir.mul(d->eyeheight*firstpersonadjust-(sinf(RAD*d->pitch)*d->eyeheight*firstpersonpitch/2)*zfade+
                idlepulse(((1.f-firstpersonidlemargin)+(1-sinf(RAD*fabs(d->pitch)))*d->eyeheight*firstpersonidleamt/32*firstpersonidlemargin))*0.5f*zfade);
            orig.add(dir);
        }
        if(firstpersondist != 0.f)
        {
            vecfromyawpitch(yaw, pitch, 1, 0, dir);
            dir.mul(d->radius*firstpersondist+(fabs(sinf(RAD*d->yaw))*d->radius*firstpersonyaw/4)*infval*zfade);
            orig.add(dir);
        }

#if 0
        if(player1->state!=CS_DEAD && player1->quadmillis)
        {
            float t = 0.5f + 0.5f*sinf(2*M_PI*lastmillis/1000.0f);
            color.y = color.y*(1-t) + t;
        }
#endif

        const playermodelinfo &mdl = getplayermodelinfo(d);

        string gunname;
        extern int dbgisinfected;
        if(!d->isInfected() && !dbgisinfected)
        {
            formatstring(gunname)(
                "%s/%s",
                hudgunsdir[0] ? hudgunsdir : mdl.hudguns,
                d->ammo[d->hudgun] < 1 && weapons[d->hudgun].emptyFile
                    ? weapons[d->hudgun].emptyFile
                    : weapons[d->hudgun].file
            );
            if((m_teammode || teamskins) && teamhudguns)
                concatstring(gunname, !strcmp(d->team, TEAM_0) ? "/blue" : "/red");
            else if(testteam > 1)
                concatstring(gunname, testteam==2 ? "/blue" : "/red");
        }
        else if(mdl.zombieHands)
        {
            copystring(gunname, mdl.zombieHands);
            vecfromyawpitch(yaw, pitch+90.f, 1, 0, dir);
            dir.mul(-0.2f-(1.f-sinf(RAD*((d->pitch+90.f)/2.f)))*0.8f);
            orig.add(dir);
            anim |= ANIM_LOOP;
        }
        else
        {
            return;
        }

        modelattach a[2];
        d->muzzle = vec(-1, -1, -1);
        a[0] = modelattach("tag_muzzle", &d->muzzle);
        dynent *interp = NULL;
        if(zoom && isscopedweap(d->hudgun) && hfade < 0.025f)
            d->muzzle = d->o;
        else
        {
            rendermodel(NULL, gunname, anim, orig, testhudgun ? 0 : yaw+addyaw+90, testhudgun ? 0 : pitch+addpitch, MDL_LIGHT, interp, a, base, (int)ceil(speed), hfade);
            if(d->muzzle.x >= 0) d->muzzle = calcavatarpos(d->muzzle, 12);
        }
    }

    void drawhudgun()
    {
        fpsent *d = hudplayer();
        if(d->state==CS_SPECTATOR || d->state==CS_EDITING || !hudgun || editmode)
        {
            d->muzzle = player1->muzzle = vec(-1, -1, -1);
            return;
        }

        int rtime = d->altfire? weapons[d->gunselect].attackdelay2: weapons[d->gunselect].attackdelay;
        bool noanim[NUMWEAPS] = { false, true, true, true, true, false, true, true, false, true, true, false, false },
            inzoom = zoom && scopev > 0.9f && noanim[d->gunselect]; // TODO: add ironscope animations to avoid this temp fix
        if(d->lastaction && (d->lastattackgun==d->gunselect || d->isInfected()) && lastmillis-d->lastaction<rtime && !inzoom)
        {
            drawhudmodel(d, ANIM_GUN_SHOOT|ANIM_SETSPEED, rtime/17.0f, d->lastaction);
        }
        else
        {
            drawhudmodel(d, ANIM_GUN_IDLE|ANIM_LOOP);
        }
    }

    void renderavatar()
    {
        drawhudgun();
    }

    void renderplayerpreview(int model, int team, int weap)
    {
        static fpsent *previewent = NULL;
        if(!previewent)
        {
            previewent = new fpsent;
            previewent->o = vec(0, 0.9f*(previewent->eyeheight + previewent->aboveeye), previewent->eyeheight - (previewent->eyeheight + previewent->aboveeye)/2);
            previewent->light.color = vec(1, 1, 1);
            previewent->light.dir = vec(0, -1, 2).normalize();
            loopi(NUMWEAPS) previewent->ammo[i] = 1;
        }
        previewent->playermodel = playermodel;
        previewent->playerclass = playerclass;
        previewent->gunselect = clamp(weap, int(WEAP_FIST), int(WEAP_USABLE_LAST));
        previewent->yaw = sinf(RAD*((totalmillis%(360*50))/50))*22.5f+157.5f;
        previewent->light.millis = -1;
        const playermodelinfo *mdlinfo = getplayermodelinfo(model);
        if(!mdlinfo) return;
        renderplayer(previewent, *mdlinfo, team >= 0 && team <= 2 ? team : 0, 1, false);
    }

    vec hudgunorigin(int gun, const vec &from, const vec &to, fpsent *d)
    {
        if(d->muzzle.x >= 0) return d->muzzle;
        vec offset(from);
        if(d!=hudplayer() || isthirdperson())
        {
            vec front, right;
            vecfromyawpitch(d->yaw, d->pitch, 1, 0, front);
            offset.add(front.mul(d->radius));
            if(d->type!=ENT_AI)
            {
                offset.z += (d->aboveeye + d->eyeheight)*0.75f - d->eyeheight;
                vecfromyawpitch(d->yaw, 0, 0, -1, right);
                offset.add(right.mul(0.5f*d->radius));
                offset.add(front);
            }
            return offset;
        }
        offset.add(vec(to).sub(from).normalize().mul(2));
        if(hudgun)
        {
            offset.sub(vec(camup).mul(1.0f));
            offset.add(vec(camright).mul(0.8f));
        }
        else offset.sub(vec(camup).mul(0.8f));
        return offset;
    }

    void preloadweapons()
    {
        const playermodelinfo &mdl = getplayermodelinfo(player1);
        loopi(NUMWEAPS)
        {
            const char *file = weapons[i].file;
            if(!file) continue;
            string fname;
            if((m_teammode || teamskins) && teamhudguns)
            {
                formatstring(fname)("%s/%s/blue", hudgunsdir[0] ? hudgunsdir : mdl.hudguns, file);
                preloadmodel(fname);
            }
            else
            {
                formatstring(fname)("%s/%s", hudgunsdir[0] ? hudgunsdir : mdl.hudguns, file);
                preloadmodel(fname);
            }
            formatstring(fname)("playermodels/vwep/%s", file);
            preloadmodel(fname);
        }
    }

    void preloadsounds()
    {
        for(int i = S_JUMP; i <= S_SPLASH2; i++) preloadsound(i);
        for(int i = S_JUMPPAD; i <= S_PISTOL; i++) preloadsound(i);
        for(int i = S_V_BOOST; i <= S_V_QUAD10; i++) preloadsound(i);
        for(int i = S_BURN; i <= S_HIT; i++) preloadsound(i);
    }

    void preload()
    {
        if(hudgun) preloadweapons();
        preloadprojmodels();
        preloadplayermodel();
        preloadsounds();
        entities::preloadentities();
        // if(m_sp) preloadmonsters(); TODO
    }

}

