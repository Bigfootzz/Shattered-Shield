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
}
