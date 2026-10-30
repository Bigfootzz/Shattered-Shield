#include "game.h"
#include "auth.h"

// --- RPG: Shattered Shield persistence (milestone steps 1+2, additive; see src/rpg) ---
#include "rpg/rpg.h"
#include "rpg/abilities.h"
#include "rpg/stats.h"
#include "rpg/crafting.h"

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

    static void rpg_syncinv(clientinfo *cq); // defined with the inventory ops below
    static void rpg_qmsg(clientinfo *cq, const char *line); // quest engine v2, defined below
    struct rpg_party;
    static rpg_party *rpg_findparty(const char *name);
    static clientinfo *rpg_findclient(const char *name);
    #define RPG_PARTY_MAX 5
    struct rpg_party
    {
        int id;
        string leader;                  // character name
        string members[RPG_PARTY_MAX];  // includes leader
        int nmembers;
        string invites[RPG_PARTY_MAX];  // pending invitee names
        int ninvites;
    };
    static vector<rpg_party> rpg_parties;
    static int rpg_nextpartyid = 1;
    // fixed-array slot helpers (string is char[260]; no vector<string>)
    static int rpg_pslot_find(string *arr, int n, const char *name)
    {
        loopi(n) if(!strcmp(arr[i], name)) return i;
        return -1;
    }
    static bool rpg_pslot_add(string *arr, int &n, const char *name)
    {
        if(n >= RPG_PARTY_MAX || rpg_pslot_find(arr, n, name) >= 0) return false;
        copystring(arr[n++], name);
        return true;
    }
    static bool rpg_pslot_del(string *arr, int &n, const char *name)
    {
        int idx = rpg_pslot_find(arr, n, name);
        if(idx < 0) return false;
        for(int k = idx; k < n - 1; k++) copystring(arr[k], arr[k+1]);
        n--;
        return true;
    }
    // --- clan system v1: in-memory invites (membership persists in rpg.db) ---
    struct rpg_claninvite { int clan_id; string target; string inviter; };
    static vector<rpg_claninvite> rpg_claninvites;
    static void rpg_syncclan(clientinfo *cq)
    {
        if(!cq) return;
        int id = rpg::clan_of(cq->name);
        int rank = id > 0 ? rpg::clan_rank(cq->name) : -1;
        string cname, ctag;
        cname[0] = ctag[0] = '\0';
        if(id > 0) rpg::clan_info(id, cname, ctag, NULL);
        sendf(cq->clientnum, 1, "ri3ss", N_RPG_CLAN, id, rank, cname, ctag);
    }
    static void rpg_clan_broadcast(int clan_id, const char *line, clientinfo *except = NULL)
    {
        if(clan_id <= 0 || !line) return;
        char names[32][64]; int ranks[32];
        int n = rpg::clan_memberlist(clan_id, names, ranks, 32);
        loopi(n)
        {
            clientinfo *m = rpg_findclient(names[i]);
            if(m && m != except) rpg_qmsg(m, line);
        }
    }
    static void rpg_clan_roster(clientinfo *cq)
    {
        int id = rpg::clan_of(cq->name);
        if(id <= 0) { rpg_qmsg(cq, "You are not in a clan. (/clancreate <name> <tag>)"); return; }
        string cname, ctag, cleader;
        rpg::clan_info(id, cname, ctag, cleader);
        defformatstring(head)("Clan %s [%s] (leader: %s):", cname, ctag, cleader);
        rpg_qmsg(cq, head);
        char names[32][64]; int ranks[32];
        int n = rpg::clan_memberlist(id, names, ranks, 32);
        const char *rnames[] = { "Leader", "Officer", "Member" };
        loopi(n)
        {
            bool online = rpg_findclient(names[i]) != NULL;
            defformatstring(ml)("  %s — %s (%s)", names[i], rnames[clamp(ranks[i], 0, 2)], online ? "online" : "offline");
            rpg_qmsg(cq, ml);
        }
    }

    static void rpg_syncquest(clientinfo *cq); // quest engine v2, defined below
    static void rpg_syncbuffs(clientinfo *cq); // bundle: active buff sync, defined below
    static void rpg_spawnchampion(clientinfo *cq); // defined with the mob spawns below
    void rpg_respond(clientinfo *cq, int idx); // quest engine v2, defined below
    void rpg_talk(clientinfo *cq, int npcidx); // quest engine v2, defined below
    void rpggrantxp_cmd(tagval *args, int numargs)
    {
        if(numargs < 2) { conoutf("usage: rpggrantxp <name> <amount>"); return; }
        if(!rpg::connected()) { conoutf("rpggrantxp: character database is not open"); return; }
        bool leveled = false;
        int newlevel = rpg::awardxp(args[0].getstr(), args[1].getint(), &leveled);
        if(newlevel < 0) { conoutf("rpggrantxp: no character named '%s'", args[0].getstr()); return; }
        conoutf("rpggrantxp: %s +%d xp (level %d)", args[0].getstr(), args[1].getint(), newlevel);
        if(leveled)
        {
            sendservmsgf("\f2%s \f7reached RPG level \f2%d!", args[0].getstr(), newlevel);
            loopv(clients) if(clients[i]->state.ai.type == ai::AI_TYPE_NONE && !strcmp(clients[i]->name, args[0].getstr()))
            {
                clientinfo *lc = clients[i];
                rpg_syncinv(lc); // refresh level/xp on the client sheet
                if(rpg::mapactive(smapname)) // max HP follows the formula; keep current HP
                {
                    int maxhp = rpg::stat_maxhp(newlevel, rpg::buffbonus(lc->name, rpg::ATTR_VITALITY) + rpg::gearbonus(lc->name, rpg::ATTR_VITALITY)); // Phase 2: Vitality buff
                    if(lc->state.health > maxhp) lc->state.health = maxhp;
                    lc->state.maxhealth = maxhp;
                    sendf(lc->clientnum, 1, "ri2", N_RPG_HPMAX, maxhp);
                }
            }
        }
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

    // TEST-ONLY HP pin for rpgspawnmonster <hp>: applied inside sendspawn
    // (after spawnstate resets health to the monster-type default), so the
    // broadcast N_SPAWNSTATE carries the requested HP. One-shot per spawn.
    static int rpg_testspawn_cn = -1, rpg_testspawn_hp = 0;
    static int rpg_testforcerange_srv = 0; // TEST-ONLY: bypass server range check
    extern void sendspawn(clientinfo *ci); // defined below; re-broadcasts after test position is set

    // Spawn one monster parked on the casting player's feet (optional hp
    // override for deterministic tests). Uses the mode spawn path
    // (aiman::addai + makeInfected); the normal reinitai/spawn pipeline
    // broadcasts it on the next serverupdate.
    void rpgspawnmonster_cmd(tagval *args, int numargs)
    {
        if(!rpg::connected()) { conoutf("rpgspawnmonster: character database is not open"); return; }
        int hp = numargs >= 1 ? args[0].getint() : 0; // >0 pins an override; 0/absent = type default
        clientinfo *caster = NULL;
        loopv(clients)
        {
            clientinfo *ci = clients[i];
            if((ci->connectionState == CONNECTION_STATE_CONNECTED || ci->local) && ci->state.ai.type == ai::AI_TYPE_NONE && ci->state.state == CS_ALIVE) { caster = ci; break; }
        }
        if(!caster) { conoutf("rpgspawnmonster: no live player to spawn next to"); return; }
        clientinfo *mi = aiman::addai(50, -1, ai::AI_TYPE_MONSTER);
        if(!mi) { conoutf("rpgspawnmonster: no free AI slot"); return; }
        makeInfected(mi, monster::getRandomType(), false);
        copystring(mi->name, monster::getMonsterType(mi->state.getMonsterType()).classInfo.name, MAXNAMELEN+1);
        mi->state.state = CS_ALIVE;
        // Run the normal spawn pipeline (reassign/respawn wipes position to
        // sentinel, sendspawn broadcasts). Then set the test position and
        // re-broadcast so clients get the correct position + HP.
        if(hp > 0) { rpg_testspawn_cn = mi->clientnum; rpg_testspawn_hp = hp; }
        aiman::reinitai(mi);
        // fallback: if sendspawn did not consume the pin, apply directly
        if(rpg_testspawn_cn == mi->clientnum)
        {
            mi->state.maxhealth = mi->state.health = rpg_testspawn_hp;
            rpg_testspawn_cn = -1; rpg_testspawn_hp = 0;
        }
        float dist = numargs >= 2 ? max(args[1].getfloat(), 0.0f) : 0.0f;
        mi->state.o = caster->state.o; // TEST-ONLY: spawn at caster
        if(dist > 0.0f) mi->state.o.add(vec(0, dist, 0)); // +Y in front of test player (yaw 0)
        // Re-broadcast with the test position (the sendspawn in reinitai went out
        // before the position was set). Re-set the HP pin (one-shot) so the
        // second sendspawn also applies the override.
        if(hp > 0) { rpg_testspawn_cn = mi->clientnum; rpg_testspawn_hp = hp; }
        sendspawn(mi);
        conoutf("rpgspawnmonster: '%s' (hp %d) at caster '%s'", mi->name, mi->state.health, caster->name);
    }
    COMMANDN(rpgspawnmonster, rpgspawnmonster_cmd, "iV");
    // TEST-ONLY godmode for targeting proofs (server console only). When on,
    // monsters cannot damage players. Does not affect monster-vs-monster or PvP.
    static bool rpg_testgod = false;
    void rpgtestgod_cmd(tagval *args, int numargs)
    {
        rpg_testgod = numargs >= 1 ? args[0].getint() != 0 : !rpg_testgod;
        conoutf("rpgtestgod: %s", rpg_testgod ? "ON" : "OFF");
    }
    ICOMMAND(rpgtestforcerangesrv, "i", (int *v), { rpg_testforcerange_srv = *v ? 1 : 0; conoutf("rpgtestforcerangesrv: %s", rpg_testforcerange_srv ? "ON" : "OFF"); });
    // Crafting Phase 2 (test-only): dump every connected character's active buffs to the
    // server log, so headless harnesses can verify apply/refresh/stack/death/logout/expiry
    // without a HUD buff display (which is out of Phase 2 scope).
    ICOMMAND(rpgbuffinfo, "", (), {
        loopv(clients) if(clients[i]->connectionState == CONNECTION_STATE_CONNECTED && clients[i]->state.ai.type == ai::AI_TYPE_NONE)
        {
            int attrs[8]; int mags[8]; int secs[8];
            int n = rpg::bufflist(clients[i]->name, attrs, mags, secs, 8);
            if(n <= 0) { conoutf("rpgbuffinfo: '%s' has no active buffs", clients[i]->name); continue; }
            loopj(n) conoutf("rpgbuffinfo: '%s' %s +%d (%ds left)", clients[i]->name, rpg::attrname(attrs[j]), mags[j], secs[j]);
        }
    });
    COMMANDN(rpgtestgod, rpgtestgod_cmd, "iV");
    // TEST-ONLY LoS diagnostic (Oct 21): reports the old single-ray vs new
    // multi-ray LoS check against the nearest live monster. Proves the
    // terrain-bump fix: old ray (eye+12 -> feet+4) vs new (any of +4/+14/+24).
    void rpgtestlos_cmd(tagval *args, int numargs)
    {
        if(!rpg::connected()) { conoutf("rpgtestlos: character database is not open"); return; }
        clientinfo *caster = NULL;
        conoutf("rpgtestlos: scanning %d clients", clients.length());
        loopv(clients)
        {
            clientinfo *ci = clients[i];
            conoutf("rpgtestlos: client %d name='%s' local=%d conn=%d aitype=%d state=%d",
                    ci->clientnum, ci->name, ci->local ? 1 : 0, ci->connectionState,
                    ci->state.ai.type, ci->state.state);
            if((ci->connectionState == CONNECTION_STATE_CONNECTED || ci->local) && ci->state.ai.type == ai::AI_TYPE_NONE && ci->state.state == CS_ALIVE) { caster = ci; break; }
        }
        if(!caster) { conoutf("rpgtestlos: no live player"); return; }
        clientinfo *best = NULL; float bestd = 1e9f;
        loopv(clients)
        {
            clientinfo *t = clients[i];
            if(t == caster || t->state.ai.type != ai::AI_TYPE_MONSTER) continue;
            if(t->state.state != CS_ALIVE || t->state.health <= 0) continue;
            vec d(t->state.o); d.sub(caster->state.o);
            float dist = d.magnitude();
            if(dist < bestd) { bestd = dist; best = t; }
        }
        if(!best) { conoutf("rpgtestlos: no live monster"); return; }
        // Old single-ray check (eye+12 -> feet+4)
        vec from(caster->state.o); from.z += 12.0f;
        vec to_old(best->state.o); to_old.z += 4.0f;
        vec hitpos;
        bool old_clear = raycubelos(from, to_old, hitpos);
        // New multi-ray check (individual heights)
        static const float heights[3] = { 4.0f, 14.0f, 24.0f };
        bool new_heights[3] = { false, false, false };
        loopi(3)
        {
            vec to(best->state.o); to.z += heights[i];
            new_heights[i] = raycubelos(from, to, hitpos);
        }
        bool new_clear = new_heights[0] || new_heights[1] || new_heights[2];
        conoutf("rpgtestlos: '%s' at %.1fu | OLD single-ray: %s | NEW +4:%s +14:%s +24:%s => %s",
                best->name, bestd,
                old_clear ? "CLEAR" : "BLOCKED",
                new_heights[0] ? "clear" : "BLOCKED",
                new_heights[1] ? "clear" : "BLOCKED",
                new_heights[2] ? "clear" : "BLOCKED",
                new_clear ? "CLEAR" : "BLOCKED");
        conoutf("rpgtestlos: caster at (%.1f, %.1f, %.1f) target at (%.1f, %.1f, %.1f)",
                caster->state.o.x, caster->state.o.y, caster->state.o.z,
                best->state.o.x, best->state.o.y, best->state.o.z);
    }
    COMMANDN(rpgtestlos, rpgtestlos_cmd, "V");
    // --- RPG: Shattered Shield inventory (milestone step 4, additive; see src/rpg) ---
    static void rpg_syncinv(clientinfo *cq); // defined with the other inventory ops below
    // TEST-ONLY debug commands (server console only, like rpgspawnmonster).
    void rpggive_cmd(tagval *args, int numargs)
    {
        if(numargs < 2 || !rpg::connected()) { conoutf("usage: rpggive <name> <itemid> [count]"); return; }
        const char *name = args[0].getstr(), *itemid = args[1].getstr();
        int count = numargs >= 3 ? max(args[2].getint(), 1) : 1;
        if(rpg::additem(name, itemid, count))
        {
            conoutf("rpggive: %s +%d %s", name, count, itemid);
            loopv(clients) if(clients[i]->state.ai.type == ai::AI_TYPE_NONE && !strcmp(clients[i]->name, name)) rpg_syncinv(clients[i]);
        }
        else conoutf("rpggive: failed (unknown item or character)");
    }
    COMMANDN(rpggive, rpggive_cmd, "ssiV");
    void rpgforceloot_cmd(tagval *args, int numargs)
    {
        if(numargs < 1 || !rpg::connected()) { conoutf("usage: rpgforceloot <itemid> [dist_ahead]"); return; }
        int id = rpg::finditem(args[0].getstr());
        if(id < 0) { conoutf("rpgforceloot: unknown item"); return; }
        float dist = numargs >= 2 ? args[1].getfloat() : 0;
        clientinfo *caster = NULL;
        loopv(clients)
        {
            clientinfo *ci = clients[i];
            if((ci->connectionState == CONNECTION_STATE_CONNECTED || ci->local) && ci->state.ai.type == ai::AI_TYPE_NONE && ci->state.state == CS_ALIVE) { caster = ci; break; }
        }
        if(!caster) { conoutf("rpgforceloot: no live player"); return; }
        vec ahead;
        // test-only: read the local player's facing (in-process listen server)
        vecfromyawpitch(game::player1 ? game::player1->yaw : 0, 0, 1, 0, ahead);
        float dx = caster->state.o.x + ahead.x*dist, dy = caster->state.o.y + ahead.y*dist;
        int idx = rpg::spoilloot(id, dx, dy, caster->state.o.z);
        rpg::setlootborn(idx, totalmillis);
        sendf(-1, 1, "ri6", N_RPG_LOOT, idx, id,
              int(dx*DMF), int(dy*DMF), int(caster->state.o.z*DMF));
        conoutf("rpgforceloot: drop #%d (%s) at '%s' (%.1f ahead)", idx, rpg::itemname(id), caster->name, dist);
    }
    COMMANDN(rpgforceloot, rpgforceloot_cmd, "sfV");
    // TEST-ONLY (fenced): force the next melee ability cast to crit, so the
    // crit multiplier is harness-observable without hundreds of casts at 5.5%.
    // Never game code: the server roll is the real path; this just pins it.
    static bool rpg_forcecrit = false;
    void rpgforcecrit_cmd() { rpg_forcecrit = true; conoutf("rpgforcecrit: next melee cast will crit"); }
    COMMANDN(rpgforcecrit, rpgforcecrit_cmd, "");
    // Drives the REAL server dodamage() with a monster as the attacker, so the armor
    // hook is exercised through the genuine code path (only the monster's decision
    // to attack is scripted; the damage math, armor absorption and sync are real).
    void rpgmonsterdmg_cmd(tagval *args, int numargs)
    {
        if(numargs < 2 || !rpg::connected()) { conoutf("usage: rpgmonsterdmg <name> <amount>"); return; }
        const char *name = args[0].getstr();
        int amount = max(args[1].getint(), 1);
        clientinfo *victim = NULL, *monster = NULL;
        loopv(clients)
        {
            clientinfo *ci = clients[i];
            if(ci->state.ai.type == ai::AI_TYPE_NONE && !strcmp(ci->name, name) && ci->state.state == CS_ALIVE) victim = ci;
            if(ci->state.ai.type == ai::AI_TYPE_MONSTER && ci->state.state == CS_ALIVE && !monster) monster = ci;
        }
        if(!victim) { conoutf("rpgmonsterdmg: no live player '%s'", name); return; }
        if(!monster) { conoutf("rpgmonsterdmg: no live monster (spawn one with rpgspawnmonster first)"); return; }
        int hpbefore = victim->state.health;

        vec push(0, 0, 0);

        bool savegod = rpg_testgod; rpg_testgod = false; // explicit test damage bypasses god mode

        dodamage(victim, monster, amount, WEAP_FIST, push, false);

        rpg_testgod = savegod;
        conoutf("rpgmonsterdmg: '%s' %d -> %d hp (raw %d)", name, hpbefore, victim->state.health, amount);
    }
    COMMANDN(rpgmonsterdmg, rpgmonsterdmg_cmd, "siV");
    // TEST-ONLY: dump player + loot positions for the walk-over diagnostic.
    void rpgwhere_cmd(tagval *args, int numargs)
    {
        loopv(clients)
        {
            clientinfo *ci = clients[i];
            if(ci->state.ai.type != ai::AI_TYPE_NONE) continue;
            conoutf("rpgwhere: player '%s' at (%.1f, %.1f, %.1f) state %d", ci->name, ci->state.o.x, ci->state.o.y, ci->state.o.z, ci->state.state);
        }
        rpg::lootdrop drops[64]; int nd = 0;
        rpg::getlootdrops(drops, 64, nd);
        loopi(nd) conoutf("rpgwhere: drop #%d (%s) at (%.1f, %.1f, %.1f)", drops[i].idx, rpg::itemname(drops[i].item), drops[i].x, drops[i].y, drops[i].z);
        if(game::player1) conoutf("rpgwhere: client player1 at (%.1f, %.1f, %.1f) yaw %.1f", game::player1->o.x, game::player1->o.y, game::player1->o.z, game::player1->yaw);
    }
    COMMANDN(rpgwhere, rpgwhere_cmd, "V");

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
        // --- RPG: content data layer (crafting build step 0): items + loot from data/rpg ---
        rpg::loadcontent();
        // Quest load fix (Oct 26, 2026): quests are NOT loaded here. At init
        // the NPC table is still empty, so ss_quest_giver lookups always
        // failed and the old done guard then blocked every retry. Quests
        // load at map change instead, after loadnpcs (see changemap below).
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

    // --- RPG: Shattered Shield stats (milestone step 5, additive; see src/rpg/stats.h) ---
    // Engine max health follows the RPG formula on RPG maps only. Fresh spawn: full
    // heal, the engine convention (health == maxhealth after spawnstate). Otherwise
    // clamp down only, never silently heal. Called from BOTH first-spawn paths:
    // sendspawn() (respawns) and the inline spawn inside sendinitclient().
    static void rpg_applyspawnhealth(clientinfo *ci)
    {
        if(!rpg::connected() || !rpg::mapactive(smapname)) return;
        if(ci->clientnum == rpg_testspawn_cn && rpg_testspawn_hp > 0)
        {
            ci->state.maxhealth = ci->state.health = rpg_testspawn_hp;
            rpg_testspawn_cn = -1; rpg_testspawn_hp = 0;
            return;
        }
        int rpg_lvl = 1, rpg_xp = 0;
        if(!rpg::getstats(ci->name, rpg_lvl, rpg_xp)) return;
        gamestate &gs = ci->state;
        int maxhp = rpg::stat_maxhp(rpg_lvl, rpg::buffbonus(ci->name, rpg::ATTR_VITALITY) + rpg::gearbonus(ci->name, rpg::ATTR_VITALITY)); // Phase 2: Vitality buff
        if(gs.health >= gs.maxhealth) gs.health = maxhp;
        else if(gs.health > maxhp) gs.health = maxhp;
        gs.maxhealth = maxhp;
    }

    void sendspawn(clientinfo *ci)
    {
        gamestate &gs = ci->state;
        spawnstate(ci);
        rpg_applyspawnhealth(ci);
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
                rpg_applyspawnhealth(ci); // RPG: formula max HP on first spawn too
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
        // --- RPG: editor Phase 1 (Oct 24, 2026) ---
        // Load per-map NPCs so server talk range checks use the same
        // coordinates as the client's data file. Falls back to the
        // hardcoded table when npcs_<map>.cfg is missing.
        if(s && s[0]) rpg::loadnpcs(s);
        // --- RPG: quest load fix (Oct 26, 2026) ---
        // Load quests AFTER NPCs, so ss_quest_giver ids resolve against the
        // full NPC table. loadquests() is a no-op once loaded and retries
        // on later map changes until it succeeds, so a broken quest file
        // still logs its failure instead of silently disabling quests.
        // Only on a real map load: an empty map has no NPC table to resolve
        // against, and quests are not needed there.
        if(s && s[0]) rpg::loadquests();
        // --- RPG: quest cache reload (quest-save load-drop fix) ---
        // ENTER WORLD connects before the first map change, so the
        // connect-time questcacheload ran with zero quest definitions and
        // the validquest filter dropped every stored row. Definitions load
        // just above, so reload the caches for already-connected clients
        // now that defs exist, and push the restored state to each client.
        // Safe on later map changes: quest/repeatable writes are
        // write-through, so a reload just re-reads current rows.
        // Gate is the map load itself (s && s[0]), NOT the numeric mode:
        // quest definitions load on every non-empty map change, and the
        // RPG maps can load in a non-RPG numeric mode (headless: plain
        // `map rpg_01` runs in mode 1, not 16), while quest UI gates on
        // the map name, not the mode — so a mode gate would silently
        // skip the reload exactly when quests are visible. The reload
        // is read-only and clients ignore quest state on non-RPG maps.
        if(s && s[0])
        {
            loopv(clients)
            {
                clientinfo *ci = clients[i];
                if(ci->connectionState == CONNECTION_STATE_CONNECTED && ci->state.ai.type == ai::AI_TYPE_NONE)
                {
                    rpg::reloadquestcaches(ci->name);
                    rpg_syncquest(ci);
                    rpg_syncbuffs(ci); // bundle: fresh buff list on every map load (covers reconnect)
                }
            }
        }
        // --- RPG: Shattered Shield stats (milestone step 5, additive) ---
        // A listen-server `map` command changes smapname after clients connected,
        // so the connect-time spawn ran before the map was an RPG map; apply the
        // formula max HP now. Position updates (sendstate) sync it to clients.
        loopv(clients)
        {
            clientinfo *mci = clients[i];
            if(mci->connectionState == CONNECTION_STATE_CONNECTED && mci->state.ai.type == ai::AI_TYPE_NONE)
            {
                rpg_applyspawnhealth(mci);
                sendf(mci->clientnum, 1, "ri2", N_RPG_HPMAX, mci->state.maxhealth);
            }
        }
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

    static void rpg_behaviordamage(clientinfo *monster, clientinfo *player); // RPG mob behaviour (Oct 20), defined with the ambient spawner below
    void dodamage(clientinfo *target, clientinfo *actor, int damage, int gun, const vec &hitpush, bool special)
    {
        // --- RPG: Shattered Shield ambient spawns (additive) ---
        // No friendly fire between monsters on RPG maps. Ambient monsters crowd
        // the player and their bites land on each other; the stock "infight if
        // very angry" retaliation (zombie loyalty 1 = two friendly bites) then
        // turns the crowd into duels to the death and the respawn timer just
        // feeds the grinder. Dropping the hit entirely (no N_DAMAGE, so no
        // pain/anger reaction) keeps monsters focused on players. Player and
        // champion damage is unaffected; FPS/survival modes are untouched.
        if(damage > 0 && rpg::connected() && target && actor && target != actor &&
           target->state.ai.type == ai::AI_TYPE_MONSTER && actor->state.ai.type == ai::AI_TYPE_MONSTER &&
           rpg::mapactive(smapname))
            return;
        // --- RPG: Shattered Shield starter tuning (Oct 21, user-approved) ---
        // The imp is the weakest ambient mob and the q1 hunt target: its
        // bite is halved on RPG maps so a level-1 character can win starter
        // hunts. Gated on the monster type name so puglins, zombies and the
        // champion are untouched. Runs before armor mitigation so the armor
        // log line shows the final numbers.
        if(damage > 0 && rpg::connected() && target && target->state.ai.type == ai::AI_TYPE_NONE &&
           actor && actor != target && actor->state.ai.type == ai::AI_TYPE_MONSTER &&
           rpg::mapactive(smapname))
        {
            const ::monster::MonsterType &mt = ::monster::getMonsterType(actor->state.getMonsterType());
            if(mt.classInfo.name && !strcmp(mt.classInfo.name, "imp"))
            {
                int newdmg = max(1, damage/2);
                if(newdmg != damage) conoutf("rpg: imp bite halved (%d -> %d)", damage, newdmg);
                damage = newdmg;
            }
        }
        // --- RPG: Shattered Shield stats (milestone step 5, additive; see src/rpg/stats.h) ---
        // DDO-style PERCENTAGE mitigation: taken = raw*100/(100 + armor*10).
        // Same gating as before (monster attacker + RPG map only): PvP damage,
        // gun damage between players, and FPS modes are never touched.
        if(damage > 0 && rpg::connected() && target && target->state.ai.type == ai::AI_TYPE_NONE &&
           actor && actor != target && actor->state.ai.type == ai::AI_TYPE_MONSTER &&
           rpg::mapactive(smapname))
        {
            int newdmg = rpg::stat_mitigate(damage, rpg::armorvalue(target->name));
            if(newdmg != damage) conoutf("rpg: armor reduces %d (%d -> %d) for '%s'", damage - newdmg, damage, newdmg, target->name);
            damage = newdmg;
        }
        // survival damage upgrade: bought damage levels scale a human's outgoing damage (dmsp only)
        if(actor && actor!=target && m_dmsp && actor->state.ai.type==ai::AI_TYPE_NONE && actor->dmgmult > 100)
            damage = damage*actor->dmgmult/100;
        if(rpg_testgod && target && target->state.ai.type == ai::AI_TYPE_NONE &&
           actor && actor != target && actor->state.ai.type == ai::AI_TYPE_MONSTER)
        {
            damage = 0; // test godmode: monsters cannot hurt players
        }
        // --- RPG: Shattered Shield mob behaviour + regen bookkeeping (Oct 20) ---
        // Any real damage between a player and a monster stamps the
        // player's combat clock (drives out-of-combat regen) and the
        // monster's engagement record (drives the leash reset).
        if(damage > 0 && target && actor && target != actor && rpg::connected() && rpg::mapactive(smapname))
        {
            if(actor->state.ai.type == ai::AI_TYPE_NONE && target->state.ai.type == ai::AI_TYPE_MONSTER)
            {
                rpg::notecombat(actor->name, totalmillis);
                rpg_behaviordamage(target, actor);
            }
            else if(actor->state.ai.type == ai::AI_TYPE_MONSTER && target->state.ai.type == ai::AI_TYPE_NONE)
            {
                rpg::notecombat(target->name, totalmillis);
                rpg_behaviordamage(actor, target);
            }
        }
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
                actor->state.guts += zt.classInfo.maxhealth/(zt.freq > 0 ? zt.freq : 1)*2; // freq 0 = named-only types (imp/puglin; also stock rat/juggernaut): guard the division
                sendf(actor->ownernum, 1, "ri3", N_GUTS, actor->clientnum, actor->state.guts);
            }
            // --- RPG: Shattered Shield persistence (milestone steps 1+2, additive; see src/rpg) ---
            if(actor && actor!=target && target->state.ai.type==ai::AI_TYPE_MONSTER && actor->state.ai.type==ai::AI_TYPE_NONE)
            {
                rpg::unpinmonster(target->clientnum); // test pin ends with the monster
                bool rpg_leveled = false;
                int rpg_newlevel = rpg::awardxp(actor->name, rpg::XP_PER_MONSTER_KILL, &rpg_leveled);
                if(rpg_leveled)
                {
                    sendservmsgf("\f2%s \f7reached RPG level \f2%d!", actor->name, rpg_newlevel);
                    // --- RPG: Shattered Shield stats (milestone step 5, additive) ---
                    // Max health follows the formula on RPG maps. Current HP is
                    // kept (no silent full heal); the client learns the new cap via
                    // N_RPG_HPMAX and refreshes its stat cache via rpg_syncinv.
                    if(rpg::mapactive(smapname))
                    {
                        int maxhp = rpg::stat_maxhp(rpg_newlevel, rpg::buffbonus(actor->name, rpg::ATTR_VITALITY) + rpg::gearbonus(actor->name, rpg::ATTR_VITALITY)); // Phase 2: Vitality buff
                        if(actor->state.health > maxhp) actor->state.health = maxhp;
                        actor->state.maxhealth = maxhp;
                        sendf(actor->clientnum, 1, "ri2", N_RPG_HPMAX, maxhp);
                    }
                    rpg_syncinv(actor); // refresh level/xp on the client sheet
                }
                // --- RPG: quest engine v2 kill counting (additive; see src/rpg) ---
                // Generalized: a kill counts toward EVERY active quest with a
                // matching kill objective. Party shared credit preserved: the
                // killer's party members get the same counting. XP stays with
                // the killer; turn-in stays individual.
                if(rpg::mapactive(smapname) && target)
                {
                    int counted[8];
                    int ncounted = rpg::questonkill(actor->name, target->name, target->clientnum, true, counted, 8);
                    if(ncounted > 0)
                    {
                        loopi(ncounted)
                        {
                            const rpg::questdef *qqd = rpg::questdefbyid(counted[i]);
                            if(!qqd) continue;
                            int have = 0, need = 0;
                            rpg::questprogress(actor->name, counted[i], have, need);
                            if(have >= need && need > 0) rpg_qmsg(actor, "Objective complete - return to the quest giver.");
                            else
                            {
                                defformatstring(qprog)("%s: %d/%d slain.", qqd->name, have, need);
                                rpg_qmsg(actor, qprog);
                            }
                        }
                        rpg_syncquest(actor);
                    }
                    rpg_party *pp = rpg_findparty(actor->name);
                    if(pp) loopi(pp->nmembers)
                    {
                        if(!strcmp(pp->members[i], actor->name)) continue;
                        clientinfo *m = rpg_findclient(pp->members[i]);
                        if(!m) continue; // offline: no live tracker to update
                        int mcounted[8];
                        int nmcounted = rpg::questonkill(m->name, target->name, target->clientnum, true, mcounted, 8);
                        if(nmcounted > 0)
                        {
                            conoutf("rpg: party credit: %s %d quest(s) (killer %s)", m->name, nmcounted, actor->name);
                            loopj(nmcounted)
                            {
                                const rpg::questdef *mqqd = rpg::questdefbyid(mcounted[j]);
                                if(!mqqd) continue;
                                int have = 0, need = 0;
                                rpg::questprogress(m->name, mcounted[j], have, need);
                                if(have >= need && need > 0) rpg_qmsg(m, "Objective complete - return to the quest giver.");
                                else
                                {
                                    defformatstring(mqprog)("%s: %d/%d slain. (party credit from %s)", mqqd->name, have, need, actor->name);
                                    rpg_qmsg(m, mqprog);
                                }
                            }
                            rpg_syncquest(m);
                        }
                    }
                }
                // --- RPG: Shattered Shield inventory (milestone step 4, additive; see src/rpg) ---
                // Loot drops roll on RPG maps only, so FPS modes never see item drops.
                if(rpg::mapactive(smapname))
                {
                    // Content layer (step 0): per-type loot tables, keyed by
                    // the victim's monster class. Ambient mobs carry their
                    // class name ("zombie 3", "imp", "puglin"); the champion
                    // is a zombie variant. Anything else rolls the global
                    // fallback table, exactly as before this change.
                    const char *lootkey = NULL;
                    if(!strcmp(target->name, "imp")) lootkey = "imp";
                    else if(!strcmp(target->name, "puglin")) lootkey = "puglin";
                    else if(!strncmp(target->name, "zombie", 6) || !strcmp(target->name, "Rotting Champion")) lootkey = "zombie";
                    int loot = rpg::rollloot(lootkey);
                    if(loot >= 0)
                    {
                        int idx = rpg::spoilloot(loot, target->state.o.x, target->state.o.y, target->state.o.z);
                        rpg::setlootborn(idx, totalmillis);
                        sendf(-1, 1, "ri6", N_RPG_LOOT, idx, loot,
                              int(target->state.o.x*DMF), int(target->state.o.y*DMF), int(target->state.o.z*DMF));
                        sendservmsgf("\f2%s \f7dropped from \f2%s\f7!", rpg::itemname(loot), target->name);
                        conoutf("rpg: loot drop #%d (%s) at (%.1f, %.1f, %.1f)", idx, rpg::itemname(loot),
                                target->state.o.x, target->state.o.y, target->state.o.z);
                    }
                    // v0.1.23: mob coin drops (MMO research: WoW mobs drop coins
                    // directly, in addition to items). Granted to the killer,
                    // not a world pickup. Amounts are copper range for early game.
                    const char *coinkey = NULL;
                    if(!strcmp(target->name, "imp")) coinkey = "imp";
                    else if(!strcmp(target->name, "puglin")) coinkey = "puglin";
                    else if(!strcmp(target->name, "Rotting Champion")) coinkey = "champion";
                    else if(!strncmp(target->name, "zombie", 6)) coinkey = "zombie";
                    // v0.1.24: fallback - any monster type not in the table above
                    // (e.g. a renamed or new variant) still drops coins at the
                    // zombie rate instead of silently dropping nothing. This
                    // block only runs for AI_TYPE_MONSTER kills, so the fallback
                    // can never fire on a player or NPC.
                    if(!coinkey) coinkey = "zombie";
                    if(coinkey && actor && actor->name[0])
                    {
                        int coins = rpg::rollcoins(coinkey);
                        if(coins > 0)
                        {
                            int newtotal = rpg::addcopper(actor->name, coins);
                            if(newtotal >= 0)
                            {
                                // v0.1.25: "ri2" (type + payload int). The old "ri" only wrote
                                // the type token, so the client read an empty buffer as 0
                                // and inventory always showed 0c despite the server log.
                                sendf(actor->clientnum, 1, "ri2", N_RPG_GOLD, newtotal);
                                sendservmsgf("\f2You found %d copper from \f2%s\f7!", coins, target->name);
                                conoutf("rpg: coin drop %dc from %s to %s (total %dc)", coins, target->name, actor->name, newtotal);
                            }
                            else conoutf("rpg: WARNING coin grant FAILED for %s (%dc from %s) - addcopper returned %d (character row missing?)", actor->name, coins, target->name, newtotal);
                        }
                    }
                }
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

    // Line-of-sight for spells (Oct 21 fix): the old single ray (eye+12 ->
    // feet+4) hugged the ground and was blocked by any terrain bump, making
    // 45-range spells unusable at range on bumpy maps. Now test three heights
    // on the target (ankle+4, torso+14, head+24); LoS holds if ANY ray is
    // clear. raycubelos only hits world geometry, never entities, so the
    // target's own body can't block the ray.
    static bool rpg_hasspelllos(clientinfo *cq, clientinfo *t)
    {
        vec from(cq->state.o); from.z += 12.0f; // ~eye height; gamestate has no eyeheight field
        static const float heights[3] = { 4.0f, 14.0f, 24.0f };
        loopi(3)
        {
            vec to(t->state.o); to.z += heights[i];
            vec hitpos;
            if(raycubelos(from, to, hitpos)) return true;
        }
        return false;
    }

    static clientinfo *rpg_findtarget(clientinfo *cq, const rpg::abilitydef &ab, int yaw, int advisory)
    {
        // Targeting v1: an explicit client selection (advisory target clientnum)
        // is validated here (monster, alive, in range) and wins over the nearest
        // pick. The melee front-cone check is waived for an explicit target: the
        // click IS the aim ("right-click to auto-attack" works from any facing).
        // Range, line-of-sight (spells), cooldown, mana and damage stay exactly
        // as before. advisory < 0 keeps the old nearest-target behavior.
        if(advisory >= 0)
        {
            clientinfo *t = NULL;
            loopv(clients) if(clients[i]->clientnum == advisory) { t = clients[i]; break; }
            if(!t || t == cq) return NULL;
            if(t->state.ai.type != ai::AI_TYPE_MONSTER) return NULL; // monsters only: PvE world
            if(t->state.state != CS_ALIVE || t->state.health <= 0) return NULL;
            vec delta(t->state.o);
            delta.sub(cq->state.o);
            if(!rpg_testforcerange_srv && delta.magnitude() > ab.range) return NULL; // TEST-ONLY bypass
            if(!ab.melee && !rpg_hasspelllos(cq, t)) return NULL; // no line of sight
            return t;
        }
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
            else if(!rpg_hasspelllos(cq, t)) continue; // no line of sight
            if(!best || dist < bestdist) { best = t; bestdist = dist; }
        }
        return best;
    }

    // --- RPG: Shattered Shield quest chain v1 (additive; see src/rpg) ---
    // Quest dialogue + state machine, server-authoritative. Dialogue lines go
    // --- RPG: quest engine v2 + dialogue (server-authoritative) ---
    // Dialogue runs as server session-state: the server picks the node from
    // the npc's graph, sends node text + response buttons (N_RPG_DIALOG);
    // the client sends back only a button index (N_RPG_RESPOND). Conditions
    // and actions are the CLOSED vocabularies from the quest data - dialogue
    // data can never execute arbitrary script. Sessions close on walk-away
    // (range re-checked on respond), death/disconnect, or explicit close.
    // Quest state persists in rpg.db (write-through); turn-in re-validates
    // the objective server-side (no double turn-in).
    static void rpg_qmsg(clientinfo *cq, const char *line)
    {
        if(!cq || !line || !line[0]) return;
        sendf(cq->clientnum, 1, "ris", N_RPG_QMSG, line);
    }

    // Shared level-up handling (broadcast + hpmax sync on RPG maps).
    static void rpg_levelupfx(clientinfo *cq, int newlevel)
    {
        if(!cq || newlevel <= 0) return;
        sendservmsgf("\f2%s \f7reached RPG level \f2%d!", cq->name, newlevel);
        if(rpg::mapactive(smapname))
        {
            int maxhp = rpg::stat_maxhp(newlevel, rpg::buffbonus(cq->name, rpg::ATTR_VITALITY) + rpg::gearbonus(cq->name, rpg::ATTR_VITALITY)); // Phase 2: Vitality buff
            if(cq->state.health > maxhp) cq->state.health = maxhp;
            cq->state.maxhealth = maxhp;
            sendf(cq->clientnum, 1, "ri2", N_RPG_HPMAX, maxhp);
        }
    }

    // Objective display text for one quest (tracker + journal).
    static void rpg_questobjtext(const char *name, int qid, char *out, int outlen)
    {
        if(out && outlen > 0) out[0] = '\0';
        const rpg::questdef *qd = rpg::questdefbyid(qid);
        if(!qd) return;
        int st = rpg::queststate(name, qid);
        const rpg::queststage *qs = rpg::queststageof(qd, st);
        if(!qs) return;
        if(qs->objtype == rpg::QOBJ_KILL)
        {
            int have = 0, need = 0;
            rpg::questprogress(name, qid, have, need);
            formatstring(out)("%d/%d slain", have, need);
        }
        else if(qs->objtype == rpg::QOBJ_FETCH)
        {
            char item[64]; int amount = 0;
            if(rpg::repeatableget(name, qid, item, sizeof(item), &amount))
            {
                int iid = rpg::finditem(item);
                int have = rpg::itemcount(name, item);
                if(have > amount) have = amount;
                formatstring(out)("%dx %s (%d/%d)", amount, rpg::itemname(iid), have, amount);
            }
        }
        else if(qs->objtype == rpg::QOBJ_TALK)
        {
            int ni = rpg::npcindexbyid(qs->objarg);
            const rpg::npcdef *nd = ni >= 0 ? rpg::npcat(ni) : NULL;
            formatstring(out)("Talk to %s", nd ? nd->name : qs->objarg);
        }
    }

    static void rpg_syncquest(clientinfo *cq) // push quest state to one client
    {
        if(!rpg::connected() || !cq) return;
        int nq = rpg::questdefcount();
        loopi(nq)
        {
            const rpg::questdef *qd = rpg::questdefbyindex(i);
            int st = rpg::queststate(cq->name, qd->qid);
            int have = 0, need = 0;
            rpg::questprogress(cq->name, qd->qid, have, need);
            sendf(cq->clientnum, 1, "ri5", N_RPG_QUEST, qd->qid, st, have, need);
            char objtext[160];
            rpg_questobjtext(cq->name, qd->qid, objtext, sizeof(objtext));
            sendf(cq->clientnum, 1, "riis", N_RPG_QTEXT, qd->qid, objtext);
        }
    }

    // Bundle (Oct 28): HUD buff display sync. Clear marker first, then one
    // message per active buff — the client replaces incrementally, so a
    // re-sync never leaves a stale icon. Called on buff use, on expiry,
    // and on every map change (the changemap reload block), which covers
    // connect/reconnect freshness: buffs are cleared server-side on
    // logout, so a reconnect + map load re-syncs empty.
    static void rpg_syncbuffs(clientinfo *cq)
    {
        if(!rpg::connected() || !cq) return;
        sendf(cq->clientnum, 1, "ri3", N_RPG_BUFFS, -1, 0); // clear all
        int attrs[8], mags[8], secs[8];
        int n = rpg::bufflist(cq->name, attrs, mags, secs, 8);
        loopi(n)
        {
            int expires = (int)time(NULL) + secs[i];
            // v0.1.37: magnitude rides along (was computed then dropped) so the
            // buff-row hover tooltip can show "+N Strength" without guessing tier.
            sendf(cq->clientnum, 1, "ri4", N_RPG_BUFFS, attrs[i], expires, mags[i]);
        }
    }

    // --- dialogue sessions ---
    struct rpg_talksession
    {
        int clientnum;      // -1 = free slot
        int npcid;
        int nodeidx;        // node index in the npc's graph
        int sentresp[4];    // resp indices (into the node) shown as buttons
        int nsentresp;
    };
    static const int MAXTALKSESSIONS = 32;
    static rpg_talksession rpg_talks[MAXTALKSESSIONS];
    static bool rpg_talksinit_done = false;
    static void rpg_talksinit()
    {
        if(rpg_talksinit_done) return;
        rpg_talksinit_done = true;
        loopi(MAXTALKSESSIONS) rpg_talks[i].clientnum = -1;
    }
    static rpg_talksession *rpg_findtalk(int clientnum)
    {
        rpg_talksinit();
        loopi(MAXTALKSESSIONS) if(rpg_talks[i].clientnum == clientnum) return &rpg_talks[i];
        return NULL;
    }
    static void rpg_closetalk(clientinfo *cq)
    {
        if(!cq) return;
        rpg_talksession *t = rpg_findtalk(cq->clientnum);
        if(t) t->clientnum = -1;
        // nodeid -1 = close the panel
        sendf(cq->clientnum, 1, "riississss", N_RPG_DIALOG, -1, "", "", 0, "", "", "", "");
    }

    // Send one dialogue node to the client (filters responses by condition).
    static void rpg_senddialog(clientinfo *cq, const rpg::dlggraph *g, int nodeidx, int npcid)
    {
        if(!cq || !g || nodeidx < 0 || nodeidx >= g->nnodes) return;
        rpg_talksinit();
        rpg_talksession *t = rpg_findtalk(cq->clientnum);
        if(!t)
        {
            loopi(MAXTALKSESSIONS) if(rpg_talks[i].clientnum < 0) { t = &rpg_talks[i]; break; }
            if(!t) return; // no session slot
            t->clientnum = cq->clientnum;
        }
        const rpg::dlgnode &nd = g->nodes[nodeidx];
        t->npcid = npcid;
        t->nodeidx = nodeidx;
        t->nsentresp = 0;
        // join say lines with $var substitution
        char text[1024]; text[0] = '\0';
        loopi(nd.nsay)
        {
            char sub[512];
            rpg::questtextsubst(cq->name, nd.quest, nd.say[i], sub, sizeof(sub));
            if(i > 0) concatstring(text, "\n");
            concatstring(text, sub);
        }
        // response buttons: only those whose conditions all hold
        char r0[160] = "", r1[160] = "", r2[160] = "", r3[160] = "";
        char *rs[4] = { r0, r1, r2, r3 };
        loopi(nd.nresp)
        {
            const rpg::dlgresponse &r = nd.resp[i];
            bool ok = true;
            loopj(r.ncond) if(!rpg::dlgcondmet(cq->name, r.cond[j])) { ok = false; break; }
            if(!ok) continue;
            if(t->nsentresp >= 4) break;
            t->sentresp[t->nsentresp] = i;
            copystring(rs[t->nsentresp], r.text, 160);
            t->nsentresp++;
        }
        const rpg::npcdef *npc = rpg::npcat(npcid);
        sendf(cq->clientnum, 1, "riississss", N_RPG_DIALOG, nodeidx,
              npc ? npc->name : "", text, t->nsentresp, r0, r1, r2, r3);
    }

    // Execute one dialogue action server-side. xp_granted accumulates givexp
    // for the turn-in summary. Returns false when validation fails (the
    // response is aborted and the session closed).
    static bool rpg_doaction(clientinfo *cq, const rpg::dlgaction &a, int &xp_granted)
    {
        const char *name = cq->name;
        switch(a.verb)
        {
            case rpg::ACT_SETSTAGE:
                rpg::questsetstage(name, a.qid, a.n);
                return true;
            case rpg::ACT_COMPLETEQUEST:
            {
                const rpg::questdef *qd = rpg::questdefbyid(a.qid);
                if(!qd) return false;
                int st = rpg::queststate(name, a.qid);
                if(st <= 0 || st == qd->finalstage) return false; // no double turn-in
                // Re-validate the objective server-side (belt and suspenders:
                // the turn-in node's when-clauses already required objready).
                rpg::dlgcond orc;
                memset(&orc, 0, sizeof(orc));
                orc.verb = rpg::CND_OBJREADY; orc.qid = a.qid;
                if(!rpg::dlgcondmet(name, orc)) return false;
                rpg::questsetstage(name, a.qid, qd->finalstage);
                // Money-only quests grant 0 XP: omit the XP tail rather
                // than printing a misleading "(+0 XP)". All existing
                // turn-ins grant XP, so their messages are unchanged.
                defformatstring(done)("Quest complete: %s", qd->name);
                if(xp_granted > 0) formatstring(done)("Quest complete: %s (+%d XP)", qd->name, xp_granted);
                rpg_qmsg(cq, done);
                return true;
            }
            case rpg::ACT_GIVEITEM:
            {
                if(!rpg::additem(name, a.item, a.count)) return false;
                int iid = rpg::finditem(a.item);
                defformatstring(it)("%s", "");
                if(a.count > 1) formatstring(it)("Received: %dx %s", a.count, rpg::itemname(iid));
                else formatstring(it)("Received: %s", rpg::itemname(iid));
                rpg_qmsg(cq, it);
                return true;
            }
            case rpg::ACT_TAKEITEM:
            {
                char item[64];
                copystring(item, a.item, sizeof(item));
                if(!strcmp(item, "rolled"))
                {
                    int amount = 0;
                    if(!rpg::repeatableget(name, a.qid, item, sizeof(item), &amount)) return false;
                }
                if(rpg::itemcount(name, item) < a.count) return false;
                rpg::removeitem(name, item, a.count);
                return true;
            }
            case rpg::ACT_GIVEXP:
            {
                bool leveled = false;
                int newlevel = rpg::awardxp(name, a.n, &leveled);
                if(newlevel < 0) return false;
                xp_granted += a.n;
                if(leveled) rpg_levelupfx(cq, newlevel);
                return true;
            }
            case rpg::ACT_GIVEMONEY:
            {
                // v0.1.35: money quest rewards (Corvin's necklace quest:
                // givemoney 100 = 1 silver; money is stored as total
                // copper, 100c = 1s, 100s = 1g). The turn-in response
                // path syncs inventory + copper afterwards, so the
                // client coin display updates without a special sync.
                if(a.n <= 0) return false;
                int newtotal = rpg::addcopper(name, a.n);
                if(newtotal < 0) return false;
                int g = a.n / 10000, rem = a.n % 10000;
                int s = rem / 100, c = rem % 100;
                defformatstring(amt)("%d copper", c);
                if(g > 0 && s > 0 && c > 0) formatstring(amt)("%d gold %d silver %d copper", g, s, c);
                else if(g > 0 && s > 0) formatstring(amt)("%d gold %d silver", g, s);
                else if(g > 0 && c > 0) formatstring(amt)("%d gold %d copper", g, c);
                else if(s > 0 && c > 0) formatstring(amt)("%d silver %d copper", s, c);
                else if(g > 0) formatstring(amt)("%d gold", g);
                else if(s > 0) formatstring(amt)("%d silver", s);
                defformatstring(msg)("Received: %s", amt);
                rpg_qmsg(cq, msg);
                return true;
            }
            case rpg::ACT_GIVEFETCHXP:
            {
                const rpg::questdef *qd = rpg::questdefbyid(a.qid);
                if(!qd) return false;
                char item[64]; int amount = 0;
                if(!rpg::repeatableget(name, a.qid, item, sizeof(item), &amount)) return false;
                int iid = rpg::finditem(item);
                int reward = amount * (iid >= 0 ? rpg::item(iid).value : 0);
                if(reward < 1) return false;
                bool leveled = false;
                int newlevel = rpg::awardxp(name, reward, &leveled);
                if(newlevel < 0) return false;
                xp_granted += reward;
                if(leveled) rpg_levelupfx(cq, newlevel);
                return true;
            }
            case rpg::ACT_SPAWNCHAMPION:
                rpg_spawnchampion(cq);
                return true;
            case rpg::ACT_ROLLFETCH:
                return rpg::repeatableroll(name, a.qid);
        }
        return false;
    }

    // The client clicked a response button. Server-validated: session must
    // exist, the button index must be one we sent, the player must still be
    // alive and in range, and response conditions are re-checked.
    void rpg_respond(clientinfo *cq, int idx)
    {
        if(!rpg::connected() || !cq) return;
        if(cq->state.ai.type != ai::AI_TYPE_NONE) return;
        if(!rpg::mapactive(smapname)) return;
        rpg_talksession *t = rpg_findtalk(cq->clientnum);
        if(!t) return;
        if(cq->state.state != CS_ALIVE) { rpg_closetalk(cq); return; }
        const rpg::npcdef *nd = rpg::npcat(t->npcid);
        // v0.1.25: horizontal distance, same 16u range as rpg_talk, so a
        // dialogue started in range cannot close on the first response.
        if(!nd || vec(cq->state.o.x, cq->state.o.y, 0.0f).dist(vec(nd->x, nd->y, 0.0f)) > 16.0f)
        {
            rpg_qmsg(cq, "Too far away to talk.");
            rpg_closetalk(cq);
            return;
        }
        const rpg::dlggraph *g = rpg::questgraph(nd->dialogue);
        if(!g || t->nodeidx < 0 || t->nodeidx >= g->nnodes) { rpg_closetalk(cq); return; }
        if(idx < 0) { rpg_closetalk(cq); return; } // v0.1.8: client X button
        if(idx >= t->nsentresp) return; // forged index
        const rpg::dlgnode &node = g->nodes[t->nodeidx];
        int respidx = t->sentresp[idx];
        if(respidx < 0 || respidx >= node.nresp) return;
        const rpg::dlgresponse &r = node.resp[respidx];
        // re-check response conditions (they could have changed)
        loopi(r.ncond) if(!rpg::dlgcondmet(cq->name, r.cond[i])) { rpg_closetalk(cq); return; }
        int xp_granted = 0;
        loopi(r.nact) if(!rpg_doaction(cq, r.act[i], xp_granted)) { rpg_closetalk(cq); return; }
        rpg_syncinv(cq); // refresh xp/inventory/level + quest tracker
        if(r.close) { rpg_closetalk(cq); return; }
        if(r.gotonode[0])
        {
            int target = -1;
            loopi(g->nnodes) if(!strcmp(g->nodes[i].id, r.gotonode)) { target = i; break; }
            if(target >= 0)
            {
                // goto lands only when the target's when-clauses hold;
                // otherwise fall back to entry selection.
                bool ok = g->nodes[target].nwhen > 0;
                if(ok) loopi(g->nodes[target].nwhen)
                    if(!rpg::dlgcondmet(cq->name, g->nodes[target].when[i])) { ok = false; break; }
                if(ok) { rpg_senddialog(cq, g, target, t->npcid); return; }
            }
            int entry = rpg::dlgnode_selectentry(g, cq->name);
            if(entry >= 0) rpg_senddialog(cq, g, entry, t->npcid);
            else rpg_closetalk(cq);
        }
        else rpg_closetalk(cq); // no flow (shouldn't happen; data requires one)
    }

    void rpg_talk(clientinfo *cq, int npcidx)
    {
        // v0.1.27 (item 1): NO silent failure here anymore. v0.1.26 F-talk
        // failed for every NPC with no message at all, and these three
        // early returns were the only paths that could produce exactly
        // that symptom once F reached the server (every later failure
        // already sends a visible message). Each now says why, in the
        // System tab, and logs the same to the console, so the next
        // playtest names the cause instead of going silent.
        if(!cq) return;
        if(!rpg::connected()) { rpg_qmsg(cq, "Talk is not available right now (character data is not ready)."); conoutf("rpg: talk: refused, character database not open"); return; }
        if(cq->state.ai.type != ai::AI_TYPE_NONE) { conoutf("rpg: talk: refused, talker is not a real player"); return; } // only real players talk; AI has no screen, so no player message
        if(!rpg::mapactive(smapname)) { rpg_qmsg(cq, "Talk is not available on this map."); conoutf("rpg: talk: refused, server map '%s' is not an RPG map", smapname); return; }
        conoutf("rpg: talk: request npcidx %d on map '%s'", npcidx, smapname);
        if(cq->state.state != CS_ALIVE) { rpg_qmsg(cq, "You must be alive to talk."); return; }
        // v0.1.26: the client sends the selected NPC index, but if that
        // index is stale/out of range on the server (client and server
        // tables ever disagreeing), silently returning made F-talk look
        // dead with no feedback at all — exactly Ed's v0.1.24/v0.1.25
        // Glimmer symptom ("press F to talk and that's it"). Fall back
        // to nearest-NPC resolution instead of a silent return, so a
        // stale index degrades to talking to the nearest NPC rather
        // than nothing happening.
        if(npcidx < 0 || !rpg::npcat(npcidx))
        {
            // The client sent no specific NPC (F pressed with nobody selected
            // or in sights) - resolve the nearest NPC inside talk range here;
            // the distance check below re-validates it.
            // v0.1.25: HORIZONTAL distance (XY only) with a 16u range. The old
            // 3D check measured to the NPC's base (z = ground) while the
            // player's origin sits eyeheight above ground, so standing next
            // to an NPC in open ground could still read >12u and F-talk
            // silently failed ("Too far away" only in the System tab, which
            // Ed read as "nothing happens"). Talking is about standing next
            // to someone, so height must not count.
            float best = 16.0f;
            npcidx = -1;
            loopi(rpg::npccount())
            {
                const rpg::npcdef *cand = rpg::npcat(i);
                if(!cand) continue;
                float d = vec(cq->state.o.x, cq->state.o.y, 0.0f).dist(vec(cand->x, cand->y, 0.0f));
                if(d <= best) { best = d; npcidx = i; }
            }
            if(npcidx < 0) { rpg_qmsg(cq, "No one nearby to talk to."); return; }
        }
        const rpg::npcdef *nd = rpg::npcat(npcidx);
        // v0.1.26: never fail silently here. An empty dialogue used to
        // return with no message at all, which Ed reads as "nothing
        // happens". Say so visibly instead.
        if(!nd || !nd->dialogue[0]) { rpg_qmsg(cq, "They have nothing to say."); return; }
        // v0.1.27 (item 1): trace the resolved NPC so the console shows
        // exactly which entry talk reached and how it routes.
        conoutf("rpg: talk: npc %d id '%s' dialogue '%s' type '%s' at %.0f %.0f", npcidx, nd->id, nd->dialogue, nd->type, nd->x, nd->y);
        if(vec(cq->state.o.x, cq->state.o.y, 0.0f).dist(vec(nd->x, nd->y, 0.0f)) > 16.0f)
        {
            rpg_qmsg(cq, "Too far away to talk.");
            return;
        }
        // v0.1.23 VENDOR: Glimmer's dialogue marker opens the vendor GUI
        // instead of a quest dialogue graph. Client requests fresh inv sync
        // (N_RPG_INVL) when the GUI opens, like rpginventory does.
        // v0.1.26: accept EITHER signal — the "vendor_main" dialogue
        // marker OR the npcdef type "vendor" (editor Phase 1). Vendor
        // talk never worked in-game (v0.1.24/v0.1.25: prompt showed,
        // F did nothing, no error), and the single-marker check is the
        // one vendor-only branch in this function; a second, independent
        // signal makes a marker mismatch unable to silently kill the
        // shop again. Quest NPCs (type questgiver, *_main dialogue)
        // match neither signal and flow to dialogue as before.
        if(!strcmp(nd->dialogue, "vendor_main") || !strcmp(nd->type, "vendor"))
        {
            conoutf("rpg: talk: opening vendor GUI for npc '%s'", nd->id[0] ? nd->id : "?");
            // v0.1.30 ROOT CAUSE: this was sendf(..., "r", N_RPG_VENDOR).
            // In sendf, 'r' is the reliable flag, not a format code, so
            // "r" alone consumed NO vararg and sent an EMPTY packet:
            // the client never received N_RPG_VENDOR, the handler never
            // ran, and F looked dead with no error anywhere. Every
            // other server send uses "ri..." so the message type is
            // the first int in the packet. Vendor now does the same.
            sendf(cq->clientnum, 1, "ri", N_RPG_VENDOR);
            return;
        }
        const rpg::dlggraph *g = rpg::questgraph(nd->dialogue);
        if(!g)
        {
            // Oct 21: this branch means the quest content failed to load
            // server-side (nqgraphs == 0) - the graph names themselves are
            // verified against the NPC table at content validation. Log the
            // miss so the cause is identifiable from the console/System tab.
            conoutf("rpg: talk: no dialogue graph '%s' for npc '%s' (quest content not loaded?)",
                    nd->dialogue[0] ? nd->dialogue : "?", nd->id[0] ? nd->id : "?");
            rpg_qmsg(cq, "They have nothing to say.");
            return;
        }
        int entry = rpg::dlgnode_selectentry(g, cq->name);
        if(entry < 0)
        {
            rpg_qmsg(cq, "They have nothing to say.");
            return;
        }
        rpg_senddialog(cq, g, entry, npcidx);
    }

    // --- RPG: Party + safety v1 (Oct 19, additive) ---
    // Server-managed parties for RPG play. Parties are session state (not
    // persisted); mute/block lists persist in rpg.db via rpg::ismuted etc.
    // Commands are chat lines starting with '/' (typed in the World tab or
    // console via say): /pinvite /paccept /pdecline /pleave /proster,
    // /mute /block /report, /phelp. All feedback goes to the System tab
    // via rpg_qmsg.

    static rpg_party *rpg_findparty(const char *name)
    {
        if(!name || !name[0]) return NULL;
        loopv(rpg_parties)
        {
            rpg_party &p = rpg_parties[i];
            if(rpg_pslot_find(p.members, p.nmembers, name) >= 0) return &p;
        }
        return NULL;
    }
    static clientinfo *rpg_findclient(const char *name)
    {
        if(!name || !name[0]) return NULL;
        loopv(clients)
        {
            clientinfo *t = clients[i];
            if(t->state.ai.type != ai::AI_TYPE_NONE) continue;
            if(!strcmp(t->name, name)) return t;
        }
        return NULL;
    }
    static void rpg_party_say(rpg_party *p, const char *line, clientinfo *except = NULL)
    {
        if(!p || !line) return;
        loopi(p->nmembers)
        {
            clientinfo *m = rpg_findclient(p->members[i]);
            if(m && m != except) rpg_qmsg(m, line);
        }
    }
    static void rpg_party_roster(clientinfo *cq)
    {
        rpg_party *p = rpg_findparty(cq->name);
        if(!p) { rpg_qmsg(cq, "Party: you are not in a party. (/pinvite <name> to start one)"); return; }
        string buf;
        formatstring(buf)("Party #%d (leader: %s):", p->id, p->leader);
        loopi(p->nmembers)
        {
            if(i) concatstring(buf, ",");
            concatstring(buf, " ");
            concatstring(buf, p->members[i]);
            if(!strcmp(p->members[i], p->leader)) concatstring(buf, " [L]");
        }
        rpg_qmsg(cq, buf);
    }
    static void rpg_party_disband(rpg_party *p, const char *reason)
    {
        if(!p) return;
        defformatstring(msg)("Party #%d disbanded (%s).", p->id, reason ? reason : "leader left");
        rpg_party_say(p, msg);
        int pid = p->id;
        loopv(rpg_parties) if(rpg_parties[i].id == pid) { rpg_parties.remove(i); break; }
    }
    // Chat command dispatcher. Returns true when the line was a command.
    static bool rpg_chatcmd(clientinfo *cq, const char *text)
    {
        if(!cq || !text || text[0] != '/') return false;
        // split "/cmd args..."
        string cmd, args;
        const char *sp = strchr(text, ' ');
        if(sp) { copystring(cmd, text, min(int(sp - text) + 1, (int)sizeof(cmd))); copystring(args, sp + 1); }
        else { copystring(cmd, text); args[0] = '\0'; }
        // ---- /phelp ----
        if(!strcmp(cmd, "/phelp"))
        {
            rpg_qmsg(cq, "Party: /pinvite <name> /paccept /pdecline /pleave /proster");
            rpg_qmsg(cq, "Safety: /mute <name> (toggle) /block <name> (toggle) /report <name> [reason]");
            rpg_qmsg(cq, "Clan: /clancreate <name> <tag> /claninvite <name> /clanaccept /clandecline");
            rpg_qmsg(cq, "Clan: /clanleave /clankick <name> /clanpromote|/clandemote <name> /clandisband /clanroster");
            return true;
        }
        // ---- /pinvite ----
        if(!strcmp(cmd, "/pinvite"))
        {
            if(!args[0]) { rpg_qmsg(cq, "Usage: /pinvite <name>"); return true; }
            clientinfo *t = rpg_findclient(args);
            if(!t) { defformatstring(m)("No player named '%s' is online.", args); rpg_qmsg(cq, m); return true; }
            if(t == cq) { rpg_qmsg(cq, "You can't invite yourself."); return true; }
            if(rpg::isblocked(t->name, cq->name)) { rpg_qmsg(cq, "That player is not accepting your invites."); return true; }
            if(rpg_findparty(t->name)) { rpg_qmsg(cq, "They are already in a party."); return true; }
            rpg_party *p = rpg_findparty(cq->name);
            if(!p)
            {
                rpg_party np; np.id = rpg_nextpartyid++;
                copystring(np.leader, cq->name);
                np.nmembers = 0; np.ninvites = 0;
                rpg_pslot_add(np.members, np.nmembers, cq->name);
                rpg_parties.add(np); p = &rpg_parties.last();
                defformatstring(m)("Party #%d formed. You are the leader.", p->id); rpg_qmsg(cq, m);
            }
            if(p->nmembers >= RPG_PARTY_MAX) { rpg_qmsg(cq, "Party is full (5)."); return true; }
            if(rpg_pslot_find(p->invites, p->ninvites, t->name) >= 0) { rpg_qmsg(cq, "Invite already pending."); return true; }
            rpg_pslot_add(p->invites, p->ninvites, t->name);
            defformatstring(m)("Party invite sent to %s.", t->name); rpg_qmsg(cq, m);
            defformatstring(im)("%s invites you to Party #%d. (/paccept or /pdecline)", cq->name, p->id);
            rpg_qmsg(t, im);
            return true;
        }
        // ---- /paccept ----
        if(!strcmp(cmd, "/paccept"))
        {
            if(rpg_findparty(cq->name)) { rpg_qmsg(cq, "You are already in a party."); return true; }
            loopv(rpg_parties)
            {
                rpg_party &p = rpg_parties[i];
                if(rpg_pslot_find(p.invites, p.ninvites, cq->name) >= 0)
                {
                    rpg_pslot_del(p.invites, p.ninvites, cq->name);
                    if(p.nmembers >= RPG_PARTY_MAX) { rpg_qmsg(cq, "That party is full."); return true; }
                    rpg_pslot_add(p.members, p.nmembers, cq->name);
                    defformatstring(m)("%s joined Party #%d.", cq->name, p.id);
                    rpg_party_say(&p, m);
                    rpg_party_roster(cq);
                    return true;
                }
            }
            rpg_qmsg(cq, "No pending party invite.");
            return true;
        }
        // ---- /pdecline ----
        if(!strcmp(cmd, "/pdecline"))
        {
            loopv(rpg_parties)
            {
                rpg_party &p = rpg_parties[i];
                if(rpg_pslot_find(p.invites, p.ninvites, cq->name) >= 0)
                {
                    rpg_pslot_del(p.invites, p.ninvites, cq->name);
                    defformatstring(m)("%s declined your party invite.", cq->name);
                    clientinfo *l = rpg_findclient(p.leader);
                    if(l) rpg_qmsg(l, m);
                    rpg_qmsg(cq, "Invite declined.");
                    return true;
                }
            }
            rpg_qmsg(cq, "No pending party invite.");
            return true;
        }
        // ---- /pleave ----
        if(!strcmp(cmd, "/pleave"))
        {
            rpg_party *p = rpg_findparty(cq->name);
            if(!p) { rpg_qmsg(cq, "You are not in a party."); return true; }
            if(!strcmp(p->leader, cq->name)) { rpg_party_disband(p, "leader left"); return true; }
            defformatstring(m)("%s left the party.", cq->name);
            // remove member, then announce (p may be reallocated; find again)
            int pid = p->id;
            rpg_pslot_del(p->members, p->nmembers, cq->name);
            rpg_party *p2 = NULL;
            loopv(rpg_parties) if(rpg_parties[i].id == pid) { p2 = &rpg_parties[i]; break; }
            if(p2) rpg_party_say(p2, m);
            rpg_qmsg(cq, "You left the party.");
            return true;
        }
        // ---- /proster ----
        if(!strcmp(cmd, "/proster") || !strcmp(cmd, "/party"))
        {
            rpg_party_roster(cq);
            return true;
        }
        // ---- /clancreate ----
        if(!strcmp(cmd, "/clancreate"))
        {
            // /clancreate <name> <tag>
            string cname, ctag;
            const char *sp2 = strchr(args, ' ');
            if(!sp2) { rpg_qmsg(cq, "Usage: /clancreate <name> <tag>  (tag 2-5 chars)"); return true; }
            copystring(cname, args, min(int(sp2 - args) + 1, (int)sizeof(cname)));
            copystring(ctag, sp2 + 1);
            if(rpg::clan_of(cq->name) != 0) { rpg_qmsg(cq, "You are already in a clan."); return true; }
            if(strlen(cname) < 3 || strlen(cname) > 32) { rpg_qmsg(cq, "Clan name must be 3-32 chars."); return true; }
            if(strlen(ctag) < 2 || strlen(ctag) > 5) { rpg_qmsg(cq, "Clan tag must be 2-5 chars."); return true; }
            int id = rpg::clan_create(cname, ctag, cq->name);
            if(id < 0) { rpg_qmsg(cq, "That clan name is taken."); return true; }
            defformatstring(m)("Clan %s [%s] founded. You are the Leader.", cname, ctag);
            rpg_qmsg(cq, m);
            conoutf("rpg: clan #%d '%s' [%s] created by %s", id, cname, ctag, cq->name);
            rpg_syncclan(cq);
            return true;
        }
        // ---- /claninvite ----
        if(!strcmp(cmd, "/claninvite"))
        {
            if(!args[0]) { rpg_qmsg(cq, "Usage: /claninvite <name>"); return true; }
            int id = rpg::clan_of(cq->name);
            if(id <= 0) { rpg_qmsg(cq, "You are not in a clan."); return true; }
            int rk = rpg::clan_rank(cq->name);
            if(rk != rpg::CLAN_RANK_LEADER && rk != rpg::CLAN_RANK_OFFICER) { rpg_qmsg(cq, "Only Leader/Officer can invite."); return true; }
            clientinfo *t = rpg_findclient(args);
            if(!t) { defformatstring(m)("No player named '%s' is online.", args); rpg_qmsg(cq, m); return true; }
            if(rpg::clan_of(t->name) != 0) { rpg_qmsg(cq, "They are already in a clan."); return true; }
            loopv(rpg_claninvites) if(rpg_claninvites[i].clan_id == id && !strcmp(rpg_claninvites[i].target, t->name)) { rpg_qmsg(cq, "Invite already pending."); return true; }
            rpg_claninvite inv; inv.clan_id = id; copystring(inv.target, t->name); copystring(inv.inviter, cq->name);
            rpg_claninvites.add(inv);
            string cname, ctag; rpg::clan_info(id, cname, ctag, NULL);
            defformatstring(m)("Clan invite sent to %s.", t->name); rpg_qmsg(cq, m);
            defformatstring(im)("%s invites you to clan %s [%s]. (/clanaccept or /clandecline)", cq->name, cname, ctag);
            rpg_qmsg(t, im);
            return true;
        }
        // ---- /clanaccept ----
        if(!strcmp(cmd, "/clanaccept"))
        {
            if(rpg::clan_of(cq->name) != 0) { rpg_qmsg(cq, "You are already in a clan."); return true; }
            loopv(rpg_claninvites) if(!strcmp(rpg_claninvites[i].target, cq->name))
            {
                int id = rpg_claninvites[i].clan_id;
                rpg_claninvites.remove(i);
                if(!rpg::clan_addmember(id, cq->name, rpg::CLAN_RANK_MEMBER)) { rpg_qmsg(cq, "Could not join (already in a clan?)."); return true; }
                string cname, ctag; rpg::clan_info(id, cname, ctag, NULL);
                defformatstring(m)("%s joined the clan.", cq->name);
                rpg_clan_broadcast(id, m);
                defformatstring(w)("Welcome to %s [%s]!", cname, ctag); rpg_qmsg(cq, w);
                rpg_syncclan(cq);
                return true;
            }
            rpg_qmsg(cq, "No pending clan invite.");
            return true;
        }
        // ---- /clandecline ----
        if(!strcmp(cmd, "/clandecline"))
        {
            loopv(rpg_claninvites) if(!strcmp(rpg_claninvites[i].target, cq->name))
            {
                clientinfo *inv = rpg_findclient(rpg_claninvites[i].inviter);
                rpg_claninvites.remove(i);
                if(inv) { defformatstring(m)("%s declined your clan invite.", cq->name); rpg_qmsg(inv, m); }
                rpg_qmsg(cq, "Invite declined.");
                return true;
            }
            rpg_qmsg(cq, "No pending clan invite.");
            return true;
        }
        // ---- /clanleave ----
        if(!strcmp(cmd, "/clanleave"))
        {
            int id = rpg::clan_of(cq->name);
            if(id <= 0) { rpg_qmsg(cq, "You are not in a clan."); return true; }
            int rk = rpg::clan_rank(cq->name);
            if(rk == rpg::CLAN_RANK_LEADER) { rpg_qmsg(cq, "Leaders cannot leave — /clandisband or promote someone first."); return true; }
            rpg::clan_removemember(cq->name);
            defformatstring(m)("%s left the clan.", cq->name);
            rpg_clan_broadcast(id, m);
            rpg_qmsg(cq, "You left the clan.");
            rpg_syncclan(cq);
            return true;
        }
        // ---- /clankick ----
        if(!strcmp(cmd, "/clankick"))
        {
            if(!args[0]) { rpg_qmsg(cq, "Usage: /clankick <name>"); return true; }
            int id = rpg::clan_of(cq->name);
            if(id <= 0) { rpg_qmsg(cq, "You are not in a clan."); return true; }
            int rk = rpg::clan_rank(cq->name);
            if(rk != rpg::CLAN_RANK_LEADER && rk != rpg::CLAN_RANK_OFFICER) { rpg_qmsg(cq, "Only Leader/Officer can kick."); return true; }
            if(rpg::clan_of(args) != id) { rpg_qmsg(cq, "They are not in your clan."); return true; }
            int trk = rpg::clan_rank(args);
            if(trk == rpg::CLAN_RANK_LEADER) { rpg_qmsg(cq, "You cannot kick the Leader."); return true; }
            if(rk == rpg::CLAN_RANK_OFFICER && trk == rpg::CLAN_RANK_OFFICER) { rpg_qmsg(cq, "Officers cannot kick other Officers."); return true; }
            rpg::clan_removemember(args);
            defformatstring(m)("%s was kicked from the clan.", args);
            rpg_clan_broadcast(id, m);
            clientinfo *t = rpg_findclient(args);
            if(t) { rpg_qmsg(t, "You were kicked from the clan."); rpg_syncclan(t); }
            return true;
        }
        // ---- /clanpromote /clandemote ----
        if(!strcmp(cmd, "/clanpromote") || !strcmp(cmd, "/clandemote"))
        {
            if(!args[0]) { rpg_qmsg(cq, "Usage: /clanpromote|/clandemote <name>"); return true; }
            int id = rpg::clan_of(cq->name);
            if(id <= 0) { rpg_qmsg(cq, "You are not in a clan."); return true; }
            if(rpg::clan_rank(cq->name) != rpg::CLAN_RANK_LEADER) { rpg_qmsg(cq, "Only the Leader can change ranks."); return true; }
            if(rpg::clan_of(args) != id) { rpg_qmsg(cq, "They are not in your clan."); return true; }
            if(!strcmp(args, cq->name)) { rpg_qmsg(cq, "You cannot change your own rank."); return true; }
            int trk = rpg::clan_rank(args);
            int nrk = !strcmp(cmd, "/clanpromote") ? trk - 1 : trk + 1;
            if(nrk < rpg::CLAN_RANK_OFFICER || nrk > rpg::CLAN_RANK_MEMBER) { rpg_qmsg(cq, "Nothing to do (already at that rank, or cannot promote to Leader)."); return true; }
            rpg::clan_setrank(args, nrk);
            const char *rnames[] = { "Leader", "Officer", "Member" };
            defformatstring(m)("%s is now %s.", args, rnames[nrk]);
            rpg_clan_broadcast(id, m);
            clientinfo *t = rpg_findclient(args);
            if(t) rpg_syncclan(t);
            return true;
        }
        // ---- /clandisband ----
        if(!strcmp(cmd, "/clandisband"))
        {
            int id = rpg::clan_of(cq->name);
            if(id <= 0) { rpg_qmsg(cq, "You are not in a clan."); return true; }
            if(rpg::clan_rank(cq->name) != rpg::CLAN_RANK_LEADER) { rpg_qmsg(cq, "Only the Leader can disband."); return true; }
            if(strcmp(args, "confirm")) { rpg_qmsg(cq, "Disband the clan? Type /clandisband confirm."); return true; }
            string cname; rpg::clan_info(id, cname, NULL, NULL);
            // notify online members first, then wipe
            char names[32][64]; int n = rpg::clan_memberlist(id, names, NULL, 32);
            loopi(n) { clientinfo *m = rpg_findclient(names[i]); if(m) { rpg_qmsg(m, "The clan has been disbanded."); rpg_syncclan(m); } }
            // drop pending invites
            loopvrev(rpg_claninvites) if(rpg_claninvites[i].clan_id == id) rpg_claninvites.remove(i);
            rpg::clan_disband(id);
            conoutf("rpg: clan '%s' disbanded by %s", cname, cq->name);
            return true;
        }
        // ---- /clanroster ----
        if(!strcmp(cmd, "/clanroster") || !strcmp(cmd, "/clan"))
        {
            rpg_clan_roster(cq);
            return true;
        }
        // ---- /mute ----
        if(!strcmp(cmd, "/mute"))
        {
            if(!args[0]) { rpg_qmsg(cq, "Usage: /mute <name> (toggles)"); return true; }
            if(!strcmp(args, cq->name)) { rpg_qmsg(cq, "You can't mute yourself."); return true; }
            bool now = rpg::togglemute(cq->name, args);
            defformatstring(m)("%s %s.", args, now ? "muted — their World/Clan lines will no longer reach you" : "unmuted");
            rpg_qmsg(cq, m);
            return true;
        }
        // ---- /block ----
        if(!strcmp(cmd, "/block"))
        {
            if(!args[0]) { rpg_qmsg(cq, "Usage: /block <name> (toggles; block = mute + no party invites)"); return true; }
            if(!strcmp(args, cq->name)) { rpg_qmsg(cq, "You can't block yourself."); return true; }
            bool now = rpg::toggleblock(cq->name, args);
            // block implies mute: enabling a block ensures the mute is on too;
            // removing a block leaves the mute list untouched.
            if(now && !rpg::ismuted(cq->name, args)) rpg::togglemute(cq->name, args);
            defformatstring(m)("%s %s.", args, now ? "blocked — muted and cannot party-invite you" : "unblocked");
            rpg_qmsg(cq, m);
            return true;
        }
        // ---- /report ----
        if(!strcmp(cmd, "/report"))
        {
            if(!args[0]) { rpg_qmsg(cq, "Usage: /report <name> [reason]"); return true; }
            string target, reason;
            const char *rsp = strchr(args, ' ');
            if(rsp) { copystring(target, args, min(int(rsp - args) + 1, (int)sizeof(target))); copystring(reason, rsp + 1); }
            else { copystring(target, args); reason[0] = '\0'; }
            // server log (stdout + logfile via conoutf)
            if(reason[0]) conoutf("rpg: REPORT by %s against %s: %s", cq->name, target, reason);
            else conoutf("rpg: REPORT by %s against %s (no reason given)", cq->name, target);
            defformatstring(m)("Report against %s logged. Thank you.", target);
            rpg_qmsg(cq, m);
            return true;
        }
        defformatstring(m)("Unknown command '%s'. (/phelp for the list)", cmd);
        rpg_qmsg(cq, m);
        return true;
    }


    // --- RPG: mob behaviour tracking for the quest champion (Oct 20) ---
    // The champion is not an ambient-table mob, so its home/full-HP and
    // engagement live here; rpg_behaviortick() applies the same leash +
    // reset rules to it as to the ambient spawns.
    static void rpg_sendmobhome(clientinfo *mi, const vec &home)
    {
        // N_SPAWNSTATE carries no position: clients park remote ents at
        // a player spawn, and the OWNING client then simulates the mob
        // from there (the spawn-convergence bug, Oct 20). Broadcast the
        // anchor so every client - above all the owner - places and
        // holds the mob at its spawn point. Additive; stock modes never
        // send this.
        sendf(-1, 1, "ri5", N_RPG_MOBHOME, mi->clientnum, (int)home.x, (int)home.y, (int)home.z);
    }

    static vec rpg_champhome(0, 0, 0);
    static int rpg_champfullhp = 0;
    static int rpg_champfoe = -1;
    static int rpg_champlfight = 0;
    static bool rpg_champengaged = false;

    static void rpg_spawnchampion(clientinfo *cq)
    {
        // reuse the live champion if there is one (e.g. player relogged mid-quest)
        int ccn = rpg::championcn();
        if(ccn >= 0)
        {
            loopv(clients)
                if(clients[i]->clientnum == ccn && clients[i]->state.state == CS_ALIVE &&
                   clients[i]->state.ai.type == ai::AI_TYPE_MONSTER) return;
            rpg::clearchampioncn();
        }
        clientinfo *mi = aiman::addai(50, -1, ai::AI_TYPE_MONSTER);
        if(!mi) { rpg_qmsg(cq, "Elder Maren: ...the champion does not answer. (no AI slot)"); return; }
        makeInfected(mi, monster::getRandomType(), false);
        copystring(mi->name, "Rotting Champion", MAXNAMELEN+1);
        mi->state.state = CS_ALIVE;
        aiman::reinitai(mi);
        // champion tuning: 400 HP (roughly 3x a normal zombie). Damage stays
        // per-type (standard) — per-instance damage would need AI changes.
        mi->state.maxhealth = mi->state.health = 400;
        const rpg::npcdef *nd = rpg::npcat(0);
        if(nd) mi->state.o = vec(nd->x + 8.0f, nd->y + 8.0f, nd->z);
        else mi->state.o = cq->state.o;
        extern void sendspawn(clientinfo *ci);
        sendspawn(mi);
        rpg::setchampioncn(mi->clientnum);
        // mob behaviour (Oct 20): anchor the champion where it rose and
        // remember its real full HP from the settled spawn pipeline.
        rpg_sendmobhome(mi, mi->state.o);
        rpg_champhome = mi->state.o;
        rpg_champfullhp = mi->state.maxhealth;
        rpg_champengaged = false; rpg_champfoe = -1; rpg_champlfight = 0;
        rpg_qmsg(cq, "The Rotting Champion has risen near the village!");
        conoutf("rpg: quest champion spawned for '%s' (hp 400)", cq->name);
    }

    // --- RPG: Shattered Shield ambient monster spawns (world map rpg_01) ---
    // Server-authoritative: the server owns one AI monster per spawn point and
    // respawns it on a timer after death. Dead monsters are respawned IN PLACE
    // (same AI slot) so bot slots never leak (MAXBOTS = 32); if a slot was
    // reaped (owner left, map changed) the spawner falls back to a fresh one.
    #define RPG_AMBIENT_RESPAWN_MS 60000
    struct rpg_ambientrec { int clientnum; int nextrespawn; int lastfight; int foecn; int fullhp; bool engaged; };
    static vector<rpg_ambientrec> rpg_ambients;
    static string rpg_ambientmap;

    static int rpg_resolvespawntype(const char *type)
    {
        // Named bestiary types ("imp", "puglin") resolve by their class name
        // in the monster table. "zombie" (or anything unmatched) keeps the
        // original behavior: a random zombie variant. The named types carry
        // freq 0 in monster.cpp, so they never leak into that random pool;
        // they spawn only where the RPG spawn table names them.
        if(type && type[0])
        {
            loopi(monster::NUMMONSTERTYPES)
                if(!strcmp(monster::getMonsterType(i).classInfo.name, type)) return i;
        }
        return monster::getRandomType();
    }

    static clientinfo *rpg_findambientcn(int cn)
    {
        loopv(clients) if(clients[i]->clientnum == cn) return clients[i];
        return NULL;
    }

    static bool rpg_ambientalive(rpg_ambientrec &rec)
    {
        if(rec.clientnum < 0) return false;
        clientinfo *mi = rpg_findambientcn(rec.clientnum);
        return mi && mi->state.ai.type == ai::AI_TYPE_MONSTER && mi->state.state == CS_ALIVE;
    }

    static void rpg_spawnsingleambient(int idx)
    {
        const rpg::spawndef *sp = rpg::spawnpoint(idx);
        if(!sp) return;
        clientinfo *mi = aiman::addai(50, -1, ai::AI_TYPE_MONSTER);
        if(!mi) { conoutf("rpg: ambient spawn %d (%s): no free AI slot", idx, sp->area); return; }
        int mtype = rpg_resolvespawntype(sp->type);
        makeInfected(mi, mtype, false);
        copystring(mi->name, monster::getMonsterType(mtype).classInfo.name, MAXNAMELEN+1);
        mi->state.state = CS_ALIVE;
        aiman::reinitai(mi); // N_INITAI broadcast (reassign wipes position to sentinel)
        mi->state.o = vec(sp->x, sp->y, sp->z + 2.0f);
        extern void sendspawn(clientinfo *ci);
        sendspawn(mi); // spawnstate resets HP to the type default (180 for zombies)
        rpg_ambients[idx].clientnum = mi->clientnum;
        rpg_ambients[idx].nextrespawn = 0;
        rpg_sendmobhome(mi, vec(sp->x, sp->y, sp->z));
        rpg_ambients[idx].fullhp = mi->state.maxhealth; // settled by sendspawn (type HP)
        rpg_ambients[idx].engaged = false; rpg_ambients[idx].foecn = -1;
        conoutf("rpg: ambient spawn %d (%s): '%s' at (%.0f, %.0f, %.0f)", idx, sp->area, mi->name, sp->x, sp->y, sp->z);
    }

    static void rpg_respawnsingleambient(int idx)
    {
        const rpg::spawndef *sp = rpg::spawnpoint(idx);
        clientinfo *mi = rpg_findambientcn(rpg_ambients[idx].clientnum);
        if(!mi || mi->state.ai.type != ai::AI_TYPE_MONSTER || mi->ownernum < 0)
        {
            // slot was reaped (owner left, map changed): fresh spawn instead
            rpg_ambients[idx].clientnum = -1;
            rpg_spawnsingleambient(idx);
            return;
        }
        mi->state.state = CS_ALIVE;
        mi->aireinit = 2;
        aiman::reinitai(mi); // N_INITAI broadcast (reassign wipes position to sentinel)
        mi->state.o = vec(sp->x, sp->y, sp->z + 2.0f);
        extern void sendspawn(clientinfo *ci);
        sendspawn(mi);
        rpg_ambients[idx].nextrespawn = 0;
        rpg_sendmobhome(mi, vec(sp->x, sp->y, sp->z));
        rpg_ambients[idx].fullhp = mi->state.maxhealth;
        rpg_ambients[idx].engaged = false; rpg_ambients[idx].foecn = -1;
        conoutf("rpg: ambient spawn %d (%s): respawned '%s'", idx, sp->area, mi->name);
    }

    void rpg_ambienttick()
    {
        // Only the world map gets ambient spawns (krpg is the PvP battleground).
        if(strcmp(smapname, "rpg_01"))
        {
            if(rpg_ambientmap[0]) { rpg_ambientmap[0] = '\0'; rpg_ambients.shrink(0); }
            return;
        }
        if(strcmp(rpg_ambientmap, smapname))
        {
            // (re)entered the world map: fresh tracking (changemap wiped all AI).
            copystring(rpg_ambientmap, smapname);
            rpg_ambients.shrink(0);
            int n = rpg::spawnpointcount();
            loopi(n) { rpg_ambientrec &r = rpg_ambients.add(); r.clientnum = -1; r.nextrespawn = 1; r.lastfight = 0; r.foecn = -1; r.fullhp = 0; r.engaged = false; }
            conoutf("rpg: ambient spawns armed for rpg_01 (%d points)", n);
        }
        // AI slots need an owner: only spawn while a live player is connected
        // (ownerless AI is immediately deleted by reinitai).
        bool haveplayer = false;
        loopv(clients)
        {
            clientinfo *ci = clients[i];
            if(ci->connectionState == CONNECTION_STATE_CONNECTED && ci->state.ai.type == ai::AI_TYPE_NONE && ci->state.state == CS_ALIVE) { haveplayer = true; break; }
        }
        if(!haveplayer) return;
        loopv(rpg_ambients)
        {
            if(rpg_ambientalive(rpg_ambients[i])) continue;
            if(rpg_ambients[i].nextrespawn == 0)
                rpg_ambients[i].nextrespawn = totalmillis + RPG_AMBIENT_RESPAWN_MS;
            if(totalmillis >= rpg_ambients[i].nextrespawn)
            {
                if(rpg_ambients[i].clientnum < 0) rpg_spawnsingleambient(i);
                else rpg_respawnsingleambient(i);
            }
        }
    }


    // --- RPG: Shattered Shield mob behaviour (Oct 20 design rules) ---
    // Server side of the behaviour rules. The client AI owns movement
    // (passive / retaliate / leash walk-home, in MonsterAi::rpgthink);
    // the server owns the RESET: once an engaged mob is back inside its
    // roam radius with the fight over — or hopelessly dragged out — the
    // fight is wiped: full health restored through the same reinit +
    // sendspawn pipeline as a respawn, so the owning client restarts
    // the mob fresh at its anchor with threat wiped. Engagement itself
    // is stamped by dodamage (rpg_behaviordamage below).
    static void rpg_behavreset(clientinfo *mi, const vec &home)
    {
        // Mirror of rpg_respawnsingleambient's revive, minus the death:
        // reinit (N_INITAI) + reposition + sendspawn (full type HP, new
        // lifesequence — the owning client reads that as a fresh life).
        mi->state.state = CS_ALIVE;
        mi->aireinit = 2;
        aiman::reinitai(mi);
        mi->state.o = vec(home.x, home.y, home.z + 2.0f);
        extern void sendspawn(clientinfo *ci);
        sendspawn(mi);
        rpg_sendmobhome(mi, home);
    }

    static void rpg_behavfixhp(clientinfo *mi, int fullhp)
    {
        // sendspawn's spawnstate settles health at the TYPE default; a mob
        // whose full HP is per-instance (the 400 HP champion) needs it put
        // back, and every client told. A negative-damage N_DAMAGE is the
        // engine's heal sync: clients set the ent's health from the packet.
        if(fullhp <= 0 || mi->state.health == fullhp) return;
        int heal = fullhp - mi->state.health;
        mi->state.health = mi->state.maxhealth = fullhp;
        sendf(-1, 1, "ri7", N_DAMAGE, mi->clientnum, mi->clientnum, -heal, mi->state.armour, mi->state.health, WEAP_FIST);
    }

    static void rpg_behaviordamage(clientinfo *monster, clientinfo *player)
    {
        if(!monster || !player) return;
        loopv(rpg_ambients) if(rpg_ambients[i].clientnum == monster->clientnum)
        {
            rpg_ambientrec &rec = rpg_ambients[i];
            rec.engaged = true;
            rec.lastfight = totalmillis;
            rec.foecn = player->clientnum;
            return;
        }
        if(monster->clientnum == rpg::championcn())
        {
            rpg_champengaged = true;
            rpg_champlfight = totalmillis;
            rpg_champfoe = player->clientnum;
        }
    }

    // One tracked mob's leash/reset evaluation; clears *engaged on a reset.
    static void rpg_behaveval(clientinfo *mi, const vec &home, int fullhp, int foecn, int lastfight, bool &engaged, const char *label)
    {
        if(!mi || mi->state.ai.type != ai::AI_TYPE_MONSTER || mi->state.state != CS_ALIVE || mi->ownernum < 0) { engaged = false; return; }
        clientinfo *foe = rpg_findambientcn(foecn);
        bool foegone = !foe || foe->state.ai.type != ai::AI_TYPE_NONE || foe->state.state != CS_ALIVE;
        // Horizontal distances only: home z is a feet level, state.o an
        // eye-height origin - a 3D distance would carry a constant bias.
        float dhome = vec(mi->state.o.x - home.x, mi->state.o.y - home.y, 0).magnitude();
        float foedist = foegone ? 1e9f : vec(foe->state.o.x - home.x, foe->state.o.y - home.y, 0).magnitude();
        const float backstop = rpg::MOB_LEASH_RANGE + 15.0f; // the client leash (60) acts first; this is the authority net
        if(!foegone && (dhome > backstop || foedist > backstop))
        {
            rpg_behavreset(mi, home);
            rpg_behavfixhp(mi, fullhp);
            engaged = false;
            conoutf("rpgbehav: %s '%s' leash reset (backstop): mob %.0f / foe %.0f from home", label, mi->name, dhome, foedist);
        }
        else if(dhome <= rpg::MOB_ROAM_RADIUS && (foegone || (vec(foe->state.o.x - mi->state.o.x, foe->state.o.y - mi->state.o.y, 0).magnitude() > rpg::MOB_AGGRO_RANGE && totalmillis - lastfight > 5000)))
        {
            // The mob broke off and walked home, its foe is gone, or the
            // foe has genuinely disengaged (out of aggro range of the mob
            // and no combat for 5s): the fight is over — wipe it back to
            // a fresh, full-health mob. A foe still standing in range
            // keeps the fight (and the mob's damage) alive no matter how
            // long the bite cadence gaps.
            rpg_behavreset(mi, home);
            rpg_behavfixhp(mi, fullhp);
            engaged = false;
            conoutf("rpgbehav: %s '%s' reset at home: full hp %d, threat wiped", label, mi->name, fullhp);
        }
        else if(foegone && dhome > rpg::MOB_ROAM_RADIUS && totalmillis - lastfight > 5000)
        {
            // Stranded mid-field with nobody to fight (foe disconnected
            // mid-kite): send it home rather than leaving it drifted.
            rpg_behavreset(mi, home);
            rpg_behavfixhp(mi, fullhp);
            engaged = false;
            conoutf("rpgbehav: %s '%s' reset (stranded, foe gone)", label, mi->name);
        }
    }

    void rpg_behaviortick()
    {
        if(strcmp(smapname, "rpg_01")) return;
        loopv(rpg_ambients)
        {
            rpg_ambientrec &rec = rpg_ambients[i];
            if(!rec.engaged) continue;
            const rpg::spawndef *sp = rpg::spawnpoint(i);
            if(!sp) { rec.engaged = false; continue; }
            rpg_behaveval(rpg_findambientcn(rec.clientnum), vec(sp->x, sp->y, sp->z), rec.fullhp, rec.foecn, rec.lastfight, rec.engaged, sp->area);
            if(!rec.engaged) rec.foecn = -1;
        }
        if(rpg_champengaged)
        {
            int ccn = rpg::championcn();
            clientinfo *mi = ccn >= 0 ? rpg_findambientcn(ccn) : NULL;
            if(!mi) rpg_champengaged = false;
            else
            {
                rpg_behaveval(mi, rpg_champhome, rpg_champfullhp, rpg_champfoe, rpg_champlfight, rpg_champengaged, "champion");
                if(!rpg_champengaged) rpg_champfoe = -1;
            }
        }
    }

    // CLEANUP item 6: Tomas - the errand's delivery target. All validation is
    // server-side like Maren's chain: the tonic is counted and removed here,
    // XP and the reward are granted here, and completion persists through
    // questsetcomplete like any other quest.

    // --- RPG: projectile spells (Oct 21, additive) ---
    // Target-following bolts (RANGED DELIVERY DESIGN v1). Server-authoritative:
    // range + line-of-sight are validated at release inside rpg_cast (shared
    // with instant spells); the bolt then tracks the target's LIVE position
    // each tick. Moving after release does not dodge; the bolt fizzles only
    // if the target dies or disconnects first. Clients render the flight
    // from N_RPG_PROJ / N_RPG_PROJHIT / N_RPG_PROJFIZZLE.
    struct rpg_projectile
    {
        int id;
        int castercn;
        int targetcn;
        int ability;   // ability id, selects the client visual style
        int damage;    // computed at release (base + 2*Knowledge, crit rolled)
        int level;     // caster level snapshot (DoT attr scaling at apply time)
        vec pos;       // current position, world units
        int born;      // totalmillis at spawn (5s safety timeout)
    };
    static vector<rpg_projectile> rpg_projectiles;
    static int rpg_projnextid = 1;
    static const float RPG_PROJ_SPEED = 130.0f;  // u/s: 40u range in ~0.3s
    static const float RPG_PROJ_HITDIST = 6.0f;  // torso-proximity hit radius

    static clientinfo *rpg_projfind(int cn, bool monsteronly)
    {
        loopv(clients)
        {
            clientinfo *t = clients[i];
            if(t->clientnum != cn) continue;
            if(monsteronly && t->state.ai.type != ai::AI_TYPE_MONSTER) return NULL;
            return t;
        }
        return NULL;
    }

    static void rpg_applydot(clientinfo *caster, clientinfo *target, int ability, int level);

    static void rpg_projectiletick()
    {
        if(rpg_projectiles.empty() || !rpg::connected()) return;
        float dt = curtime/1000.0f;
        if(dt <= 0) return;
        loopvrev(rpg_projectiles)
        {
            rpg_projectile &pr = rpg_projectiles[i];
            clientinfo *t = rpg_projfind(pr.targetcn, true);
            bool gone = !t || t->state.state != CS_ALIVE || t->state.health <= 0;
            bool timeout = totalmillis - pr.born > 5000;
            if(gone || timeout)
            {
                sendf(-1, 1, "ri2", N_RPG_PROJFIZZLE, pr.id);
                rpg_projectiles.remove(i);
                continue;
            }
            vec top(t->state.o); top.z += 4; // torso height, matches the LoS check
            vec d(top); d.sub(pr.pos);
            float dist = d.magnitude();
            if(dist <= RPG_PROJ_HITDIST)
            {
                clientinfo *caster = rpg_projfind(pr.castercn, false);
                vec push(t->state.o); push.sub(pr.pos); push.z = 0;
                if(push.squaredlen() > 0.01f) push.normalize();
                int hpbefore = t->state.health;
                dodamage(t, caster, pr.damage, WEAP_FIST, push, false);
                sendf(-1, 1, "ri4", N_RPG_PROJHIT, pr.id, pr.targetcn, pr.damage);
                const rpg::abilitydef &ab = rpg::ability(pr.ability);
                conoutf("rpg: projectile %s hits '%s' for %d (%d -> %d hp)",
                        ab.name, t->name, pr.damage, hpbefore, t->state.health);
                rpg_applydot(caster, t, pr.ability, pr.level); // Venom Shot poison
                rpg_projectiles.remove(i);
                continue;
            }
            d.normalize();
            d.mul(min(dist, RPG_PROJ_SPEED*dt));
            pr.pos.add(d);
        }
    }

    // --- RPG: damage-over-time (v0.1.8, Oct 22; Venom Shot + Rend) ---
    // Server-authoritative DoTs. The server owns the per-target list and
    // ticks it in serverupdate(); damage goes through dodamage() so kills
    // feed the XP hook exactly like direct hits. Re-applying the same
    // ability's DoT to a target REFRESHES it (duration reset, no stacking).
    // Ticks stop when the target dies, disconnects, or the duration ends.
    struct rpg_dot
    {
        int targetcn;
        int casterncn;  // -1 when the caster is gone; dodamage accepts NULL
        int ability;    // ability id (logging + refresh matching)
        int tickdmg;    // damage per tick, computed at apply (base + attr)
        int tickms;     // ms between ticks
        int ticksleft;
        int nexttick;   // totalmillis timestamp of the next tick
    };
    static vector<rpg_dot> rpg_dots;

    static void rpg_applydot(clientinfo *caster, clientinfo *target, int ability, int level)
    {
        const rpg::dotdef *dd = rpg::abilitydot(ability);
        if(!dd || !target) return;
        int ticks = dd->durationms / dd->tickms;
        if(ticks < 1) ticks = 1;
        loopv(rpg_dots) if(rpg_dots[i].targetcn == target->clientnum && rpg_dots[i].ability == ability)
        {
            // Refresh: reset the clock, keep the caster/level snapshot.
            rpg_dots[i].ticksleft = ticks;
            rpg_dots[i].nexttick = totalmillis + dd->tickms;
            return;
        }
        rpg_dot d;
        d.targetcn = target->clientnum;
        d.casterncn = caster ? caster->clientnum : -1;
        d.ability = ability;
        d.tickdmg = dd->tickdmg + (dd->attr >= 0 ? (caster ? rpg::effectiveattr(caster->name, level, dd->attr) : rpg::attrscore(level, dd->attr)) : 0);
        d.tickms = dd->tickms;
        d.ticksleft = ticks;
        d.nexttick = totalmillis + dd->tickms;
        rpg_dots.add(d);
        const rpg::abilitydef &ab = rpg::ability(ability);
        conoutf("rpg: %s applies to '%s' (%ds)", ab.name, target->name, dd->durationms/1000);
    }

    static void rpg_dottick()
    {
        if(rpg_dots.empty() || !rpg::connected()) return;
        loopvrev(rpg_dots)
        {
            rpg_dot &d = rpg_dots[i];
            clientinfo *t = rpg_projfind(d.targetcn, true);
            bool gone = !t || t->state.state != CS_ALIVE || t->state.health <= 0;
            if(gone || d.ticksleft <= 0) { rpg_dots.remove(i); continue; }
            if(totalmillis < d.nexttick) continue;
            d.nexttick += d.tickms;
            d.ticksleft--;
            clientinfo *caster = d.casterncn >= 0 ? rpg_projfind(d.casterncn, false) : NULL;
            vec nopush(0, 0, 0); // DoT: no knockback
            int hpbefore = t->state.health;
            dodamage(t, caster, d.tickdmg, WEAP_FIST, nopush, false);
            const rpg::abilitydef &ab = rpg::ability(d.ability);
            conoutf("rpg: %s ticks '%s' for %d (%d -> %d hp, %d ticks left)",
                    ab.name, t->name, d.tickdmg, hpbefore, t->state.health, d.ticksleft);
            if(d.ticksleft <= 0) rpg_dots.remove(i);
        }
    }

    void rpg_cast(clientinfo *cq, int ability, int yaw, int advisory)
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
        clientinfo *target = rpg_findtarget(cq, ab, yaw, advisory);
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
        // Class gating (Oct 21): abilities with a reqclass are restricted to
        // the listed archetypes. Basic Attack (NULL) stays universal.
        // Matrix: Basic Attack = all; Power Strike = Warrior;
        // Spark/Arcane/Ice Bolt = Magi; Arrow Shot = Ranger.
        if(ab.reqclass)
        {
            char arch[32]; arch[0] = '\0';
            rpg::chararchetype(cq->name, arch, sizeof(arch));
            bool allowed = false;
            // reqclass is a space-separated list of display names
            const char *p = ab.reqclass;
            char token[32];
            while(*p)
            {
                while(*p == ' ') p++;
                if(!*p) break;
                int i = 0;
                while(*p && *p != ' ' && i < 31) token[i++] = *p++;
                token[i] = '\0';
                if(!strcmp(arch, token)) { allowed = true; break; }
            }
            if(!allowed)
            {
                defformatstring(gmsg)("\f2%s: \f7requires %s.", ab.name, ab.reqclass);
                sendf(cq->clientnum, 1, "ris", N_SERVMSG, gmsg);
                return;
            }
        }
        rpg::spendmana(cq->name, ab.manacost);
        vec push(target->state.o);
        push.sub(cq->state.o);
        push.z = 0;
        if(push.squaredlen() > 0.01f) push.normalize();
        int hpbefore = target->state.health;
        // --- RPG: Shattered Shield stats (milestone step 5, additive; see src/rpg/stats.h) ---
        // Ability damage derives from attributes (automatic growth from level).
        // Melee: ability base + weapon + Strength (Power Strike's base is 35,
        // the basic swing's is 0 - CLEANUP item 8). Spell: 50 + 2*Knowledge.
        // RPG maps only.
        // Melee abilities can crit: the SERVER rolls (never the client) and the
        // crit is announced in the cast message so it is harness-observable.
        int castdmg = ab.damage;
        bool crit = false;
        if(rpg::mapactive(smapname))
        {
            if(ab.melee) castdmg = ab.damage + rpg::weapondamage(cq->name) + rpg::effectiveattr(cq->name, level, rpg::ATTR_STRENGTH); // Phase 2: Strength buff
            // Spell damage: ability base + 2xKnowledge. (stat_sparkbolt() is
            // exactly this with base 50; the table now carries each spell's
            // own base so the formula stays uniform.)
            // Arrow Shot (physical pierce): base + 2xDexterity instead.
            // Venom Shot (poison arrow, v0.1.8): same Dex scaling -- it's
            // an arrow, not a spell.
            else if(!strcmp(ab.damagetype, "pierce") || !strcmp(ab.damagetype, "poison")) castdmg = ab.damage + 2*rpg::effectiveattr(cq->name, level, rpg::ATTR_DEXTERITY);
            else castdmg = ab.damage + 2*rpg::effectiveattr(cq->name, level, rpg::ATTR_KNOWLEDGE);
            if(ab.melee && (rpg_forcecrit || rpg::stat_rollcrit(rpg::stat_critchance(level, rpg::gearbonus(cq->name, rpg::ATTR_DEXTERITY) + rpg::buffbonus(cq->name, rpg::ATTR_DEXTERITY)))))
            {
                crit = true;
                castdmg = int(castdmg*rpg::stat_critmult(level, rpg::gearbonus(cq->name, rpg::ATTR_CUNNING) + rpg::buffbonus(cq->name, rpg::ATTR_CUNNING)) + 0.5f);
            }
            rpg_forcecrit = false; // one-shot: the hook never leaks into real casts
        }
        // Projectile delivery (Oct 21): range + LoS were already validated at
        // release above (shared with instant spells). Spawn the bolt; the
        // server tick moves it and applies damage on arrival.
        if(ab.delivery == rpg::DELIVERY_PROJECTILE)
        {
            rpg_projectile pr;
            pr.id = rpg_projnextid++;
            pr.castercn = cq->clientnum;
            pr.targetcn = target->clientnum;
            pr.ability = ability;
            pr.damage = castdmg;
            pr.level = level;
            // v0.1.11 fix 12: spawn the bolt at the staff tip, not the chest,
            // so it looks like the Magi casts from the staff. Staff is held
            // forward/up when casting: push out along the facing direction.
            pr.pos = cq->state.o; pr.pos.z += 18; // ~staff/hand height
            vec staffdir(push); staffdir.mul(10); pr.pos.add(staffdir);
            pr.born = totalmillis;
            rpg_projectiles.add(pr);
            sendf(-1, 1, "ri8", N_RPG_PROJ, pr.id, pr.castercn, pr.targetcn, ability,
                  int(pr.pos.x*DMF), int(pr.pos.y*DMF), int(pr.pos.z*DMF));
            conoutf("rpg: '%s' casts %s at '%s' (projectile %d, %d dmg)",
                    cq->name, ab.name, target->name, pr.id, castdmg);
            defformatstring(cmsg)("\f2%s \f7flies toward %s.", ab.name, target->name);
            sendf(cq->clientnum, 1, "ris", N_SERVMSG, cmsg);
            // push fresh mana + cooldown to the caster for the RPG HUD
            rpg::getmana(cq->name, mana, maxmana);
            sendf(cq->clientnum, 1, "ri3", N_RPG_MANA, mana, maxmana);
            sendf(cq->clientnum, 1, "ri3", N_RPG_CD, ability, ab.cooldownms);
            return;
        }
        dodamage(target, cq, castdmg, WEAP_FIST, push, false); // real plumbing: kills feed the XP hook
        rpg_applydot(cq, target, ability, level); // Rend bleed (no-op for non-DoT abilities)
        conoutf("rpg: '%s' casts %s on '%s' for %d%s (%d -> %d hp)", cq->name, ab.name, target->name, castdmg, crit ? " CRIT" : "", hpbefore, target->state.health);
        defformatstring(msg)("\f2%s %s\f7hits %s for %d damage.", ab.name, crit ? "\f3CRIT " : "", target->name, castdmg);
        sendf(cq->clientnum, 1, "ris", N_SERVMSG, msg);
        // push fresh mana + cooldown to the caster for the RPG HUD
        rpg::getmana(cq->name, mana, maxmana);
        sendf(cq->clientnum, 1, "ri3", N_RPG_MANA, mana, maxmana);
        sendf(cq->clientnum, 1, "ri3", N_RPG_CD, ability, ab.cooldownms);
    }

    // --- RPG: Shattered Shield inventory (milestone step 4, additive; see src/rpg) ---
    // Server-authoritative inventory operations. The client only names an item
    // id / slot; every validation happens here. Equipment effects never leak
    // into FPS modes: the RPG map gate (rpg::mapactive) is checked on every op.

    static bool rpg_checkinv(clientinfo *cq) // shared validation for inventory ops
    {
        if(!rpg::connected()) return false;
        if(!cq || cq->state.ai.type != ai::AI_TYPE_NONE) return false;
        int level = 1, xp = 0;
        if(!rpg::getstats(cq->name, level, xp)) return false; // character must be loaded
        if(!rpg::mapactive(smapname)) return false;           // RPG maps only
        return true;
    }

    static void rpg_syncinv(clientinfo *cq) // push inventory+equipment state to one client
    {
        if(!rpg::connected()) return;
        rpg::itemstack inv[64];
        int n = 0, level = 1, xp = 0;
        if(!rpg::getinventory(cq->name, inv, 64, n)) return;
        rpg::getstats(cq->name, level, xp);
        string invs, eqs;
        invs[0] = eqs[0] = '\0';
        loopi(n)
        {
            defformatstring(e)("%s:%d;", rpg::item(inv[i].item).id, inv[i].count);
            concatstring(invs, e);
        }
        loopi(rpg::SLOT_COUNT)
        {
            int id = rpg::equippeditem(cq->name, i);
            if(id >= 0)
            {
                defformatstring(e)("%d:%s;", i, rpg::item(id).id);
                concatstring(eqs, e);
            }
        }
        sendf(cq->clientnum, 1, "riiiss", N_RPG_INV, level, xp, invs, eqs);
        // v0.1.23 currency: copper sync alongside inventory (total copper int)
        // v0.1.25: "ri2" (type + payload int) - the old "ri" dropped the payload,
        // so the client always read 0 and inventory showed 0c (v0.1.24 item 2).
        sendf(cq->clientnum, 1, "ri2", N_RPG_GOLD, rpg::getcopper(cq->name));
        rpg_syncquest(cq); // quest chain v1: tracker + radar stay in sync
        rpg_syncclan(cq); // clan system v1: membership sync
    }

    static void rpg_sendvwep(clientinfo *cq) // broadcast this client's equipped weapon model
    {
        const char *path = rpg::equippedvwep(cq->name);
        sendf(-1, 1, "riis", N_RPG_VWEP, cq->clientnum, path ? path : "");
        conoutf("rpg: vwep for '%s' -> %s", cq->name, path && path[0] ? path : "(model default)");
    }

    void rpg_equip(clientinfo *cq, const char *itemid)
    {
        if(!rpg_checkinv(cq)) return;
        if(cq->state.state != CS_ALIVE) { sendf(cq->clientnum, 1, "ris", N_SERVMSG, "\f2You must be alive to equip."); return; }
        int id = rpg::finditem(itemid);
        if(rpg::equipitem(cq->name, itemid))
        {
            defformatstring(msg)("\f2%s \f7equipped.", rpg::itemname(id));
            sendf(cq->clientnum, 1, "ris", N_SERVMSG, msg);
            rpg_sendvwep(cq); // weapon model swap is visible to everyone
            rpg_syncinv(cq);
        }
        else
        {
            const char *deny = rpg::equipdeny();
            defformatstring(msg)("\f2%s", deny && deny[0] ? deny : "Cannot equip that.");
            sendf(cq->clientnum, 1, "ris", N_SERVMSG, msg);
        }
    }

    void rpg_unequip(clientinfo *cq, int slot)
    {
        if(!rpg_checkinv(cq)) return;
        if(rpg::unequipslot(cq->name, slot))
        {
            rpg_sendvwep(cq);
            rpg_syncinv(cq);
        }
        else sendf(cq->clientnum, 1, "ris", N_SERVMSG, "\f2Nothing equipped there.");
    }

    // Crafting Phase 2: buff duration in seconds on the server clock. Default 3600 = 1 hour
    // (approved). Tunable for headless expiry tests (set to a few seconds); the shipped
    // default never changes. rpg_use passes this to rpg::applybuff per use.
    // Declared here (before rpg_use) because VARs are only visible after their declaration point.
    VAR(rpg_buff_duration_sec, 1, 3600, 86400);

    void rpg_use(clientinfo *cq, const char *itemid)
    {
        if(!rpg_checkinv(cq)) return;
        if(cq->state.state != CS_ALIVE) { sendf(cq->clientnum, 1, "ris", N_SERVMSG, "\f2You must be alive to use that."); return; }
        int id = rpg::finditem(itemid);
        // Crafting Phase 2: buff consumables branch BEFORE the heal path (buff items have
        // heal 0, so usestack would reject them). Same-type re-use refreshes the timer via
        // applybuff; it never stacks. Vitality raises the HP cap only (no silent heal).
        if(id >= 0 && rpg::item(id).buffattr >= 0)
        {
            const rpg::itemdef &bd = rpg::item(id);
            if(!rpg::removeitem(cq->name, itemid, 1)) { sendf(cq->clientnum, 1, "ris", N_SERVMSG, "\f2You don't have that."); return; }
            int mag = rpg::buffmagnitude(bd.tier);
            if(!rpg::applybuff(cq->name, bd.buffattr, mag, rpg_buff_duration_sec))
            {
                rpg::additem(cq->name, itemid, 1); // refund: apply failed (character not cached)
                sendf(cq->clientnum, 1, "ris", N_SERVMSG, "\f2Cannot use that.");
                return;
            }
            char durbuf[32];
            if(rpg_buff_duration_sec >= 3600) snprintf(durbuf, sizeof(durbuf), "%d hour", rpg_buff_duration_sec/3600);
            else snprintf(durbuf, sizeof(durbuf), "%d seconds", rpg_buff_duration_sec);
            defformatstring(bmsg)("%s used: +%d %s for %s.", bd.name, mag, rpg::attrname(bd.buffattr), durbuf);
            rpg_qmsg(cq, bmsg);
            conoutf("rpg: '%s' gains +%d %s from %s (tier %s, %ds)", cq->name, mag, rpg::attrname(bd.buffattr), bd.name, rpg::crafttiername(bd.tier), rpg_buff_duration_sec);
            if(bd.buffattr == rpg::ATTR_VITALITY && rpg::mapactive(smapname))
            {
                int lvl = 1, xp = 0;
                if(rpg::getstats(cq->name, lvl, xp))
                {
                    int maxhp = rpg::stat_maxhp(lvl, rpg::buffbonus(cq->name, rpg::ATTR_VITALITY) + rpg::gearbonus(cq->name, rpg::ATTR_VITALITY));
                    cq->state.maxhealth = maxhp; // cap rises; current HP unchanged (no silent heal)
                    sendf(cq->clientnum, 1, "ri2", N_RPG_HPMAX, maxhp);
                }
            }
            rpg_syncinv(cq);
            rpg_syncbuffs(cq); // bundle: HUD buff row gains the new icon
            return;
        }
        int heal = rpg::usestack(cq->name, itemid);
        if(heal < 0) { sendf(cq->clientnum, 1, "ris", N_SERVMSG, "\f2Cannot use that."); return; }
        gamestate &gs = cq->state;
        gs.health = min(gs.maxhealth, gs.health + heal);
        defformatstring(msg)("\f2%s \f7used: +%d health.", rpg::itemname(id), heal);
        sendf(cq->clientnum, 1, "ris", N_SERVMSG, msg);
        conoutf("rpg: '%s' uses %s (+%d hp -> %d)", cq->name, rpg::itemname(id), heal, gs.health);
        rpg_syncinv(cq);
    }

    void rpg_dropitem(clientinfo *cq, const char *itemid)
    {
        if(!rpg_checkinv(cq)) return;
        if(cq->state.state != CS_ALIVE) return;
        int id = rpg::finditem(itemid);
        if(id < 0) { sendf(cq->clientnum, 1, "ris", N_SERVMSG, "\f2You don't have that."); return; }
        // strip it from the body first (it moves to the pack), then drop from the pack
        loopi(rpg::SLOT_COUNT) if(rpg::equippeditem(cq->name, i) == id) rpg::unequipslot(cq->name, i);
        if(!rpg::removeitem(cq->name, itemid, 1)) { sendf(cq->clientnum, 1, "ris", N_SERVMSG, "\f2You don't have that."); return; }
        int idx = rpg::spoilloot(id, cq->state.o.x, cq->state.o.y, cq->state.o.z);
        rpg::setlootborn(idx, totalmillis);
        sendf(-1, 1, "ri6", N_RPG_LOOT, idx, id,
              int(cq->state.o.x*DMF), int(cq->state.o.y*DMF), int(cq->state.o.z*DMF));
        defformatstring(msg)("\f2%s \f7dropped.", rpg::itemname(id));
        sendf(cq->clientnum, 1, "ris", N_SERVMSG, msg);
        conoutf("rpg: '%s' drops %s (drop #%d)", cq->name, rpg::itemname(id), idx);
        rpg_sendvwep(cq);
        rpg_syncinv(cq);
    }

    // v0.1.23 VENDOR: buy/sell (server-authoritative).
    // Buy: pay item value in copper, item goes to inventory. Vendor stock
    // is unlimited (no stock tracking in v1) — any known equippable item
    // can be bought. Sell: only equippable gear (weapons/armor, not
    // quest items, consumables, or materials) can be sold, for value/2.
    // Both sync inventory + copper to the client afterwards.
    void rpg_buy(clientinfo *cq, const char *itemid)
    {
        if(!rpg_checkinv(cq)) return;
        if(cq->state.state != CS_ALIVE) { sendf(cq->clientnum, 1, "ris", N_SERVMSG, "\f2You must be alive to trade."); return; }
        int id = rpg::finditem(itemid);
        if(id < 0) { sendf(cq->clientnum, 1, "ris", N_SERVMSG, "\f2Unknown item."); return; }
        const rpg::itemdef &d = rpg::item(id);
        if(d.slot == rpg::SLOT_NONE) { sendf(cq->clientnum, 1, "ris", N_SERVMSG, "\f2That cannot be bought."); return; }
        int price = d.value;
        if(price <= 0) { sendf(cq->clientnum, 1, "ris", N_SERVMSG, "\f2That has no price."); return; }
        if(!rpg::spendcopper(cq->name, price))
        {
            defformatstring(msg)("\f2Not enough copper (need %d).", price);
            sendf(cq->clientnum, 1, "ris", N_SERVMSG, msg);
            return;
        }
        if(!rpg::additem(cq->name, itemid, 1))
        {
            // Refund on failure (should not happen for known items).
            rpg::addcopper(cq->name, price);
            sendf(cq->clientnum, 1, "ris", N_SERVMSG, "\f2Purchase failed.");
            return;
        }
        defformatstring(msg)("\f2%s \f7bought for %d copper.", rpg::itemname(id), price);
        sendf(cq->clientnum, 1, "ris", N_SERVMSG, msg);
        conoutf("rpg: '%s' buys %s for %dc", cq->name, rpg::itemname(id), price);
        rpg_syncinv(cq);
    }
    // Crafting Phase 1 (Oct 27, Ed's settled design): server-validated.
    // craft_fail_lose is the future toggle for failed-combo item loss:
    // 0 (default, settled) = a failed combo does NOTHING and items are
    // NOT lost; 1 = a failed combo consumes the ingredients. The toggle
    // exists now so flipping it later is a variable change, not a rewrite.
    VAR(craft_fail_lose, 0, 0, 1);

    // Push the player's discovered-recipe lines to one client. Lines are
    // pre-formatted server-side ("1 Grave Moss + 1 Old Bone = Herbal
    // Tonic"); the client displays them verbatim and never resolves
    // anything. Unknown keys (data changed since discovery) are skipped.
    static void rpg_sendrecipes(clientinfo *cq)
    {
        if(!rpg::connected() || !cq) return;
        char keys[64][256];
        int n = rpg::recipelist(cq->name, keys, 64);
        // Phase 3: the discovered list outgrew the 260-char `string`
        // buffer on both ends (6 Phase 1/2 lines just fit; 13+ do not —
        // R1 showed 5 lines, the 5th cut mid-word). Send in chunks: an
        // empty message clears the client's list first, then each chunk
        // (kept under 200 chars) appends. Single-threaded sends keep a
        // burst atomic; no protocol change beyond clear + append.
        sendf(cq->clientnum, 1, "ris", N_RPG_RECIPES, "");
        string out;
        out[0] = '\0';
        loopi(n)
        {
            const rpg::craftrecipe *r = rpg::craftrecipebykey(keys[i]);
            if(!r) continue;
            defformatstring(line)("%d %s + %d %s = %s",
                r->ingcount[0], rpg::itemname(rpg::finditem(r->ing[0])),
                r->ingcount[1], rpg::itemname(rpg::finditem(r->ing[1])),
                rpg::itemname(rpg::finditem(r->out)));
            if(out[0] && (int)strlen(out) + 1 + (int)strlen(line) > 200)
            {
                sendf(cq->clientnum, 1, "ris", N_RPG_RECIPES, out);
                out[0] = '\0';
            }
            if(out[0]) concatstring(out, "\n");
            concatstring(out, line);
        }
        if(out[0]) sendf(cq->clientnum, 1, "ris", N_RPG_RECIPES, out);
    }

    // v0.1.34: craft result notice — the server sends the outcome BOTH as
    // a dedicated result message (shown as a persistent line inside the
    // craft window, so it cannot be missed when the window stays open)
    // AND as the usual System-tab line. Relying on the System tab alone
    // is why the user never saw whether a craft worked.
    static void rpg_craftresult(clientinfo *cq, int ok, const char *line)
    {
        if(!cq || !line) return;
        sendf(cq->clientnum, 1, "riis", N_RPG_CRAFTRESULT, ok, line);
        rpg_qmsg(cq, line);
    }

    void rpg_craft(clientinfo *cq, const char *itema, const char *itemb)
    {
        if(!rpg_checkinv(cq)) return;
        if(cq->state.state != CS_ALIVE) { sendf(cq->clientnum, 1, "ris", N_SERVMSG, "\f2You must be alive to craft."); return; }
        if(!itema || !itema[0] || !itemb || !itemb[0]) { rpg_craftresult(cq, 0, "Choose two ingredients first."); return; }
        int ida = rpg::finditem(itema), idb = rpg::finditem(itemb);
        if(ida < 0 || idb < 0) { rpg_craftresult(cq, 0, "Unknown ingredient."); return; }
        char key[256];
        if(!rpg::craftkey(itema, itemb, key, sizeof(key))) { rpg_craftresult(cq, 0, "Unknown ingredient."); return; }
        const rpg::craftrecipe *r = rpg::craftrecipebykey(key);
        // Phase 3: an already-enhanced piece + material resolves back to
        // its base recipe (enhanced ids carry ss_item_enhbase). The
        // recorded recipe is the base pair, so one recipe covers a base
        // piece and all of its enhanced tiers.
        const char *enhgear = NULL;
        if(!r)
        {
            const char *cands[2] = { itema, itemb };
            for(int k = 0; k < 2 && !r; k++)
            {
                int cid = rpg::finditem(cands[k]);
                if(cid < 0) continue;
                const rpg::itemdef &cd = rpg::item(cid);
                if(cd.gearstat < 0 || !cd.enhbase) continue;
                const char *other = cands[1 - k];
                char bkey[256];
                if(!rpg::craftkey(cd.enhbase, other, bkey, sizeof(bkey))) continue;
                const rpg::craftrecipe *br = rpg::craftrecipebykey(bkey);
                if(br && br->enhance) { r = br; enhgear = cands[k]; }
            }
        }
        // Which slotted id is the gear, which the material (enhance only).
        const char *gearid = NULL, *matid = NULL;
        if(r && r->enhance)
        {
            if(enhgear) { gearid = enhgear; matid = !strcmp(enhgear, itema) ? itemb : itema; }
            else if(rpg::item(ida).type == rpg::ITEM_ARMOR) { gearid = itema; matid = itemb; }
            else { gearid = itemb; matid = itema; }
        }
        // Count check FIRST: never lose (or consume) what you don't have,
        // in either the success or the failure branch. Same-id slots need
        // the folded count (2 of one item); a recipe's own counts govern
        // when one matches.
        if(r && r->enhance)
        {
            if(rpg::itemcount(cq->name, gearid) < 1 || rpg::itemcount(cq->name, matid) < 1)
            { rpg_craftresult(cq, 0, "You don't have those ingredients."); return; }
        }
        else if(r)
        {
            if(!strcmp(r->ing[0], r->ing[1]))
            {
                if(rpg::itemcount(cq->name, r->ing[0]) < r->ingcount[0] + r->ingcount[1])
                { rpg_craftresult(cq, 0, "You don't have those ingredients."); return; }
            }
            else if(rpg::itemcount(cq->name, r->ing[0]) < r->ingcount[0] ||
                    rpg::itemcount(cq->name, r->ing[1]) < r->ingcount[1])
            { rpg_craftresult(cq, 0, "You don't have those ingredients."); return; }
        }
        else if(ida == idb)
        {
            if(rpg::itemcount(cq->name, itema) < 2) { rpg_craftresult(cq, 0, "You don't have those ingredients."); return; }
        }
        else if(rpg::itemcount(cq->name, itema) < 1 || rpg::itemcount(cq->name, itemb) < 1)
        { rpg_craftresult(cq, 0, "You don't have those ingredients."); return; }

        if(!r)
        {
            // Failed combo: settled behavior is NOTHING happens, items
            // stay. The toggle branch consumes them instead (future).
            if(craft_fail_lose)
            {
                rpg::removeitem(cq->name, itema, ida == idb ? 2 : 1);
                if(ida != idb) rpg::removeitem(cq->name, itemb, 1);
                rpg_syncinv(cq);
                rpg_craftresult(cq, 0, "That combination doesn't craft anything. Your materials were lost.");
            }
            else rpg_craftresult(cq, 0, "That combination doesn't craft anything. Your materials are safe.");
            conoutf("rpg: '%s' failed craft %s + %s (fail_lose %d)", cq->name, itema, itemb, craft_fail_lose);
            return;
        }

        if(r->enhance)
        {
            // Phase 3 enhancement: transform the piece in place, consume
            // the material. Nothing is removed until every gate passes,
            // so a refused enhance loses nothing.
            int gearidx = rpg::finditem(gearid), matidx = rpg::finditem(matid);
            if(gearidx < 0 || matidx < 0 || rpg::item(matidx).type != rpg::ITEM_MATERIAL)
            { rpg_craftresult(cq, 0, "That combination doesn't craft anything. Your materials are safe."); return; }
            // No equipped guard is needed here, deliberately: the count
            // check above already refuses when no inventory copy exists
            // (an equipped-only piece is not in inventory), and when a
            // pack copy exists the transform below consumes exactly that
            // copy — the equipped piece is never touched. An explicit
            // equipped check would wrongly refuse the spare-copy case.
            const rpg::itemdef &gd = rpg::item(gearidx);
            const rpg::itemdef &md = rpg::item(matidx);
            // Tier gate: unenhanced (no gear bonus) takes any material
            // tier; an enhanced piece takes ONLY a strictly higher-tier
            // material, so it can never be downgraded by accident.
            if(gd.gearstat >= 0 && md.tier <= gd.tier)
            {
                defformatstring(ref)("That piece is already enhanced to %s. Only a higher-tier material can improve it.", rpg::crafttiername(gd.tier));
                rpg_craftresult(cq, 0, ref);
                conoutf("rpg: '%s' refused enhance %s (tier %s) with %s (tier %s): not strictly higher",
                        cq->name, gearid, rpg::crafttiername(gd.tier), matid, rpg::crafttiername(md.tier));
                return;
            }
            // Output tier equals the material tier directly (NOT the
            // two-material midpoint rule); validation guarantees every
            // tier variant exists, so this is the exact tier variant.
            const char *outid = rpg::craftoutfortier(r, md.tier);
            rpg::removeitem(cq->name, gearid, 1);
            rpg::removeitem(cq->name, matid, 1);
            if(!rpg::additem(cq->name, outid, 1))
            {
                // Refund on failure (should not happen for known items).
                rpg::additem(cq->name, gearid, 1);
                rpg::additem(cq->name, matid, 1);
                rpg_craftresult(cq, 0, "Crafting failed.");
                rpg_syncinv(cq);
                return;
            }
            bool isnew = !rpg::recipediscovered(cq->name, r->key);
            if(isnew) rpg::discoverrecipe(cq->name, r->key);
            defformatstring(msg)("You enhance %s into %s.", rpg::itemname(gearidx), rpg::itemname(rpg::finditem(outid)));
            rpg_craftresult(cq, 1, msg);
            conoutf("rpg: '%s' enhances %s into %s (tier %s) with %s%s", cq->name,
                    gearid, outid, rpg::crafttiername(md.tier), matid,
                    isnew ? " [recipe discovered]" : "");
            if(rpg::queststate(cq->name, 7) == 2)
            {
                rpg::questsetstage(cq->name, 7, 3);
                bool leveled = false;
                int newlevel = rpg::awardxp(cq->name, 100, &leveled);
                if(newlevel > 0 && leveled) rpg_levelupfx(cq, newlevel);
                rpg_qmsg(cq, "Quest complete: Necessity Is the Mother of Invention (+100 XP)");
            }
            rpg_syncinv(cq);
            rpg_syncquest(cq);
            rpg_sendrecipes(cq);
            return;
        }

        // Success: tier from ingredient tiers (Phase 1 materials are all
        // common, so this is tier 0 today; the math is the real one, and
        // rarer variants will flow through it when they exist).
        int tiers[2] = { rpg::item(rpg::finditem(r->ing[0])).tier, rpg::item(rpg::finditem(r->ing[1])).tier };
        unsigned int seed = (unsigned int)(totalmillis ^ (gamemillis << 1) ^ (cq->clientnum * 2654435761u));
        int tier = rpg::craft_tier(tiers, 2, seed);
        const char *outid = rpg::craftoutfortier(r, tier);
        if(!strcmp(r->ing[0], r->ing[1])) rpg::removeitem(cq->name, r->ing[0], r->ingcount[0] + r->ingcount[1]);
        else
        {
            rpg::removeitem(cq->name, r->ing[0], r->ingcount[0]);
            rpg::removeitem(cq->name, r->ing[1], r->ingcount[1]);
        }
        if(!rpg::additem(cq->name, outid, r->outcount))
        {
            // Refund on failure (should not happen for known items).
            if(!strcmp(r->ing[0], r->ing[1])) rpg::additem(cq->name, r->ing[0], r->ingcount[0] + r->ingcount[1]);
            else { rpg::additem(cq->name, r->ing[0], r->ingcount[0]); rpg::additem(cq->name, r->ing[1], r->ingcount[1]); }
            rpg_craftresult(cq, 0, "Crafting failed.");
            rpg_syncinv(cq);
            return;
        }
        bool isnew = !rpg::recipediscovered(cq->name, key);
        if(isnew) rpg::discoverrecipe(cq->name, key);
        defformatstring(msg)("You craft %s.", rpg::itemname(rpg::finditem(outid)));
        rpg_craftresult(cq, 1, msg);
        conoutf("rpg: '%s' crafts %s (tier %s) from %s + %s%s", cq->name,
                rpg::itemname(rpg::finditem(outid)), rpg::crafttiername(tier),
                r->ing[0], r->ing[1], isnew ? " [recipe discovered]" : "");
        // Quest 7 (Necessity Is the Mother of Invention) completes on the
        // first craft AFTER Tomas's walkthrough (stage 2). Crafts before
        // the walkthrough still succeed and still record the recipe, but
        // do not complete the quest: the lesson is the point.
        if(rpg::queststate(cq->name, 7) == 2)
        {
            rpg::questsetstage(cq->name, 7, 3);
            bool leveled = false;
            int newlevel = rpg::awardxp(cq->name, 100, &leveled);
            if(newlevel > 0 && leveled) rpg_levelupfx(cq, newlevel);
            rpg_qmsg(cq, "Quest complete: Necessity Is the Mother of Invention (+100 XP)");
        }
        rpg_syncinv(cq);
        rpg_syncquest(cq);
        rpg_sendrecipes(cq);
    }

    void rpg_sell(clientinfo *cq, const char *itemid)
    {
        if(!rpg_checkinv(cq)) return;
        if(cq->state.state != CS_ALIVE) { sendf(cq->clientnum, 1, "ris", N_SERVMSG, "\f2You must be alive to trade."); return; }
        int id = rpg::finditem(itemid);
        if(id < 0) { sendf(cq->clientnum, 1, "ris", N_SERVMSG, "\f2Unknown item."); return; }
        const rpg::itemdef &d = rpg::item(id);
        // v0.1.31 (Ed, Oct 27): equipped items must NOT sell. The old
        // code silently unequipped then sold, so Ed sold worn gear
        // without meaning to. Reject with a visible unequip-first
        // message instead (rpg_qmsg -> System tab; SERVMSG only
        // reaches the console, which is why the old rejections
        // looked like nothing happened).
        loopi(rpg::SLOT_COUNT) if(rpg::equippeditem(cq->name, i) == id)
        {
            rpg_qmsg(cq, "That is equipped. Unequip it first, then sell it.");
            return;
        }
        // v0.1.31 (Ed, Oct 27): non-gear is sellable too, not just
        // equippable weapons/armor. Consumables and materials all
        // carry a value (potion 10, moss 3, bone 2, horn 6, hide 4),
        // so they sell at half value like gear. Quest items are the
        // only exception: they have value 0, so price 0 still blocks
        // them, protecting quest progress. Crafting does not exist
        // yet (items.cfg is only the content data layer), so selling
        // is the route Ed approved, not crafting use.
        if(d.type == rpg::ITEM_QUEST) { rpg_qmsg(cq, "That cannot be sold."); return; }
        int price = rpg::sellprice(id);
        if(price <= 0) { rpg_qmsg(cq, "That has no sell value."); return; }
        // v0.1.34 (Ed, Oct 28): sell-count bug — the user sold items and
        // the stack count never went down, and reopening inventory did
        // not fix it, so this was not a display refresh glitch. The old
        // failure messages went to N_SERVMSG (console only, invisible in
        // game), so a silent sale failure looked like nothing happened.
        // All sell messages now go through rpg_qmsg (System tab, visible),
        // and copper is granted ONLY when the server-side count is
        // verified to have dropped by exactly one — if removal does not
        // stick, no copper is granted and the player is told, instead of
        // money appearing while the item stays.
        int beforecount = rpg::itemcount(cq->name, itemid);
        if(!rpg::removeitem(cq->name, itemid, 1)) { rpg_qmsg(cq, "You don't have that."); return; }
        int aftercount = rpg::itemcount(cq->name, itemid);
        if(aftercount != beforecount - 1)
        {
            rpg_qmsg(cq, "Sale did not go through. Your items are unchanged.");
            conoutf("rpg: sell verification failed for '%s' item %s (before %d, after %d)", cq->name, itemid, beforecount, aftercount);
            rpg_syncinv(cq);
            return;
        }
        rpg::addcopper(cq->name, price);
        defformatstring(msg)("%s sold for %d copper.", rpg::itemname(id), price);
        rpg_qmsg(cq, msg);
        conoutf("rpg: '%s' sells %s for %dc", cq->name, rpg::itemname(id), price);
        rpg_sendvwep(cq);
        rpg_syncinv(cq);
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
        // --- RPG: projectile spells (Oct 21, additive) ---
        rpg_projectiletick(); // moves bolts, applies hits/fizzles; no-op when empty
        // --- RPG: damage-over-time (v0.1.8, additive) ---
        rpg_dottick(); // ticks poison/bleed; no-op when empty
        // --- RPG: Shattered Shield inventory (milestone step 4, additive; see src/rpg) ---
        // Loot: server-validated walk-over pickup + expiry, RPG maps only.
        if(rpg::connected() && rpg::mapactive(smapname))
        {
            rpg::lootdrop drops[64];
            int nd = 0;
            rpg::getlootdrops(drops, 64, nd);
            if(nd > 0) loopv(clients)
            {
                clientinfo *cq = clients[i];
                if(cq->state.ai.type != ai::AI_TYPE_NONE || cq->state.state != CS_ALIVE) continue;
                int level = 1, xp = 0;
                if(!rpg::getstats(cq->name, level, xp)) continue;
                loopj(nd)
                {
                    vec delta(drops[j].x - cq->state.o.x, drops[j].y - cq->state.o.y, drops[j].z - cq->state.o.z);
                    if(delta.magnitude() <= rpg::LOOT_PICKUP_DIST)
                    {
                        int id = drops[j].item, idx = drops[j].idx;
                        if(rpg::takeloot(idx) && rpg::additem(cq->name, rpg::item(id).id, 1))
                        {
                            sendf(-1, 1, "ri2", N_RPG_UNLOOT, idx);
                            sendservmsgf("\f2%s \f7picks up \f2%s\f7.", cq->name, rpg::itemname(id));
                            conoutf("rpg: '%s' picks up %s (drop #%d)", cq->name, rpg::itemname(id), idx);
                            rpg_syncinv(cq);
                        }
                        break; // one pickup per client per tick
                    }
                }
            }
            int expired[64], nexp = 0;
            rpg::updateloot(totalmillis, expired, 64, nexp);
            loopi(nexp) sendf(-1, 1, "ri2", N_RPG_UNLOOT, expired[i]);
        }
        // mana push for the RPG HUD (once a second is plenty)
        if(rpg::connected())
        {
            static int rpg_lastmanapush = 0;
            if(totalmillis - rpg_lastmanapush >= 1000)
            {
                rpg_lastmanapush = totalmillis;
                loopv(clients)
                {
                    clientinfo *cq = clients[i];
                    if(cq->state.ai.type != ai::AI_TYPE_NONE) continue;
                    int mana = 0, maxmana = 0;
                    if(rpg::getmana(cq->name, mana, maxmana))
                        sendf(cq->clientnum, 1, "ri3", N_RPG_MANA, mana, maxmana);
                    // --- Crafting Phase 2: buff expiry (server clock, 1s cadence) ---
                    // Prune runs on any map (expiry is wall-clock); the HP-cap clamp
                    // below is RPG-map only so FPS maxhealth is never touched.
                    {
                        int expired[8]; int nexp = rpg::buffprune(cq->name, expired, 8);
                        loopi(nexp)
                        {
                            defformatstring(emsg)("%s buff has worn off.", rpg::attrname(expired[i]));
                            rpg_qmsg(cq, emsg);
                            conoutf("rpg: '%s' buff %s expired", cq->name, rpg::attrname(expired[i]));
                        }
                        if(nexp > 0 && rpg::mapactive(smapname))
                        {
                            int lvl = 1, xp = 0;
                            if(rpg::getstats(cq->name, lvl, xp))
                            {
                                int maxhp = rpg::stat_maxhp(lvl, rpg::buffbonus(cq->name, rpg::ATTR_VITALITY) + rpg::gearbonus(cq->name, rpg::ATTR_VITALITY));
                                if(cq->state.health > maxhp) { cq->state.health = maxhp; sendf(cq->clientnum, 1, "ri2", N_RPG_HP, cq->state.health); conoutf("rpg: '%s' vitality expiry clamped HP to %d", cq->name, maxhp); }
                                if(cq->state.maxhealth != maxhp) { cq->state.maxhealth = maxhp; sendf(cq->clientnum, 1, "ri2", N_RPG_HPMAX, maxhp); }
                            }
                        }
                        if(nexp > 0) rpg_syncbuffs(cq); // bundle: expiry must clear the client's HUD icon promptly
                    }
                    // --- RPG: out-of-combat health regen (Oct 20 rule) ---
                    // After REGEN_DELAY_MS with no damage dealt or taken,
                    // health climbs at the slow regen rate back to full.
                    // Pushed per second (same cadence as mana) via N_RPG_HP,
                    // so the HUD orb tracks the server-authoritative value.
                    if(rpg::mapactive(smapname) && cq->state.state == CS_ALIVE && cq->state.health > 0 && cq->state.health < cq->state.maxhealth)
                    {
                        int lc = rpg::lastcombat(cq->name);
                        if(lc < 0 || totalmillis - lc >= rpg::REGEN_DELAY_MS)
                        {
                            int heal = max(rpg::REGEN_RATE_MIN, cq->state.maxhealth*rpg::REGEN_RATE_PCT/100);
                            cq->state.health = min(cq->state.maxhealth, cq->state.health + heal);
                            sendf(cq->clientnum, 1, "ri2", N_RPG_HP, cq->state.health);
                        }
                    }
                }
            }
        }
        // --- RPG: Shattered Shield ambient monster spawns (world map) ---
        rpg_ambienttick();
        rpg_behaviortick(); // RPG mob behaviour: leash/reset evaluation (Oct 20)
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
            { // quest engine v2: drop any open dialogue session (talk state is never persisted)
                rpg_talksinit();
                loopi(MAXTALKSESSIONS) if(rpg_talks[i].clientnum == n) rpg_talks[i].clientnum = -1;
            }
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
        // --- RPG: Shattered Shield stats (milestone step 5, additive) ---
        // The inline spawn inside sendinitclient() ran BEFORE the character was
        // loaded, so a fresh connect on an RPG map left the client at the engine
        // default 90/90 HP while the sheet said Max HP 100. Re-apply the derived
        // HP now that the character is loaded, and sync the cap to the client.
        if(rpg::mapactive(smapname))
        {
            rpg_applyspawnhealth(ci);
            sendf(ci->clientnum, 1, "ri2", N_RPG_HPMAX, ci->state.maxhealth);
        }
        rpg_syncinv(ci); // push level/xp + inventory so the client sheet is right on connect
        // --- RPG: Shattered Shield inventory (milestone step 4, additive; see src/rpg) ---
        // Everyone learns this client's equipped weapon model; the newcomer learns everyone's.
        rpg_sendvwep(ci);
        loopv(clients)
        {
            clientinfo *other = clients[i];
            if(other == ci) continue;
            const char *path = rpg::equippedvwep(other->name);
            sendf(ci->clientnum, 1, "riis", N_RPG_VWEP, other->clientnum, path ? path : "");
        }

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
                int ability = getint(p), yaw = getint(p), advisory = getint(p);
                if(cq) rpg_cast(cq, ability, yaw, advisory);
                break;
            }
            // --- RPG: Shattered Shield quest chain v1 (additive; see src/rpg) ---
            case N_RPG_TALK:
            {
                int npcidx = getint(p);
                if(cq) rpg_talk(cq, npcidx);
                break;
            }
            case N_RPG_RESPOND: // quest engine v2: dialogue response button
            {
                int idx = getint(p);
                if(cq) rpg_respond(cq, idx);
                break;
            }
            // --- RPG: Shattered Shield inventory (milestone step 4, additive; see src/rpg) ---
            case N_RPG_EQUIP:
            {
                string itemid; getstring(itemid, p);
                if(cq) rpg_equip(cq, itemid);
                break;
            }
            case N_RPG_UNEQUIP:
            {
                int slot = getint(p);
                if(cq) rpg_unequip(cq, slot);
                break;
            }
            case N_RPG_USE:
            {
                string itemid; getstring(itemid, p);
                if(cq) rpg_use(cq, itemid);
                break;
            }
            case N_RPG_DROP:
            {
                string itemid; getstring(itemid, p);
                if(cq) rpg_dropitem(cq, itemid);
                break;
            }
            case N_RPG_INVL:
            {
                if(cq) rpg_syncinv(cq);
                break;
            }
            // v0.1.23 VENDOR: buy/sell (server-authoritative, gold + inventory).
            case N_RPG_BUY:
            {
                string itemid; getstring(itemid, p);
                if(cq) rpg_buy(cq, itemid);
                break;
            }
            case N_RPG_SELL:
            {
                string itemid; getstring(itemid, p);
                if(cq) rpg_sell(cq, itemid);
                break;
            }
            // Crafting Phase 1 (server-authoritative, Oct 27).
            case N_RPG_CRAFT:
            {
                string itema, itemb; getstring(itema, p); getstring(itemb, p);
                if(cq) rpg_craft(cq, itema, itemb);
                break;
            }
            case N_RPG_CRAFTL:
            {
                if(cq) rpg_sendrecipes(cq);
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
                getstring(text, p);
                filtertext(text, text);
                // RPG: '/' chat commands (party + safety v1). Handled here and
                // never broadcast. Non-RPG maps keep stock behavior below.
                if(text[0] == '/' && rpg::mapactive(smapname))
                {
                    rpg_chatcmd(cq, text);
                    break;
                }
                if(rpg::mapactive(smapname))
                {
                    // RPG: World-tab broadcast with per-recipient mute filtering.
                    // Mirrors the stock worldstate fan-out (sender gets no echo;
                    // they already saw the line via the local conoutf in say).
                    loopv(clients)
                    {
                        clientinfo *t = clients[i];
                        if(t == cq || t->state.ai.type != ai::AI_TYPE_NONE) continue;
                        if(t->state.state == CS_SPECTATOR) continue;
                        if(rpg::ismuted(t->name, cq->name)) continue; // muted: line never reaches them
                        sendf(t->clientnum, 1, "riis", N_TEXT, cq->clientnum, text);
                    }
                    break;
                }
                QUEUE_MSG;
                QUEUE_STR(text);
                break;
            }

            case N_SAYTEAM:
            {
                getstring(text, p);
                // RPG: Clan tab repoints from teamsay to the real clan channel.
                // On RPG maps, a clan member's Clan-tab line goes to online clan
                // members only (with [TAG]); non-members keep stock team behavior.
                if(rpg::mapactive(smapname))
                {
                    int clanid = rpg::clan_of(cq->name);
                    if(clanid > 0)
                    {
                        string ctag; rpg::clan_tagof(cq->name, ctag);
                        char names[32][64]; int n = rpg::clan_memberlist(clanid, names, NULL, 32);
                        loopi(n)
                        {
                            clientinfo *t = rpg_findclient(names[i]);
                            if(!t || t->state.state == CS_SPECTATOR) continue;
                            if(rpg::ismuted(t->name, cq->name)) continue;
                            sendf(t->clientnum, 1, "riiss", N_RPG_CLANCHAT, cq->clientnum, ctag, text);
                        }
                        break;
                    }
                    // no clan: fall through to stock team chat
                }
                if(!ci || !cq || (ci->state.state==CS_SPECTATOR && !ci->local && ci->privilege < PRIV_MASTER) || !m_teammode || !cq->team[0]) break;
                loopv(clients)
                {
                    clientinfo *t = clients[i];
                    if(t==cq || t->state.state==CS_SPECTATOR || t->state.ai.type != ai::AI_TYPE_NONE || strcmp(cq->team, t->team)) continue;
                    if(rpg::mapactive(smapname) && rpg::ismuted(t->name, cq->name)) continue; // RPG mute applies to Clan too
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
