#include "game.h"
#include "auth.h"

// --- RPG: Shattered Shield persistence (milestone steps 1+2, additive; see src/rpg) ---
#include "rpg/rpg.h"
#include "rpg/abilities.h"

#ifdef SERVER
#include "server/lua.h"
#endif

namespace game
{
#ifdef STANDALONE
    SVAR(demodir, "resources/demos");
#else
    SVAR(demodir, "demos");
#endif

    const char *buyablesnames[BA_NUM] = { "ammo", "dammo", "health", "dhealth", "garmour", "yarmour", "quad", "dquad", "support", "dsupport", "damage", "extralife", "rquad" };
    const int buyablesprices[BA_NUM] = { 500, 900, 500, 900, 600, 1000, 1000, 1800, 2000, 3600, 600, 4000, 1500 };

#ifdef CLIENT
    void parseoptions(vector<const char *> &args)
    {
        loopv(args)
            if(!game::clientoption(args[i]))
            if(!server::serveroption(args[i]))
                conoutf(CON_ERROR, "unknown command-line option: %s", args[i]);
    }
#endif
    const char *gameident() { return "fps"; }
}

namespace server
{
    using game::demodir;

    struct server_entity            // server side version of "entity" type
    {
        int type;
        int spawntime;
        char spawned;
    };

    static const int DEATHMILLIS = 300;

    struct clientinfo;

    struct gameevent
    {
        virtual ~gameevent() {}

        virtual bool flush(clientinfo *ci, int fmillis);
        virtual void process(clientinfo *ci) {}

        virtual bool keepable() const { return false; }
    };

    struct timedevent : gameevent
    {
        int millis;

        bool flush(clientinfo *ci, int fmillis);
    };

    struct hitinfo
    {
        int target;
        int lifesequence;
        int rays;
        float dist;
        int headshot;
        vec dir;
    };

    struct shotevent : timedevent
    {
        int id, gun, headshot, numrays;
        vec from, to;
        vector<hitinfo> hits;
        vector<ivec> rays;

        void process(clientinfo *ci);
    };

    struct explodeevent : timedevent
    {
        int id, gun, direct;
        vector<hitinfo> hits;

        bool keepable() const { return true; }

        void process(clientinfo *ci);
    };

    struct suicideevent : gameevent
    {
        int type;

        void process(clientinfo *ci);

        suicideevent(int type) : type(type) {}
    };

    struct pickupevent : gameevent
    {
        int ent;

        void process(clientinfo *ci);
    };

    template <int N>
    struct projectilestate
    {
        int projs[N];
        int numprojs, last;

        projectilestate() : numprojs(0), last(0) {}

        void reset() { numprojs = 0; last = 0; }

        void add(int val)
        {
            if(last>=N) last = 0;
            projs[last++] = val;
            numprojs = max(numprojs+1, N);
        }

        bool remove(int val)
        {
            loopi(numprojs) if(projs[i]==val)
            {
                projs[i] = projs[--numprojs];
                return true;
            }
            return false;
        }

        bool check(int val)
        {
            loopi(numprojs) if(projs[i]==val) return true;
            return false;
        }
    };

    struct gamestate : fpsstate
    {
        vec o;
        int state, editstate;
        int lastdeath, deadflush, lastspawn, lifesequence;
        int lastshot;
        projectilestate<32> rockets, grenades;//, flames;
        int frags, flags, deaths, teamkills, shotdamage, damage;
        int lasttimeplayed, timeplayed;
        float effectiveness;
        int lastburnpain, burnmillis;
        bool onfire, teamkilled, teamshooter;
        struct clientinfo *fireattacker;

        gamestate() : state(CS_DEAD), editstate(CS_DEAD), lifesequence(0), burnmillis(0), onfire(false) {}

        bool isalive(int gamemillis)
        {
            return state==CS_ALIVE || (state==CS_DEAD && gamemillis - lastdeath <= DEATHMILLIS);
        }

        bool waitexpired(int gamemillis)
        {
            return gamemillis - lastshot >= gunwait;
        }

        void reset()
        {
            if(state!=CS_SPECTATOR) state = editstate = CS_DEAD;
            maxhealth = 100;
            guts = 0;
            rockets.reset();
            grenades.reset();

            timeplayed = 0;
            effectiveness = 0;
            frags = flags = deaths = teamkills = shotdamage = damage = 0;

            lastdeath = 0;

            teamkilled = teamshooter = false;
            burnmillis = 0;

            infectedType = 0;

            respawn();
        }

        void respawn()
        {
            fpsstate::respawn();
            o = vec(-1e10f, -1e10f, -1e10f);
            deadflush = 0;
            lastspawn = -1;
            lastshot = 0;
            onfire = false;
        }

        void reassign()
        {
            respawn();
            rockets.reset();
            grenades.reset();
        }
    };

    struct savedscore
    {
        uint ip;
        string name;
        int maxhealth, frags, flags, deaths, teamkills, shotdamage, damage;
        int timeplayed;
        float effectiveness;
        int infectedType;

        void save(gamestate &gs)
        {
            maxhealth = gs.maxhealth;
            frags = gs.frags;
            flags = gs.flags;
            deaths = gs.deaths;
            teamkills = gs.teamkills;
            shotdamage = gs.shotdamage;
            damage = gs.damage;
            timeplayed = gs.timeplayed;
            effectiveness = gs.effectiveness;
            infectedType = gs.infectedType;
        }

        void restore(gamestate &gs)
        {
            if(gs.health==gs.maxhealth) gs.health = maxhealth;
            gs.maxhealth = maxhealth;
            gs.frags = frags;
            gs.flags = flags;
            gs.deaths = deaths;
            gs.teamkills = teamkills;
            gs.shotdamage = shotdamage;
            gs.damage = damage;
            gs.timeplayed = timeplayed;
            gs.effectiveness = effectiveness;
            gs.infectedType = infectedType;
        }
    };

    extern int gamemillis, nextexceeded;

    struct clientinfo
    {
        int clientnum, ownernum, connectmillis, sessionid, overflow;
        string name, team, mapvote;
        int modevote;
        int privilege;
        int connectionState;
        bool local, timesync;
        int gameoffset, lastevent, pushed, exceeded;
        gamestate state;
        vector<gameevent *> events;
        vector<uchar> position, messages;
        uchar *wsdata;
        int wslen;
        vector<clientinfo *> bots;
        ustring authrandom;
        int ping, aireinit;
        // survival shop / death-rule state (dmsp): dmgmult is a percent (100 = base),
        // dmglevel counts damage upgrades, extralives are banked extra lives,
        // pendingquad arms quad at next round start,
        // deadout marks a human as out for the current round (death rule)
        int dmgmult, dmglevel, extralives, pendingquad;
        bool deadout;
        string clientmap;
        int mapcrc; 
        bool warned, gameclip;
        ENetPacket *getdemo, *getmap, *clipboard;
        int lastclipboard, needclipboard;
        stream *clientCertificate;
        int maploaded;
        vector<AppliedWeaponEffect> effects;

        clientinfo() : getdemo(NULL), getmap(NULL), clipboard(NULL), clientCertificate(NULL) { reset(); }
        ~clientinfo() { events.deletecontents(); cleanclipboard(); DELETEP(clientCertificate); }

        void addevent(gameevent *e)
        {
            if(state.state==CS_SPECTATOR || events.length()>100) delete e;
            else events.add(e);
        }

        enum
        {
            PUSHMILLIS = 2500
        };

        int calcpushrange()
        {
            ENetPeer *peer = getclientpeer(ownernum);
            return PUSHMILLIS + (peer ? peer->roundTripTime + peer->roundTripTimeVariance : ENET_PEER_DEFAULT_ROUND_TRIP_TIME);
        }

        bool checkpushed(int millis, int range)
        {
            return millis >= pushed - range && millis <= pushed + range;
        }

        void scheduleexceeded()
        {
            if(state.state!=CS_ALIVE || !exceeded) return;
            int range = calcpushrange();
            if(!nextexceeded || exceeded + range < nextexceeded) nextexceeded = exceeded + range;
        }

        void setexceeded()
        {
            if(state.state==CS_ALIVE && !exceeded && !checkpushed(gamemillis, calcpushrange())) exceeded = gamemillis;
            scheduleexceeded();
        }

        void setpushed()
        {
            pushed = max(pushed, gamemillis);
            if(exceeded && checkpushed(exceeded, calcpushrange())) exceeded = 0;
        }

        bool checkexceeded()
        {
            return state.state==CS_ALIVE && exceeded && gamemillis > exceeded + calcpushrange();
        }

        void mapchange()
        {
            mapvote[0] = 0;
            modevote = INT_MAX;
            state.reset();
            events.deletecontents();
            overflow = 0;
            timesync = false;
            lastevent = 0;
            exceeded = 0;
            pushed = 0;
            dmgmult = 100;
            dmglevel = 0;
            extralives = 0;
            pendingquad = 0;
            deadout = false;
            clientmap[0] = '\0';
            mapcrc = 0;
            warned = false;
            gameclip = false;
            maploaded = 0;
        }

        void reassign()
        {
            state.reassign();
            events.deletecontents();
            timesync = false;
            lastevent = 0;
        }

        void cleanclipboard(bool fullclean = true)
        {
            if(clipboard) { if(--clipboard->referenceCount <= 0) enet_packet_destroy(clipboard); clipboard = NULL; }
            if(fullclean) lastclipboard = 0;
        }

        void reset()
        {
            name[0] = team[0] = 0;
            privilege = PRIV_NONE;
            connectionState = CONNECTION_STATE_NONE;
            local = false;
            position.setsize(0);
            messages.setsize(0);
            ping = 0;
            aireinit = 0;
            needclipboard = 0;
            DELETEP(clientCertificate);
            cleanclipboard();
            mapchange();
        }

        int geteventmillis(int servmillis, int clientmillis)
        {
            if(!timesync || (events.empty() && state.waitexpired(servmillis)))
            {
                timesync = true;
                gameoffset = servmillis - clientmillis;
                return servmillis;
            }
            else return gameoffset + clientmillis;
        }
    };


    struct ban
    {
        int time, expire;
        uint ip;
    };

    namespace aiman
    {
        extern void removeai(clientinfo *ci);
        extern void clearai();
        extern void resetai();
        extern void scheduleresetai();
        extern void doresetai();
        extern void checkai();
        extern void reqadd(clientinfo *ci, ai::AiType type, int skill);
        extern void reqdel(clientinfo *ci, ai::AiType type);
        extern void setbotlimit(clientinfo *ci, int limit);
        extern void setbotbalance(clientinfo *ci, bool balance);
        extern void changemap();
        extern void addclient(clientinfo *ci);
        extern void changeteam(clientinfo *ci);

        extern clientinfo *addai(int skill, int limit, ai::AiType aiType = ai::AI_TYPE_BOT);
        extern void deleteai(clientinfo *ci);
        extern void reinitai(clientinfo *ci); // RPG ability-layer test spawner runs the normal spawn pipeline eagerly
    }

    #define MM_MODE 0xF
    #define MM_AUTOAPPROVE 0x1000
    #define MM_PRIVSERV (MM_MODE | MM_AUTOAPPROVE)
    #define MM_PUBSERV ((1<<MM_OPEN) | (1<<MM_VETO))
    #define MM_COOPSERV (MM_AUTOAPPROVE | MM_PUBSERV | (1<<MM_LOCKED))

    bool notgotitems = true;        // true when map has changed and waiting for clients to send item
    int gamemode = 0;
    int gamemillis = 0, gamelimit = 0, nextexceeded = 0, gamespeed = 100;
    bool gamepaused = false, shouldstep = true, waitingForMapLoad = false;

    string smapname = "";
    int interm = 0;
    enet_uint32 lastsend = 0;
    int mastermode = MM_OPEN, mastermask = MM_PRIVSERV;
    stream *mapdata = NULL;

    vector<uint> allowedips;
    vector<ban> bannedips;
    void addban(uint ip, int expire)
    {
        allowedips.removeobj(ip);
        ban b;
        b.time = totalmillis;
        b.expire = totalmillis + expire;
        b.ip = ip;
        loopv(bannedips) if(bannedips[i].expire - b.expire > 0) { bannedips.insert(i, b); return; }
        bannedips.add(b);
    }
    vector<clientinfo *> connects, clients, bots;
    void kickclients(uint ip, clientinfo *actor = NULL, int priv = PRIV_NONE)
    {
        loopvrev(clients)
        {
            clientinfo &c = *clients[i];
            if(c.state.ai.type != ai::AI_TYPE_NONE || c.privilege >= PRIV_ADMIN || c.local) continue;
            if(actor && ((c.privilege > priv && !actor->local) || c.clientnum == actor->clientnum)) continue;
            if(getclientip(c.clientnum) == ip) disconnect_client(c.clientnum, DISC_KICK);
        }
    }
 
    struct maprotation
    {
        static int exclude;
        int modes;
        string map;
        
        int calcmodemask() const { return modes&(1<<RR_NUM_GAMEMODE) ? modes & ~exclude : modes; }
        bool hasmode(int mode) const { return (calcmodemask() & (1 << (getGameMode(mode)->id-MIN_GAMEMODE))) != 0; }

        int findmode(int mode) const
        {
            if(!hasmode(mode)) RR_FOREACH_GAMEMODE if(hasmode(gameModes[i].id)) return i;
            return mode;
        }

        bool match(int reqmode, const char *reqmap) const
        {
            return hasmode(reqmode) && (!map[0] || !reqmap[0] || !strcmp(map, reqmap));
        }

        bool includes(const maprotation &rot) const
        {
            return rot.modes == modes ? rot.map[0] && !map[0] : (rot.modes & modes) == rot.modes;
        }
    };
    int maprotation::exclude = 0;
    vector<maprotation> maprotations;
    int curmaprotation = 0;

    VAR(lockmaprotation, 0, 0, 2);

    void maprotationreset()
    {
        maprotations.setsize(0);
        curmaprotation = 0;
        maprotation::exclude = 0;
    }

    void nextmaprotation()
    {
        curmaprotation++;
        if(maprotations.inrange(curmaprotation) && maprotations[curmaprotation].modes) return;
        do curmaprotation--;
        while(maprotations.inrange(curmaprotation) && maprotations[curmaprotation].modes);
        curmaprotation++;
    }

    int findmaprotation(int mode, const char *map)
    {
        for(int i = max(curmaprotation, 0); i < maprotations.length(); i++)
        {
            maprotation &rot = maprotations[i];
            if(!rot.modes) break;
            if(rot.match(mode, map)) return i;
        }
        int start;
        for(start = max(curmaprotation, 0) - 1; start >= 0; start--) if(!maprotations[start].modes) break;
        start++;
        for(int i = start; i < curmaprotation; i++)
        {
            maprotation &rot = maprotations[i];
            if(!rot.modes) break;
            if(rot.match(mode, map)) return i;
        }
        int best = -1;
        loopv(maprotations)
        {
            maprotation &rot = maprotations[i];
            if(rot.match(mode, map) && (best < 0 || maprotations[best].includes(rot))) best = i;
        }
        return best;
    }

    bool searchmodename(const char *haystack, const char *needle)
    {
        if(!needle[0]) return true;
        do
        {
            if(needle[0] != '.')
            {
                haystack = strchr(haystack, needle[0]);
                if(!haystack) break;
                haystack++;
            }
            const char *h = haystack, *n = needle+1;
            for(; *h && *n; h++)
            {
                if(*h == *n) n++;
                else if(*h != ' ') break; 
            }
            if(!*n) return true;
            if(*n == '.') return !*h;
        } while(needle[0] != '.');
        return false;
    }

    int genmodemask(vector<char *> &modes)
    {
        int modemask = 0;
        loopv(modes)
        {
            const char *mode = modes[i];
            int op = mode[0];
            switch(mode[0])
            {
                case '*':
                    modemask |= 1<<RR_NUM_GAMEMODE;
                    RR_FOREACH_GAMEMODEk if(m_checknot(gameModes[k].id, M_DEMO|M_EDIT|M_LOCAL)) modemask |= 1<<k;
                    continue;
                case '!':
                    mode++;
                    if(mode[0] != '?') break;
                case '?':
                    mode++;
                    RR_FOREACH_GAMEMODEk if(searchmodename(gameModes[k].name, mode))
                    {
                        if(op == '!') modemask &= ~(1<<k);
                        else modemask |= 1<<k;
                    }
                    continue;
            }
            int modenum = INT_MAX;
            if(isdigit(mode[0])) modenum = atoi(mode);
            else RR_FOREACH_GAMEMODEk if(searchmodename(gameModes[k].name, mode)) { modenum = k; break; }
            if(!m_valid(modenum)) continue;
            switch(op)
            {
                case '!': modemask &= ~(1 << (modenum)); break;
                default: modemask |= 1 << (modenum); break;
            }
        }
        return modemask;
    }
         
    bool addmaprotation(int modemask, const char *map)
    {
        if(!map[0]) RR_FOREACH_GAMEMODEk if(modemask&(1<<k) && !m_check(k, M_EDIT)) modemask &= ~(1<<k);
        if(!modemask) return false;
        if(!(modemask&(1<<RR_NUM_GAMEMODE))) maprotation::exclude |= modemask;
        maprotation &rot = maprotations.add();
        rot.modes = modemask;
        copystring(rot.map, map);
        return true;
    }
    #ifdef CLIENT
    void addmaprotations(tagval *args, int numargs)
    {
        vector<char *> modes, maps;
        for(int i = 0; i + 1 < numargs; i += 2)
        {
            explodelist(args[i].getstr(), modes);
            explodelist(args[i+1].getstr(), maps);
            int modemask = genmodemask(modes);
            if(maps.length()) loopvj(maps) addmaprotation(modemask, maps[j]);
            else addmaprotation(modemask, "");
            modes.deletearrays();
            maps.deletearrays();
        }
        if(maprotations.length() && maprotations.last().modes)
        {
            maprotation &rot = maprotations.add();
            rot.modes = 0;
            rot.map[0] = '\0';
        }
    }
    
    COMMAND(maprotationreset, "");
    COMMANDN(maprotation, addmaprotations, "ss2V");
    #endif

    struct demofile
    {
        string info, name;
        uchar *data;
        int len;
    };

    #define MAXDEMOS 5
    vector<demofile> demos;

    bool demonextmatch = false;
    stream *demotmp = NULL, *demorecord = NULL, *demoplayback = NULL;
    int nextplayback = 0, demomillis = 0;

    VARFP(localrecorddemo, 0, 2, 2, demonextmatch = localrecorddemo != 0 );
    VAR(restrictdemos, 0, 1, 1);

    VAR(restrictpausegame, 0, 0, 1);
    VAR(restrictgamespeed, 0, 0, 1);

    //add server vars here
    SVAR(serverdesc, "");
    SVAR(serverpass, "");
    SVAR(adminpass, "");

    VAR(maxzombies, 0, 20, 100);
    VAR(flaglimit, 0, 10, 100);
    VAR(timelimit, 0, 10, 1000);
    VAR(allowweaps, 0, 0, 2);
    VAR(persistteams, 0, 0, 1);
    VAR(intermissionstats, 0, 1, 1);

    struct teamkillkick
    {
        int modes, limit, ban;

        bool match(int mode) const
        {
            return (modes&(1<<(mode)))!=0;
        }

        bool includes(const teamkillkick &tk) const
        {
            return tk.modes != modes && (tk.modes & modes) == tk.modes;
        }
    };
    vector<teamkillkick> teamkillkicks;

    void teamkillkickreset()
    {
        teamkillkicks.setsize(0);
    }

    #ifdef CLIENT
    void addteamkillkick(char *modestr, int *limit, int *ban)
    {
        vector<char *> modes;
        explodelist(modestr, modes);
        teamkillkick &kick = teamkillkicks.add();
        kick.modes = genmodemask(modes);
        kick.limit = *limit;
        kick.ban = *ban > 0 ? *ban*60000 : (*ban < 0 ? 0 : 30*60000); 
        modes.deletearrays();
    }

    COMMAND(teamkillkickreset, "");
    COMMANDN(teamkillkick, addteamkillkick, "sii");
    #endif

    struct teamkillinfo
    {
        uint ip;
        int teamkills;
    };
    vector<teamkillinfo> teamkills;
    bool shouldcheckteamkills = false;

    void addteamkill(clientinfo *actor, clientinfo *victim, int n)
    {
        if(!m_timed || actor->state.ai.type != ai::AI_TYPE_NONE || actor->local || actor->privilege || (victim && victim->state.ai.type != ai::AI_TYPE_NONE)) return;
        shouldcheckteamkills = true;
        uint ip = getclientip(actor->clientnum);
        loopv(teamkills) if(teamkills[i].ip == ip) 
        { 
            teamkills[i].teamkills += n;
            return;
        }
        teamkillinfo &tk = teamkills.add();
        tk.ip = ip;
        tk.teamkills = n;
    }

    void checkteamkills()
    {
        teamkillkick *kick = NULL;
        if(m_timed) loopv(teamkillkicks) if(teamkillkicks[i].match(gamemode) && (!kick || kick->includes(teamkillkicks[i])))
            kick = &teamkillkicks[i];
        if(kick) loopvrev(teamkills)
        {
            teamkillinfo &tk = teamkills[i];
            if(tk.teamkills >= kick->limit)
            {
                if(kick->ban > 0) addban(tk.ip, kick->ban);
                kickclients(tk.ip);
                teamkills.removeunordered(i);
            }
        }
        shouldcheckteamkills = false;
    }

    void *newclientinfo() { return new clientinfo; }
    void deleteclientinfo(void *ci) { delete (clientinfo *)ci; }

    clientinfo *getinfo(int n)
    {
        if(n < MAXCLIENTS) return (clientinfo *)getclientinfo(n);
        n -= MAXCLIENTS;
        return bots.inrange(n) ? bots[n] : NULL;
    }

    uint mcrc = 0;
    vector<entity> ments;
    vector<server_entity> sents;
    vector<savedscore> scores;

    void pausegame(bool val, clientinfo *ci = NULL);

    int msgsizelookup(int msg)
    {
        static int sizetable[NUMSV] = { -1 };
        if(sizetable[0] < 0)
        {
            memset(sizetable, -1, sizeof(sizetable));
            for(const int *p = msgsizes; *p >= 0; p += 2) sizetable[p[0]] = p[1];
        }
        return msg >= 0 && msg < NUMSV ? sizetable[msg] : -1;
    }

    const char *modename(int n, const char *unknown)
    {
        const GameMode *mode = getGameMode(n);

        if(mode)
        {
            return mode->name;
        }
        return unknown;
    }

    const char *mastermodename(int n, const char *unknown)
    {
        return (n>=MM_START && size_t(n-MM_START)<sizeof(mastermodenames)/sizeof(mastermodenames[0])) ? mastermodenames[n-MM_START] : unknown;
    }

    const char *privname(int type)
    {
        return getPrivilegeName(type);
    }

    void sendservmsg(const char *s)
    {
#ifndef STANDALONE
        if(!clients.length())
#endif
            conoutf("Broadcast: %s", s);
        sendf(-1, 1, "ris", N_SERVMSG, s);
    }

    void sendservmsgf(int cn, const char *f, ...)
    {
        defvformatstring(str, f, f);
        sendservmsg(str);
    }

    void sendservmsgf(const char *fmt, ...)
    {
         defvformatstring(s, fmt, fmt);
        sendservmsg(s);
    }

// --- RPG: Shattered Shield persistence (milestone steps 1+2, additive; see src/rpg) ---
    void rpgstats_cmd(tagval *args, int numargs)
    {
        if(numargs < 1 || !args[0].getstr()[0]) { conoutf("usage: rpgstats <name>"); return; }
        if(!rpg::connected()) { conoutf("rpgstats: character database is not open"); return; }
        int level = 0, xp = 0;
        if(!rpg::getstats(args[0].getstr(), level, xp)) conoutf("rpgstats: no character named '%s'", args[0].getstr());
        else conoutf("rpgstats: %s -- level %d, xp %d", args[0].getstr(), level, xp);
    }
    COMMANDN(rpgstats, rpgstats_cmd, "sV");

    void rpggrantxp_cmd(tagval *args, int numargs)
    {
        if(numargs < 2) { conoutf("usage: rpggrantxp <name> <amount>"); return; }
        if(!rpg::connected()) { conoutf("rpggrantxp: character database is not open"); return; }
        bool leveled = false;
        int newlevel = rpg::awardxp(args[0].getstr(), args[1].getint(), &leveled);
        if(newlevel < 0) { conoutf("rpggrantxp: no character named '%s'", args[0].getstr()); return; }
        conoutf("rpggrantxp: %s +%d xp (level %d)", args[0].getstr(), args[1].getint(), newlevel);
        if(leveled) sendservmsgf("\f2%s \f7reached RPG level \f2%d!", args[0].getstr(), newlevel);
    }
    COMMANDN(rpggrantxp, rpggrantxp_cmd, "siV");

    // --- RPG: Shattered Shield ability layer (milestone step 3, additive; see src/rpg) ---
    // TEST-ONLY helpers for the scripted harness. Harmless in production: they
    // use the same spawn path as the game modes. Not part of the shipped design.
    void makeInfected(clientinfo *victim, int infectionType, bool forceNetworking); // defined below
    void rpgmana_cmd(tagval *args, int numargs)
    {
        if(numargs < 1) { conoutf("usage: rpgmana <name>"); return; }
        int mana = 0, maxmana = 0;
        if(!rpg::getmana(args[0].getstr(), mana, maxmana)) { conoutf("rpgmana: no session for '%s'", args[0].getstr()); return; }
        conoutf("rpgmana: %s -- %d/%d", args[0].getstr(), mana, maxmana);
    }
    COMMANDN(rpgmana, rpgmana_cmd, "sV");

    // Spawn one monster parked on the casting player's feet (optional hp
    // override for deterministic tests). Uses the mode spawn path
    // (aiman::addai + makeInfected); the normal reinitai/spawn pipeline
    // broadcasts it on the next serverupdate.
    void rpgspawnmonster_cmd(tagval *args, int numargs)
    {
        if(!rpg::connected()) { conoutf("rpgspawnmonster: character database is not open"); return; }
        int hp = numargs >= 1 ? max(args[0].getint(), 1) : 0;
        clientinfo *caster = NULL;
        loopv(clients)
        {
            clientinfo *ci = clients[i];
            if(ci->connectionState == CONNECTION_STATE_CONNECTED && ci->state.ai.type == ai::AI_TYPE_NONE && ci->state.state == CS_ALIVE) { caster = ci; break; }
        }
        if(!caster) { conoutf("rpgspawnmonster: no live player to spawn next to"); return; }
        clientinfo *mi = aiman::addai(50, -1, ai::AI_TYPE_MONSTER);
        if(!mi) { conoutf("rpgspawnmonster: no free AI slot"); return; }
        makeInfected(mi, monster::getRandomType(), false);
        copystring(mi->name, monster::getMonsterType(mi->state.getMonsterType()).classInfo.name, MAXNAMELEN+1);
        mi->state.state = CS_ALIVE;
        // Run the normal spawn pipeline now (reassign()->respawn() resets the
        // position to the -1e10 sentinel, sendspawn() broadcasts); the later
        // serverupdate pass then becomes a no-op for this monster.
        aiman::reinitai(mi);
        if(hp > 0) mi->state.maxhealth = mi->state.health = hp;
        mi->state.o = caster->state.o; // on the caster's feet: melee cone auto-passes, range trivially met
        // The owning client simulates the monster at its own spawn point and reports
        // that via N_POS; pin it so the server keeps the test position (see N_POS).
        rpg::pinmonster(mi->clientnum);
        conoutf("rpgspawnmonster: '%s' (hp %d) at caster '%s'", mi->name, mi->state.health, caster->name);
    }
    COMMANDN(rpgspawnmonster, rpgspawnmonster_cmd, "iV");

    void resetitems()
    {
        mcrc = 0;
        ments.setsize(0);
        sents.setsize(0);
        //cps.reset();
    }

#ifdef CLIENT
    bool serveroption(const char *arg)
    {
        if(arg[0]=='-') switch(arg[1])
        {
            case 'n': setsvar("serverdesc", &arg[2]); return true;
            case 'y': setsvar("serverpass", &arg[2]); return true;
            case 'p': setsvar("adminpass", &arg[2]); return true;
            case 'g': setvar("serverbotlimit", atoi(&arg[2])); return true;
        }
        return false;
    }
#endif
    
    void serverinit()
    {
        smapname[0] = '\0';
        resetitems();
        auth::init();
        // --- RPG: Shattered Shield persistence (milestone steps 1+2, additive; see src/rpg) ---
        rpg::init();
    }

    int numclients(int exclude = -1, bool nospec = true, bool noai = true, bool priv = false)
    {
        int n = 0;
        loopv(clients)
        {
            clientinfo *ci = clients[i];
            if(ci->clientnum!=exclude && (!nospec || ci->state.state!=CS_SPECTATOR || (priv && (ci->privilege >= PRIV_MASTER || ci->local))) && (!noai || ci->state.ai.type == ai::AI_TYPE_NONE)) n++;
        }
        return n;
    }

    bool duplicatename(clientinfo *ci, char *name)
    {
        if(!name) name = ci->name;
        loopv(clients) if(clients[i]!=ci && !strcmp(name, clients[i]->name)) return true;
        return false;
    }

    const char *colorname(clientinfo *ci, char *name = NULL)
    {
        if(!name) name = ci->name;
        if(name[0] && !duplicatename(ci, name) && ci->state.ai.type == ai::AI_TYPE_NONE) return name;
        static string cname[3];
        static int cidx = 0;
        cidx = (cidx+1)%3;
        formatstring(cname[cidx])(ci->state.ai.type == ai::AI_TYPE_NONE ? "%s \fs\f5(%d)\fS" : "%s \fs\f5[%d]\fS", name, ci->clientnum);
        return cname[cidx];
    }

    void makeInfected(clientinfo *victim, int infectionType = -1, bool forceNetworking = false)
    {
        infectionType++;
        if(!forceNetworking && victim->aireinit == 2) victim->state.infectedType = infectionType;
        else sendf(-1, 1, "ri3", N_INFECT, victim->clientnum, victim->state.infectedType = infectionType);
    }

    void instantRespawn(clientinfo *victim)
    {
        extern void sendspawn(clientinfo *ci);
        victim->position.setsize(0);
        victim->state.state = CS_DEAD;
        victim->state.respawn();
        sendspawn(victim);
    }

    struct servmode
    {
        virtual ~servmode() {}

        virtual void entergame(clientinfo *ci) {}
        virtual void leavegame(clientinfo *ci, bool disconnecting = false) {}

        virtual void moved(clientinfo *ci, const vec &oldpos, bool oldclip, const vec &newpos, bool newclip) {}
        virtual bool canspawn(clientinfo *ci, bool connecting = false) { return true; }
        virtual void spawned(clientinfo *ci) {}
        virtual int fragvalue(clientinfo *victim, clientinfo *actor)
        {
            if(victim==actor || isteam(victim->team, actor->team)) return -1;
            return 1;
        }
        virtual bool died(clientinfo *victim, clientinfo *actor) { return false; }
        virtual bool canchangeteam(clientinfo *ci, const char *oldteam, const char *newteam) { return true; }
        virtual void changeteam(clientinfo *ci, const char *oldteam, const char *newteam) {}
        virtual void initclient(clientinfo *ci, packetbuf &p, bool connecting) {}
        virtual void update() {}
        virtual void updateRaw() {}
        virtual void cleanup() {}
        virtual void newmap() {}
        virtual void setup() {}
        virtual void intermission() {}
        virtual bool hidefrags() { return false; }
        virtual int getteamscore(const char *team) { return 0; }
        virtual void getteamscores(vector<teamscore> &scores) {}
        virtual bool extinfoteam(const char *team, ucharbuf &p) { return false; }
        virtual clientinfo *getcinfo(int n) { return NULL; }

        virtual void onSpawn(clientinfo *victim) {}

        virtual void buildworldstate(struct worldstate &ws) { }
        virtual bool shouldBalanceBots(bool botbalance) { return botbalance; }
    };

    #define SERVMODE 1
    #include "gamemode/capture.h"
    #include "gamemode/ctf.h"
    #include "gamemode/infection.h"
    #include "gamemode/dmsp.h"

    captureservmode capturemode;
    ctfservmode ctfmode;
    infectionservmode infectionmode;
    dmspservmode dmspmode;
    servmode *smode = NULL;

    bool canspawnitem(int type) { return !m_noitems && !m_noammo && type>=I_AMMO && type<=I_QUAD; }

    int spawntime(int type)
    {
        if(m_classicsp) return INT_MAX;
        int np = numclients(-1, true, false);
        np = np<3 ? 4 : (np>4 ? 2 : 3);         // spawn times are dependent on number of players
        int sec = 0;
        switch(type)
        {
            case I_AMMO:
            case I_AMMO2:
            case I_AMMO3:
            case I_AMMO4:
                sec = np*4;
                break;

            case I_HEALTH:
            case I_HEALTH2:
            case I_HEALTH3:
                sec = np*5;
                if(m_infection) sec += rnd(120);
                break;

            case I_GREENARMOUR:
            case I_YELLOWARMOUR:
                sec = 20;
                break;

            case I_MORTAR:
            case I_QUAD:
                sec = 40+rnd(40);
                break;
        }
        return sec*1000;
    }

    bool delayspawn(int type)
    {
        switch(type)
        {
            case I_GREENARMOUR:
            case I_YELLOWARMOUR:
                return !m_classicsp;
                break;

            case I_HEALTH:
            case I_HEALTH2:
            case I_HEALTH3:
                return m_infection;
                break;

            case I_QUAD:
                return true;
                break;

            default:
                return false;
        }
    }

    bool pickup(int i, int sender)         // server side item pickup, acknowledge first client that gets it
    {
        if((m_timed && gamemillis>=gamelimit) || !sents.inrange(i) || !sents[i].spawned) return false;
        clientinfo *ci = getinfo(sender);
        if(!ci || (!ci->local && !ci->state.canpickup(sents[i].type))) return false;
        sents[i].spawned = false;
        sents[i].spawntime = spawntime(sents[i].type);
        sendf(-1, 1, "ri3", N_ITEMACC, i, sender);
        ci->state.pickup(sents[i].type);
        return true;
    }

    clientinfo *choosebestclient(float &bestrank)
    {
        clientinfo *best = NULL;
        bestrank = -1;
        loopv(clients)
        {
            clientinfo *ci = clients[i];
            if(ci->state.timeplayed<0) continue;
            float rank = ci->state.state!=CS_SPECTATOR ? ci->state.effectiveness/max(ci->state.timeplayed, 1) : -1;
            if(!best || rank > bestrank) { best = ci; bestrank = rank; }
        }
        return best;
    }

    void autoteam()
    {
        static const char * const teamnames[2] = {TEAM_0, TEAM_1};
        vector<clientinfo *> team[2];
        float teamrank[2] = {0, 0};
        for(int round = 0, remaining = clients.length(); remaining>=0; round++)
        {
            int first = round&1, second = (round+1)&1, selected = 0;
            while(teamrank[first] <= teamrank[second])
            {
                float rank;
                clientinfo *ci = choosebestclient(rank);
                if(!ci) break;
                if(smode && smode->hidefrags()) rank = 1;
                else if(selected && rank<=0) break;
                ci->state.timeplayed = -1;
                team[first].add(ci);
                if(rank>0) teamrank[first] += rank;
                selected++;
                if(rank<=0) break;
            }
            if(!selected) break;
            remaining -= selected;
        }
        loopi(sizeof(team)/sizeof(team[0]))
        {
            loopvj(team[i])
            {
                clientinfo *ci = team[i][j];
                if(!strcmp(ci->team, teamnames[i])) continue;
                copystring(ci->team, teamnames[i], MAXTEAMLEN+1);
                sendf(-1, 1, "riisi", N_SETTEAM, ci->clientnum, teamnames[i], -1);
            }
        }
    }

    struct teamrank
    {
        const char *name;
        float rank;
        int clients;

        teamrank(const char *name) : name(name), rank(0), clients(0) {}
    };

    const char *chooseworstteam(const char *suggest = NULL, clientinfo *exclude = NULL)
    {
        teamrank teamranks[2] = { teamrank(TEAM_0), teamrank(TEAM_1) };
        const int numteams = sizeof(teamranks)/sizeof(teamranks[0]);
        loopv(clients)
        {
            clientinfo *ci = clients[i];
            if(ci==exclude || ci->state.ai.type != ai::AI_TYPE_NONE || ci->state.state==CS_SPECTATOR || !ci->team[0]) continue;
            ci->state.timeplayed += lastmillis - ci->state.lasttimeplayed;
            ci->state.lasttimeplayed = lastmillis;

            loopj(numteams) if(!strcmp(ci->team, teamranks[j].name))
            {
                teamrank &ts = teamranks[j];
                ts.rank += ci->state.effectiveness/max(ci->state.timeplayed, 1);
                ts.clients++;
                break;
            }
        }
        teamrank *worst = &teamranks[numteams-1];
        loopi(numteams-1)
        {
            teamrank &ts = teamranks[i];
            if(smode && smode->hidefrags())
            {
                if(ts.clients < worst->clients || (ts.clients == worst->clients && ts.rank < worst->rank)) worst = &ts;
            }
            else if(ts.rank < worst->rank || (ts.rank == worst->rank && ts.clients < worst->clients)) worst = &ts;
        }
        return worst->name;
    }

    void writedemo(int chan, void *data, int len)
    {
        if(!demorecord) return;
        int stamp[3] = { gamemillis, chan, len };
        lilswap(stamp, 3);
        demorecord->write(stamp, sizeof(stamp));
        demorecord->write(data, len);
    }

    void recordpacket(int chan, void *data, int len)
    {
        writedemo(chan, data, len);
    }

    void enddemorecord()
    {
        if(!demorecord) return;

        DELETEP(demorecord);

        if(!demotmp) return;

        int len = demotmp->size();
        time_t t = time(NULL);
        char *timestr = ctime(&t), *trim = timestr + strlen(timestr);
        while(trim>timestr && isspace(*--trim)) *trim = '\0';
        defformatstring(demoinfo)("%s: %s, %s, %.2f%s", timestr, modename(gamemode), smapname, len > 1024*1024 ? len/(1024*1024.f) : len/1024.0f, len > 1024*1024 ? "MB" : "kB");
        sendservmsgf(-1, "demo \"%s\" recorded", demoinfo);

        createdir(demodir);
        defformatstring(demoname)("%s/%s-%s.dmo", demodir, timestr, smapname);
        demotmp->seek(0, SEEK_SET);
        uchar buffer[4096];
        int read;
        stream *f = openfile(demoname, "w+b");
        if(f)
        {
            while((read = demotmp->read(buffer, 4096)) > 0)
            {
                f->write(buffer, read);
            }
            delete f;
        }
        else
        {
            conoutf(CON_ERROR, "Could not write demo, could not open demo file: %s", demoname);
        }
        DELETEP(demotmp);
    }

    int welcomepacket(packetbuf &p, clientinfo *ci);
    void sendwelcome(clientinfo *ci);

    void setupdemorecord()
    {
        if(!m_mp(gamemode) || m_edit) return;

        demotmp = opentempfile("demorecord", "w+b");
        if(!demotmp) return;

        stream *f = opengzfile(NULL, "wb", demotmp);
        if(!f) { DELETEP(demotmp); return; }

        sendservmsg("recording demo");

        demorecord = f;

        demoheader hdr;
        memcpy(hdr.magic, DEMO_MAGIC, sizeof(hdr.magic));
        hdr.version = DEMO_VERSION;
        hdr.protocol = PROTOCOL_VERSION;
        lilswap(&hdr.version, 2);
        demorecord->write(&hdr, sizeof(demoheader));

        packetbuf p(MAXTRANS, ENET_PACKET_FLAG_RELIABLE);
        welcomepacket(p, NULL);
        writedemo(1, p.buf, p.len);
    }

    void listdemos(int cn)
    {
        packetbuf p(MAXTRANS, ENET_PACKET_FLAG_RELIABLE);
        putint(p, N_SENDDEMOLIST);
        putint(p, demos.length());
        loopv(demos) sendstring(demos[i].info, p);
        sendpacket(cn, 1, p.finalize());
    }

    void cleardemos(int n)
    {
        if(!n)
        {
            loopv(demos) delete[] demos[i].data;
            demos.shrink(0);
            sendservmsg("cleared all demos");
        }
        else if(demos.inrange(n-1))
        {
            delete[] demos[n-1].data;
            demos.remove(n-1);
            sendservmsgf("cleared demo %d", n);
        }
    }

    static void freegetmap(ENetPacket *packet)
    {
        loopv(clients)
        {
            clientinfo *ci = clients[i];
            if(ci->getmap == packet) ci->getmap = NULL;
        }
    }

    static void freegetdemo(ENetPacket *packet)
    {
        loopv(clients)
        {
            clientinfo *ci = clients[i];
            if(ci->getdemo == packet) ci->getdemo = NULL;
        }
    }

    void senddemo(clientinfo *ci, int num)
    {
        if(ci->getdemo) return;
        if(!num) num = demos.length();
        if(!demos.inrange(num-1)) return;
        demofile &d = demos[num-1];
        if((ci->getdemo = sendf(ci->clientnum, 2, "rim", N_SENDDEMO, d.len, d.data)))
            ci->getdemo->freeCallback = freegetdemo;
    }

    void enddemoplayback()
    {
        if(!demoplayback) return;
        DELETEP(demoplayback);

        loopv(clients) sendf(clients[i]->clientnum, 1, "ri3", N_DEMOPLAYBACK, 0, clients[i]->clientnum);

        sendservmsg("demo playback finished");

        loopv(clients) sendwelcome(clients[i]);
    }

    void setupdemoplayback()
    {
        if(demoplayback) return;
        demoheader hdr;
        string msg;
        msg[0] = '\0';
        defformatstring(file)("%s/%s.dmo", game::demodir, smapname);
        demoplayback = opengzfile(file, "rb");
        if(!demoplayback) formatstring(msg)("could not read demo \"%s\"", file);
        else if(demoplayback->read(&hdr, sizeof(demoheader))!=sizeof(demoheader) || memcmp(hdr.magic, DEMO_MAGIC, sizeof(hdr.magic)))
            formatstring(msg)("\"%s\" is not a demo file", file);
        else
        {
            lilswap(&hdr.version, 2);
            if(hdr.version!=DEMO_VERSION) formatstring(msg)("demo \"%s\" requires an %s version of Revelade Revolution", file, hdr.version<DEMO_VERSION ? "older" : "newer");
            else if(hdr.protocol!=PROTOCOL_VERSION) formatstring(msg)("demo \"%s\" requires an %s version of Revelade Revolution", file, hdr.protocol<PROTOCOL_VERSION ? "older" : "newer");
        }
        if(msg[0])
        {
            DELETEP(demoplayback);
            sendservmsg(msg);
            return;
        }

        sendservmsgf("playing demo \"%s\"", file);

        demomillis = 0;
        sendf(-1, 1, "ri3", N_DEMOPLAYBACK, 1, -1);

        if(demoplayback->read(&nextplayback, sizeof(nextplayback))!=sizeof(nextplayback))
        {
            enddemoplayback();
            return;
        }
        lilswap(&nextplayback, 1);
    }

    void readdemo()
    {
        if(!demoplayback) return;
        demomillis += curtime;
        while(demomillis>=nextplayback)
        {
            int chan, len;
            if(demoplayback->read(&chan, sizeof(chan))!=sizeof(chan) ||
               demoplayback->read(&len, sizeof(len))!=sizeof(len))
            {
                enddemoplayback();
                return;
            }
            lilswap(&chan, 1);
            lilswap(&len, 1);
            ENetPacket *packet = enet_packet_create(NULL, len+1, 0);
            if(!packet || demoplayback->read(packet->data+1, len)!=size_t(len))
            {
                if(packet) enet_packet_destroy(packet);
                enddemoplayback();
                return;
            }
            packet->data[0] = N_DEMOPACKET;
            sendpacket(-1, chan, packet);
            if(!packet->referenceCount) enet_packet_destroy(packet);
            if(!demoplayback) break;
            if(demoplayback->read(&nextplayback, sizeof(nextplayback))!=sizeof(nextplayback))
            {
                enddemoplayback();
                return;
            }
            lilswap(&nextplayback, 1);
        }
    }

    void stopdemo()
    {
        if(m_demo) enddemoplayback();
        else enddemorecord();
    }

    void pausegame(bool val, clientinfo *ci)
    {
        if(gamepaused==val) return;
        gamepaused = val;
        sendf(-1, 1, "riii", N_PAUSEGAME, gamepaused ? 1 : 0, ci ? ci->clientnum : -1);
    }

    void checkpausegame()
    {
        if(!gamepaused) return;
        int admins = 0;
        loopv(clients) if(clients[i]->privilege >= (restrictpausegame ? PRIV_ADMIN : PRIV_MASTER) || clients[i]->local) admins++;
        if(!admins) pausegame(false);
    }

    void forcepaused(bool paused)
    {
        pausegame(paused);
    }

    bool ispaused() { return gamepaused; }

    void changegamespeed(int val, clientinfo *ci = NULL)
    {
        val = clamp(val, 10, 1000);
        if(gamespeed==val) return;
        gamespeed = val;
        sendf(-1, 1, "riii", N_GAMESPEED, gamespeed, ci ? ci->clientnum : -1);
    }

    void forcegamespeed(int speed)
    {
        changegamespeed(speed);
    }

    int scaletime(int t) { return t*gamespeed; }

    void hashpassword(int cn, int sessionid, const char *pwd, char *result, int maxlen)
    {
        char buf[2*sizeof(string)];
        formatstring(buf)("%d %d ", cn, sessionid);
        copystring(&buf[strlen(buf)], pwd);
        if(!hashstring(buf, result, maxlen)) *result = '\0';
    }

    bool checkpassword(clientinfo *ci, const char *wanted, const char *given)
    {
        string hash;
        hashpassword(ci->clientnum, ci->sessionid, wanted, hash, sizeof(hash));
        return !strcmp(hash, given);
    }

    //TODO: check if the player deserves PRIV_PLAYER
    void revokemaster(clientinfo *ci)
    {
        ci->privilege = getDowngradedPrivilege(ci->privilege);
        if(ci->state.state==CS_SPECTATOR && !ci->local) aiman::removeai(ci);

        loopv(clients)
        {
            if(clients[i]->privilege >= PRIV_MASTER) return; //There are still masters left
        }

        //Reset
        mastermode = MM_OPEN;
        allowedips.shrink(0);

        loopv(clients)
        {
            if(clients[i]->privilege >= PRIV_ADMIN) return; //There are still admins left
        }

        checkpausegame();
    }

    void sendPrivilegeList()
    {
        packetbuf p(MAXTRANS, ENET_PACKET_FLAG_RELIABLE);

        putint(p, N_CURRENTMASTER);
        putint(p, mastermode);

        loopv(clients)
        {
            putint(p, clients[i]->clientnum);
            putint(p, clients[i]->privilege);
        }

        sendpacket(-1, 1, p.finalize());
    }

    void setmaster(clientinfo *ci, bool val, int target = -1, const char *pass = "")
    {
        clientinfo *targetInfo = NULL;

        if(val)
        {
            if(target >= 0 && target != ci->clientnum)
            {
                if(ci->privilege < PRIV_MASTER) return; //Access denied

                targetInfo = (server::clientinfo *)getclientinfo(target);
                if(!targetInfo) return; //Invalid client
                int newRank = getUpgradedPrivilege(targetInfo->privilege);
                if(newRank <= targetInfo->privilege) return; //Nothing changed
                targetInfo->privilege = newRank;

                sendservmsgf(-1, "%s gave %s %s.", colorname(ci), colorname(targetInfo), getPrivilegeName(targetInfo->privilege));
            }
            else
            {
                targetInfo = ci;

                int newRank = PRIV_NONE;
                #ifdef SERVER
                if(pass && pass[0] && lua::pushEvent("client.setmaster"))
                {
                    lua_pushnumber(lua::L, ci->clientnum);
                    lua_pushstring(lua::L, pass);
                    lua_call(lua::L, 2, 1);
                        newRank = lua_tonumber(lua::L, -1);
                    lua_pop(lua::L, 1);
                }
                else
                {
                #endif
                    if(ci->state.state==CS_SPECTATOR && !ci->local) return; // Spectators cannot claim master

                    loopv(clients) if(ci!=clients[i] && clients[i]->privilege >= PRIV_MASTER && getUpgradedPrivilege(targetInfo->privilege) <= clients[i]->privilege)
                    {
                        return; //Higher ranked are present
                    }

                    if(!(mastermask&MM_AUTOAPPROVE) && getUpgradedPrivilege(targetInfo->privilege) <= PRIV_MASTER && !ci->local)
                    {
                        sendf(ci->clientnum, 1, "ris", N_SERVMSG, "This server does not allow public mastership.");
                        return; // ^
                    }
                    else
                    {
                        #ifdef SERVER
                        if(lua::pushEvent("client.setmaster"))
                        {
                            lua_pushnumber(lua::L, ci->clientnum);
                            lua_pushstring(lua::L, "");
                            lua_call(lua::L, 2, 1);
                                newRank = lua_tonumber(lua::L, -1);
                            lua_pop(lua::L, 1);
                        }
                        #endif
                        
                        if(newRank < PRIV_NONE)
                        {
                            newRank = getUpgradedPrivilege(targetInfo->privilege);
                        }
                    }
                #ifdef SERVER
                }
                #endif
                
                if(newRank <= targetInfo->privilege) return; //Nothing changed
                targetInfo->privilege = newRank;
                
                sendservmsgf(-1, "%s claimed %s.", colorname(targetInfo), getPrivilegeName(targetInfo->privilege));                     
            }
        }
        else
        {
            int oldPriv;
            string msg;
            if(target >= 0 && target != ci->clientnum)
            {
                targetInfo = (server::clientinfo *)getclientinfo(target);
                if(!targetInfo) return; //Invalid client

                if(targetInfo->privilege > ci->privilege) return; //Higher ranked

                formatstring(msg)("%s took away the privilege of %s.", colorname(ci), colorname(targetInfo));
            }
            else
            {
                if(!ci->privilege) return; //Nothing to give away
                targetInfo = ci;

                formatstring(msg)("%s gave up his privilege.", colorname(targetInfo));
            }

            oldPriv = targetInfo->privilege;
            revokemaster(targetInfo);
            if(oldPriv > targetInfo->privilege)
            {
                sendservmsg(msg);
            }
            else
            {
                return; //Nothing changed
            }
        }

        checkpausegame();
        sendPrivilegeList();
    }

    bool trykick(clientinfo *ci, int victim, const char *reason = NULL, const char *authname = NULL, const char *authdesc = NULL, int authpriv = PRIV_NONE, bool trial = false)
    {
        int priv = ci->privilege;
        if(authname)
        {
            if(priv >= authpriv || ci->local) authname = authdesc = NULL;
            else priv = authpriv;
        }
        if((priv >= PRIV_MASTER || ci->local) && ci->clientnum!=victim)
        {
            clientinfo *vinfo = (clientinfo *)getclientinfo(victim);
            if(vinfo && vinfo->connectionState == CONNECTION_STATE_CONNECTED && (priv >= vinfo->privilege || ci->local) && vinfo->privilege < PRIV_ADMIN && !vinfo->local)
            {
                if(trial) return true;
                string kicker;
                if(authname)
                {
                    if(authdesc && authdesc[0]) formatstring(kicker)("%s as '\fs\f5%s\fS' [\fs\f0%s\fS]", colorname(ci), authname, authdesc);
                    else formatstring(kicker)("%s as '\fs\f5%s\fS'", colorname(ci), authname);
                }
                else copystring(kicker, colorname(ci));
                if(reason && reason[0]) sendservmsgf("%s kicked %s because: %s", kicker, colorname(vinfo), reason);
                else sendservmsgf("%s kicked %s", kicker, colorname(vinfo));
                uint ip = getclientip(victim);
                addban(ip, 4*60*60000);
                kickclients(ip, ci, priv);
            }
        }
        return false;
    }
    savedscore *findscore(clientinfo *ci, bool insert)
    {
        uint ip = getclientip(ci->clientnum);
        if(!ip && !ci->local) return 0;
        if(!insert)
        {
            loopv(clients)
            {
                clientinfo *oi = clients[i];
                if(oi->clientnum != ci->clientnum && getclientip(oi->clientnum) == ip && !strcmp(oi->name, ci->name))
                {
                    oi->state.timeplayed += lastmillis - oi->state.lasttimeplayed;
                    oi->state.lasttimeplayed = lastmillis;
                    static savedscore curscore;
                    curscore.save(oi->state);
                    return &curscore;
                }
            }
        }
        loopv(scores)
        {
            savedscore &sc = scores[i];
            if(sc.ip == ip && !strcmp(sc.name, ci->name)) return &sc;
        }
        if(!insert) return 0;
        savedscore &sc = scores.add();
        sc.ip = ip;
        copystring(sc.name, ci->name);
        return &sc;
    }

    void savescore(clientinfo *ci)
    {
        savedscore *sc = findscore(ci, true);
        if(sc) sc->save(ci->state);
    }

    int checktype(int type, clientinfo *ci)
    {
        if(ci && ci->local) return type;
        // only allow edit messages in coop-edit mode
        if(type>=N_EDITENT && type<=N_EDITVAR && !m_edit) return -1;
        // server only messages
        static const int servtypes[] = {
            N_SERVINFO, N_INITCLIENT, N_WELCOME, N_MAPRELOAD, N_SERVMSG, N_DAMAGE, N_HITPUSH, N_SHOTFX,
            N_EXPLODEFX, N_DIED, N_SPAWNSTATE, N_FORCEDEATH, N_ITEMACC, N_ITEMSPAWN, N_TIMEUP, N_CDIS,
            N_PONG, N_RESUME, N_BASESCORE, N_BASEINFO, N_BASEREGEN, N_ANNOUNCE,
            N_SENDDEMOLIST, N_SENDDEMO, N_DEMOPLAYBACK, N_SENDMAP, N_DROPFLAG, N_SCOREFLAG, N_RETURNFLAG,
            N_RESETFLAG, N_INVISFLAG, N_CLIENT, N_AUTH_SERVER_HELLO, N_SERVER_AUTH, N_INITAI, N_SETONFIRE,
            N_SURVINIT, N_SURVREASSIGN, N_SURVSPAWNSTATE, N_SURVNEWROUND, N_GUTS, N_SHOPOPEN };
        if(ci)
        {
            loopi(sizeof(servtypes)/sizeof(int)) if(type == servtypes[i]) return -1;
            if(type < N_EDITENT || type > N_EDITVAR || !m_edit)
            {
                if(type != N_POS && ++ci->overflow >= 250) return -2;
            }
        }
        return type;
    }

    struct worldstate
    {
        int uses, len;
        uchar *data;

        worldstate() : uses(0), len(0), data(NULL) {}

        void setup(int n) { len = n; data = new uchar[n]; }
        void cleanup() { DELETEA(data); len = 0; }
        bool contains(const uchar *p) const { return p >= data && p < &data[len]; }
    };

    vector<worldstate> worldstates;
    bool reliablemessages = false;

    void cleanworldstate(ENetPacket *packet)
    {
        loopv(worldstates)
        {
            worldstate &ws = worldstates[i];
            if(!ws.contains(packet->data)) continue;
            ws.uses--;
            if(ws.uses <= 0)
            {
                ws.cleanup();
                worldstates.removeunordered(i);
            }
            break;
        }
    }

    void flushclientposition(clientinfo &ci)
    {
        if(ci.position.empty() || (!hasnonlocalclients() && !demorecord)) return;
        packetbuf p(ci.position.length(), 0);
        p.put(ci.position.getbuf(), ci.position.length());
        ci.position.setsize(0);
        sendpacket(-1, 0, p.finalize(), ci.ownernum);
    }

    static void sendpositions(worldstate &ws, ucharbuf &wsbuf)
    {
        if(wsbuf.empty()) return;
        int wslen = wsbuf.length();
        recordpacket(0, wsbuf.buf, wslen);
        wsbuf.put(wsbuf.buf, wslen);
        loopv(clients)
        {
            clientinfo &ci = *clients[i];
            if(ci.state.ai.type != ai::AI_TYPE_NONE) continue;
            uchar *data = wsbuf.buf;
            int size = wslen;
            if(ci.wsdata >= wsbuf.buf) { data = ci.wsdata + ci.wslen; size -= ci.wslen; }
            if(size <= 0) continue;
            ENetPacket *packet = enet_packet_create(data, size, ENET_PACKET_FLAG_NO_ALLOCATE);
            sendpacket(ci.clientnum, 0, packet);
            if(packet->referenceCount) { ws.uses++; packet->freeCallback = cleanworldstate; }
            else enet_packet_destroy(packet);
        }
        wsbuf.offset(wsbuf.length());
    }

    static inline void addposition(worldstate &ws, ucharbuf &wsbuf, int mtu, clientinfo &bi, clientinfo &ci)
    {
        if(bi.position.empty()) return;
        if(wsbuf.length() + bi.position.length() > mtu) sendpositions(ws, wsbuf);
        int offset = wsbuf.length();
        wsbuf.put(bi.position.getbuf(), bi.position.length());
        bi.position.setsize(0);
        int len = wsbuf.length() - offset;
        if(ci.wsdata < wsbuf.buf) { ci.wsdata = &wsbuf.buf[offset]; ci.wslen = len; }
        else ci.wslen += len;
    }

    static void sendmessages(worldstate &ws, ucharbuf &wsbuf)
    {
        if(wsbuf.empty()) return;
        int wslen = wsbuf.length();
        recordpacket(1, wsbuf.buf, wslen);
        wsbuf.put(wsbuf.buf, wslen);
        loopv(clients)
        {
            clientinfo &ci = *clients[i];
            if(ci.state.ai.type != ai::AI_TYPE_NONE) continue;
            uchar *data = wsbuf.buf;
            int size = wslen;
            if(ci.wsdata >= wsbuf.buf) { data = ci.wsdata + ci.wslen; size -= ci.wslen; }
            if(size <= 0) continue;
            ENetPacket *packet = enet_packet_create(data, size, (reliablemessages ? ENET_PACKET_FLAG_RELIABLE : 0) | ENET_PACKET_FLAG_NO_ALLOCATE);
            sendpacket(ci.clientnum, 1, packet);
            if(packet->referenceCount) { ws.uses++; packet->freeCallback = cleanworldstate; }
            else enet_packet_destroy(packet);
        }
        wsbuf.offset(wsbuf.length());
    }

    static inline void addmessages(worldstate &ws, ucharbuf &wsbuf, int mtu, clientinfo &bi, clientinfo &ci)
    {
        if(bi.messages.empty()) return;
        if(wsbuf.length() + 10 + bi.messages.length() > mtu) sendmessages(ws, wsbuf);
        int offset = wsbuf.length();
        putint(wsbuf, N_CLIENT);
        putint(wsbuf, bi.clientnum);
        putuint(wsbuf, bi.messages.length());
        wsbuf.put(bi.messages.getbuf(), bi.messages.length());
        bi.messages.setsize(0);
        int len = wsbuf.length() - offset;
        if(ci.wsdata < wsbuf.buf) { ci.wsdata = &wsbuf.buf[offset]; ci.wslen = len; }
        else ci.wslen += len;
    }

    bool buildworldstate()
    {
        int wsmax = 0;
        loopv(clients)
        {
            clientinfo &ci = *clients[i];
            ci.overflow = 0;
            ci.wsdata = NULL;
            wsmax += ci.position.length();
            if(ci.messages.length()) wsmax += 10 + ci.messages.length();
        }
        if(wsmax <= 0)
        {
            reliablemessages = false;
            return false;
        }
        worldstate &ws = worldstates.add();
        ws.setup(2*wsmax);
        int mtu = getservermtu() - 100;
        if(mtu <= 0) mtu = ws.len;
        ucharbuf wsbuf(ws.data, ws.len);

        loopv(clients)
        {
            clientinfo &ci = *clients[i];
            if(ci.state.ai.type != ai::AI_TYPE_NONE) continue;
            addposition(ws, wsbuf, mtu, ci, ci);
            loopvj(ci.bots) addposition(ws, wsbuf, mtu, *ci.bots[j], ci);
        }
        sendpositions(ws, wsbuf);

        loopv(clients)
        {
            clientinfo &ci = *clients[i];
            if(ci.state.ai.type != ai::AI_TYPE_NONE) continue;
            addmessages(ws, wsbuf, mtu, ci, ci);
            loopvj(ci.bots) addmessages(ws, wsbuf, mtu, *ci.bots[j], ci);
        }
        sendmessages(ws, wsbuf);

        reliablemessages = false;
        if(ws.uses) return true;
        ws.cleanup();
        worldstates.drop();
        return false;
    }

    bool sendpackets(bool force)
    {
        if(clients.empty() || (!hasnonlocalclients() && !demorecord)) return false;
        enet_uint32 curtime = enet_time_get()-lastsend;
        if(curtime<33 && !force) return false;
        bool flush = buildworldstate();
        lastsend += curtime - (curtime%33);
        return flush;
    }

    template<class T>
    void sendstate(gamestate &gs, T &p)
    {
        putint(p, gs.lifesequence);
        putint(p, gs.guts);
        putint(p, gs.health);
        putint(p, gs.maxhealth);
        putint(p, gs.armour);
        putint(p, gs.armourtype);
        putint(p, gs.playerclass);
        putint(p, gs.playermodel);
        putint(p, gs.gunselect);
        loopi(NUMWEAPS) putint(p, gs.ammo[i]);
    }

    void spawnstate(clientinfo *ci)
    {
        gamestate &gs = ci->state;
        gs.spawnstate(gamemode);
        gs.lifesequence = (gs.lifesequence + 1)&0x7F;
    }

    void sendspawn(clientinfo *ci)
    {
        gamestate &gs = ci->state;
        spawnstate(ci);
        sendf(ci->ownernum, 1, "ri2i9v", N_SPAWNSTATE, ci->clientnum, gs.lifesequence, gs.guts,
            gs.health, gs.maxhealth,
            gs.armour, gs.armourtype,
            gs.playerclass, gs.playermodel, gs.gunselect, NUMWEAPS, &gs.ammo);
        gs.lastspawn = gamemillis;
    }

    void sendwelcome(clientinfo *ci)
    {
        packetbuf p(MAXTRANS, ENET_PACKET_FLAG_RELIABLE);
        int chan = welcomepacket(p, ci);
        sendpacket(ci->clientnum, chan, p.finalize());
    }

    void putinitclient(clientinfo *ci, packetbuf &p)
    {
        if(ci->state.ai.type != ai::AI_TYPE_NONE)
        {
            putint(p, N_INITAI);
            putint(p, ci->clientnum);
            putint(p, ci->ownernum);
            putint(p, ci->state.ai.type);
            putint(p, ci->state.ai.skill);
            putint(p, ci->state.playerclass);
            putint(p, ci->state.playermodel);
            sendstring(ci->name, p);
            sendstring(ci->team, p);
        }
        else
        {
            putint(p, N_INITCLIENT);
            putint(p, ci->clientnum);
            sendstring(ci->name, p);
            sendstring(ci->team, p);
            putint(p, ci->state.playerclass);
            putint(p, ci->state.playermodel);
        }
    }

    void welcomeinitclient(packetbuf &p, int exclude = -1)
    {
        loopv(clients)
        {
            clientinfo *ci = clients[i];
            if(ci->connectionState != CONNECTION_STATE_CONNECTED || ci->clientnum == exclude) continue;

            putinitclient(ci, p);
        }
    }

    bool hasmap(clientinfo *ci)
    {
        return (m_edit && (clients.length() > 0 || ci->local)) ||
               (smapname[0] && (!m_timed || gamemillis < gamelimit || (ci->state.state==CS_SPECTATOR && !ci->privilege && !ci->local) || numclients(ci->clientnum, true, true, true)));
    }
    int welcomepacket(packetbuf &p, clientinfo *ci)
    {
        putint(p, N_WELCOME);
        putint(p, allowweaps);
        putint(p, N_MAPCHANGE);
        sendstring(smapname, p);
        putint(p, gamemode);
        putint(p, notgotitems ? 1 : 0);
        if(!ci || (m_timed && smapname[0]))
        {
            putint(p, N_TIMEUP);
            putint(p, gamemillis < gamelimit && !interm ? max((gamelimit - gamemillis)/1000, 1) : 0);
        }
        if(!notgotitems)
        {
            putint(p, N_ITEMLIST);
            loopv(sents) if(sents[i].spawned)
            {
                putint(p, i);
                putint(p, sents[i].type);
            }
            putint(p, -1);
        }
        bool hasmaster = false;
        if(mastermode != MM_OPEN)
        {
            putint(p, N_CURRENTMASTER);
            putint(p, mastermode);

            hasmaster = true;
        }
        loopv(clients) if(clients[i]->privilege >= PRIV_MASTER)
        {
            if(!hasmaster)
            {
                putint(p, N_CURRENTMASTER);
                putint(p, mastermode);
                hasmaster = true;
            }
            putint(p, clients[i]->clientnum);
            putint(p, clients[i]->privilege);
        }

        if(hasmaster) putint(p, -1);
        if(gamepaused)
        {
            putint(p, N_PAUSEGAME);
            putint(p, 1);
            putint(p, -1);
        }
        if(gamespeed != 100)
        {
            putint(p, N_GAMESPEED);
            putint(p, gamespeed);
            putint(p, -1);
        }
        if(ci)
        {
            putint(p, N_SETTEAM);
            putint(p, ci->clientnum);
            sendstring(ci->team, p);
            putint(p, -1);
        }
        if(ci && (m_demo || m_mp(gamemode)) && ci->state.state!=CS_SPECTATOR)
        {
            if(smode && !smode->canspawn(ci, true))
            {
                ci->state.state = CS_DEAD;
                putint(p, N_FORCEDEATH);
                putint(p, ci->clientnum);
                sendf(-1, 1, "ri2x", N_FORCEDEATH, ci->clientnum, ci->clientnum);
            }
            else
            {
                gamestate &gs = ci->state;
                spawnstate(ci);
                putint(p, N_SPAWNSTATE);
                putint(p, ci->clientnum);
                sendstate(gs, p);
                gs.lastspawn = gamemillis;
            }
        }
        if(ci && ci->state.state==CS_SPECTATOR)
        {
            putint(p, N_SPECTATOR);
            putint(p, ci->clientnum);
            putint(p, 1);
            sendf(-1, 1, "ri3x", N_SPECTATOR, ci->clientnum, 1, ci->clientnum);
        }
        if(!ci || clients.length()>1)
        {
            putint(p, N_RESUME);
            loopv(clients)
            {
                clientinfo *oi = clients[i];
                if(ci && oi->clientnum==ci->clientnum) continue;
                putint(p, oi->clientnum);
                putint(p, oi->state.state);
                putint(p, oi->state.frags);
                putint(p, oi->state.flags);
                putint(p, oi->state.quadmillis);
                sendstate(oi->state, p);
            }
            putint(p, -1);
            welcomeinitclient(p, ci ? ci->clientnum : -1);
            loopv(clients)
            {
                clientinfo *oi = clients[i];
                if(oi->state.ai.type != ai::AI_TYPE_NONE || (ci && oi->clientnum == ci->clientnum)) continue;
                if(oi->mapvote[0])
                {
                    putint(p, N_MAPVOTE);
                    putint(p, oi->clientnum);
                    sendstring(oi->mapvote, p);
                    putint(p, oi->modevote);
                }
            }
        }
        if(smode) smode->initclient(ci, p, true);
        return 1;
    }

    bool restorescore(clientinfo *ci)
    {
        //if(ci->local) return false;
        savedscore *sc = findscore(ci, false);
        if(sc)
        {
            sc->restore(ci->state);
            return true;
        }
        return false;
    }

    void sendresume(clientinfo *ci)
    {
        gamestate &gs = ci->state;
        sendf(-1, 1, "ri3i9i3vi",
            N_RESUME, ci->clientnum, gs.state,
            gs.frags, gs.flags, gs.quadmillis,
            gs.lifesequence, gs.guts, gs.health,
            gs.maxhealth, gs.armour, gs.armourtype,
            gs.playerclass, gs.playermodel, gs.gunselect,
            NUMWEAPS, &gs.ammo, -1);
    }

    void sendinitclient(clientinfo *ci)
    {
        packetbuf p(MAXTRANS, ENET_PACKET_FLAG_RELIABLE);
        putinitclient(ci, p);
        sendpacket(-1, 1, p.finalize(), ci->clientnum);
    }

    void loaditems()
    {
        resetitems();
        notgotitems = true;
        if(m_edit || !loadents(smapname, ments, &mcrc))
        {
            sendservmsg("Requesting entities from clients.");
            return;
        }
        else
        {
            sendservmsgf("Loaded %i entities from map file.", ments.length());
        }
        loopv(ments) if(canspawnitem(ments[i].type))
        {
            server_entity se = { NOTUSED, 0, false };
            while(sents.length()<=i) sents.add(se);
            sents[i].type = ments[i].type;
            if(m_mp(gamemode) && delayspawn(sents[i].type)) sents[i].spawntime = spawntime(sents[i].type);
            else sents[i].spawned = true;
        }
        notgotitems = false;
    }
    void changemap(const char *s, int mode)
    {
        // --- RPG: Shattered Shield persistence (milestone steps 1+2, additive; see src/rpg) ---
        loopv(clients)
        {
            clientinfo *ci = clients[i];
            if(ci->connectionState == CONNECTION_STATE_CONNECTED && ci->state.ai.type == ai::AI_TYPE_NONE)
                rpg::savepos(ci->name, smapname, ci->state.o.x, ci->state.o.y, ci->state.o.z, 0.0f /* yaw: not on gamestate; reserved for later */);
        }
        stopdemo();
        sendservmsg("Waiting for map to load on all clients");
        pausegame(true);
        changegamespeed(100);
        waitingForMapLoad = true;
        if(smode) smode->cleanup();
        aiman::resetai();

        gamemode = mode;
        gamemillis = 0;
        gamelimit = (m_overtime ? 15 : 10)*60000;
        interm = 0;
        nextexceeded = 0;
        copystring(smapname, s);
        loaditems();
        scores.shrink(0);
        shouldcheckteamkills = false;
        teamkills.shrink(0);
        loopv(clients)
        {
            clientinfo *ci = clients[i];
            ci->state.timeplayed += lastmillis - ci->state.lasttimeplayed;
        }

        if(!m_mp(gamemode)) kicknonlocalclients(DISC_LOCAL);

        sendf(-1, 1, "risii", N_MAPCHANGE, smapname, gamemode, 1);

        if(m_teammode && !persistteams) autoteam();
        else if (m_oneteam) loopv(clients) strcpy(clients[i]->team, TEAM_0);

        if(m_capture) smode = &capturemode;
        else if(m_ctf) smode = &ctfmode;
        else if(m_infection) smode = &infectionmode;
        else if(m_dmsp) smode = &dmspmode;
        else smode = NULL;

        if(m_timed && smapname[0]) sendf(-1, 1, "ri2", N_TIMEUP, gamemillis < gamelimit && !interm ? max((gamelimit - gamemillis)/1000, 1) : 0);
        loopv(clients)
        {
            clientinfo *ci = clients[i];
            ci->mapchange();
            ci->state.lasttimeplayed = lastmillis;
        }

        aiman::changemap();

        if(m_demo)
        {
            if(clients.length()) setupdemoplayback();
        }
        else if(demonextmatch)
        {
            demonextmatch = false;
            setupdemorecord();
        }

        if (localrecorddemo == 2) demonextmatch = true;
        else localrecorddemo = 0;

        if(smode) smode->setup();
    }

    void rotatemap(bool next)
    {
        if(!maprotations.inrange(curmaprotation))
        {
            changemap("", 1);
            return;
        }
        if(next) 
        {
            curmaprotation = findmaprotation(gamemode, smapname);
            if(curmaprotation >= 0) nextmaprotation();
            else curmaprotation = smapname[0] ? max(findmaprotation(gamemode, ""), 0) : 0;
        }
        maprotation &rot = maprotations[curmaprotation];
        changemap(rot.map, rot.findmode(gamemode));
    }

    struct votecount
    {
        char *map;
        int mode, count;
        votecount() {}
        votecount(char *s, int n) : map(s), mode(n), count(0) {}
    };

    void checkvotes(bool force = false)
    {
        vector<votecount> votes;
        int maxvotes = 0;
        loopv(clients)
        {
            clientinfo *oi = clients[i];
            if(oi->state.state==CS_SPECTATOR && !oi->privilege && !oi->local) continue;
            if(oi->state.ai.type != ai::AI_TYPE_NONE) continue;
            maxvotes++;
            if(!m_valid(oi->modevote)) continue;
            votecount *vc = NULL;
            loopvj(votes) if(!strcmp(oi->mapvote, votes[j].map) && oi->modevote==votes[j].mode)
            {
                vc = &votes[j];
                break;
            }
            if(!vc) vc = &votes.add(votecount(oi->mapvote, oi->modevote));
            vc->count++;
        }
        votecount *best = NULL;
        loopv(votes) if(!best || votes[i].count > best->count || (votes[i].count == best->count && rnd(2))) best = &votes[i];
        if(force || (best && best->count > maxvotes/2))
        {
            if(demorecord) enddemorecord();
            if(best && (best->count > (force ? 1 : maxvotes/2)))
            {
                sendservmsg(force ? "vote passed by default" : "vote passed by majority");
                changemap(best->map, best->mode);
            }
            else rotatemap(true);
        }
    }

    void forcemap(const char *map, int mode)
    {
        stopdemo();
        if(!map[0] && !m_check(mode, M_EDIT)) 
        {
            int idx = findmaprotation(mode, smapname);
            if(idx < 0 && smapname[0]) idx = findmaprotation(mode, "");
            if(idx < 0) return;
            map = maprotations[idx].map;
        }
        if(hasnonlocalclients()) sendservmsgf("local player forced %s on map %s", modename(mode), map[0] ? map : "[new map]");
        changemap(map, mode);
    }

    void vote(const char *map, int reqmode, int sender)
    {
        clientinfo *ci = getinfo(sender);
        if(!ci || (ci->state.state==CS_SPECTATOR && !ci->privilege && !ci->local) || (!ci->local && !m_mp(reqmode))) return;
        if(!m_valid(reqmode)) return;
        if(!map[0] && !m_check(reqmode, M_EDIT)) 
        {
            int idx = findmaprotation(reqmode, smapname);
            if(idx < 0 && smapname[0]) idx = findmaprotation(reqmode, "");
            if(idx < 0) return;
            map = maprotations[idx].map;
        }
        if(lockmaprotation && !ci->local && ci->privilege < (lockmaprotation > 1 ? PRIV_ADMIN : PRIV_MASTER) && findmaprotation(reqmode, map) < 0) 
        {
            sendf(sender, 1, "ris", N_SERVMSG, "This server has locked the map rotation.");
            return;
        }
        copystring(ci->mapvote, map);
        ci->modevote = reqmode;
        if(ci->local || (ci->privilege && mastermode>=MM_VETO))
        {
            if(demorecord) enddemorecord();
            if(!ci->local || hasnonlocalclients())
                sendservmsgf("%s forced %s on map %s", colorname(ci), modename(ci->modevote), ci->mapvote[0] ? ci->mapvote : "[new map]");
            changemap(ci->mapvote, ci->modevote);
        }
        else
        {
            sendservmsgf("%s suggests %s on map %s (select map to vote)", colorname(ci), modename(reqmode), map[0] ? map : "[new map]");
            checkvotes();
        }
    }

    void checkintermission()
    {
        if(gamemillis >= gamelimit && !interm)
        {
            sendf(-1, 1, "ri2", N_TIMEUP, 0);
            if(smode) smode->intermission();
            changegamespeed(100);
            interm = gamemillis + 40000;

            if(intermissionstats)
            {
                #define MAX_VAR(x) \
                    string max##x##Msg = {0}; int max##x = 0; clientinfo *max##x##Client = NULL; \
                    loopv(clients) \
                    { \
                        if(clients[i]->state. x == max##x) \
                        { \
                            max##x##Client = NULL; \
                            break; \
                        } \
                        else if(clients[i]->state. x > max##x) \
                        { \
                            max##x = clients[i]->state. x; \
                            max##x##Client = clients[i]; \
                    } \
                } \
                if(max##x##Client) formatstring(max##x##Msg)("Max " #x ": %s (%i), ", colorname(max##x##Client), max##x);

                MAX_VAR(frags)
                MAX_VAR(flags)
                MAX_VAR(deaths)
                MAX_VAR(shotdamage)
                MAX_VAR(damage)

                sendservmsgf(-1, "\f2%s%s%s%s%s", maxfragsMsg, m_ctf ? maxflagsMsg : "", maxdeathsMsg, maxshotdamageMsg, maxdamageMsg);
                #undef MAX_VAR
            }
        }
    }

    void startintermission() { gamelimit = min(gamelimit, gamemillis); checkintermission(); }

    void dodamage(clientinfo *target, clientinfo *actor, int damage, int gun, const vec &hitpush, bool special)
    {
        // survival damage upgrade: bought damage levels scale a human's outgoing damage (dmsp only)
        if(actor && actor!=target && m_dmsp && actor->state.ai.type==ai::AI_TYPE_NONE && actor->dmgmult > 100)
            damage = damage*actor->dmgmult/100;
        gamestate &ts = target->state;
        ts.dodamage(damage);
        if (damage>0 && target!=actor && !isteam(target->team, actor->team)) actor->state.damage += damage;
        sendf(-1, 1, "ri7", N_DAMAGE, target->clientnum, actor->clientnum, damage, ts.armour, ts.health, gun);
        if(target==actor) target->setpushed();
        else if(!hitpush.iszero())
        {
            ivec v = vec(hitpush).rescale(DNF);
            sendf(ts.health<=0 ? -1 : target->ownernum, 1, "ri7", N_HITPUSH, target->clientnum, gun, damage, v.x, v.y, v.z);
            target->setpushed();
        }
        if (actor!=target && isteam(actor->team, target->team)) actor->state.teamshooter = true;
        if(ts.health<=0)
        {
            if (target->state.isInfected() && monster::providesGuts(&target->state))
            {
                const ::monster::MonsterType &zt = ::monster::getMonsterType(target->state.getMonsterType());
                actor->state.guts += zt.classInfo.maxhealth/zt.freq*2;
                sendf(actor->ownernum, 1, "ri3", N_GUTS, actor->clientnum, actor->state.guts);
            }
            // --- RPG: Shattered Shield persistence (milestone steps 1+2, additive; see src/rpg) ---
            if(actor && actor!=target && target->state.ai.type==ai::AI_TYPE_MONSTER && actor->state.ai.type==ai::AI_TYPE_NONE)
            {
                rpg::unpinmonster(target->clientnum); // test pin ends with the monster
                bool rpg_leveled = false;
                int rpg_newlevel = rpg::awardxp(actor->name, rpg::XP_PER_MONSTER_KILL, &rpg_leveled);
                if(rpg_leveled) sendservmsgf("\f2%s \f7reached RPG level \f2%d!", actor->name, rpg_newlevel);
            }
            target->state.deaths++;
            if(actor!=target && isteam(actor->team, target->team))
            {
                actor->state.teamkills++;
                if (!target->state.teamshooter)
                {
                    actor->state.teamkilled = true;
                    actor->state.guts -= 700;
                    sendf(actor->ownernum, 1, "ri3", N_GUTS, actor->clientnum, actor->state.guts);
                }
            }
            int fragvalue = smode ? smode->fragvalue(target, actor) : (target==actor || isteam(target->team, actor->team) ? -1 : 1);
            actor->state.frags += fragvalue;
            if(fragvalue>0)
            {
                int friends = 0, enemies = 0; // note: friends also includes the fragger
                if(m_teammode) loopv(clients) if(strcmp(clients[i]->team, actor->team)) enemies++; else friends++;
                else { friends = 1; enemies = clients.length()-1; }
                actor->state.effectiveness += fragvalue*friends/float(max(enemies, 1));
            }
            sendf(-1, 1, "ri6", N_DIED, target->clientnum, actor->clientnum, actor->state.frags, gun, special);
            target->position.setsize(0);
            bool didrespawn = false;
            if(smode) didrespawn = smode->died(target, actor);
            ts.lastdeath = gamemillis;
            if(!didrespawn)
            {
                ts.state = CS_DEAD;
                ts.deadflush = ts.lastdeath + DEATHMILLIS;
            }
            if(actor!=target && isteam(actor->team, target->team)) 
            {
                actor->state.teamkills++;
                addteamkill(actor, target, 1);
            }
        }
        else
        {
            if(WEAPONI(gun) == WEAP_SLIME)
            {
                AppliedWeaponEffect &e = target->effects.add();
                e.endTime = gamemillis + 2000;
                e.type = WE_SLIMED;
            }
            
            // Weapon effects are networked the next time updateEffects is called
        }
    }

    // --- RPG: Shattered Shield ability layer (milestone step 3, additive; see src/rpg) ---
    // Server-authoritative ability casting. The client only sends (ability id,
    // client-advisory yaw); cooldown, mana, damage and target validity are all
    // decided here. Damage flows through dodamage(), so a killing blow feeds
    // the steps 1+2 XP hook exactly like a weapon kill. No game-mode logic
    // is touched. Yaw is advisory: it only steers the melee front-cone target
    // pick, never damage/cooldown/mana.

    static clientinfo *rpg_findtarget(clientinfo *cq, const rpg::abilitydef &ab, int yaw)
    {
        clientinfo *best = NULL;
        float bestdist = ab.range;
        vec fwd(-sinf(yaw*RAD), cosf(yaw*RAD), 0); // engine yaw->facing convention, cf. vec(yaw, pitch)
        loopv(clients)
        {
            clientinfo *t = clients[i];
            if(t == cq) continue;
            if(t->state.ai.type != ai::AI_TYPE_MONSTER) continue; // monsters only: PvE world
            if(t->state.state != CS_ALIVE || t->state.health <= 0) continue;
            vec delta(t->state.o);
            delta.sub(cq->state.o);
            float dist = delta.magnitude();
            if(dist > ab.range) continue;
            if(ab.melee)
            {
                if(dist > 0.75f) // standing on the caster counts as "in front"
                {
                    vec dir(delta);
                    dir.normalize();
                    if(fwd.dot(dir) < 0.5f) continue; // ~60-degree front cone
                }
            }
            else
            {
                vec from(cq->state.o); from.z += 12.0f; // ~eye height; gamestate has no eyeheight field
                vec to(t->state.o); to.z += 4;
                vec hitpos;
                if(!raycubelos(from, to, hitpos)) continue; // no line of sight
            }
            if(!best || dist < bestdist) { best = t; bestdist = dist; }
        }
        return best;
    }

    void rpg_cast(clientinfo *cq, int ability, int yaw)
    {
        if(!rpg::connected() || !rpg::validability(ability)) return;
        if(cq->state.ai.type != ai::AI_TYPE_NONE) return; // only real players cast
        const rpg::abilitydef &ab = rpg::ability(ability);
        if(cq->state.state != CS_ALIVE)
        {
            sendf(cq->clientnum, 1, "ris", N_SERVMSG, "\f2You must be alive to cast.");
            return;
        }
        int level = 1, xp = 0;
        if(!rpg::getstats(cq->name, level, xp))
        {
            sendf(cq->clientnum, 1, "ris", N_SERVMSG, "\f2Character not loaded.");
            return;
        }
        clientinfo *target = rpg_findtarget(cq, ab, yaw);
        if(!target)
        {
            defformatstring(msg)("\f2%s: \f7no target in range.", ab.name);
            sendf(cq->clientnum, 1, "ris", N_SERVMSG, msg);
            return;
        }
        int mana = 0, maxmana = 0;
        rpg::getmana(cq->name, mana, maxmana);
        if(mana < ab.manacost)
        {
            defformatstring(msg)("\f2%s: \f7not enough mana (%d/%d).", ab.name, mana, ab.manacost);
            sendf(cq->clientnum, 1, "ris", N_SERVMSG, msg);
            return;
        }
        int waitms = 0;
        if(!rpg::castready(cq->name, ability, totalmillis, waitms))
        {
            defformatstring(msg)("\f2%s \f7on cooldown (%.1fs).", ab.name, waitms/1000.0f);
            sendf(cq->clientnum, 1, "ris", N_SERVMSG, msg);
            return;
        }
        rpg::spendmana(cq->name, ab.manacost);
        vec push(target->state.o);
        push.sub(cq->state.o);
        push.z = 0;
        if(push.squaredlen() > 0.01f) push.normalize();
        int hpbefore = target->state.health;
        dodamage(target, cq, ab.damage, WEAP_FIST, push, false); // real plumbing: kills feed the XP hook
        conoutf("rpg: '%s' casts %s on '%s' for %d (%d -> %d hp)", cq->name, ab.name, target->name, ab.damage, hpbefore, target->state.health);
        defformatstring(msg)("\f2%s \f7hits %s for %d damage.", ab.name, target->name, ab.damage);
        sendf(cq->clientnum, 1, "ris", N_SERVMSG, msg);
    }

    void suicide(clientinfo *ci, int type = 0)
    {
        gamestate &gs = ci->state;
        if(gs.state!=CS_ALIVE) return;
        ci->state.frags += smode ? smode->fragvalue(ci, ci) : -1;
        ci->state.deaths++;
        sendf(-1, 1, "ri6", N_DIED, ci->clientnum, ci->clientnum, gs.frags, -2-type, 0);
        ci->position.setsize(0);
        bool didrespawn = false;
        if(smode) didrespawn = smode->died(ci, NULL);
        gs.lastdeath = gamemillis;
        if(!didrespawn)
        {
            gs.state = CS_DEAD;
            gs.deadflush = gs.lastdeath;
        }
    }

    void suicideevent::process(clientinfo *ci)
    {
        suicide(ci, this->type);
    }

    void explodeevent::process(clientinfo *ci)
    {
        gamestate &gs = ci->state;
        switch(WEAPONI(gun))
        {
            case WEAP_ROCKETL:
                if(!gs.rockets.remove(id)) return;
                break;

            case WEAP_GRENADIER:
                if(!gs.grenades.remove(id)) return;
                break;
        }
        sendf(-1, 1, "ri4x", N_EXPLODEFX, ci->clientnum, gun, id, ci->ownernum);
        loopv(hits)
        {
            hitinfo &h = hits[i];
            clientinfo *target = getinfo(h.target);
            if(!target || target->state.state!=CS_ALIVE || (h.lifesequence!=target->state.lifesequence) || h.dist<0 || (WEAP_IS_EXPLOSIVE(gun) && h.dist>WEAP(gun,projradius))) continue;

            bool dup = false;
            loopj(i) if(hits[j].target==h.target) { dup = true; break; }
            if(dup) continue;
            int damage = game::getradialdamage(WEAP(gun,damage), gun, h.headshot, gs.quadmillis, h.dist/(WEAP_IS_EXPLOSIVE(gun)? WEAP(gun,projradius): WEAP(gun,range)));
            if (!WEAP_IS_EXPLOSIVE(gun)) direct = h.headshot;
            if(target==ci) damage /= GUN_EXP_SELFDAMDIV;
            dodamage(target, ci, damage, gun, h.dir, direct);
        }
    }

    void shotevent::process(clientinfo *ci)
    {
        gamestate &gs = ci->state;
        int wait = millis - gs.lastshot;
        if(!gs.isalive(gamemillis) || wait<gs.gunwait || (WEAP(gun,range) && from.dist(to) > WEAP(gun,range) + 1)) return;
        if (!CAN_SHOOT_WITH2(gs,gun))
        {
            DEBUG_ERROR("%s[%i], cannot shoot with %i", colorname(ci), gs.playerclass, gun);
            return;
        }
        gs.ammo[WEAPONI(gun)] = max(gs.ammo[WEAPONI(gun)]-WEAP(gun,numshots), 0);

        gs.lastshot = millis;
        gs.gunwait = WEAP(gun,attackdelay);
        if (numrays != WEAP(gun,numrays))
        {
            DEBUG_ERROR("%s, cannot shoot with %i numrays mismatch net: %i ours: %i", colorname(ci), gun, numrays, WEAP(gun,numrays));
            return;
        }
        sendf(-1, 1, "rii9ivx", N_SHOTFX, ci->clientnum, gun, id,
                int(from.x*DMF), int(from.y*DMF), int(from.z*DMF),
                int(to.x*DMF), int(to.y*DMF), int(to.z*DMF),
                numrays, numrays*sizeof(ivec)/sizeof(int), &rays,
                ci->ownernum);

        int damage;
        switch(WEAPONI(gun))
        {
            case WEAP_ROCKETL: gs.rockets.add(id); break;
            case WEAP_GRENADIER: gs.grenades.add(id); break;
            case WEAP_FLAMEJET: /*gs.flames.add(id);*/ break;
            default:
            {
                int totalrays = 0, maxrays = WEAP(gun,numrays);
                loopv(hits)
                {
                    hitinfo &h = hits[i];
                    clientinfo *target = getinfo(h.target);
                    if(!target || target->state.state!=CS_ALIVE || h.lifesequence!=target->state.lifesequence || h.rays<1 || h.dist > WEAP(gun,range) + 1) continue;

                    damage = h.rays*game::getdamageranged(WEAP(gun,damage), gun, headshot, gs.quadmillis, from, target->state.o);
                    gs.shotdamage += damage;

                    totalrays += h.rays;
                    if(totalrays>maxrays) continue;
                    dodamage(target, ci, damage, gun, h.dir, h.headshot);
                }
                break;
            }
        }
    }

    void pickupevent::process(clientinfo *ci)
    {
        gamestate &gs = ci->state;
        if(m_mp(gamemode) && !gs.isalive(gamemillis)) return;
        pickup(ent, ci->clientnum);
    }

    bool gameevent::flush(clientinfo *ci, int fmillis)
    {
        process(ci);
        return true;
    }

    bool timedevent::flush(clientinfo *ci, int fmillis)
    {
        if(millis > fmillis) return false;
        else if(millis >= ci->lastevent)
        {
            ci->lastevent = millis;
            process(ci);
        }
        return true;
    }

    void flushevents(clientinfo *ci, int millis)
    {
        while(ci->events.length())
        {
            gameevent *ev = ci->events[0];
            if(ev->flush(ci, millis))
            {
                ASSERT(ev == ci->events[0]);
                delete ci->events.remove(0);
            }
            else break;
        }
    }

    void processevents()
    {
        loopv(clients)
        {
            clientinfo *ci = clients[i];
            if(curtime>0 && ci->state.quadmillis) ci->state.quadmillis = max(ci->state.quadmillis-curtime, 0);
            flushevents(ci, gamemillis);
        }
    }

    void cleartimedevents(clientinfo *ci)
    {
        int keep = 0;
        loopv(ci->events)
        {
            if(ci->events[i]->keepable())
            {
                if(keep < i)
                {
                    for(int j = keep; j < i; j++) delete ci->events[j];
                    ci->events.remove(keep, i - keep);
                    i = keep;
                }
                keep = i+1;
                continue;
            }
        }
        while(ci->events.length() > keep) delete ci->events.pop();
        ci->timesync = false;
    }
    
    void updateEffects()
    {
        loopv(clients)
        {
            WeaponEffect newEffects = WE_NONE;
            loopvj(clients[i]->effects)
            {
                if(clients[i]->effects[j].endTime <= gamemillis)
                {
                    
                }
                else
                {
                    newEffects |= clients[i]->effects[j].type;
                }
            }

            if(newEffects != clients[i]->state.effects)
            {
                clients[i]->state.effects = newEffects;
                sendf(-1, 1, "ri3", N_EFFECT, clients[i]->clientnum, clients[i]->state.effects);
            }
        }
    }

    void serverupdate()
    {
        // --- RPG: Shattered Shield persistence (milestone steps 1+2, additive; see src/rpg) ---
        // --- RPG: Shattered Shield ability layer (milestone step 3, additive; see src/rpg) ---
        rpg::regen(totalmillis); // session mana regen; cheap, runs every tick
        static int rpg_lastautosave = 0;
        if(totalmillis - rpg_lastautosave >= 60000)
        {
            rpg_lastautosave = totalmillis;
            loopv(clients)
            {
                clientinfo *ci = clients[i];
                if(ci->connectionState == CONNECTION_STATE_CONNECTED && ci->state.ai.type == ai::AI_TYPE_NONE)
                    rpg::savepos(ci->name, smapname, ci->state.o.x, ci->state.o.y, ci->state.o.z, 0.0f /* yaw: not on gamestate; reserved for later */);
            }
        }
        if(waitingForMapLoad)
        {
            int totalMapLoadTime = 0;
            int numLoadedMap = 0, numTotalClients = 0;
            loopv(clients)
            {
                if(clients[i]->state.ai.type == ai::AI_TYPE_NONE)
                {
                    if(clients[i]->maploaded)
                    {
                        totalMapLoadTime += clients[i]->maploaded;
                        numLoadedMap++;
                    }
                    numTotalClients ++;
                }
            }

            // everyone is done loading or we reached the threshold (avg + 5 sec)
            if(numLoadedMap == numTotalClients || (numLoadedMap > 1 && totalmillis >= totalMapLoadTime/numLoadedMap + 5000))
            {
                sendservmsg(numLoadedMap == numTotalClients ? "All clients loaded the map" : "Some clients were too slow, starting the match anyway.");
                pausegame(false);
                waitingForMapLoad = false;
            }
        }

        if(shouldstep && !gamepaused)
        {
            gamemillis += curtime;

            if(m_demo) readdemo(); 
            else if(!m_timed || gamemillis < gamelimit)
            {
                processevents();
                aiman::doresetai(); // apply any AI reset deferred from inside event processing
                if(curtime)
                {
                    loopv(sents) if(sents[i].spawntime) // spawn entities when timer reached
                    {
                        int oldtime = sents[i].spawntime;
                        sents[i].spawntime -= curtime;
                        if(sents[i].spawntime<=0)
                        {
                            sents[i].spawntime = 0;
                            sents[i].spawned = true;
                            sendf(-1, 1, "ri2", N_ITEMSPAWN, i);
                        }
                        else if(sents[i].spawntime<=10000 && oldtime>10000 && (sents[i].type==I_QUAD))
                        {
                            sendf(-1, 1, "ri2", N_ANNOUNCE, sents[i].type);
                        }
                    }
                }
                aiman::checkai();
                updateEffects();
                if(smode) smode->update();
            }
        }

        if(smode) smode->updateRaw();

        while(bannedips.length() && bannedips[0].expire-totalmillis <= 0) bannedips.remove(0);
        loopv(connects) if(totalmillis-connects[i]->connectmillis>15000) disconnect_client(connects[i]->clientnum, DISC_TIMEOUT);


        if(nextexceeded && gamemillis > nextexceeded && (!m_timed || gamemillis < gamelimit))
        {
            nextexceeded = 0;
            loopvrev(clients)
            {
                clientinfo &c = *clients[i];
                if(c.state.ai.type != ai::AI_TYPE_NONE) continue;
                if(c.checkexceeded()) disconnect_client(c.clientnum, DISC_MSGERR);
                else c.scheduleexceeded();
            }
        }

        if(shouldcheckteamkills) checkteamkills();

        if(shouldstep && !gamepaused)
        {
            if(m_timed && smapname[0] && gamemillis-curtime>0) checkintermission();
            if(interm > 0 && gamemillis>interm)
            {
                if(demorecord) enddemorecord();
                    interm = -1;
                checkvotes(true);
            }
        }

        loopv(clients) if (clients[i]->state.onfire)
        {
            gamestate &gs = clients[i]->state;
            int burnmillis = gamemillis-gs.burnmillis;
            if (gs.state == CS_ALIVE && gamemillis-gs.lastburnpain >= clamp(burnmillis, 200, 1000))
            {
                int damage = min(WEAP(WEAP_FLAMEJET,damage)*1000/max(burnmillis, 1000), gs.health)*(gs.fireattacker->state.quadmillis? 4: 1);
                dodamage(clients[i], gs.fireattacker, damage, WEAP_FLAMEJET);
                gs.lastburnpain = gamemillis;
            }

            if (burnmillis > 4000)
            {
                gs.onfire = false;
                sendf(-1, 1, "ri5", N_SETONFIRE, clients[i]->state.fireattacker->clientnum, clients[i]->clientnum, WEAP_FLAMEJET, 0);
            }
        }

        shouldstep = clients.length() > 0;
    }

    void forcespectator(clientinfo *ci)
    {
        if(ci->state.state==CS_ALIVE) suicide(ci);
        if(smode) smode->leavegame(ci);
        ci->state.state = CS_SPECTATOR;
        ci->state.timeplayed += lastmillis - ci->state.lasttimeplayed;
        if(!ci->local && (!ci->privilege || ci->warned)) aiman::removeai(ci);
        sendf(-1, 1, "ri3", N_SPECTATOR, ci->clientnum, 1);
    }

    struct crcinfo
    {
        int crc, matches;

        crcinfo() {}
        crcinfo(int crc, int matches) : crc(crc), matches(matches) {}

        static bool compare(const crcinfo &x, const crcinfo &y) { return x.matches > y.matches; }
    };
    VAR(modifiedmapspectator, 0, 0, 2);

    void checkmaps(int req = -1)
    {
        if(m_edit || !smapname[0]) return;
        vector<crcinfo> crcs;
        int total = 0, unsent = 0, invalid = 0;
        if(mcrc) crcs.add(crcinfo(mcrc, clients.length() + 1));
        loopv(clients)
        {
            clientinfo *ci = clients[i];
            if(ci->state.state==CS_SPECTATOR || ci->state.ai.type != ai::AI_TYPE_NONE) continue;
            total++;
            if(!ci->clientmap[0])
            {
                if(ci->mapcrc < 0) invalid++;
                else if(!ci->mapcrc) unsent++;
            }
            else
            {
                crcinfo *match = NULL;
                loopvj(crcs) if(crcs[j].crc == ci->mapcrc) { match = &crcs[j]; break; }
                if(!match) crcs.add(crcinfo(ci->mapcrc, 1));
                else match->matches++;
            }
        }
        if(!mcrc && total - unsent < min(total, 4)) return;
        crcs.sort(crcinfo::compare);
        string msg;
        loopv(clients)
        {
            clientinfo *ci = clients[i];
            if(ci->state.state==CS_SPECTATOR || ci->state.ai.type != ai::AI_TYPE_NONE || ci->clientmap[0] || ci->mapcrc >= 0 || (req < 0 && ci->warned)) continue;
            formatstring(msg)("%s has modified map \"%s\"", colorname(ci), smapname);
            sendf(req, 1, "ris", N_SERVMSG, msg);
            if(req < 0) ci->warned = true;
        }
        if(crcs.length() >= 2) loopv(crcs)
        {
            crcinfo &info = crcs[i];
            if(i || info.matches <= crcs[i+1].matches) loopvj(clients)
            {
                clientinfo *ci = clients[j];
                if(ci->state.state==CS_SPECTATOR || ci->state.ai.type != ai::AI_TYPE_NONE || !ci->clientmap[0] || ci->mapcrc != info.crc || (req < 0 && ci->warned)) continue;
                formatstring(msg)("%s has modified map \"%s\"", colorname(ci), smapname);
                sendf(req, 1, "ris", N_SERVMSG, msg);
                if(req < 0) ci->warned = true;
            }
        }
        if(req < 0 && modifiedmapspectator && (mcrc || modifiedmapspectator > 1)) loopv(clients)
        {
            clientinfo *ci = clients[i];
            if(!ci->local && ci->warned && ci->state.state != CS_SPECTATOR) forcespectator(ci);
        }
    }

    bool shouldspectate(clientinfo *ci)
    {
        return !ci->local && ci->warned && modifiedmapspectator && (mcrc || modifiedmapspectator > 1);
    }

    void unspectate(clientinfo *ci)
    {
        if(shouldspectate(ci)) return;
        ci->state.state = CS_DEAD;
        ci->state.respawn();
        ci->state.lasttimeplayed = lastmillis;
        aiman::addclient(ci);
        if(ci->clientmap[0] || ci->mapcrc) checkmaps();
        sendf(-1, 1, "ri3", N_SPECTATOR, ci->clientnum, 0);
        if(!hasmap(ci)) rotatemap(true);
    }

    void sendservinfo(clientinfo *ci)
    {
        int authMode = 0;
        if(serverpass[0])                       authMode |= 1 << 0;
        if(!ci->local && auth::serverCertificate)   authMode |= 1 << 1;

        sendf(ci->clientnum, 1, "ri5s", N_SERVINFO, ci->clientnum, PROTOCOL_VERSION, ci->sessionid, authMode, serverdesc);

        // TODO Note: we assume that the client will always receive N_AUTH_SERVER_HELLO AFTER N_SERVINFO due to the fact it's sending a file. Can we be so certain?
        if(!ci->local && auth::serverCertificate)
        {
            conoutf("Sending server authentication info to %i", ci->clientnum);
            ci->connectionState = CONNECTION_STATE_SERVER_HELLO;
            sendfile(ci->clientnum, 2, auth::serverCertificate, "ri", N_AUTH_SERVER_HELLO);
        }
    }

    void noclients()
    {
        bannedips.shrink(0);
        aiman::clearai();
    }

    void localconnect(int n)
    {
        clientinfo *ci = getinfo(n);
        ci->clientnum = ci->ownernum = n;
        ci->connectmillis = totalmillis;
        ci->sessionid = (rnd(0x1000000)*((totalmillis%10000)+1))&0xFFFFFF;
        ci->local = true;

        connects.add(ci);
        sendservinfo(ci);
    }

    void localdisconnect(int n)
    {
        if(m_demo) enddemoplayback();
        clientdisconnect(n);
    }

    int clientconnect(int n, uint ip)
    {
        clientinfo *ci = getinfo(n);
        ci->clientnum = ci->ownernum = n;
        ci->connectmillis = totalmillis;
        ci->sessionid = (rnd(0x1000000)*((totalmillis%10000)+1))&0xFFFFFF;

        connects.add(ci);
        if(!m_mp(gamemode)) return DISC_LOCAL;
        sendservinfo(ci);
        return DISC_NONE;
    }

    void clientdisconnect(int n)
    {
        clientinfo *ci = getinfo(n);
        if(ci->connectionState == CONNECTION_STATE_CONNECTED)
        {
            #ifdef SERVER
            if(lua::pushEvent("client.disconnect"))
            {
                lua_pushnumber(lua::L, ci->clientnum);
                lua_call(lua::L, 1, 0);
            }
            #endif

            if(ci->privilege) setmaster(ci, false, ci->clientnum);
            if(smode) smode->leavegame(ci, true);
            ci->state.timeplayed += lastmillis - ci->state.lasttimeplayed;
            // --- RPG: Shattered Shield persistence (milestone steps 1+2, additive; see src/rpg) ---
            rpg::ondisconnect(ci->name, smapname, ci->state.o.x, ci->state.o.y, ci->state.o.z, 0.0f /* yaw: not on gamestate; reserved for later */);
            savescore(ci);
            sendf(-1, 1, "ri2", N_CDIS, n);
            clients.removeobj(ci);
            aiman::removeai(ci);
            if(!numclients(-1, false, true)) noclients(); // bans clear when server empties
            if(ci->local) checkpausegame();
        }
        else connects.removeobj(ci);
    }

    int reserveclients() { return 3; }

    vector<ipmask> gbans;

    void cleargbans()
    {
        gbans.shrink(0);
    }

    bool checkgban(uint ip)
    {
        loopv(gbans) if(gbans[i].check(ip)) return true;
        return false;
    }

    void addgban(const char *name)
    {
        ipmask ban;
        ban.parse(name);
        gbans.add(ban);

        loopvrev(clients)
        {
            clientinfo *ci = clients[i];
            if(ci->state.ai.type != ai::AI_TYPE_NONE || ci->local || ci->privilege >= PRIV_ADMIN) continue;
            if(checkgban(getclientip(ci->clientnum))) disconnect_client(ci->clientnum, DISC_IPBAN);
        }
    }

    int allowconnect(clientinfo *ci, const char *pwd = "")
    {
        if(ci->local) return DISC_NONE;
        if(!m_mp(gamemode)) return DISC_LOCAL;
        if(serverpass[0])
        {
            if(!checkpassword(ci, serverpass, pwd)) return DISC_PASSWORD;
            return DISC_NONE;
        }
        if(adminpass[0] && checkpassword(ci, adminpass, pwd)) return DISC_NONE;
        if(numclients(-1, false, true)>=maxclients) return DISC_MAXCLIENTS;
        uint ip = getclientip(ci->clientnum);
        loopv(bannedips) if(bannedips[i].ip==ip) return DISC_IPBAN;
        if(checkgban(ip)) return DISC_IPBAN;
        if(mastermode>=MM_PRIVATE && allowedips.find(ip)<0) return DISC_PRIVATE;
        return DISC_NONE;
    }

    bool allowbroadcast(int n)
    {
        clientinfo *ci = getinfo(n);
        return ci && ci->connectionState == CONNECTION_STATE_CONNECTED;
    }

    void receivefile(int sender, uchar *data, int len)
    {
        if(!m_edit || len > 4*1024*1024) return;
        clientinfo *ci = getinfo(sender);
        if(ci->state.state==CS_SPECTATOR && !ci->privilege && !ci->local) return;
        if(mapdata) DELETEP(mapdata);
        if(!len) return;
        mapdata = opentempfile("mapdata", "w+b");
        if(!mapdata) { sendf(sender, 1, "ris", N_SERVMSG, "failed to open temporary file for map"); return; }
        mapdata->write(data, len);
        sendservmsgf("[%s sent a map to server, \"/getmap\" to receive it]", colorname(ci));
    }

    void sendclipboard(clientinfo *ci)
    {
        if(!ci->lastclipboard || !ci->clipboard) return;
        bool flushed = false;
        loopv(clients)
        {
            clientinfo &e = *clients[i];
            if(e.clientnum != ci->clientnum && e.needclipboard - ci->lastclipboard >= 0)
            {
                if(!flushed) { flushserver(true); flushed = true; }
                sendpacket(e.clientnum, 1, ci->clipboard);
            }
        }
    }

    void finishConnect(clientinfo *ci)
    {
        if(m_demo) enddemoplayback();

        if(!hasmap(ci)) rotatemap(false);

        shouldstep = true;
        connects.removeobj(ci);
        clients.add(ci);

        ci->connectionState = CONNECTION_STATE_CONNECTED;
        ci->needclipboard = totalmillis ? totalmillis : 1;
        if(mastermode>=MM_LOCKED) ci->state.state = CS_SPECTATOR;
        ci->state.lasttimeplayed = lastmillis;

        const char *worst = m_teammode ? chooseworstteam(NULL, ci) : NULL;
        copystring(ci->team, worst ? worst : "good", MAXTEAMLEN+1);

        sendwelcome(ci);
        if(restorescore(ci)) sendresume(ci);
        sendinitclient(ci);

        aiman::addclient(ci);

        if(m_demo) setupdemoplayback();
        if(smode) smode->entergame(ci);

        // --- RPG: Shattered Shield persistence (milestone steps 1+2, additive; see src/rpg) ---
        rpg::onconnect(ci->name);

        #ifdef SERVER
        if(false && lua::pushEvent("client.connect"))
        {
            lua_pushnumber(lua::L, ci->clientnum);
            lua_call(lua::L, 1, 0);
        }
        #endif
    }

    VAR(dbgnetwork, 0, 0, 1);
    void dumpReceivedPackage(int realSender, int chan, const packetbuf &p_)
    {
        if(chan == 0) return; //ignore N_POS
        ucharbuf p = p_;
        int sender = realSender;
        clientinfo *ci = sender>=0 ? getinfo(sender) : NULL, *cq = ci, *cm = ci;
        printf("%i(%i)[%i]->", sender, chan, ci ? ci->connectionState : -1);
        if(sender<0)
        {
            printf("<ignored: invalid client>");
        }
        else if(p_.packet->flags&ENET_PACKET_FLAG_UNSEQUENCED)
        {
            printf("<ignored: not sequenced>");
        }
        else if(chan > 2)
        {
            printf("<ignored: invalid channel>");
        }
        else
        {
            string str;
            int curmsg, type, rawtype;
            while((curmsg = p.length()) < p.maxlen) switch(type = checktype(rawtype = getint(p), ci))
            {
                case N_CONNECT:
                    getstring(str, p);
                    printf("<N_CONNECT[%i], %s", N_CONNECT, str);
                    getstring(str, p);
                    printf(" (pwd: %s mdl: %i pc: %i)>", str, getint(p), getint(p));
                    break;
                case N_TEXT:
                    getstring(str, p);
                    printf("<N_TEXT[%i], %s>", N_TEXT, str);
                    break;
                case N_SOUND:
                    printf("<N_SOUND[%i], %i>", N_TEXT, getint(p));
                    break;
                case N_GUNSELECT:
                    printf("<N_GUNSELECT[%i]: %i>", N_GUNSELECT, getint(p));
                    break;
                case N_SHOOT:
                {
                    int hits;
                    printf("<N_SHOOT[%i]: (id:%i millis:%i gun:%i from:[%i,%i,%i] to:[%i,%i,%i] hits[%i]:[", N_SHOOT, getint(p), getint(p), getint(p), getint(p), getint(p), getint(p), getint(p), getint(p), getint(p), hits = getint(p));
                    loopk(hits)
                    {
                        if(p.overread()) break;
                        printf("(target:%i lifesequence: %i dist:%i rays: %i headshot: %i dir:[%i,%i,%i]),", getint(p), getint(p), getint(p), getint(p), getint(p), getint(p), getint(p), getint(p));
                    }
                    int rays;
                    printf("] rays[%i]:[", rays = getint(p));
                    loopj(rays)
                    {
                        if(p.overread()) break;
                        printf("[%i,%i,%i],", getint(p), getint(p), getint(p));
                    }
                    printf("]>");
                }
                break;
                case N_EXPLODE:
                {
                    int hits;
                    printf("<N_EXPODE[%i]: (millis:%i gun:%i id:%i direct:%i hits[%i]:[", N_EXPLODE, getint(p), getint(p), getint(p), getint(p), hits = getint(p));
                    loopk(hits)
                    {
                        if(p.overread()) break;
                        printf("(target:%i lifesequence:%i dist:%i rays:%i headshot:%i dir:[%i, %i, %i])", getint(p), getint(p), getint(p), getint(p), getint(p), getint(p), getint(p), getint(p));
                    }
                    printf("]>");
                    break;
                }
                break;
                case N_TRYSPAWN:
                    printf("<N_TRYSPAWN[%i] (pc:%i pm:%i)>", N_TRYSPAWN, getint(p), getint(p));
                    break;
                case N_SPAWN:
                    printf("<N_SPAWN[%i]: (ls:%i gun:%i)>", N_SPAWN, getint(p), getint(p));
                    break;
                case N_SWITCHNAME:
                    getstring(str, p);
                    printf("<N_SWITCHNAME[%i]: %s>", N_SWITCHNAME, str);
                    break;
                case N_MAPCRC:
                    getstring(str, p);
                    printf("<N_MAPCRC[%i]: %s = %i>", N_MAPCRC, str, getint(p));
                    break;
                case N_BOTBALANCE:
                    printf("<N_BOTBALANCE[%i]: %i>", N_BOTBALANCE, getint(p));
                    break;
                case N_PING:
                    printf("<N_PING[%i]: %i>", N_PING, getint(p));
                    break;
                case N_CLIENTPING:
                    printf("<N_CLIENTPING[%i]: %i>", N_CLIENTPING, getint(p));
                    break;
                case N_TAKEFLAG:
                    printf("<N_TAKEFLAG[%i]: (flag:%i version:%i)>", N_TAKEFLAG, getint(p), getint(p));
                    break;
                case N_FROMAI:
                    printf("<N_FROMAI[%i] %i>", N_FROMAI, sender = getint(p));
                    break;
                default:
                    printf("<unkown packet %i (raw:%i n:%i)>", type, rawtype, curmsg);
                    break;
            }
        }
        ucharbuf q = p_;
        while(q.remaining()) printf("%i|", getint(q));
        printf("\n");
    }

    void parsepacket(int sender, int chan, packetbuf &p)     // has to parse exactly each byte of the packet
    {
        if(dbgnetwork) dumpReceivedPackage(sender, chan, p);
        if(sender<0 || p.packet->flags&ENET_PACKET_FLAG_UNSEQUENCED || chan > 2) return;
        char text[MAXTRANS];
        int type;
        clientinfo *ci = sender>=0 ? getinfo(sender) : NULL, *cq = ci, *cm = ci;
        if(ci && ci->connectionState != CONNECTION_STATE_CONNECTED)
        {
            // Only allow client to send us a file while when it should
            if(chan != 1 && (chan != 2 || ci->connectionState != CONNECTION_STATE_SERVER_HELLO)) return;
            else
            {
                type = getint(p);

                switch(type)
                {
                    case N_CONNECT:
                    {
                        if(chan != 1) return;
                        getstring(text, p);
                        filtertext(text, text, NAMEALLOWSPACES, MAXNAMELEN);
                        if(!text[0]) copystring(text, "unnamed");
                        copystring(ci->name, text, MAXNAMELEN+1);

                        ci->state.playerclass = getint(p);
                        ci->state.playermodel = getint(p);

                        getstring(text, p);
                        int disc = allowconnect(ci, text);
                        if(disc)
                        {
                            //TODO: check if would be allowed to stay when having authenticated
                            disconnect_client(sender, disc);
                            return;
                        }

                        // Only finish connection when we're not waiting for authentication
                        if(ci->connectionState == CONNECTION_STATE_NONE) finishConnect(ci);
                    }
                    break;

                    case N_CLIENT_AUTH:
                        if(ci->connectionState != CONNECTION_STATE_SERVER_HELLO) return;
                        if(chan == 1)
                        {
                            // Client does not have a cert, we don't allow that in vanilla
                            sendservmsgf(sender, "This servers requires you to be authenticated to play.");
                            conoutf("%i failed to authenticate (no cert).", sender);
                            disconnect_client(sender, DISC_KICK);
                        }
                        else
                        {
                            conoutf("Receiving client certificate and challenge from %s.", colorname(ci));

                            int encryptedRandomnessLength = getint(p);
                            ucharbuf encryptedRandomness = p.subbuf(encryptedRandomnessLength);

                            defformatstring(clientCertFileName)("resources/cert/current-client-%i.pub", ci->clientnum);
                            ci->clientCertificate = opentempfile(clientCertFileName, "w+b");

                            if(!ci->clientCertificate)
                            {
                                sendservmsgf(sender, "Could not open client cert file on the server.");
                                conoutf("Could not open cert file for %s.", colorname(ci));
                                disconnect_client(sender, DISC_KICK);
                            }
                            else
                            {
                                ci->clientCertificate->seek(0, SEEK_SET);
                                ci->clientCertificate->write(p.buf+p.len, p.remaining());
                                ci->clientCertificate->seek(0, SEEK_SET);

                                if(!auth::verifyCertificateWithCA(ci->clientCertificate))
                                {
                                    sendservmsgf(sender, "Failed to verify certificate with CA.");
                                    conoutf("%s's certificate could not be verified with CA.", colorname(ci));
                                    disconnect_client(sender, DISC_KICK);
                                }
                                else if(!auth::verifyNotInCRL(ci->clientCertificate))
                                {
                                    sendservmsgf(sender, "Your certificate was found in the CRL.");
                                    conoutf("Client certificate is in the CRL %s.", colorname(ci));
                                    disconnect_client(sender, DISC_KICK);
                                }
                                else
                                {
                                    uchar *out = NULL; int outSize = 0;
                                    if(!auth::decryptWithPrivateCert(auth::serverCertificatePrivate, encryptedRandomness.buf, encryptedRandomness.maxlen, &out, &outSize))
                                    {
                                        sendservmsgf(sender, "Failed to decrypt the random data.");
                                        conoutf("Failed to decrypt the random client data of %s.", colorname(ci));
                                        disconnect_client(sender, DISC_KICK);
                                    }
                                    else
                                    {
                                        uchar *challengeOut = NULL; int challengeOutSize = 0;

                                        auth::getRandom(&ci->authrandom);

                                        if(!auth::encryptWithPublicCert(ci->clientCertificate, ci->authrandom, sizeof(string), &challengeOut, &challengeOutSize))
                                        {
                                            sendservmsgf(sender, "Failed to generate challenge.");
                                            conoutf("Failed to generate challenge for %s.", colorname(ci));
                                            disconnect_client(sender, DISC_KICK);
                                        }
                                        else
                                        {
                                            // Return the favour
                                            // NOTE: we use channel 2 here too, even though we're not sending files
                                            ci->connectionState = CONNECTION_STATE_CLIENT_AUTH;
                                            sendf(sender, 2, "rim+m+", N_SERVER_AUTH, outSize, out, challengeOutSize, challengeOut);
                                            delete [] challengeOut;
                                            delete [] out;
                                        }
                                    }
                                }
                            }
                        }
                    break;

                    case N_AUTH_FINISH:
                    {
                        if(chan != 1 && ci->connectionState != CONNECTION_STATE_CLIENT_AUTH) return;

                        //Verify the challenge result
                        int challengeResultLength = getint(p);
                        ucharbuf challengeResult = p.subbuf(challengeResultLength);
                        bool passed = false;

                        if(challengeResultLength == sizeof(ci->authrandom))
                        {
                            passed = true;
                            for(int i = 0; i < sizeof(ci->authrandom); i++)
                            {
                                if(challengeResult.buf[i] != ci->authrandom[i])
                                {
                                    passed = false;
                                    break;
                                }
                            }
                        }

                        if(!passed)
                        {
                            sendservmsgf(sender, "Failed to verify challenge.");
                            conoutf("%s failed it's challenge. %c (%i = %i) -> %c (%lu)\n", colorname(ci), challengeResult.buf[0], challengeResult.maxlen, challengeResultLength, ci->authrandom[0], sizeof(ci->authrandom));
                            disconnect_client(sender, DISC_KICK);
                        }
                        else
                        {
                            sendservmsgf(sender, "successfully authenticated");
                            conoutf("%s successfully autenticated", colorname(ci));
                            //Finish connecting, client successfully authenticated
                            finishConnect(ci);
                        }
                    }
                    break;

                    case N_PING:
                        sendf(sender, 1, "i2", N_PONG, getint(p));
                        break;

                    case N_CLIENTPING:
                        ci->ping = getint(p);
                        break;

                    default:
                        printf("Ignoring client packet awaiting authentication %s(%i): %i (%i).\n", ci->name, ci->clientnum, type, ci->connectionState);
                        //disconnect_client(sender, DISC_MSGERR);
                    break;
                }
                return;
            }
        }
        else if(chan==2)
        {
            receivefile(sender, p.buf, p.maxlen);
            return;
        }

        if(p.packet->flags&ENET_PACKET_FLAG_RELIABLE) reliablemessages = true;
        #define QUEUE_AI clientinfo *cm = cq;
        #define QUEUE_MSG { if(cm && (!cm->local || demorecord || hasnonlocalclients())) while(curmsg<p.length()) cm->messages.add(p.buf[curmsg++]); }
        #define QUEUE_BUF(body) { \
            if(cm && (!cm->local || demorecord || hasnonlocalclients())) \
            { \
                curmsg = p.length(); \
                { body; } \
            } \
        }

        #define QUEUE_INT(n) QUEUE_BUF(putint(cm->messages, n))
        #define QUEUE_UINT(n) QUEUE_BUF(putuint(cm->messages, n))
        #define QUEUE_STR(text) QUEUE_BUF(sendstring(text, cm->messages))
        #ifdef SERVER
            #define CHECK_EDITMUTE  \
                if(lua::pushEvent("client.isMuted")) \
                { \
                    lua_pushnumber(lua::L, ci->clientnum); \
                    lua_pushnumber(lua::L, 1); \
                    lua_call(lua::L, 2, 1); \
                        int res = lua_tonumber(lua::L, -1); \
                    lua_pop(lua::L, 1); \
                    if (res == 1) break; /* Is muted */ \
                }
        #else
            #define CHECK_EDITMUTE 
        #endif
        int curmsg;
        while((curmsg = p.length()) < p.maxlen) switch(type = checktype(getint(p), ci))
        {
            case N_POS:
            {
                int pcn = getuint(p);
                p.get();
                uint flags = getuint(p);
                clientinfo *cp = getinfo(pcn);
                if(cp && pcn != sender && cp->ownernum != sender) cp = NULL;
                vec pos;
                loopk(3)
                {
                    int n = p.get(); n |= p.get()<<8; if(flags&(1<<k)) { n |= p.get()<<16; if(n&0x800000) n |= -1<<24; }
                    pos[k] = n/DMF;
                }
                loopk(3) p.get();
                int mag = p.get(); if(flags&(1<<3)) mag |= p.get()<<8;
                int dir = p.get(); dir |= p.get()<<8;
                vec vel = vec((dir%360)*RAD, (clamp(dir/360, 0, 180)-90)*RAD).mul(mag/DVELF);
                if(flags&(1<<4))
                {
                    p.get(); if(flags&(1<<5)) p.get();
                    if(flags&(1<<6)) loopk(2) p.get();
                }
                if(cp)
                {
                    if((!ci->local || demorecord || hasnonlocalclients()) && (cp->state.state==CS_ALIVE || cp->state.state==CS_EDITING))
                    {
                        // 220 used to be 180 but that caused a TAG_TYPE disconnect for classes with higher speeds
                        //todo: raise (or lower) the bar as more classes are added
                        float dvel = max(vel.magnitude2(), (float)fabs(vel.z));
                        if(!ci->local && !m_edit && ((!m_classes && dvel >= 180) || dvel >= 220))
                            cp->setexceeded();
                        cp->position.setsize(0);
                        while(curmsg<p.length()) cp->position.add(p.buf[curmsg++]);
                    }
                    if(smode && cp->state.state==CS_ALIVE) smode->moved(cp, cp->state.o, cp->gameclip, pos, (flags&0x80)!=0);
                    // --- RPG: Shattered Shield ability layer (milestone step 3, additive; see src/rpg) ---
                    // Test-pinned monsters keep their server-set position; client N_POS is ignored.
                    // Only the rpgspawnmonster debug command sets the pin. Real gameplay is untouched.
                    if(!rpg::ispinmonster(cp->clientnum)) cp->state.o = pos;
                    cp->gameclip = (flags&0x80)!=0;

                    if(!cp->maploaded && cp->state.ai.type == ai::AI_TYPE_NONE)
                    {
                        cp->maploaded = totalmillis;
                    }
                }
                break;
            }

            case N_TELEPORT:
            {
                int pcn = getint(p), teleport = getint(p), teledest = getint(p);
                clientinfo *cp = getinfo(pcn);
                if(cp && pcn != sender && cp->ownernum != sender) cp = NULL;
                if(cp && (!ci->local || demorecord || hasnonlocalclients()) && (cp->state.state==CS_ALIVE || cp->state.state==CS_EDITING))
                {
                    flushclientposition(*cp);
                    sendf(-1, 0, "ri4x", N_TELEPORT, pcn, teleport, teledest, cp->ownernum);
                }
                break;
            }

            case N_JUMPPAD:
            {
                int pcn = getint(p), jumppad = getint(p);
                clientinfo *cp = getinfo(pcn);
                if(cp && pcn != sender && cp->ownernum != sender) cp = NULL;
                if(cp && (!ci->local || demorecord || hasnonlocalclients()) && (cp->state.state==CS_ALIVE || cp->state.state==CS_EDITING))
                {
                    cp->setpushed();
                    flushclientposition(*cp);
                    sendf(-1, 0, "ri3x", N_JUMPPAD, pcn, jumppad, cp->ownernum);
                }
                break;
            }

            case N_FROMAI:
            {
                int qcn = getint(p);
                if(qcn < 0) cq = ci;
                else
                {
                    cq = getinfo(qcn);
                    if(cq && qcn != sender && cq->ownernum != sender) cq = NULL;
                }
                break;
            }

            case N_EDITMODE:
            {
                int val = getint(p);
                if(!ci->local && !m_edit) break;
                if(val ? ci->state.state!=CS_ALIVE && ci->state.state!=CS_DEAD : ci->state.state!=CS_EDITING) break;
                if(smode)
                {
                    if(val) smode->leavegame(ci);
                    else smode->entergame(ci);
                }
                if(val)
                {
                    ci->state.editstate = ci->state.state;
                    ci->state.state = CS_EDITING;
                    ci->events.setsize(0);
                    ci->state.rockets.reset();
                    ci->state.grenades.reset();
                }
                else ci->state.state = ci->state.editstate;
                QUEUE_MSG;
                break;
            }

            case N_MAPCRC:
            {
                getstring(text, p);
                int crc = getint(p);
                if(!ci) break;
                if(strcmp(text, smapname))
                {
                    if(ci->clientmap[0])
                    {
                        ci->clientmap[0] = '\0';
                        ci->mapcrc = 0;
                    }
                    else if(ci->mapcrc > 0) ci->mapcrc = 0;
                    break;
                }
                copystring(ci->clientmap, text);
                ci->mapcrc = text[0] ? crc : 1;
                checkmaps();
                if(cq && cq != ci && cq->ownernum != ci->clientnum) cq = NULL;
                break;
            }

            case N_CHECKMAPS:
                checkmaps(sender);
                break;

            case N_TRYSPAWN:
            {
                int pc = getint(p),
                    pm = getint(p);
                if(!ci || !cq || cq->state.state!=CS_DEAD || cq->state.lastspawn>=0 || (smode && !smode->canspawn(cq))) break;
                if(!ci->clientmap[0] && !ci->mapcrc)
                {
                    ci->mapcrc = -1;
                    checkmaps();
                    if(ci == cq) { if(ci->state.state != CS_DEAD) break; }
                    else if(cq->ownernum != ci->clientnum) { cq = NULL; break; }
                }
                cq->state.playerclass = pc;
                cq->state.playermodel = pm;
                if(cq->state.deadflush)
                {
                    flushevents(cq, cq->state.deadflush);
                    cq->state.respawn();
                }
                cleartimedevents(cq);
                if (smode) smode->onSpawn(cq);
                sendspawn(cq);
                break;
            }

            case N_GUNSELECT:
            {
                int gunselect = getint(p);
                if(!cq || cq->state.state!=CS_ALIVE) break;
                cq->state.gunselect = WEAP_CLAMP(gunselect);
                QUEUE_AI;
                QUEUE_MSG;
                break;
            }

            case N_SPAWN:
            {
                int ls = getint(p), gunselect = getint(p);
                if(!cq || (cq->state.state!=CS_ALIVE && cq->state.state!=CS_DEAD) || ls!=cq->state.lifesequence || cq->state.lastspawn<0) break;
                cq->state.lastspawn = -1;
                cq->state.state = CS_ALIVE;
                cq->state.gunselect = WEAP_CLAMP(gunselect);
                cq->exceeded = 0;
                if(smode) smode->spawned(cq);
                QUEUE_AI;
                QUEUE_BUF({
                    putint(cm->messages, N_SPAWN);
                    sendstate(cq->state, cm->messages);
                });
                break;
            }

            case N_SUICIDE:
            {
                if(cq) cq->addevent(new suicideevent(getint(p)));
                break;
            }

            // --- RPG: Shattered Shield ability layer (milestone step 3, additive; see src/rpg) ---
            case N_RPG_CAST:
            {
                int ability = getint(p), yaw = getint(p);
                if(cq) rpg_cast(cq, ability, yaw);
                break;
            }

            case N_SHOOT:
            {
                shotevent *shot = new shotevent;
                shot->id = getint(p);
                shot->millis = cq ? cq->geteventmillis(gamemillis, shot->id) : 0;
                shot->gun = getint(p);
                loopk(3) shot->from[k] = getint(p)/DMF;
                loopk(3) shot->to[k] = getint(p)/DMF;
                int hits = getint(p);
                loopk(hits)
                {
                    if(p.overread()) break;
                    hitinfo &hit = shot->hits.add();
                    hit.target = getint(p);
                    hit.lifesequence = getint(p);
                    hit.dist = getint(p)/DMF;
                    hit.rays = getint(p);
                    hit.headshot = getint(p);
                    loopk(3) hit.dir[k] = getint(p)/DNF;
                }
                shot->numrays = getint(p);
                loopj(shot->numrays)
                {
                    if(p.overread()) break;
                    ivec &ray = shot->rays.add();
                    loopk(3) ray[k] = getint(p);
                }
                if(cq)
                {
                    cq->addevent(shot);
                    cq->setpushed();
                }
                else delete shot;
                break;
            }

            case N_EXPLODE:
            {
                explodeevent *exp = new explodeevent;
                int cmillis = getint(p);
                exp->millis = cq ? cq->geteventmillis(gamemillis, cmillis) : 0;
                exp->gun = getint(p);
                exp->id = getint(p);
                exp->direct = getint(p);
                int hits = getint(p);
                loopk(hits)
                {
                    if(p.overread()) break;
                    hitinfo &hit = exp->hits.add();
                    hit.target = getint(p);
                    hit.lifesequence = getint(p);
                    hit.dist = getint(p)/DMF;
                    hit.rays = getint(p);
                    hit.headshot = getint(p);
                    loopk(3) hit.dir[k] = getint(p)/DNF;
                }
                if(cq) cq->addevent(exp);
                else delete exp;
                break;
            }

            case N_ITEMPICKUP:
            {
                int n = getint(p);
                if(!cq) break;
                pickupevent *pickup = new pickupevent;
                pickup->ent = n;
                cq->addevent(pickup);
                break;
            }

            case N_TEXT:
            {
                QUEUE_AI;
                QUEUE_MSG;
                getstring(text, p);
                filtertext(text, text);
                QUEUE_STR(text);
                break;
            }

            case N_SAYTEAM:
            {
                getstring(text, p);
                if(!ci || !cq || (ci->state.state==CS_SPECTATOR && !ci->local && ci->privilege < PRIV_MASTER) || !m_teammode || !cq->team[0]) break;
                loopv(clients)
                {
                    clientinfo *t = clients[i];
                    if(t==cq || t->state.state==CS_SPECTATOR || t->state.ai.type != ai::AI_TYPE_NONE || strcmp(cq->team, t->team)) continue;
                    sendf(t->clientnum, 1, "riis", N_SAYTEAM, cq->clientnum, text);
                }
                break;
            }

            case N_SWITCHNAME:
            {
                QUEUE_AI;
                QUEUE_MSG;
                getstring(text, p);
                filtertext(cq->name, text, NAMEALLOWSPACES, MAXNAMELEN);
                if(!cq->name[0]) copystring(cq->name, "unnamed");
                QUEUE_STR(cq->name);
                break;
            }

            case N_SWITCHTEAM:
            {
                getstring(text, p);
                filtertext(text, text, false, MAXTEAMLEN);
                if(strcmp(ci->team, text) && m_teammode && (!smode || smode->canchangeteam(ci, ci->team, text)))
                {
                    if(ci->state.state==CS_ALIVE) suicide(ci);
                    copystring(ci->team, text);
                    aiman::changeteam(ci);
                    sendf(-1, 1, "riisi", N_SETTEAM, sender, ci->team, ci->state.state==CS_SPECTATOR ? -1 : 0);
                }
                break;
            }

            case N_MAPVOTE:
            case N_MAPCHANGE:
            {
                getstring(text, p);
                filtertext(text, text, false);
                int reqmode = getint(p);
                if(type!=N_MAPVOTE) break;
                vote(text, reqmode, sender);
                break;
            }

            case N_CLEARVOTE:
            {
                if(ci && ci->mapvote[0])
                {
                    ci->mapvote[0] = 0;
                    ci->modevote = -1;
                    sendf(-1, 1, "ri2", N_CLEARVOTE, ci->clientnum); //TODO: Queue instead?
                }
                break;
            }

            case N_ITEMLIST:
            {
                if((ci->state.state==CS_SPECTATOR && ci->privilege < PRIV_MASTER && !ci->local) || !notgotitems || strcmp(ci->clientmap, smapname)) { while(getint(p)>=0 && !p.overread()) getint(p); break; }
                int n;
                while((n = getint(p))>=0 && n<MAXENTS && !p.overread())
                {
                    server_entity se = { NOTUSED, 0, false };
                    while(sents.length()<=n) sents.add(se);
                    sents[n].type = getint(p);
                    if(canspawnitem(sents[n].type))
                    {
                        if(m_mp(gamemode) && delayspawn(sents[n].type)) sents[n].spawntime = spawntime(sents[n].type);
                        else sents[n].spawned = true;
                    }
                }
                notgotitems = false;
                break;
            }

            case N_EDITENT:
            {
                int i = getint(p);
                loopk(3) getint(p);
                int type = getint(p);
                loopk(5) getint(p);
                if(!ci || ci->state.state==CS_SPECTATOR) break;
                CHECK_EDITMUTE;
                QUEUE_MSG;
                bool canspawn = canspawnitem(type);
                if(i<MAXENTS && (sents.inrange(i) || canspawnitem(type)))
                {
                    server_entity se = { NOTUSED, 0, false };
                    while(sents.length()<=i) sents.add(se);
                    sents[i].type = type;
                    if(canspawn ? !sents[i].spawned : (sents[i].spawned || sents[i].spawntime))
                    {
                        sents[i].spawntime = canspawn ? 1 : 0;
                        sents[i].spawned = false;
                    }
                }
                break;
            }

            case N_EDITVAR:
            {
                int type = getint(p);
                getstring(text, p);
                switch(type)
                {
                    case ID_VAR: getint(p); break;
                    case ID_FVAR: getfloat(p); break;
                    case ID_SVAR: getstring(text, p);
                }
                if(!ci || ci->state.state==CS_SPECTATOR) break;
                CHECK_EDITMUTE;
                QUEUE_MSG;
                break;
            }

            case N_PING:
                if(ci && !ci->maploaded && ci->state.ai.type == ai::AI_TYPE_NONE)
                {
                    ci->maploaded = totalmillis;
                }
                sendf(sender, 1, "i2", N_PONG, getint(p));
                break;

            case N_CLIENTPING:
            {
                int ping = getint(p);
                if(ci)
                {
                    ci->ping = ping;
                    loopv(ci->bots) ci->bots[i]->ping = ping;
                }
                QUEUE_MSG;
                break;
            }

            case N_MASTERMODE:
            {
                int mm = getint(p);
                if((ci->privilege >= PRIV_MASTER || ci->local) && mm>=MM_OPEN && mm<=MM_PRIVATE)
                {
                    bool allow = (ci->privilege>=PRIV_ADMIN || ci->local) || (mastermask&(1<<mm));
#ifdef SERVER
                    if(lua::pushEvent("client.mastermode"))
                    {
                        lua_pushnumber(lua::L, ci->clientnum);
                        lua_pushnumber(lua::L, mm);
                        lua_call(lua::L, 2, 1);
                            int res = lua_tonumber(lua::L, 1);
                            if(res != -1)
                            {
                                allow = res == 0 ? false : true;
                            }
                        lua_pop(lua::L, 1);
                    }
#endif
                    if(allow)
                    {
                        mastermode = mm;
                        allowedips.shrink(0);
                        if(mm>=MM_PRIVATE)
                        {
                            loopv(clients) allowedips.add(getclientip(clients[i]->clientnum));
                        }
                        sendf(-1, 1, "rii", N_MASTERMODE, mastermode);
                    }
                    else
                    {
                        sendservmsgf(sender, "mastermode %d is disabled on this server", mm);
                    }
                }
                break;
            }

            case N_CLEARBANS:
            {
                if(ci->privilege >= PRIV_MASTER || ci->local)
                {
                    bannedips.shrink(0);
                    sendservmsg("cleared all bans");
                }
                break;
            }

            case N_KICK:
            {
                int victim = getint(p);
                getstring(text, p);
                filtertext(text, text);
                trykick(ci, victim, text);
                break;
            }

            case N_SPECTATOR:
            {
                int spectator = getint(p), val = getint(p);
                if(ci->privilege < PRIV_MASTER && !ci->local && (spectator!=sender || (ci->state.state==CS_SPECTATOR && mastermode>=MM_LOCKED))) break;
                clientinfo *spinfo = (clientinfo *)getclientinfo(spectator); // no bots
                if(!spinfo || spinfo->connectionState != CONNECTION_STATE_CONNECTED || (spinfo->state.state==CS_SPECTATOR ? val : !val)) break;

                if(spinfo->state.state!=CS_SPECTATOR && val) forcespectator(spinfo);
                else if(spinfo->state.state==CS_SPECTATOR && !val) unspectate(spinfo);

                if(cq && cq != ci && cq->ownernum != ci->clientnum) cq = NULL;
                break;
            }

            case N_SETTEAM:
            {
                int who = getint(p);
                getstring(text, p);
                filtertext(text, text, false, MAXTEAMLEN);
                if(ci->privilege < PRIV_MASTER && !ci->local) break;
                clientinfo *wi = getinfo(who);
                if(!m_teammode || !text[0] || !wi || wi->connectionState != CONNECTION_STATE_CONNECTED ||  !strcmp(wi->team, text)) break;
                if(!smode || smode->canchangeteam(wi, wi->team, text))
                {
                    if(wi->state.state==CS_ALIVE) suicide(wi);
                    copystring(wi->team, text, MAXTEAMLEN+1);
                }
                aiman::changeteam(wi);
                sendf(-1, 1, "riisi", N_SETTEAM, who, wi->team, 1);
                break;
            }

            case N_FORCEINTERMISSION:
                if(ci->local && !hasnonlocalclients()) startintermission();
                break;

            case N_RECORDDEMO:
            {
                int val = getint(p);
                if(ci->privilege< (restrictdemos ? PRIV_ADMIN: PRIV_MASTER) && !ci->local) break;
                demonextmatch = val!=0;
                defformatstring(msg)("demo recording is %s for next match", demonextmatch ? "enabled" : "disabled");
                sendservmsg(msg);
                break;
            }

            case N_STOPDEMO:
            {
                if(ci->privilege < (restrictdemos ? PRIV_ADMIN : PRIV_MASTER) && !ci->local) break;
                stopdemo();
                break;
            }

            case N_CLEARDEMOS:
            {
                int demo = getint(p);
                if(ci->privilege < (restrictdemos ? PRIV_ADMIN : PRIV_MASTER) && !ci->local) break;
                cleardemos(demo);
                break;
            }

            case N_LISTDEMOS:
                if(ci->privilege < PRIV_MASTER && !ci->local && ci->state.state==CS_SPECTATOR) break;
                listdemos(sender);
                break;

            case N_GETDEMO:
            {
                int n = getint(p);
                if(ci->privilege < PRIV_MASTER && !ci->local && ci->state.state==CS_SPECTATOR) break;
                senddemo(ci, n);
                break;
            }

            case N_GETMAP:
                if(!mapdata) sendf(sender, 1, "ris", N_SERVMSG, "no map to send");
                else if(ci->getmap) sendf(sender, 1, "ris", N_SERVMSG, "already sending map");
                else
                {
                    sendservmsgf("[%s is getting the map]", colorname(ci));
                    if((ci->getmap = sendfile(sender, 2, mapdata, "ri", N_SENDMAP)))
                        ci->getmap->freeCallback = freegetmap;
                    ci->needclipboard = totalmillis ? totalmillis : 1;
                }
                break;

            case N_NEWMAP:
            {
                int size = getint(p);
                if(ci->privilege < PRIV_MASTER && !ci->local && ci->state.state==CS_SPECTATOR) break;
                if(size>=0)
                {
                    smapname[0] = '\0';
                    resetitems();
                    notgotitems = false;
                    if(smode) smode->newmap();
                }
                QUEUE_MSG;
                break;
            }

            case N_SETMASTER:
            {
                int target = getint(p);
                int val = getint(p);
                getstring(text, p);
                setmaster(ci, val!=0, target, text);
                // don't broadcast the master password
                break;
            }

            case N_REQADDAI:
            {
                int type = getint(p);
                aiman::reqadd(ci, (ai::AiType)type, getint(p));
                break;
            }

            case N_REQDELAI:
            {
                int type = getint(p);
                aiman::reqdel(ci, (ai::AiType)type);
                break;
            }

            case N_BOTLIMIT:
            {
                int limit = getint(p);
                if(ci) aiman::setbotlimit(ci, limit);
                break;
            }

            case N_BOTBALANCE:
            {
                int balance = getint(p);
                if(ci) aiman::setbotbalance(ci, balance!=0);
                break;
            }

            case N_PAUSEGAME:
            {
                int val = getint(p);
                if(ci->privilege < (restrictpausegame ? PRIV_ADMIN : PRIV_MASTER) && !ci->local) break;
                pausegame(val > 0, ci);
                break;
            }

            case N_GAMESPEED:
            {
                int val = getint(p);
                if(ci->privilege < (restrictgamespeed ? PRIV_ADMIN : PRIV_MASTER) && !ci->local) break;
                changegamespeed(val, ci);
                break;
            }

            case N_COPY:
                ci->cleanclipboard();
                ci->lastclipboard = totalmillis ? totalmillis : 1;
                goto genericmsg;

            case N_PASTE:
                if(ci->state.state!=CS_SPECTATOR) sendclipboard(ci);
                goto genericmsg;

            case N_CLIPBOARD:
            {
                int unpacklen = getint(p), packlen = getint(p);
                ci->cleanclipboard(false);
                if(ci->state.state==CS_SPECTATOR)
                {
                    if(packlen > 0) p.subbuf(packlen);
                    break;
                }
                if(packlen <= 0 || packlen > (1<<16) || unpacklen <= 0)
                {
                    if(packlen > 0) p.subbuf(packlen);
                    packlen = unpacklen = 0;
                }
                packetbuf q(32 + packlen, ENET_PACKET_FLAG_RELIABLE);
                putint(q, N_CLIPBOARD);
                putint(q, ci->clientnum);
                putint(q, unpacklen);
                putint(q, packlen);
                if(packlen > 0) p.get(q.subbuf(packlen).buf, packlen);
                ci->clipboard = q.finalize();
                ci->clipboard->referenceCount++;
                break;
            }

            case N_ONFIRE:
            {
                int cattacker = getint(p);
                int cvictim = getint(p);
                int gun = getint(p);
                /*int id = */(void)getint(p); // ? TODO
                int on = getint(p);

                clientinfo *ti = getinfo(cattacker);
                clientinfo *vi = getinfo(cvictim);

                if(on && ti && vi && ti->state.state==CS_ALIVE && vi->state.state==CS_ALIVE && (ti==cq || ti->ownernum==cq->clientnum) && vi->state.burnmillis<(gamemillis-200))
                {
                    vi->state.onfire = true;
                    vi->state.lastburnpain = 0;
                    vi->state.burnmillis = gamemillis;
                    vi->state.fireattacker = ti;
                    sendf(-1, 1, "ri5", N_SETONFIRE, cattacker, cvictim, gun, 1);
                }
                else if (!on && vi && (vi==cq || vi->ownernum==cq->clientnum))
                {
                    vi->state.onfire = false;
                    sendf(-1, 1, "ri5", N_SETONFIRE, cvictim, cvictim, gun, 0);
                }
                break;
            }

            case N_RADIOALL:
            {
                getstring(text, p);
                filtertext(text, text, false);
                if (text[0] && ci->state.state != CS_SPECTATOR) sendf(-1, 1, "riis", N_RADIOALL, ci->clientnum, text);
                break;
            }

            case N_RADIOTEAM:
            {
                getstring(text, p);
                //filtertext(text, text, false);
                if (text[0] && ci->state.state != CS_SPECTATOR) loopv(clients)
                {
                    clientinfo *t = clients[i];
                    if(t==cq || t->state.state==CS_SPECTATOR || t->state.ai.type != ai::AI_TYPE_NONE || strcmp(cq->team, t->team)) continue;
                    sendf(t->clientnum, 1, "riis", N_RADIOTEAM, cq->clientnum, text);
                }
                break;
            }

            case N_BUY:
            {
                int item = getint(p);

                // survival shop: buying is allowed while alive, or while the dmsp intermission shop is open
                bool canshop = ci->state.state == CS_ALIVE || (m_dmsp && dmspmode.shopopen);
                if(canshop && item >= 0 && item < game::BA_NUM)
                {
                    // damage upgrade has a scaling price per level; the table holds the level-1 price
                    int guts = (item==game::BA_DAMAGE) ? 600*(ci->dmglevel+1) : game::buyablesprices[item];
                    bool denied = false;
                    if(item==game::BA_DAMAGE && ci->dmglevel >= 10) { sendservmsgf(ci->clientnum, "\f2damage upgrade maxed out"); denied = true; }
                    if(item==game::BA_EXTRALIFE && ci->extralives >= 3) { sendservmsgf(ci->clientnum, "\f2no more extra lives can be banked"); denied = true; }
                    if(!denied && ci->state.guts>=guts)
                    {
                        gamestate &gs = ci->state;
                        gs.guts -= guts;

                        switch (item)
                        {
                            case game::BA_AMMO:
                            {
                                const playerclassinfo &pci = game::getplayerclassinfo(&gs);
                                loopi(WEAPONS_PER_CLASS) gs.ammo[pci.weap[i]] = min(gs.ammo[pci.weap[i]] + GUN_AMMO_MAX(pci.weap[i])/2, GUN_AMMO_MAX(pci.weap[i]));
                                break;
                            }

                            case game::BA_AMMOD:
                            {
                                const playerclassinfo &pci = game::getplayerclassinfo(&gs);
                                loopi(WEAPONS_PER_CLASS) gs.ammo[pci.weap[i]] = GUN_AMMO_MAX(pci.weap[i]);
                                break;
                            }

                            case game::BA_HEALTH:
                                gs.health = min(gs.health + gs.maxhealth/2, gs.maxhealth);
                                break;

                            case game::BA_HEALTHD:
                                gs.health = gs.maxhealth;
                                break;

                            case game::BA_ARMOURG:
                            case game::BA_ARMOURY:
                                gs.armourtype = (item==game::BA_ARMOURY)? A_YELLOW: A_GREEN;
                                gs.armour = (item==game::BA_ARMOURY)? 200: 100;
                                break;

                            case game::BA_QUAD:
                            case game::BA_QUADD:
                                gs.quadmillis = (item==game::BA_QUADD)? 60000: 30000;
                                break;

                            case game::BA_SUPPORT:
                            case game::BA_SUPPORTD:
                                //spawnsupport((item==game::BA_SUPPORTD)? 6: 3);
                                break;

                            case game::BA_DAMAGE:
                                ci->dmglevel++;
                                ci->dmgmult = 100 + 10*ci->dmglevel; // +10% outgoing damage per level
                                sendservmsgf(ci->clientnum, "\f2damage upgraded to %d%%", ci->dmgmult);
                                break;

                            case game::BA_EXTRALIFE:
                                ci->extralives++;
                                sendservmsgf(ci->clientnum, "\f2extra life banked (%d total) - death will consume one instead of ending your run", ci->extralives);
                                break;

                            case game::BA_QUADR:
                                // banked: quadmillis is armed at the NEXT round start so none is wasted in the shop
                                ci->pendingquad++;
                                sendservmsgf(ci->clientnum, "\f2quad damage armed for next round (%d banked)", ci->pendingquad);
                                break;
                            default:
                                DEBUG_ERROR("Invalid buyable: %i from %i", item, ci->clientnum);
                                break;
                        }

                        if(item==game::BA_QUADR)
                        {
                            // banked quad: do NOT broadcast N_BUY now (that would start the
                            // quad timer during the shop). Just sync guts; the quad is armed
                            // by a price-0 N_BUY at the next round start.
                            sendf(ci->ownernum, 1, "ri3", N_GUTS, ci->clientnum, ci->state.guts);
                        }
                        else sendf(-1, 1, "ri4", N_BUY, ci->clientnum, item, guts);
                    }
                }
                break;
            }

            #define PARSEMESSAGES 1
            #include "gamemode/capture.h"
            #include "gamemode/ctf.h"
            #include "gamemode/infection.h"
            #include "gamemode/dmsp.h"
            #undef PARSEMESSAGES

            case N_SERVER_COMMAND:
            {
                int req = getint(p);
                int argc = getint(p);

                if(argc < 1 || ci == NULL) return;
                vector<char *> args;
                loopi(argc)
                {
                    getstring(text, p);
                    if(argc < 10) args.add(newstring(text));
                }

                conoutf("%s executed %s (argc: %i)", colorname(ci), args[0], argc);

                string result = "error \"unkown command\"";

                #ifdef SERVER
                if(lua::pushEvent("client.command"))
                {
                    lua_pushnumber(lua::L, ci->clientnum);
                    lua_pushnumber(lua::L, args.length());
                    loopv(args)
                    {
                        lua_pushstring(lua::L, args[i]);
                    }
                    lua_call(lua::L, 2+args.length(), 1);
                        const char *str = lua_tostring(lua::L, -1);
                        if(str) copystring(result, str);
                    lua_pop(lua::L, 1);
                }
                else
                #endif
                if(0 == strcmp(args[0], "bots.add"))
                {
                    if(args.length() < 2)
                    {
                        copystring(result, "error \"Not enough arguments\"");
                    }
                    else
                    {
                        int skill = atoi(args[1]);
                        aiman::reqadd(ci, ai::AI_TYPE_BOT, skill);
                        copystring(result, "added bot");
                    }
                }

                sendf(ci->clientnum, 1, "riis", N_SERVER_COMMAND, req, (const char *)result);

                args.deletearrays();
            }
                break;

            case -1:
                disconnect_client(sender, DISC_MSGERR);
                return;

            case -2:
                disconnect_client(sender, DISC_OVERFLOW);
                return;

            default: genericmsg:
            {
                int size = server::msgsizelookup(type);
                if(size<=0) { disconnect_client(sender, DISC_MSGERR); return; }
                loopi(size-1) getint(p);
                if(type>=N_EDITENT && type<=N_EDITVAR)
                {
                    CHECK_EDITMUTE;
                }
                if(ci && cq && (ci != cq || ci->state.state!=CS_SPECTATOR)) { QUEUE_AI; QUEUE_MSG; }
                break;
            }
        }
    }

    int laninfoport() { return RR_LANINFO_PORT; }
    int serverinfoport(int servport) { return servport < 0 ? RR_SERVINFO_PORT : servport+1; }
    int serverport(int infoport) { return infoport < 0 ? RR_SERVER_PORT : infoport-1; }
    const char *defaultmaster() { return RR_MASTER_HOST; }
    int masterport() { return RR_MASTER_PORT; }
    int numchannels() { return 3; }

    #include "extinfo.h"

    void serverinforeply(ucharbuf &req, ucharbuf &p)
    {
        if(req.remaining() && !getint(req))
        {
            extserverinforeply(req, p);
            return;
        }

        putint(p, numclients(-1, false, true));
        putint(p, gamepaused || gamespeed != 100 ? 7 : 5);                   // number of attrs following
        putint(p, PROTOCOL_VERSION);    // generic attributes, passed back below
        putint(p, gamemode);
        putint(p, m_timed ? max((gamelimit - gamemillis)/1000, 0) : 0);
        putint(p, maxclients);
        putint(p, serverpass[0] ? MM_PASSWORD : (!m_mp(gamemode) ? MM_PRIVATE : (mastermode || mastermask&MM_AUTOAPPROVE ? mastermode : MM_AUTH)));
        if(gamepaused || gamespeed != 100)
        {
            putint(p, gamepaused ? 1 : 0);
            putint(p, gamespeed);
        }
        sendstring(smapname, p);
        sendstring(serverdesc, p);
        sendserverinforeply(p);
    }

    bool servercompatible(char *name, char *sdec, char *map, int ping, const vector<int> &attr, int np)
    {
        return attr.length() && attr[0]==PROTOCOL_VERSION;
    }

    int protocolversion()
    {
        return PROTOCOL_VERSION;
    }

    #include "ai/aiman.h"
}

#ifdef SERVER

EXPORT(int getPrivilege(int cn))
{
    server::clientinfo *ci = (server::clientinfo *)getclientinfo(cn);
    return ci ? ci->privilege : -1;
}

EXPORT(unsigned long getIp(int cn))
{
    return ntohl(getclientip(cn));
}

EXPORT(const char *getDisplayName(int cn))
{
    server::clientinfo *ci = (server::clientinfo *)getclientinfo(cn);
    return ci ? colorname(ci) : NULL;
}

/**
 * Sends a message to the player specified with cn
 * \param cn The channel of the recipient or -1.
 * \param msg The message to send
 */
EXPORT(void sendServerMessageTo(int cn, const char *msg))
{
    server::sendservmsgf(cn, "%s", msg);
}

/**
 * Broadcasts a message to all players
 * \param msg The message to broadcast
 */
EXPORT(void sendServerMessage(const char *msg))
{
    server::sendservmsg(msg);
}

EXPORT(void sendMapTo(int cn))
{
    server::clientinfo *ci = (server::clientinfo *)getclientinfo(cn);
    if(ci && server::mapdata)
    {
        server::sendservmsgf(-1, "[%s is being sent the map]", colorname(ci));
        server::sendservmsgf(ci->clientnum, "[server sending map]");

        sendfile(ci->clientnum, 2, server::mapdata, "ri", N_SENDMAP);
        ci->needclipboard = totalmillis;
    }
}
#endif
