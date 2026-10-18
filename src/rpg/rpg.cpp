// Shattered Shield RPG persistence (milestone steps 1+2).
// SQLite-backed character store. Identity = player name for now.
#include "rpg.h"
#include "sqlite3.h"
#include "cube.h"

#include <ctime>
#include <map>
#include <string>

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
    struct cachedchar
    {
        int level;
        int xp;
    };
    static std::map<std::string, cachedchar> cache;

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
        cache[std::string(name)] = cc;
        return existed;
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
}
