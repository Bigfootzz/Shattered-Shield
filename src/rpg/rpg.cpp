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
        int mana;
        int lastcast[ABILITY_COUNT]; // millis of last successful cast per ability
        int lastregen;               // millis of last regen tick (<0 = not started)
        std::map<int, int> inv;      // item index -> count (session view of rpg_inventory)
        int equipped[SLOT_COUNT];    // item index per slot, -1 = empty (session view of rpg_equipment)
    };

    static void initinv(cachedchar &cc)
    {
        cc.inv.clear();
        loopi(SLOT_COUNT) cc.equipped[i] = -1;
    }
    static std::map<std::string, cachedchar> cache;

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
                "map TEXT NOT NULL DEFAULT '',"
                "posx REAL NOT NULL DEFAULT 0, posy REAL NOT NULL DEFAULT 0, posz REAL NOT NULL DEFAULT 0,"
                "yaw REAL NOT NULL DEFAULT 0,"
                "archetype TEXT NOT NULL DEFAULT '',"
                "appearance TEXT NOT NULL DEFAULT '',"
                "created_at INTEGER NOT NULL DEFAULT 0,"
                "last_seen INTEGER NOT NULL DEFAULT 0)");
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

    static bool loadrow(const char *name, int &level, int &xp)
    {
        sqlite3_stmt *st = NULL;
        if(sqlite3_prepare_v2(db, "SELECT level, xp FROM characters WHERE name=?1", -1, &st, NULL) != SQLITE_OK) return false;
        sqlite3_bind_text(st, 1, name, -1, SQLITE_TRANSIENT);
        bool found = sqlite3_step(st) == SQLITE_ROW;
        if(found) { level = sqlite3_column_int(st, 0); xp = sqlite3_column_int(st, 1); }
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

    bool onconnect(const char *name)
    {
        if(!db || !name || !name[0]) return false;
        int level = 1, xp = 0;
        bool existed = loadrow(name, level, xp);
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
        initcaststate(cc, level, -1); // mana full, abilities ready; regen starts on first tick
        initinv(cc);
        cache[std::string(name)] = cc;
        // --- inventory & equipment v1 (milestone step 4) ---
        loadinv(name); // rows into the session cache (no-op when none)
        if(!existed)
        {
            // starter kit: a stick in hand and two potions in the pack
            additem(name, "puglin_stick", 1);
            equipitem(name, "puglin_stick");
            additem(name, "health_potion", 2);
        }
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
        if(itemid < 0 || itemid >= ITEM_COUNT) return -1;
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
    static int clhover = HOVER_DEFAULT;
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
    int selectedtarget() { return clselclient; }
    void selecttarget(int clientnum) { clselclient = clientnum; }
    bool autoattacking() { return clautoattack; }
    void setautoattack(bool on) { clautoattack = on; }
    void cleartargeting() { clselclient = -1; clautoattack = false; }
}
