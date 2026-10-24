// Shattered Shield RPG persistence (milestone steps 1+2)
// + ability session state (milestone step 3).
// SQLite-backed character store. Identity = player name for now.
#include "rpg.h"
#include "stats.h"
#include "abilities.h"
#include "items.h"
#include "sqlite3.h"
#include "cube.h"

#include <ctime>
#include <map>
#include <set>
#include <string>
#include <vector>

// Load-path profiling flag (Oct 22, fix-list item 1); defined in
// fpsgame/client.cpp as VARP(rpg_loadprofile) inside namespace game.
namespace game { extern int rpg_loadprofile; }

namespace rpg
{
    static sqlite3 *db = NULL;
    static string dbfile;

    // --- data-driven level thresholds: cumulative XP to REACH each level ---
    static const int XP_TABLE[MAX_LEVEL + 1] =
    {
        /* 0 */ 0,
        /* 1 */ 0,
        /* 2 */ 100,
        /* 3 */ 250,
        /* 4 */ 500,
        /* 5 */ 900,
        /* 6 */ 1500,
        /* 7 */ 2300,
        /* 8 */ 3300,
        /* 9 */ 4600,
        /*10 */ 6200,
    };

    // in-memory view of currently connected characters (write-through to the DB)
    // level/xp persist; mana/cooldowns are session-only (milestone step 3).
    // inventory + equipment persist write-through (milestone step 4).
    struct cachedchar
    {
        int level;
        int xp;
        int copper; // total copper (100c = 1s, 100s = 1g)
        int mana;
        int lastcast[ABILITY_COUNT]; // millis of last successful cast per ability
        int lastregen;               // millis of last regen tick (<0 = not started)
        int lastcombatms = -1;       // server millis of last damage dealt/taken (<0 = never)
        std::map<int, int> inv;      // item index -> count (session view of rpg_inventory)
        int equipped[SLOT_COUNT];    // item index per slot, -1 = empty (session view of rpg_equipment)
    };

    static void initinv(cachedchar &cc)
    {
        cc.inv.clear();
        loopi(SLOT_COUNT) cc.equipped[i] = -1;
    }
    static std::map<std::string, cachedchar> cache;
    // quest chain v1: session view of rpg_quests (defined/loaded below)
    struct qrow { int state; int kills; };
    static std::map<std::string, std::map<int, qrow> > questcache;
    static void questcacheload(const char *name);
    static void rpcacheload(const char *name);
    // quest engine v2: repeatable fetch state, session view of rpg_repeatables
    struct rprow { char item[64]; int amount; int readyat; };
    static std::map<std::string, std::map<int, rprow> > rpcache;

    static cachedchar *findcached(const char *name)
    {
        if(!name || !name[0]) return NULL;
        std::map<std::string, cachedchar>::iterator it = cache.find(std::string(name));
        return it == cache.end() ? NULL : &it->second;
    }

    static void initcaststate(cachedchar &cc, int level, int now)
    {
        cc.mana = maxmana(level);
        loopi(ABILITY_COUNT) cc.lastcast[i] = -1000000000; // every ability ready on connect
        cc.lastregen = now;
        cc.lastcombatms = -1; // never fought: out of combat (mana/HP start full anyway)
    }

    static void loadinv(const char *name); // defined below onconnect

    int xpforlevel(int level)
    {
        if(level < 2 || level > MAX_LEVEL) return -1;
        return XP_TABLE[level];
    }

    const char *dbpath()
    {
        return dbfile;
    }

    bool connected()
    {
        return db != NULL;
    }

    static bool execsql(const char *sql)
    {
        char *err = NULL;
        int rc = sqlite3_exec(db, sql, NULL, NULL, &err);
        if(rc != SQLITE_OK)
        {
            conoutf(CON_ERROR, "rpg: sqlite error: %s", err ? err : sqlite3_errmsg(db));
            sqlite3_free(err);
            return false;
        }
        return true;
    }

    void init()
    {
        if(db) return;
        const char *p = findfile("rpg.db", "w"); // engine home dir, same convention as other server data
        copystring(dbfile, p ? p : "rpg.db");
        if(sqlite3_open(dbfile, &db) != SQLITE_OK)
        {
            conoutf(CON_ERROR, "rpg: could not open character database '%s': %s", (const char *)dbfile, sqlite3_errmsg(db));
            sqlite3_close(db);
            db = NULL;
            return;
        }
        execsql("CREATE TABLE IF NOT EXISTS characters("
                "name TEXT PRIMARY KEY,"
                "level INTEGER NOT NULL DEFAULT 1,"
                "xp INTEGER NOT NULL DEFAULT 0,"
                "copper INTEGER NOT NULL DEFAULT 0,"
                "map TEXT NOT NULL DEFAULT '',"
                "posx REAL NOT NULL DEFAULT 0, posy REAL NOT NULL DEFAULT 0, posz REAL NOT NULL DEFAULT 0,"
                "yaw REAL NOT NULL DEFAULT 0,"
                "archetype TEXT NOT NULL DEFAULT '',"
                "appearance TEXT NOT NULL DEFAULT '',"
                "created_at INTEGER NOT NULL DEFAULT 0,"
                "last_seen INTEGER NOT NULL DEFAULT 0)");
        // v0.1.23 migration: existing DBs lack the copper column. ALTER fails
        // harmlessly (logged once) when the column already exists (new DBs).
        { char *err = NULL; sqlite3_exec(db, "ALTER TABLE characters ADD COLUMN copper INTEGER NOT NULL DEFAULT 0", NULL, NULL, &err); sqlite3_free(err); }
        // --- inventory & equipment v1 (milestone step 4) ---
        execsql("CREATE TABLE IF NOT EXISTS rpg_inventory("
                "name TEXT NOT NULL,"
                "item TEXT NOT NULL,"
                "count INTEGER NOT NULL DEFAULT 1,"
                "PRIMARY KEY(name, item))");
        execsql("CREATE TABLE IF NOT EXISTS rpg_equipment("
                "name TEXT NOT NULL,"
                "slot INTEGER NOT NULL,"
                "item TEXT NOT NULL,"
                "PRIMARY KEY(name, slot))");
        // --- quest chain v1: per-character quest state (additive) ---
        execsql("CREATE TABLE IF NOT EXISTS rpg_quests("
                "name TEXT NOT NULL,"
                "quest INTEGER NOT NULL,"
                "state INTEGER NOT NULL DEFAULT 0,"
                "kills INTEGER NOT NULL DEFAULT 0,"
                "PRIMARY KEY(name, quest))");
        // --- quest engine v2: repeatables (fetch template), additive ---
        execsql("CREATE TABLE IF NOT EXISTS rpg_repeatables("
                "name TEXT NOT NULL,"
                "quest INTEGER NOT NULL,"
                "item TEXT NOT NULL DEFAULT '',"
                "amount INTEGER NOT NULL DEFAULT 0,"
                "readyat INTEGER NOT NULL DEFAULT 0,"
                "PRIMARY KEY(name, quest))");
        // --- party + safety v1 (Oct 19): mute/block lists, additive ---
        execsql("CREATE TABLE IF NOT EXISTS rpg_mutes("
                "owner TEXT NOT NULL,"
                "target TEXT NOT NULL,"
                "PRIMARY KEY(owner, target))");
        execsql("CREATE TABLE IF NOT EXISTS rpg_blocks("
                "owner TEXT NOT NULL,"
                "target TEXT NOT NULL,"
                "PRIMARY KEY(owner, target))");
        // --- clan system v1 (Oct 19) ---
        execsql("CREATE TABLE IF NOT EXISTS rpg_clans("
                "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                "name TEXT NOT NULL UNIQUE,"
                "tag TEXT NOT NULL,"
                "leader TEXT NOT NULL,"
                "created INTEGER NOT NULL DEFAULT 0)");
        execsql("CREATE TABLE IF NOT EXISTS rpg_clan_members("
                "clan_id INTEGER NOT NULL,"
                "name TEXT NOT NULL PRIMARY KEY,"
                "rank INTEGER NOT NULL DEFAULT 2)");
        conoutf("rpg: character database open at %s (sqlite %s)", (const char *)dbfile, sqlite3_libversion());
    }

    void shutdown()
    {
        if(!db) return;
        sqlite3_close(db);
        db = NULL;
        cache.clear();
        conoutf("rpg: character database closed");
    }

    static bool loadrow(const char *name, int &level, int &xp, char *arch = NULL, int archmax = 0)
    {
        sqlite3_stmt *st = NULL;
        if(sqlite3_prepare_v2(db, "SELECT level, xp, archetype, appearance FROM characters WHERE name=?1", -1, &st, NULL) != SQLITE_OK) return false;
        sqlite3_bind_text(st, 1, name, -1, SQLITE_TRANSIENT);
        bool found = sqlite3_step(st) == SQLITE_ROW;
        if(found)
        {
            level = sqlite3_column_int(st, 0); xp = sqlite3_column_int(st, 1);
            // Archetype: prefer the stored column, but fall back to deriving
            // from the model name (same logic as rpg_createchar) — the stored
            // value has proven unreliable across DB connections.
            const char *dbarch = (const char *)sqlite3_column_text(st, 2);
            const char *model = (const char *)sqlite3_column_text(st, 3);
            if(arch && archmax > 0)
            {
                if(dbarch && dbarch[0]) copystring(arch, dbarch, archmax);
                else if(model && model[0])
                {
                    copystring(arch, model, archmax);
                    char *u = strrchr(arch, '_');
                    if(u) *u = '\0';
                    if(!strcmp(arch, "wizard") || !strcmp(arch, "sorceress")) copystring(arch, "magi", archmax);
                    if(arch[0] >= 'a' && arch[0] <= 'z') arch[0] -= 32;
                }
                else arch[0] = '\0';
            }
        }
        sqlite3_finalize(st);
        return found;
    }

    static void touchseen(const char *name)
    {
        sqlite3_stmt *st = NULL;
        if(sqlite3_prepare_v2(db, "UPDATE characters SET last_seen=?1 WHERE name=?2", -1, &st, NULL) != SQLITE_OK) return;
        sqlite3_bind_int64(st, 1, (sqlite3_int64)time(NULL));
        sqlite3_bind_text(st, 2, name, -1, SQLITE_TRANSIENT);
        sqlite3_step(st);
        sqlite3_finalize(st);
    }

    // v0.1.23 currency: forward declaration (loadcopper defined below, used by onconnect).
    static bool loadcopper(const char *name, int &copper);

    bool onconnect(const char *name)
    {
        if(!db || !name || !name[0]) return false;
        // Load-path profiling (Oct 22, fix-list item 1): gated, silent by default.
        int t0 = game::rpg_loadprofile ? SDL_GetTicks() : 0;
        int level = 1, xp = 0;
        char archbuf[32]; archbuf[0] = '\0';
        bool existed = loadrow(name, level, xp, archbuf, sizeof(archbuf));
        if(!existed)
        {
            sqlite3_stmt *st = NULL;
            if(sqlite3_prepare_v2(db, "INSERT INTO characters(name, level, xp, created_at, last_seen) VALUES(?1, 1, 0, ?2, ?2)", -1, &st, NULL) == SQLITE_OK)
            {
                sqlite3_bind_text(st, 1, name, -1, SQLITE_TRANSIENT);
                sqlite3_bind_int64(st, 2, (sqlite3_int64)time(NULL));
                sqlite3_step(st);
                sqlite3_finalize(st);
            }
            conoutf("rpg: created character '%s' (level 1, xp 0)", name);
        }
        else
        {
            touchseen(name);
            conoutf("rpg: loaded character '%s' (level %d, xp %d)", name, level, xp);
        }
        cachedchar cc;
        cc.level = level; cc.xp = xp;
        // v0.1.23 currency: copper loads from DB (fresh chars get
        // STARTING_COPPER via the createchar INSERT; old chars load saved
        // value, 0 when the column is missing/empty).
        { int c = 0; cc.copper = loadcopper(name, c) ? c : 0; }
        initcaststate(cc, level, -1); // mana full, abilities ready; regen starts on first tick
        initinv(cc);
        cache[std::string(name)] = cc;
        // --- inventory & equipment v1 (milestone step 4) ---
        loadinv(name); // rows into the session cache (no-op when none)
        // --- quest engine v2: quest rows + repeatable rows into the session caches ---
        questcacheload(name);
        rpcacheload(name);
        // Starter kit: new characters, OR existing characters with no items
        // at all (rpg_createchar inserts the DB row before onconnect runs, so
        // !existed is false for fresh characters — they arrive with an empty
        // pack and nothing equipped). v0.1.8 fix 9: rangers get the wooden
        // bow, everyone else the puglin stick. Archetype comes from the
        // loadrow above (same DB handle, no fragile re-query).
        itemstack invcheck[1]; int ninv = 0;
        getinventory(name, invcheck, 1, ninv);
        bool hasitems = ninv > 0 || equippeditem(name, SLOT_MAINHAND) >= 0;
        if(!existed || !hasitems)
        {
            // starter kit: a class weapon in hand and two potions in the pack
            // (v0.1.8 fix 9: rangers start with the wooden bow, not a
            // stick/mace; everyone else keeps the puglin stick).
            // Archetype derived from the character's MODEL via charmodel()
            // (reliable) — the stored archetype column is empty due to a DB
            // write issue. Same derivation logic as rpg_createchar.
            char arch[32]; arch[0] = '\0';
            char modeldir[64]; modeldir[0] = '\0';
            if(charmodel(name, modeldir, sizeof(modeldir)) && modeldir[0])
            {
                copystring(arch, modeldir, sizeof(arch));
                char *u = strrchr(arch, '_');
                if(u) *u = '\0';
                if(!strcmp(arch, "wizard") || !strcmp(arch, "sorceress")) copystring(arch, "magi", sizeof(arch));
                if(arch[0] >= 'a' && arch[0] <= 'z') arch[0] -= 32;
            }
            int bowid = finditem("wooden_bow");
            const char *starter = (!strcmp(arch, "Ranger") && bowid >= 0)
                ? "wooden_bow" : "puglin_stick";
            additem(name, starter, 1);
            equipitem(name, starter);
            additem(name, "health_potion", 2);
        }
        // v0.1.8 fix 9 (migration): rangers created before the bow starter
        // kit keep everything they own; grant + equip a wooden bow only if
        // they don't have one. Nothing is ever removed (equipitem swaps the
        // old weapon back into the pack).
        {
            // Derive archetype from model (charmodel is reliable; the stored
            // archetype column is not).
            char march[32]; march[0] = '\0';
            char mmodel[64]; mmodel[0] = '\0';
            if(charmodel(name, mmodel, sizeof(mmodel)) && mmodel[0])
            {
                copystring(march, mmodel, sizeof(march));
                char *u = strrchr(march, '_');
                if(u) *u = '\0';
                if(!strcmp(march, "wizard") || !strcmp(march, "sorceress")) copystring(march, "magi", sizeof(march));
                if(march[0] >= 'a' && march[0] <= 'z') march[0] -= 32;
            }
            if(!strcmp(march, "Ranger") &&
               finditem("wooden_bow") >= 0 && itemcount(name, "wooden_bow") <= 0)
            {
                int mainhand = equippeditem(name, SLOT_MAINHAND);
                if(mainhand < 0 || strcmp(item(mainhand).id, "wooden_bow"))
                {
                    additem(name, "wooden_bow", 1);
                    if(equipitem(name, "wooden_bow"))
                        conoutf("rpg: '%s' (Ranger) issued a Wooden Bow", name);
                }
            }
        }
        if(game::rpg_loadprofile) conoutf("rpgprof: rpg::onconnect(%s): character+inventory+quest DB load %dms", name, SDL_GetTicks()-t0);
        return existed;
    }

    // Load rpg_inventory + rpg_equipment rows for a cached character.
    // Silently ignores rows for unknown items (table grew since the row was written).
    static void loadinv(const char *name)
    {
        cachedchar *cc = findcached(name);
        if(!cc || !db) return;
        initinv(*cc);
        sqlite3_stmt *st = NULL;
        if(sqlite3_prepare_v2(db, "SELECT item, count FROM rpg_inventory WHERE name=?1", -1, &st, NULL) == SQLITE_OK)
        {
            sqlite3_bind_text(st, 1, name, -1, SQLITE_TRANSIENT);
            while(sqlite3_step(st) == SQLITE_ROW)
            {
                const char *itemid = (const char *)sqlite3_column_text(st, 0);
                int count = sqlite3_column_int(st, 1);
                int id = finditem(itemid);
                if(id >= 0 && count > 0) cc->inv[id] = count;
            }
            sqlite3_finalize(st);
        }
        if(sqlite3_prepare_v2(db, "SELECT slot, item FROM rpg_equipment WHERE name=?1", -1, &st, NULL) == SQLITE_OK)
        {
            sqlite3_bind_text(st, 1, name, -1, SQLITE_TRANSIENT);
            while(sqlite3_step(st) == SQLITE_ROW)
            {
                int slot = sqlite3_column_int(st, 0);
                const char *itemid = (const char *)sqlite3_column_text(st, 1);
                int id = finditem(itemid);
                if(id >= 0 && slot >= 0 && slot < SLOT_COUNT && item(id).slot == slot) cc->equipped[slot] = id;
            }
            sqlite3_finalize(st);
        }
    }

    void savepos(const char *name, const char *map, float x, float y, float z, float yaw)
    {
        if(!db || !name || !name[0]) return;
        std::map<std::string, cachedchar>::iterator it = cache.find(std::string(name));
        if(it == cache.end())
        {
            // not in cache: make sure a row exists, then update position only
            int level = 1, xp = 0;
            loadrow(name, level, xp);
            cachedchar cc; cc.level = level; cc.xp = xp;
            initinv(cc);
            it = cache.insert(std::make_pair(std::string(name), cc)).first;
        }
        sqlite3_stmt *st = NULL;
        if(sqlite3_prepare_v2(db, "UPDATE characters SET level=?1, xp=?2, map=?3, posx=?4, posy=?5, posz=?6, yaw=?7, last_seen=?8 WHERE name=?9", -1, &st, NULL) != SQLITE_OK) return;
        sqlite3_bind_int(st, 1, it->second.level);
        sqlite3_bind_int(st, 2, it->second.xp);
        sqlite3_bind_text(st, 3, map ? map : "", -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(st, 4, x);
        sqlite3_bind_double(st, 5, y);
        sqlite3_bind_double(st, 6, z);
        sqlite3_bind_double(st, 7, yaw);
        sqlite3_bind_int64(st, 8, (sqlite3_int64)time(NULL));
        sqlite3_bind_text(st, 9, name, -1, SQLITE_TRANSIENT);
        sqlite3_step(st);
        sqlite3_finalize(st);
    }

    void ondisconnect(const char *name, const char *map, float x, float y, float z, float yaw)
    {
        if(!db || !name || !name[0]) return;
        savepos(name, map, x, y, z, yaw);
        saveinv(name); // milestone step 4: inventory + equipment persist with the character
        // quest engine v2: quest + repeatable rows are already write-through; drop the session views
        questcache.erase(std::string(name));
        rpcache.erase(std::string(name));
        cache.erase(std::string(name));
        conoutf("rpg: saved character '%s'", name);
    }

    int awardxp(const char *name, int amount, bool *leveledup)
    {
        if(!db || !name || !name[0]) return -1;
        std::map<std::string, cachedchar>::iterator it = cache.find(std::string(name));
        if(it == cache.end())
        {
            int level = 1, xp = 0;
            if(!loadrow(name, level, xp)) return -1;
            cachedchar cc; cc.level = level; cc.xp = xp;
            initinv(cc);
            it = cache.insert(std::make_pair(std::string(name), cc)).first;
        }
        it->second.xp += amount;
        bool leveled = false;
        while(it->second.level < MAX_LEVEL && it->second.xp >= XP_TABLE[it->second.level + 1])
        {
            it->second.level++;
            leveled = true;
        }
        // write-through: level+xp persist immediately (crash-safe)
        sqlite3_stmt *st = NULL;
        if(sqlite3_prepare_v2(db, "UPDATE characters SET level=?1, xp=?2, last_seen=?3 WHERE name=?4", -1, &st, NULL) == SQLITE_OK)
        {
            sqlite3_bind_int(st, 1, it->second.level);
            sqlite3_bind_int(st, 2, it->second.xp);
            sqlite3_bind_int64(st, 3, (sqlite3_int64)time(NULL));
            sqlite3_bind_text(st, 4, name, -1, SQLITE_TRANSIENT);
            sqlite3_step(st);
            sqlite3_finalize(st);
        }
        if(leveledup) *leveledup = leveled;
        conoutf("rpg: '%s' +%d xp (level %d, xp %d)%s", name, amount, it->second.level, it->second.xp, leveled ? " -- LEVEL UP" : "");
        return it->second.level;
    }

    bool getstats(const char *name, int &level, int &xp)
    {
        if(!db || !name || !name[0]) return false;
        std::map<std::string, cachedchar>::iterator it = cache.find(std::string(name));
        if(it != cache.end()) { level = it->second.level; xp = it->second.xp; return true; }
        return loadrow(name, level, xp);
    }

    // --- currency v1 (v0.1.23): copper/silver/gold, stored as total copper ---
    static bool loadcopper(const char *name, int &copper)
    {
        if(!db || !name || !name[0]) return false;
        sqlite3_stmt *st = NULL;
        if(sqlite3_prepare_v2(db, "SELECT copper FROM characters WHERE name=?1", -1, &st, NULL) != SQLITE_OK) return false;
        sqlite3_bind_text(st, 1, name, -1, SQLITE_TRANSIENT);
        bool found = sqlite3_step(st) == SQLITE_ROW;
        if(found) copper = sqlite3_column_int(st, 0);
        sqlite3_finalize(st);
        return found;
    }
    static void savecopper(const char *name, int copper)
    {
        if(!db || !name || !name[0]) return;
        sqlite3_stmt *st = NULL;
        if(sqlite3_prepare_v2(db, "UPDATE characters SET copper=?1 WHERE name=?2", -1, &st, NULL) == SQLITE_OK)
        {
            sqlite3_bind_int(st, 1, copper);
            sqlite3_bind_text(st, 2, name, -1, SQLITE_TRANSIENT);
            sqlite3_step(st);
            sqlite3_finalize(st);
        }
    }
    int getcopper(const char *name)
    {
        if(!db || !name || !name[0]) return -1;
        cachedchar *cc = findcached(name);
        if(cc) return cc->copper;
        int copper = 0;
        return loadcopper(name, copper) ? copper : -1;
    }
    int addcopper(const char *name, int amount)
    {
        if(!db || !name || !name[0]) return -1;
        cachedchar *cc = findcached(name);
        if(!cc)
        {
            int copper = 0;
            if(!loadcopper(name, copper)) return -1;
            cachedchar tmp; tmp.level = 1; tmp.xp = 0; tmp.copper = copper;
            initinv(tmp);
            cc = &cache.insert(std::make_pair(std::string(name), tmp)).first->second;
        }
        cc->copper += amount;
        if(cc->copper < 0) cc->copper = 0;
        savecopper(name, cc->copper);
        return cc->copper;
    }
    bool spendcopper(const char *name, int amount)
    {
        if(amount < 0) return false;
        int cur = getcopper(name);
        if(cur < 0 || cur < amount) return false;
        addcopper(name, -amount);
        return true;
    }
    int sellprice(int itemidx)
    {
        if(itemidx < 0 || itemidx >= itemcount()) return 0;
        int v = item(itemidx).value;
        if(v <= 0) return 0;
        return v / SELL_DIVISOR;
    }

    // --- ability layer v1: session state (milestone step 3) ---

    bool getmana(const char *name, int &mana, int &maxmana)
    {
        cachedchar *cc = findcached(name);
        if(!cc) return false;
        mana = cc->mana;
        maxmana = rpg::maxmana(cc->level);
        return true;
    }

    bool castready(const char *name, int ability, int now, int &waitms)
    {
        waitms = 0;
        if(!validability(ability)) return false;
        cachedchar *cc = findcached(name);
        if(!cc) return false;
        int ready_at = cc->lastcast[ability] + rpg::ability(ability).cooldownms;
        if(now < ready_at) { waitms = ready_at - now; return false; }
        cc->lastcast[ability] = now; // consume the cooldown only on a validated cast
        return true;
    }

    bool spendmana(const char *name, int cost)
    {
        cachedchar *cc = findcached(name);
        if(!cc || cc->mana < cost) return false;
        cc->mana -= cost;
        return true;
    }

    void notecombat(const char *name, int now)
    {
        cachedchar *cc = findcached(name);
        if(cc) cc->lastcombatms = now;
    }

    int lastcombat(const char *name)
    {
        cachedchar *cc = findcached(name);
        return cc ? cc->lastcombatms : -1;
    }

    void regen(int now)
    {
        if(!db) return;
        for(std::map<std::string, cachedchar>::iterator it = cache.begin(); it != cache.end(); ++it)
        {
            cachedchar &cc = it->second;
            if(cc.lastregen < 0) { cc.lastregen = now; continue; } // first tick: start the clock
            int dt = now - cc.lastregen;
            if(dt >= 1000)
            {
                int cap = maxmana(cc.level);
                cc.mana = min(cap, cc.mana + dt*stat_manaregen(cc.level)/1000);
                // Oct 20 regen rule: out of combat (no damage dealt or taken
                // for REGEN_DELAY_MS), mana also climbs at the slow regen
                // rate until full. This stacks on the Wisdom regen above,
                // which keeps running in combat exactly as before.
                if(cc.lastcombatms < 0 || now - cc.lastcombatms >= REGEN_DELAY_MS)
                {
                    int rate = max(REGEN_RATE_MIN, cap*REGEN_RATE_PCT/100);
                    cc.mana = min(cap, cc.mana + dt*rate/1000);
                }
                cc.lastregen = now;
            }
        }
    }

    // --- test-only monster pinning (milestone step 3) ---
    static std::set<int> pinnedmonsters;

    void pinmonster(int clientnum)
    {
        pinnedmonsters.insert(clientnum);
    }

    void unpinmonster(int clientnum)
    {
        pinnedmonsters.erase(clientnum);
    }

    bool ispinmonster(int clientnum)
    {
        return pinnedmonsters.find(clientnum) != pinnedmonsters.end();
    }

    // --- inventory & equipment v1 (milestone step 4) ---
    // The RPG layer (equipment effects, loot, abilities-as-RPG) lives on the
    // RPG maps only: rpg_01 is the world, krpg the battleground test map.
    // Everything else keeps pure FPS behavior.
    bool mapactive(const char *map)
    {
        if(!map || !map[0]) return false;
        return !strcmp(map, "rpg_01") || !strcmp(map, "krpg");
    }

    // Write the session cache's inventory + equipment rows for one character.
    // Delete + reinsert is simple and always consistent for these small sets.
    void saveinv(const char *name)
    {
        cachedchar *cc = findcached(name);
        if(!cc || !db || !name || !name[0]) return;
        sqlite3_stmt *st = NULL;
        if(sqlite3_prepare_v2(db, "DELETE FROM rpg_inventory WHERE name=?1", -1, &st, NULL) == SQLITE_OK)
        {
            sqlite3_bind_text(st, 1, name, -1, SQLITE_TRANSIENT);
            sqlite3_step(st);
            sqlite3_finalize(st);
        }
        if(sqlite3_prepare_v2(db, "DELETE FROM rpg_equipment WHERE name=?1", -1, &st, NULL) == SQLITE_OK)
        {
            sqlite3_bind_text(st, 1, name, -1, SQLITE_TRANSIENT);
            sqlite3_step(st);
            sqlite3_finalize(st);
        }
        for(std::map<int, int>::iterator it = cc->inv.begin(); it != cc->inv.end(); ++it)
        {
            if(it->second <= 0) continue;
            if(sqlite3_prepare_v2(db, "INSERT INTO rpg_inventory(name, item, count) VALUES(?1, ?2, ?3)", -1, &st, NULL) == SQLITE_OK)
            {
                sqlite3_bind_text(st, 1, name, -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(st, 2, item(it->first).id, -1, SQLITE_TRANSIENT);
                sqlite3_bind_int(st, 3, it->second);
                sqlite3_step(st);
                sqlite3_finalize(st);
            }
        }
        loopi(SLOT_COUNT)
        {
            if(cc->equipped[i] < 0) continue;
            if(sqlite3_prepare_v2(db, "INSERT INTO rpg_equipment(name, slot, item) VALUES(?1, ?2, ?3)", -1, &st, NULL) == SQLITE_OK)
            {
                sqlite3_bind_text(st, 1, name, -1, SQLITE_TRANSIENT);
                sqlite3_bind_int(st, 2, i);
                sqlite3_bind_text(st, 3, item(cc->equipped[i]).id, -1, SQLITE_TRANSIENT);
                sqlite3_step(st);
                sqlite3_finalize(st);
            }
        }
    }

    bool additem(const char *name, const char *itemid, int count)
    {
        cachedchar *cc = findcached(name);
        int id = finditem(itemid);
        if(!cc || id < 0 || count <= 0) return false;
        cc->inv[id] += count;
        saveinv(name);
        return true;
    }

    bool removeitem(const char *name, const char *itemid, int count)
    {
        cachedchar *cc = findcached(name);
        int id = finditem(itemid);
        if(!cc || id < 0 || count <= 0) return false;
        std::map<int, int>::iterator it = cc->inv.find(id);
        if(it == cc->inv.end() || it->second < count) return false;
        it->second -= count;
        if(it->second <= 0) cc->inv.erase(it);
        saveinv(name);
        return true;
    }

    int itemcount(const char *name, const char *itemid)
    {
        cachedchar *cc = findcached(name);
        int id = finditem(itemid);
        if(!cc || id < 0) return 0;
        std::map<int, int>::iterator it = cc->inv.find(id);
        return it == cc->inv.end() ? 0 : it->second;
    }

    bool getinventory(const char *name, itemstack *out, int maxout, int &n)
    {
        n = 0;
        cachedchar *cc = findcached(name);
        if(!cc || !out || maxout <= 0) return false;
        for(std::map<int, int>::iterator it = cc->inv.begin(); it != cc->inv.end() && n < maxout; ++it)
        {
            if(it->second <= 0) continue;
            out[n].item = it->first;
            out[n].count = it->second;
            n++;
        }
        return true;
    }

    bool equipitem(const char *name, const char *itemid)
    {
        cachedchar *cc = findcached(name);
        int id = finditem(itemid);
        if(!cc || id < 0) return false;
        const itemdef &def = item(id);
        if(def.slot < 0 || def.slot >= SLOT_COUNT) return false; // not equippable
        std::map<int, int>::iterator it = cc->inv.find(id);
        if(it == cc->inv.end() || it->second <= 0) return false; // must own it
        int old = cc->equipped[def.slot];
        if(old == id) return true; // already wearing it
        // take one from the pack...
        it->second--;
        if(it->second <= 0) cc->inv.erase(it);
        // ...and put the previously worn piece back (swap, never delete)
        if(old >= 0) cc->inv[old]++;
        cc->equipped[def.slot] = id;
        saveinv(name);
        conoutf("rpg: '%s' equips %s (%s)", name, def.name, slotname(def.slot));
        return true;
    }

    bool unequipslot(const char *name, int slot)
    {
        cachedchar *cc = findcached(name);
        if(!cc || slot < 0 || slot >= SLOT_COUNT) return false;
        int old = cc->equipped[slot];
        if(old < 0) return false;
        cc->inv[old]++;
        cc->equipped[slot] = -1;
        saveinv(name);
        conoutf("rpg: '%s' unequips %s", name, item(old).name);
        return true;
    }

    int equippeditem(const char *name, int slot)
    {
        cachedchar *cc = findcached(name);
        if(!cc || slot < 0 || slot >= SLOT_COUNT) return -1;
        return cc->equipped[slot];
    }

    int weapondamage(const char *name)
    {
        int id = equippeditem(name, SLOT_MAINHAND);
        return id >= 0 ? item(id).damage : 0;
    }

    int armorvalue(const char *name)
    {
        cachedchar *cc = findcached(name);
        if(!cc) return 0;
        int total = 0;
        loopi(SLOT_COUNT) if(cc->equipped[i] >= 0) total += item(cc->equipped[i]).armor;
        return total;
    }

    // Consume one stackable consumable; returns the heal amount, -1 when unusable.
    int usestack(const char *name, const char *itemid)
    {
        cachedchar *cc = findcached(name);
        int id = finditem(itemid);
        if(!cc || id < 0) return -1;
        const itemdef &def = item(id);
        if(def.type != ITEM_CONSUMABLE || def.heal <= 0) return -1;
        if(!removeitem(name, itemid, 1)) return -1;
        return def.heal;
    }

    const char *equippedvwep(const char *name)
    {
        int id = equippeditem(name, SLOT_MAINHAND);
        if(id < 0) return NULL;
        return item(id).vwep; // NULL when the weapon has no model path
    }

    // --- loot drops v1 (milestone step 4): server-side, session-only ---
    static std::vector<lootdrop> lootdrops;
    static int nextlootidx = 1;

    int spoilloot(int itemid, float x, float y, float z)
    {
        if(itemid < 0 || itemid >= itemcount()) return -1;
        lootdrop d;
        d.idx = nextlootidx++;
        d.item = itemid;
        d.x = x; d.y = y; d.z = z;
        d.born = 0; // server stamps totalmillis via setlootborn
        lootdrops.push_back(d);
        return d.idx;
    }

    void setlootborn(int idx, int now)
    {
        for(size_t i = 0; i < lootdrops.size(); i++)
            if(lootdrops[i].idx == idx) { lootdrops[i].born = now; break; }
    }

    bool getlootdrops(lootdrop *out, int maxout, int &n)
    {
        n = 0;
        if(!out || maxout <= 0) return false;
        for(size_t i = 0; i < lootdrops.size() && n < maxout; i++)
        {
            out[n] = lootdrops[i];
            n++;
        }
        return true;
    }

    bool takeloot(int idx)
    {
        for(size_t i = 0; i < lootdrops.size(); i++)
            if(lootdrops[i].idx == idx) { lootdrops.erase(lootdrops.begin() + i); return true; }
        return false;
    }

    void updateloot(int now, int *expired, int maxexpired, int &nexpired)
    {
        nexpired = 0;
        for(size_t i = 0; i < lootdrops.size();)
        {
            if(now - lootdrops[i].born >= LOOT_LIFETIME_MS)
            {
                if(nexpired < maxexpired) expired[nexpired++] = lootdrops[i].idx;
                lootdrops.erase(lootdrops.begin() + i);
            }
            else i++;
        }
    }

    // --- client presentation caches (milestone step 4; never authoritative) ---
    static std::map<int, std::string> vwepmap; // clientnum -> model path override
    static int climana = -1, climaxmana = -1;
    static int cllastcd[ABILITY_COUNT];
    static std::map<int, int> clinv;           // item index -> count (player1)
    static int clequip[SLOT_COUNT];            // item index per slot, -1 = empty
    static std::vector<lootdrop> clloot;       // drops the client knows about

    static bool clinited = false;
    static void initclient()
    {
        if(clinited) return;
        clinited = true;
        loopi(ABILITY_COUNT) cllastcd[i] = -1000000000;
        loopi(SLOT_COUNT) clequip[i] = -1;
    }

    void setvwep(int clientnum, const char *path)
    {
        initclient();
        if(!path || !path[0]) vwepmap.erase(clientnum);
        else vwepmap[clientnum] = path;
    }

    const char *vwepfor(int clientnum)
    {
        initclient();
        std::map<int, std::string>::iterator it = vwepmap.find(clientnum);
        return it == vwepmap.end() ? NULL : it->second.c_str();
    }

    void setmana(int mana, int maxmana)
    {
        initclient();
        climana = mana; climaxmana = maxmana;
    }

    bool getclientmana(int &mana, int &maxmana)
    {
        initclient();
        if(climana < 0) return false;
        mana = climana; maxmana = climaxmana;
        return true;
    }

    void setcooldown(int ability, int waitms, int now)
    {
        initclient();
        if(ability < 0 || ability >= ABILITY_COUNT) return;
        cllastcd[ability] = now + waitms;
    }

    int cooldownleft(int ability, int now)
    {
        initclient();
        if(ability < 0 || ability >= ABILITY_COUNT) return 0;
        int left = cllastcd[ability] - now;
        return left > 0 ? left : 0;
    }

    static int cllevel = 1, clxp = 0;
    static int clcopper = 0; // client copper cache (from N_RPG_GOLD)

    // TEST-ONLY: set client-side XP directly for HUD verification screenshots
    void setclientxp(int xp) { initclient(); clxp = xp; }
    void setclientcopper(int copper) { initclient(); clcopper = copper; }
    int getclientcopper() { initclient(); return clcopper; }
    bool getclientstats(int &level, int &xp)
    {
        initclient();
        level = cllevel; xp = clxp;
        return true;
    }

    // Parse "itemid:count;itemid:count;..." and "slot:itemid;slot:itemid;..."
    void setinv(const char *invstr, const char *equipstr, int level, int xp)
    {
        initclient();
        clinv.clear();
        loopi(SLOT_COUNT) clequip[i] = -1;
        cllevel = level; clxp = xp;
        if(invstr && invstr[0])
        {
            const char *p = invstr;
            while(*p)
            {
                char id[64] = ""; int count = 0;
                int n = 0;
                while(*p && *p != ':' && n < 63) { id[n++] = *p++; }
                id[n] = 0;
                if(*p == ':') p++;
                while(*p >= '0' && *p <= '9') { count = count*10 + (*p - '0'); p++; }
                if(*p == ';') p++;
                int idx = finditem(id);
                if(idx >= 0 && count > 0) clinv[idx] = count;
            }
        }
        if(equipstr && equipstr[0])
        {
            const char *p = equipstr;
            while(*p)
            {
                int slot = 0;
                while(*p >= '0' && *p <= '9') { slot = slot*10 + (*p - '0'); p++; }
                if(*p == ':') p++;
                char id[64] = ""; int n = 0;
                while(*p && *p != ';' && n < 63) { id[n++] = *p++; }
                id[n] = 0;
                if(*p == ';') p++;
                int idx = finditem(id);
                if(idx >= 0 && slot >= 0 && slot < SLOT_COUNT) clequip[slot] = idx;
            }
        }
    }

    bool getclientinv(itemstack *out, int maxout, int &n, int equipped[SLOT_COUNT])
    {
        initclient();
        n = 0;
        if(!out || maxout <= 0) return false;
        for(std::map<int, int>::iterator it = clinv.begin(); it != clinv.end() && n < maxout; ++it)
        {
            out[n].item = it->first;
            out[n].count = it->second;
            n++;
        }
        loopi(SLOT_COUNT) equipped[i] = clequip[i];
        return true;
    }

    void setloot(int idx, int itemid, float x, float y, float z)
    {
        initclient();
        for(size_t i = 0; i < clloot.size(); i++)
            if(clloot[i].idx == idx) { clloot[i].item = itemid; clloot[i].x = x; clloot[i].y = y; clloot[i].z = z; return; }
        lootdrop d;
        d.idx = idx; d.item = itemid; d.x = x; d.y = y; d.z = z; d.born = 0;
        clloot.push_back(d);
    }

    void removeloot(int idx)
    {
        initclient();
        for(size_t i = 0; i < clloot.size(); i++)
            if(clloot[i].idx == idx) { clloot.erase(clloot.begin() + i); return; }
    }

    void clearloot()
    {
        initclient();
        clloot.clear();
    }

    bool getclientloot(lootdrop *out, int maxout, int &n)
    {
        initclient();
        n = 0;
        if(!out || maxout <= 0) return false;
        for(size_t i = 0; i < clloot.size() && n < maxout; i++)
        {
            out[n] = clloot[i];
            n++;
        }
        return true;
    }

    // --- mouse & targeting v1: client presentation state (plain data) ---
    static float clcursorx = 640.0f, clcursory = 360.0f;
    static int clhover = RPG_HOVER_DEFAULT;
    static int clselclient = -1;
    static bool clautoattack = false;

    void cursoraccum(int dx, int dy)
    {
        clcursorx += dx; clcursory += dy;
    }
    void cursorget(float &x, float &y) { x = clcursorx; y = clcursory; }
    void cursorset(float x, float y) { clcursorx = x; clcursory = y; }
    void cursorclamp(float &x, float &y, int w, int h)
    {
        if(x < 0) x = 0; if(y < 0) y = 0;
        if(x > w) x = w; if(y > h) y = h;
        clcursorx = x; clcursory = y;
    }
    int hoverstate() { return clhover; }
    void sethoverstate(int s) { clhover = s; }
    static int clnpcsel = -1;
    int selectedtarget() { return clselclient; }
    void selecttarget(int clientnum) { clselclient = clientnum; if(clientnum >= 0) clnpcsel = -1; }
    int selectednpc() { return clnpcsel; }
    void selectnpc(int idx) { clnpcsel = idx; if(idx >= 0) clselclient = -1; }
    bool autoattacking() { return clautoattack; }
    void setautoattack(bool on) { clautoattack = on; }
    void cleartargeting() { clselclient = -1; clnpcsel = -1; clautoattack = false; }

    // RPG radar marker sources. Gathering does not exist yet: gathermarkers
    // returns 0 today and lights up when that system lands. NPC markers are
    // LIVE (quest chain v1). Quest-mob positions are dynamic, so the HUD
    // enumerates them from AI state filtered by isquestmobtarget() (same
    // pattern as the red hostile pass); questmobmarkers stays 0 by design.
    int npcmarkers(float *out, int maxout)
    {
        int n = 0;
        for(int i = 0; i < npccount() && n < maxout; i++)
        {
            const npcdef *nd = npcat(i);
            if(!nd) continue;
            out[3*n] = nd->x; out[3*n+1] = nd->y; out[3*n+2] = nd->z;
            n++;
        }
        return n;
    }
    int questmobmarkers(float *out, int maxout) { (void)out; (void)maxout; return 0; }
    int gathermarkers(float *out, int maxout) { (void)out; (void)maxout; return 0; }

    // --- quest chain v1 ---
    // NPC models are the STATIC peasant variants (npcs/*_peasant_static):
    // same mesh/skins as the animated peasants, but their iqm.cfg registers
    // no anim, so the engine renders the bind pose standing still. The
    // animated peasants' retargeted ranger idle left NPC heads backwards
    // in-game; NPCs only stand and talk, so static is the approved fallback.
    // Elder Maren stands in the spawn building on rpg_01, a few paces from
    // the player start (2220, 1972), facing it. v1: one static NPC; the
    // table is the single place a second quest giver slots in later.
    static const npcdef NPCS[] =
    {
        { "maren", "Elder Maren", "npcs/female_peasant_static", 2228.0f, 1980.0f, 2116.0f, 135.0f, "maren_main" },
        // CLEANUP item 6: Tomas, the errand's delivery target. A few paces
        // east of Maren in the spawn clearing (10u - the Oct 21 fix for the
        // two NPCs spawning on top of each other, which also made the F-talk
        // nearest-NPC resolver grab the wrong one). The errand is still a
        // conversation, not a trek.
        // v0.1.8: moved from (2238,1980) — 10u east put him outside the spawn
        // building (or in its wall); Ed couldn't find him. Now 8u west of
        // Maren, inside the building near the player spawn.
        { "tomas", "Tomas", "npcs/male_peasant_static", 2220.0f, 1980.0f, 2116.0f, 115.0f, "tomas_main" },
        // QUEST BUILD (Oct 21): Corvin, the relay quest's recipient. The
        // design's "Tomas Reed" collided with the existing Tomas, so the
        // relay recipient is a distinct NPC: a hunter camped by the east
        // homesteads. warrior_male per the approved recommendation.
        { "corvin", "Corvin", "npcs/male_peasant_static", 3062.0f, 2612.0f, 2116.0f, 270.0f, "corvin_main" },
        // v0.1.23 VENDOR: Glimmer, a friendly imp merchant (Quaternius
        // Bestiary, BaseColor_2 — distinct from hostile BaseColor_1 imps).
        // NON-HUMAN per Ed's requirement (gnome-like folklore creature).
        // Location: village area east of spawn, near the east homesteads
        // but NOT in the starting building and NOT on Corvin (~200u away).
        // Dialogue "vendor_main" is a marker: talking to Glimmer opens the
        // vendor GUI instead of a quest dialogue graph (see rpg_talk).
        { "glimmer", "Glimmer", "npcs/imp_vendor", 2980.0f, 2450.0f, 2116.0f, 180.0f, "vendor_main" },
    };
    static const int NPC_COUNT = sizeof(NPCS)/sizeof(NPCS[0]);
    int npcindexbyid(const char *id)
    {
        if(!id || !id[0]) return -1;
        loopi(NPC_COUNT) if(!strcmp(NPCS[i].id, id)) return i;
        return -1;
    }

    int npccount() { return NPC_COUNT; }
    const npcdef *npcat(int i) { return (i >= 0 && i < NPC_COUNT) ? &NPCS[i] : NULL; }

    // --- ambient monster spawns: static table for rpg_01 ---
    // 12 points around the village outskirts — never on the player spawn at
    // (2220,1972) or on Maren at (2228,1980); a new player meets the first
    // imps a short walk out. z is the measured ground height at (x,y).
    // Density: 12 live monsters with a 60s respawn comfortably feeds q1
    // (5 kills) and q2 (8 kills) for 2-10 players sharing the world.
    // Mix: 10 imp rows (the ambient zombie rows became imps on the user's
    // call, Oct 20 - the bestiary imp is the mob new players actually face;
    // questonkill counts any monster kill, so q1/q2 credit is unchanged)
    // and 2 puglin rows at the far west/northwest. The Rotting Champion
    // (quest 3) is NOT in this table and stays a zombie. Types resolve by
    // name in rpg_resolvespawntype.
    static const spawndef SPAWNS[] =
    {
        { "imp"   , 2150.0f, 1450.0f, 2344.0f, "south fields" },
        { "imp"   , 2380.0f, 1600.0f, 2277.0f, "south fields east" },
        { "imp"   , 1980.0f, 1280.0f, 2373.0f, "south fields west" },
        { "imp"   , 1300.0f, 2050.0f, 2180.0f, "west forest edge" },
        { "puglin", 1120.0f, 2420.0f, 2181.0f, "west forest" },
        { "imp"   , 1480.0f, 2520.0f, 2180.0f, "west forest edge north" },
        { "imp",    3080.0f, 2280.0f, 2111.0f, "east clearing" }, // Oct 21: z was 2188, imp fell 68u through terrain (invisible)
        { "imp",    3050.0f, 2600.0f, 2116.0f, "east homesteads" },
        { "imp"   , 2200.0f, 3350.0f, 2116.0f, "north road" },
        { "puglin", 1800.0f, 3600.0f, 2180.0f, "northwest treeline" },
        { "imp"   , 2600.0f, 1300.0f, 2383.0f, "southeast field" },
        { "imp"   , 1500.0f, 1500.0f, 2381.0f, "southwest field" },
    };
    static const int SPAWN_COUNT = sizeof(SPAWNS)/sizeof(SPAWNS[0]);

    int spawnpointcount() { return SPAWN_COUNT; }
    const spawndef *spawnpoint(int i) { return (i >= 0 && i < SPAWN_COUNT) ? &SPAWNS[i] : NULL; }

    // Quest engine v2: quest definitions are content now (data/rpg/quests.cfg,
    // parsed by loadquests() in quests.cpp). validquest() is the single range
    // test every quest guard uses: true exactly when a quest def exists.
    bool validquest(int qid)
    {
        return questdefbyid(qid) != NULL;
    }

    // Champion display name (also the radar/client match string).
    static const char *CHAMPION_NAME = "Rotting Champion";

    // --- quest engine v2: persistence rpg_quests(name, quest, state, kills) ---
    // `state` is now a stage integer: 0 = never started, 1..N in progress,
    // finalstage = done. (The column keeps its v1 name; no migration needed.)
    void repeatablesetcooldown(const char *name, int qid); // fwd (defined below)
    bool repeatableget(const char *name, int qid, char *item, int itemlen, int *amount); // fwd
    static void questcacheload(const char *name)
    {
        questcache[std::string(name)].clear();
        if(!db || !name || !name[0]) return;
        sqlite3_stmt *st = NULL;
        if(sqlite3_prepare_v2(db, "SELECT quest, state, kills FROM rpg_quests WHERE name=?1", -1, &st, NULL) == SQLITE_OK)
        {
            sqlite3_bind_text(st, 1, name, -1, SQLITE_TRANSIENT);
            while(sqlite3_step(st) == SQLITE_ROW)
            {
                int q = sqlite3_column_int(st, 0);
                qrow r; r.state = sqlite3_column_int(st, 1); r.kills = sqlite3_column_int(st, 2);
                if(validquest(q)) questcache[std::string(name)][q] = r;
            }
            sqlite3_finalize(st);
        }
    }

    static void questsave(const char *name, int qid)
    {
        if(!db || !name || !name[0]) return;
        std::map<int, qrow> &m = questcache[std::string(name)];
        std::map<int, qrow>::iterator it = m.find(qid);
        if(it == m.end()) return;
        sqlite3_stmt *st = NULL;
        if(sqlite3_prepare_v2(db, "INSERT OR REPLACE INTO rpg_quests(name, quest, state, kills) VALUES(?1, ?2, ?3, ?4)", -1, &st, NULL) == SQLITE_OK)
        {
            sqlite3_bind_text(st, 1, name, -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(st, 2, qid);
            sqlite3_bind_int(st, 3, it->second.state);
            sqlite3_bind_int(st, 4, it->second.kills);
            sqlite3_step(st);
            sqlite3_finalize(st);
        }
    }

    int queststate(const char *name, int qid)
    {
        if(!name || !name[0] || !validquest(qid)) return 0;
        std::map<int, qrow> &m = questcache[std::string(name)];
        std::map<int, qrow>::iterator it = m.find(qid);
        return it == m.end() ? 0 : it->second.state; // no row = never started = stage 0
    }

    int questkills(const char *name, int qid)
    {
        if(!name || !name[0] || !validquest(qid)) return 0;
        std::map<int, qrow> &m = questcache[std::string(name)];
        std::map<int, qrow>::iterator it = m.find(qid);
        return it == m.end() ? 0 : it->second.kills;
    }

    void questsetstage(const char *name, int qid, int stage)
    {
        if(!name || !name[0] || !validquest(qid)) return;
        const questdef *qd = questdefbyid(qid);
        if(!qd || stage < 0 || stage > qd->finalstage) return;
        qrow r; r.state = stage; r.kills = 0;
        questcache[std::string(name)][qid] = r;
        questsave(name, qid);
        // Repeatable turn-in: setting the stage back to 0 stamps the cooldown.
        if(stage == 0 && qd->repeatsec > 0) repeatablesetcooldown(name, qid);
    }

    void questaddprogress(const char *name, int qid, int n)
    {
        if(!name || !name[0] || !validquest(qid) || n <= 0) return;
        const questdef *qd = questdefbyid(qid);
        std::map<int, qrow> &m = questcache[std::string(name)];
        std::map<int, qrow>::iterator it = m.find(qid);
        if(it == m.end()) return;
        const queststage *qs = queststageof(qd, it->second.state);
        if(!qs || qs->objtype != QOBJ_KILL) return; // kill stages only
        it->second.kills += n;
        questsave(name, qid);
    }

    bool questiscomplete(const char *name, int qid)
    {
        const questdef *qd = questdefbyid(qid);
        if(!qd) return false;
        return queststate(name, qid) == qd->finalstage;
    }

    void questprogress(const char *name, int qid, int &have, int &need)
    {
        have = 0; need = 0;
        const questdef *qd = questdefbyid(qid);
        if(!qd) return;
        const queststage *qs = queststageof(qd, queststate(name, qid));
        if(!qs) return;
        if(qs->objtype == QOBJ_KILL) { have = questkills(name, qid); need = qs->objcount; }
        else if(qs->objtype == QOBJ_FETCH)
        {
            char item[64]; int amount = 0;
            if(repeatableget(name, qid, item, sizeof(item), &amount))
            {
                have = itemcount(name, item);
                if(have > amount) have = amount;
                need = amount;
            }
        }
    }

    // Champion tracking (server session state).
    static int champion_clientnum = -1;
    void setchampioncn(int cn) { champion_clientnum = cn; }
    int championcn() { return champion_clientnum; }
    void clearchampioncn() { champion_clientnum = -1; }

    // --- repeatables: rpg_repeatables(name, quest, item, amount, readyat) ---
    // One row per (character, quest): the rolled fetch item/amount and the
    // earliest re-accept time (unix). Additive table; server-side only.
    // (rprow + rpcache are declared at the top of the namespace.)
    static void rpcacheload(const char *name)
    {
        rpcache[std::string(name)].clear();
        if(!db || !name || !name[0]) return;
        sqlite3_stmt *st = NULL;
        if(sqlite3_prepare_v2(db, "SELECT quest, item, amount, readyat FROM rpg_repeatables WHERE name=?1", -1, &st, NULL) == SQLITE_OK)
        {
            sqlite3_bind_text(st, 1, name, -1, SQLITE_TRANSIENT);
            while(sqlite3_step(st) == SQLITE_ROW)
            {
                int q = sqlite3_column_int(st, 0);
                if(!validquest(q)) continue;
                rprow r;
                copystring(r.item, (const char *)sqlite3_column_text(st, 1), sizeof(r.item));
                r.amount = sqlite3_column_int(st, 2);
                r.readyat = sqlite3_column_int(st, 3);
                rpcache[std::string(name)][q] = r;
            }
            sqlite3_finalize(st);
        }
    }
    static void rpsave(const char *name, int qid)
    {
        if(!db || !name || !name[0]) return;
        std::map<int, rprow> &m = rpcache[std::string(name)];
        std::map<int, rprow>::iterator it = m.find(qid);
        if(it == m.end()) return;
        sqlite3_stmt *st = NULL;
        if(sqlite3_prepare_v2(db, "INSERT OR REPLACE INTO rpg_repeatables(name, quest, item, amount, readyat) VALUES(?1, ?2, ?3, ?4, ?5)", -1, &st, NULL) == SQLITE_OK)
        {
            sqlite3_bind_text(st, 1, name, -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(st, 2, qid);
            sqlite3_bind_text(st, 3, it->second.item, -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(st, 4, it->second.amount);
            sqlite3_bind_int(st, 5, it->second.readyat);
            sqlite3_step(st);
            sqlite3_finalize(st);
        }
    }

    bool repeatableready(const char *name, int qid)
    {
        const questdef *qd = questdefbyid(qid);
        if(!qd || qd->repeatsec <= 0) return false;
        std::map<int, rprow> &m = rpcache[std::string(name)];
        std::map<int, rprow>::iterator it = m.find(qid);
        if(it == m.end()) return true; // never rolled: ready
        return (int)time(NULL) >= it->second.readyat;
    }

    bool repeatableroll(const char *name, int qid)
    {
        const questdef *qd = questdefbyid(qid);
        if(!qd || qd->npool <= 0 || qd->poolmax < qd->poolmin) return false;
        rprow r;
        copystring(r.item, qd->pool[rnd(qd->npool)], sizeof(r.item));
        r.amount = qd->poolmin + rnd(qd->poolmax - qd->poolmin + 1);
        r.readyat = 0;
        rpcache[std::string(name)][qid] = r;
        rpsave(name, qid);
        return true;
    }

    bool repeatableget(const char *name, int qid, char *item, int itemlen, int *amount)
    {
        std::map<int, rprow> &m = rpcache[std::string(name)];
        std::map<int, rprow>::iterator it = m.find(qid);
        if(it == m.end()) return false;
        copystring(item, it->second.item, itemlen);
        if(amount) *amount = it->second.amount;
        return true;
    }

    void repeatablesetcooldown(const char *name, int qid)
    {
        const questdef *qd = questdefbyid(qid);
        if(!qd || qd->repeatsec <= 0) return;
        rprow &r = rpcache[std::string(name)][qid];
        r.readyat = (int)time(NULL) + qd->repeatsec;
        rpsave(name, qid);
    }

    // Server kill-path hook (generalized v2). Counts a kill toward EVERY
    // active quest whose current stage has a matching kill objective:
    // "any" matches any monster, "champion" matches only the tracked
    // champion clientnum, otherwise the victim's class name must start with
    // the objective class. Appends counted qids (up to maxcounted) and
    // returns how many quests counted it.
    int questonkill(const char *name, const char *victimclass, int targetclientnum,
                    bool ismonster, int *counted, int maxcounted)
    {
        int n = 0;
        if(!name || !name[0] || !ismonster || !counted || maxcounted <= 0) return 0;
        int nq = questdefcount();
        loopi(nq)
        {
            const questdef *qd = questdefbyindex(i);
            const queststage *qs = queststageof(qd, queststate(name, qd->qid));
            if(!qs || qs->objtype != QOBJ_KILL) continue;
            bool match = false;
            if(!strcmp(qs->objarg, "any")) match = true;
            else if(!strcmp(qs->objarg, "champion")) match = (targetclientnum == championcn());
            else if(victimclass && victimclass[0])
                match = !strncmp(victimclass, qs->objarg, strlen(qs->objarg));
            if(!match) continue;
            questaddprogress(name, qd->qid, 1);
            if(!strcmp(qs->objarg, "champion")) clearchampioncn(); // champion dead; stop tracking
            if(n < maxcounted) counted[n] = qd->qid;
            n++;
        }
        return n;
    }


    // --- party + safety v1: mute/block lists (server-side) ---
    static bool hasrow(const char *table, const char *owner, const char *target)
    {
        if(!db || !owner || !owner[0] || !target || !target[0]) return false;
        sqlite3_stmt *st = NULL;
        defformatstring(sql)("SELECT 1 FROM %s WHERE owner=? AND target=?", table);
        if(sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) return false;
        sqlite3_bind_text(st, 1, owner, -1, SQLITE_STATIC);
        sqlite3_bind_text(st, 2, target, -1, SQLITE_STATIC);
        bool found = sqlite3_step(st) == SQLITE_ROW;
        sqlite3_finalize(st);
        return found;
    }
    static bool togglerow(const char *table, const char *owner, const char *target)
    {
        if(!db || !owner || !owner[0] || !target || !target[0]) return false;
        if(hasrow(table, owner, target))
        {
            defformatstring(sql)("DELETE FROM %s WHERE owner=? AND target=?", table);
            sqlite3_stmt *st = NULL;
            if(sqlite3_prepare_v2(db, sql, -1, &st, NULL) == SQLITE_OK)
            {
                sqlite3_bind_text(st, 1, owner, -1, SQLITE_STATIC);
                sqlite3_bind_text(st, 2, target, -1, SQLITE_STATIC);
                sqlite3_step(st);
                sqlite3_finalize(st);
            }
            return false; // now unmuted/unblocked
        }
        defformatstring(sql)("INSERT INTO %s(owner, target) VALUES(?, ?)", table);
        sqlite3_stmt *st = NULL;
        if(sqlite3_prepare_v2(db, sql, -1, &st, NULL) == SQLITE_OK)
        {
            sqlite3_bind_text(st, 1, owner, -1, SQLITE_STATIC);
            sqlite3_bind_text(st, 2, target, -1, SQLITE_STATIC);
            sqlite3_step(st);
            sqlite3_finalize(st);
        }
        return true; // now muted/blocked
    }
    bool ismuted(const char *owner, const char *target) { return hasrow("rpg_mutes", owner, target); }
    bool togglemute(const char *owner, const char *target) { return togglerow("rpg_mutes", owner, target); }
    bool isblocked(const char *owner, const char *target) { return hasrow("rpg_blocks", owner, target); }
    bool toggleblock(const char *owner, const char *target) { return togglerow("rpg_blocks", owner, target); }

    // --- clan system v1: persisted membership ---
    int clan_create(const char *name, const char *tag, const char *leader)
    {
        if(!db || !name || !name[0] || !tag || !tag[0] || !leader || !leader[0]) return -1;
        sqlite3_stmt *st = NULL;
        if(sqlite3_prepare_v2(db, "INSERT INTO rpg_clans(name, tag, leader, created) VALUES(?,?,?,?)", -1, &st, NULL) != SQLITE_OK) return -1;
        sqlite3_bind_text(st, 1, name, -1, SQLITE_STATIC);
        sqlite3_bind_text(st, 2, tag, -1, SQLITE_STATIC);
        sqlite3_bind_text(st, 3, leader, -1, SQLITE_STATIC);
        sqlite3_bind_int64(st, 4, (sqlite3_int64)time(NULL));
        int rc = sqlite3_step(st);
        sqlite3_finalize(st);
        if(rc != SQLITE_DONE) return -1; // name taken (UNIQUE) or other error
        int id = (int)sqlite3_last_insert_rowid(db);
        clan_addmember(id, leader, CLAN_RANK_LEADER);
        return id;
    }
    int clan_of(const char *name)
    {
        if(!db || !name || !name[0]) return 0;
        sqlite3_stmt *st = NULL;
        if(sqlite3_prepare_v2(db, "SELECT clan_id FROM rpg_clan_members WHERE name=?", -1, &st, NULL) != SQLITE_OK) return 0;
        sqlite3_bind_text(st, 1, name, -1, SQLITE_STATIC);
        int id = sqlite3_step(st) == SQLITE_ROW ? sqlite3_column_int(st, 0) : 0;
        sqlite3_finalize(st);
        return id;
    }
    int clan_rank(const char *name)
    {
        if(!db || !name || !name[0]) return -1;
        sqlite3_stmt *st = NULL;
        if(sqlite3_prepare_v2(db, "SELECT rank FROM rpg_clan_members WHERE name=?", -1, &st, NULL) != SQLITE_OK) return -1;
        sqlite3_bind_text(st, 1, name, -1, SQLITE_STATIC);
        int r = sqlite3_step(st) == SQLITE_ROW ? sqlite3_column_int(st, 0) : -1;
        sqlite3_finalize(st);
        return r;
    }
    bool clan_info(int id, char *name, char *tag, char *leader)
    {
        if(!db || id <= 0) return false;
        sqlite3_stmt *st = NULL;
        if(sqlite3_prepare_v2(db, "SELECT name, tag, leader FROM rpg_clans WHERE id=?", -1, &st, NULL) != SQLITE_OK) return false;
        sqlite3_bind_int(st, 1, id);
        bool ok = sqlite3_step(st) == SQLITE_ROW;
        if(ok)
        {
            if(name) copystring(name, (const char *)sqlite3_column_text(st, 0));
            if(tag) copystring(tag, (const char *)sqlite3_column_text(st, 1));
            if(leader) copystring(leader, (const char *)sqlite3_column_text(st, 2));
        }
        sqlite3_finalize(st);
        return ok;
    }
    bool clan_addmember(int id, const char *name, int rank)
    {
        if(!db || id <= 0 || !name || !name[0]) return false;
        if(clan_of(name) != 0) return false; // one clan per character
        sqlite3_stmt *st = NULL;
        if(sqlite3_prepare_v2(db, "INSERT OR REPLACE INTO rpg_clan_members(clan_id, name, rank) VALUES(?,?,?)", -1, &st, NULL) != SQLITE_OK) return false;
        sqlite3_bind_int(st, 1, id);
        sqlite3_bind_text(st, 2, name, -1, SQLITE_STATIC);
        sqlite3_bind_int(st, 3, rank);
        int rc = sqlite3_step(st);
        sqlite3_finalize(st);
        return rc == SQLITE_DONE;
    }
    void clan_removemember(const char *name)
    {
        if(!db || !name || !name[0]) return;
        sqlite3_stmt *st = NULL;
        if(sqlite3_prepare_v2(db, "DELETE FROM rpg_clan_members WHERE name=?", -1, &st, NULL) == SQLITE_OK)
        {
            sqlite3_bind_text(st, 1, name, -1, SQLITE_STATIC);
            sqlite3_step(st);
            sqlite3_finalize(st);
        }
    }
    bool clan_setrank(const char *name, int rank)
    {
        if(!db || !name || !name[0] || rank < 0 || rank > 2) return false;
        sqlite3_stmt *st = NULL;
        if(sqlite3_prepare_v2(db, "UPDATE rpg_clan_members SET rank=? WHERE name=?", -1, &st, NULL) != SQLITE_OK) return false;
        sqlite3_bind_int(st, 1, rank);
        sqlite3_bind_text(st, 2, name, -1, SQLITE_STATIC);
        sqlite3_step(st);
        sqlite3_finalize(st);
        return sqlite3_changes(db) > 0;
    }
    void clan_disband(int id)
    {
        if(!db || id <= 0) return;
        defformatstring(s1)("DELETE FROM rpg_clan_members WHERE clan_id=%d", id);
        defformatstring(s2)("DELETE FROM rpg_clans WHERE id=%d", id);
        execsql(s1); execsql(s2);
    }
    int clan_membercount(int id)
    {
        if(!db || id <= 0) return 0;
        sqlite3_stmt *st = NULL;
        if(sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM rpg_clan_members WHERE clan_id=?", -1, &st, NULL) != SQLITE_OK) return 0;
        sqlite3_bind_int(st, 1, id);
        int n = sqlite3_step(st) == SQLITE_ROW ? sqlite3_column_int(st, 0) : 0;
        sqlite3_finalize(st);
        return n;
    }
    void clan_tagof(const char *name, char *tag)
    {
        if(tag) tag[0] = '\0';
        if(!db || !name || !name[0] || !tag) return;
        int id = clan_of(name);
        if(id <= 0) return;
        clan_info(id, NULL, tag, NULL);
    }
    int clan_memberlist(int id, char names[][64], int *ranks, int max)
    {
        if(!db || id <= 0 || !names || max <= 0) return 0;
        sqlite3_stmt *st = NULL;
        if(sqlite3_prepare_v2(db, "SELECT name, rank FROM rpg_clan_members WHERE clan_id=? ORDER BY rank, name", -1, &st, NULL) != SQLITE_OK) return 0;
        sqlite3_bind_int(st, 1, id);
        int n = 0;
        while(n < max && sqlite3_step(st) == SQLITE_ROW)
        {
            copystring(names[n], (const char *)sqlite3_column_text(st, 0), 64);
            if(ranks) ranks[n] = sqlite3_column_int(st, 1);
            n++;
        }
        sqlite3_finalize(st);
        return n;
    }

    // --- clan system v1: client presentation cache ---
    static int clclan_id = 0, clclan_rank = -1;
    static string clclan_name, clclan_tag;
    void setclientclan(int id, int rank, const char *name, const char *tag)
    {
        clclan_id = id; clclan_rank = rank;
        copystring(clclan_name, name ? name : "");
        copystring(clclan_tag, tag ? tag : "");
    }
    bool getclientclan(int &id, int &rank, char *name, char *tag)
    {
        id = clclan_id; rank = clclan_rank;
        if(name) copystring(name, clclan_name);
        if(tag) copystring(tag, clclan_tag);
        return clclan_id > 0;
    }
    // --- character select (ITEM 5): client-side direct rpg.db access ---
    static sqlite3 *chardb_open()
    {
        const char *p = findfile("rpg.db", "w");
        if(!p) p = "rpg.db";
        // also try the home dir explicitly
        {
            const char *h = getenv("HOME");
            if(h)
            {
                defformatstring(hp)("%s/.shattered-shield/rpg.db", h);
                FILE *f = fopen(hp, "r");
                if(f) { fclose(f); p = hp; }
            }
        }
        sqlite3 *cdb = NULL;
        if(sqlite3_open(p, &cdb) != SQLITE_OK) { if(cdb) sqlite3_close(cdb); return NULL; }
        return cdb;
    }
    int charlist(char names[][64], int *levels, char archetypes[][32], char models[][64], int max)
    {
        if(max <= 0) return 0;
        sqlite3 *cdb = chardb_open();
        if(!cdb) return 0;
        sqlite3_stmt *st = NULL;
        int n = 0;
        if(sqlite3_prepare_v2(cdb, "SELECT name, level, archetype, appearance FROM characters ORDER BY last_seen DESC LIMIT ?", -1, &st, NULL) == SQLITE_OK)
        {
            sqlite3_bind_int(st, 1, max);
            while(n < max && sqlite3_step(st) == SQLITE_ROW)
            {
                copystring(names[n], (const char *)sqlite3_column_text(st, 0), 64);
                if(levels) levels[n] = sqlite3_column_int(st, 1);
                if(archetypes) copystring(archetypes[n], (const char *)sqlite3_column_text(st, 2), 32);
                if(models) copystring(models[n], (const char *)sqlite3_column_text(st, 3), 64);
                n++;
            }
            sqlite3_finalize(st);
        }
        sqlite3_close(cdb);
        return n;
    }
    // Archetype for one character (class gating). Direct DB read; casts are
    // infrequent so no session caching. Magi class (Ed, Oct 21): characters
    // created before the merge store "Wizard"/"Sorceress" - normalize to
    // "Magi" so their spells keep gating correctly.
    bool chararchetype(const char *name, char *out, int max)
    {
        if(out && max > 0) out[0] = '\0';
        if(!name || !name[0]) return false;
        sqlite3 *cdb = chardb_open();
        if(!cdb) return false;
        bool ok = false;
        sqlite3_stmt *st = NULL;
        if(sqlite3_prepare_v2(cdb, "SELECT archetype FROM characters WHERE name=?1", -1, &st, NULL) == SQLITE_OK)
        {
            sqlite3_bind_text(st, 1, name, -1, SQLITE_STATIC);
            if(sqlite3_step(st) == SQLITE_ROW)
            {
                copystring(out, (const char *)sqlite3_column_text(st, 0), max);
                if(!strcmp(out, "Wizard") || !strcmp(out, "Sorceress")) copystring(out, "Magi", max);
                ok = true;
            }
            sqlite3_finalize(st);
        }
        sqlite3_close(cdb);
        return ok;
    }
    // Model dir for one character (used by rpg_enterworld to set the player
    // model). Direct DB read; infrequent so no session caching.
    bool charmodel(const char *name, char *out, int max)
    {
        if(out && max > 0) out[0] = '\0';
        if(!name || !name[0]) return false;
        sqlite3 *cdb = chardb_open();
        if(!cdb) return false;
        bool ok = false;
        sqlite3_stmt *st = NULL;
        if(sqlite3_prepare_v2(cdb, "SELECT appearance FROM characters WHERE name=?1", -1, &st, NULL) == SQLITE_OK)
        {
            sqlite3_bind_text(st, 1, name, -1, SQLITE_STATIC);
            if(sqlite3_step(st) == SQLITE_ROW)
            {
                copystring(out, (const char *)sqlite3_column_text(st, 0), max);
                ok = true;
            }
            sqlite3_finalize(st);
        }
        sqlite3_close(cdb);
        return ok;
    }
    bool charcreate(const char *name, const char *archetype, const char *model)
    {
        if(!name || !name[0] || strlen(name) > 32) return false;
        sqlite3 *cdb = chardb_open();
        if(!cdb) return false;
        // enforce 3-slot cap
        sqlite3_stmt *st = NULL;
        int count = 0;
        if(sqlite3_prepare_v2(cdb, "SELECT COUNT(*) FROM characters", -1, &st, NULL) == SQLITE_OK)
        {
            if(sqlite3_step(st) == SQLITE_ROW) count = sqlite3_column_int(st, 0);
            sqlite3_finalize(st);
        }
        if(count >= 3) { sqlite3_close(cdb); return false; }
        bool ok = false;
        // v0.1.23: new chars start with 50 copper (STARTING_COPPER in rpg.h; literal here because SQL)
        if(sqlite3_prepare_v2(cdb, "INSERT INTO characters(name, level, xp, copper, archetype, appearance, created_at, last_seen) VALUES(?1, 1, 0, 50, ?2, ?3, ?4, ?4)", -1, &st, NULL) == SQLITE_OK)
        {
            sqlite3_bind_text(st, 1, name, -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 2, archetype ? archetype : "", -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 3, model ? model : "", -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(st, 4, (sqlite3_int64)time(NULL));
            ok = sqlite3_step(st) == SQLITE_DONE;
            sqlite3_finalize(st);
        }
        sqlite3_close(cdb);
        return ok;
    }
    bool chardelete(const char *name)
    {
        if(!name || !name[0]) return false;
        sqlite3 *cdb = chardb_open();
        if(!cdb) return false;
        // wipe character and all their rows
        const char *tables[] = { "characters", "rpg_inventory", "rpg_equipment", "rpg_quests", "rpg_clan_members", "rpg_mutes", "rpg_blocks" };
        sqlite3_stmt *st = NULL;
        for(size_t i = 0; i < sizeof(tables)/sizeof(tables[0]); i++)
        {
            defformatstring(sql)("DELETE FROM %s WHERE %s=?", tables[i], !strcmp(tables[i], "characters") || !strcmp(tables[i], "rpg_clan_members") ? "name" : "owner");
            // rpg_clan_members uses name; others use name or owner
            if(!strcmp(tables[i], "rpg_inventory") || !strcmp(tables[i], "rpg_equipment") || !strcmp(tables[i], "rpg_quests"))
                formatstring(sql)("DELETE FROM %s WHERE name=?", tables[i]);
            else if(!strcmp(tables[i], "rpg_mutes") || !strcmp(tables[i], "rpg_blocks"))
                formatstring(sql)("DELETE FROM %s WHERE owner=? OR target=?", tables[i]);
            else
                formatstring(sql)("DELETE FROM %s WHERE name=?", tables[i]);
            if(sqlite3_prepare_v2(cdb, sql, -1, &st, NULL) == SQLITE_OK)
            {
                sqlite3_bind_text(st, 1, name, -1, SQLITE_STATIC);
                if(!strcmp(tables[i], "rpg_mutes") || !strcmp(tables[i], "rpg_blocks"))
                    sqlite3_bind_text(st, 2, name, -1, SQLITE_STATIC);
                sqlite3_step(st);
                sqlite3_finalize(st);
            }
        }
        // if they were a clan leader, disbanding is complex; just remove (leader field may dangle - acceptable v1)
        sqlite3_close(cdb);
        return true;
    }

    // --- quest engine v2: client presentation caches (never authoritative) ---
    // One entry per quest the server has told us about. The tracker shows the
    // lowest-id active quest; the journal (J key) lists all of them.
    struct clquest { int state; int kills; int need; char objtext[160]; };
    static std::map<int, clquest> clquests;
    void setclientquest(int qid, int state, int kills, int need)
    {
        clquest &c = clquests[qid];
        c.state = state; c.kills = kills; c.need = need;
    }
    void setclientquesttext(int qid, const char *text)
    {
        if(!text) return;
        copystring(clquests[qid].objtext, text, sizeof(clquests[qid].objtext));
    }
    bool getclientquest(int &qid, int &state, int &kills, int &need)
    {
        // Tracked quest: the lowest-id quest with an in-progress (non-final) stage.
        for(std::map<int, clquest>::iterator it = clquests.begin(); it != clquests.end(); ++it)
        {
            const questdef *qd = questdefbyid(it->first);
            if(!qd) continue;
            if(it->second.state > 0 && it->second.state != qd->finalstage)
            {
                qid = it->first; state = it->second.state;
                kills = it->second.kills; need = it->second.need;
                return true;
            }
        }
        return false;
    }
    bool getclientquesttext(int qid, char *buf, int buflen)
    {
        std::map<int, clquest>::iterator it = clquests.find(qid);
        if(it == clquests.end() || !it->second.objtext[0]) return false;
        copystring(buf, it->second.objtext, buflen);
        return true;
    }
    int clientqueststate(int qid)
    {
        std::map<int, clquest>::iterator it = clquests.find(qid);
        return it == clquests.end() ? -1 : it->second.state;
    }
    bool isquestmobtarget(const char *monname)
    {
        if(!monname || !monname[0]) return false;
        for(std::map<int, clquest>::iterator it = clquests.begin(); it != clquests.end(); ++it)
        {
            const questdef *qd = questdefbyid(it->first);
            if(!qd) continue;
            if(it->second.state <= 0 || it->second.state == qd->finalstage) continue;
            const queststage *qs = queststageof(qd, it->second.state);
            if(!qs || qs->objtype != QOBJ_KILL) continue;
            if(!strcmp(qs->objarg, "any")) return true;
            if(!strcmp(qs->objarg, "champion"))
            {
                if(!strcmp(monname, CHAMPION_NAME)) return true;
                continue;
            }
            if(!strncmp(monname, qs->objarg, strlen(qs->objarg))) return true;
        }
        return false;
    }
    // --- radar combat tracking (Oct 21): client-side "in combat with" set ---
    // Keyed by monster clientnum (stable per spawn, visible to the radar's
    // client loop). Entries expire after REGEN_DELAY_MS with no new signal;
    // pruning happens on insert so the map can't grow over a long session.
    static std::map<int, int> combatmarks;
    void notecombatsignal(int clientnum)
    {
        if(clientnum < 0) return;
        combatmarks[clientnum] = lastmillis;
        for(std::map<int, int>::iterator it = combatmarks.begin(); it != combatmarks.end();)
        {
            if(lastmillis - it->second >= REGEN_DELAY_MS) { std::map<int, int>::iterator kill = it++; combatmarks.erase(kill); }
            else ++it;
        }
    }
    bool incombatwith(int clientnum)
    {
        std::map<int, int>::iterator it = combatmarks.find(clientnum);
        return it != combatmarks.end() && lastmillis - it->second < REGEN_DELAY_MS;
    }
    // --- J-key quest log: derived view (no new table) ---
    static std::vector<std::string> journalines;
    static void journaladdwrapped(const char *prefix, const char *text)
    {
        // Wrap at ~58 chars on word boundaries; continuation lines indent.
        char line[256];
        const char *p = text;
        bool first = true;
        while(*p)
        {
            const char *pre = first ? prefix : "  ";
            size_t prelen = strlen(pre);
            size_t maxw = 58 > prelen ? 58 - prelen : 10;
            size_t len = strlen(p);
            if(len > maxw)
            {
                len = maxw;
                while(len > 0 && p[len] != ' ' && p[len] != '\0') len--;
                if(len == 0) len = maxw;
            }
            copystring(line, pre, sizeof(line));
            strncat(line, p, min(len, sizeof(line) - strlen(line) - 1));
            journalines.push_back(std::string(line));
            p += len;
            while(*p == ' ') p++;
            first = false;
        }
        if(first) journalines.push_back(std::string(prefix)); // empty text
    }
    void rpg_buildjournal()
    {
        journalines.clear();
        journalines.push_back("=== Active Quests ===");
        bool anyactive = false, anydone = false;
        int nq = questdefcount();
        loopi(nq)
        {
            const questdef *qd = questdefbyindex(i);
            std::map<int, clquest>::iterator it = clquests.find(qd->qid);
            int st = (it == clquests.end()) ? -1 : it->second.state;
            if(st > 0 && st != qd->finalstage)
            {
                anyactive = true;
                journalines.push_back(std::string(qd->name));
                const queststage *qs = queststageof(qd, st);
                char obj[192] = "";
                if(it->second.objtext[0]) copystring(obj, it->second.objtext, sizeof(obj));
                else if(qs && qs->objtype == QOBJ_KILL)
                    formatstring(obj)("%d/%d slain", it->second.kills, qs->objcount);
                char desc[512];
                formatstring(desc)("%s%s%s", qs ? qs->journal : "",
                    obj[0] ? " [" : "", obj[0] ? obj : "");
                if(obj[0]) strcat(desc, "]");
                journaladdwrapped("  ", desc);
            }
        }
        if(!anyactive) journalines.push_back("  (none)");
        journalines.push_back("=== Completed ===");
        loopi(nq)
        {
            const questdef *qd = questdefbyindex(i);
            std::map<int, clquest>::iterator it = clquests.find(qd->qid);
            int st = (it == clquests.end()) ? -1 : it->second.state;
            if(st == qd->finalstage)
            {
                anydone = true;
                char done[512];
                formatstring(done)("* %s - %s", qd->name, qd->donetext);
                journaladdwrapped("  ", done);
            }
        }
        if(!anydone) journaladdwrapped("  ", "(none)");
    }
    int rpg_journal_lines() { return (int)journalines.size(); }
    void rpg_journal_line(int i, char *buf, int buflen)
    {
        if(i < 0 || i >= (int)journalines.size() || !buf || buflen <= 0) { if(buf && buflen > 0) buf[0] = '\0'; return; }
        copystring(buf, journalines[i].c_str(), buflen);
    }
}
