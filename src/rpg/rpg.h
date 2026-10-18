// Shattered Shield RPG layer — persistence foundation (milestone steps 1+2).
//
// Purely additive to the engine: identity for this pass is the player's
// in-game name. Accounts/auth are a separate later system (see the gap map).
// The level table below is the single data-driven place for thresholds.
#pragma once

#include <cstddef>

namespace rpg
{
    // XP awarded server-side per monster kill (additive to kill handling)
    const int XP_PER_MONSTER_KILL = 25;
    // Highest level covered by the threshold table
    const int MAX_LEVEL = 10;

    struct charinfo
    {
        int level;
        int xp;
    };

    void init();        // open/create the character DB in the engine home dir; create schema
    void shutdown();    // close the DB (sqlite recovers cleanly if the process just exits)
    bool connected();   // true once init() has opened the DB
    const char *dbpath();

    // Character lifecycle. Returns true when the character already existed
    // (row loaded), false when it was created fresh with defaults.
    bool onconnect(const char *name);
    void ondisconnect(const char *name, const char *map, float x, float y, float z, float yaw);
    void savepos(const char *name, const char *map, float x, float y, float z, float yaw);

    // Award XP. Returns the new level, or -1 when the character is unknown
    // or the DB is closed. *leveledup is set true when a level threshold
    // was crossed (may be NULL). Persists immediately (crash-safe).
    int awardxp(const char *name, int amount, bool *leveledup = NULL);
    bool getstats(const char *name, int &level, int &xp);

    // Cumulative XP required to REACH the given level (2..MAX_LEVEL); -1 out of range.
    int xpforlevel(int level);

    // --- ability layer v1 (milestone step 3): session state, server-authoritative ---
    // Mana and cooldowns live only for connected characters (cleared on
    // disconnect); they are NOT persisted to the DB. The server enforces all
    // of it — the client only asks to cast.
    bool getmana(const char *name, int &mana, int &maxmana);
    // True when the cooldown is ready AND marks it consumed; false sets waitms
    // (millis until ready). Call only after every other validation passes, so
    // failed casts never consume the cooldown.
    bool castready(const char *name, int ability, int now, int &waitms);
    bool spendmana(const char *name, int cost);  // false when mana < cost
    void regen(int now);                         // mana regen tick for all cached chars

    // --- test-only monster pinning (milestone step 3) ---
    // Lets the scripted harness park a monster at the caster server-side.
    // The owning client still simulates/renders it at its own spawn point,
    // but the server ignores its N_POS while pinned, so ability targeting
    // (which is server-side) sees the pinned position. Cleared on death.
    void pinmonster(int clientnum);
    void unpinmonster(int clientnum);
    bool ispinmonster(int clientnum);
}
