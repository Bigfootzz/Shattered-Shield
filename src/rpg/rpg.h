// Shattered Shield RPG layer — persistence foundation (milestone steps 1+2).
//
// Purely additive to the engine: identity for this pass is the player's
// in-game name. Accounts/auth are a separate later system (see the gap map).
// The level table below is the single data-driven place for thresholds.
#pragma once

#include <cstddef>
#include "items.h"

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

    // --- inventory & equipment v1 (milestone step 4): server state ---
    // The RPG layer is active on the RPG maps only (world + battleground).
    // Equipment effects (weapon damage, armor) and loot apply there and only
    // there, so FPS-mode behavior on other maps is never altered.
    bool mapactive(const char *map);

    struct itemstack { int item; int count; }; // item = index into the items table

    // All server-side and server-validated. Mutations persist write-through.
    bool additem(const char *name, const char *itemid, int count);    // false: unknown item/char
    bool removeitem(const char *name, const char *itemid, int count);// false: not enough
    int itemcount(const char *name, const char *itemid);
    bool getinventory(const char *name, itemstack *out, int maxout, int &n); // session cache
    bool equipitem(const char *name, const char *itemid);  // validates slot; swaps occupied slot back to inv
    bool unequipslot(const char *name, int slot);
    int equippeditem(const char *name, int slot);          // item index, or -1
    int weapondamage(const char *name);                    // equipped mainhand bonus (0 when none)
    int armorvalue(const char *name);                      // sum of equipped armor values
    int usestack(const char *name, const char *itemid);     // consumable: heal amount, -1 when unusable
    const char *equippedvwep(const char *name);             // equipped weapon model path, or NULL
    void saveinv(const char *name);                        // write-through persistence of inv+equipment

    // --- loot drops v1 (milestone step 4): server-side ---
    struct lootdrop { int idx; int item; float x, y, z; int born; };
    int spoilloot(int item, float x, float y, float z);     // returns drop idx, -1 on bad item
    void setlootborn(int idx, int now);                   // stamp server time at spawn
    bool getlootdrops(lootdrop *out, int maxout, int &n);
    bool takeloot(int idx);                                 // remove (picked up / expired)
    void updateloot(int now, int *expired, int maxexpired, int &nexpired); // expire old drops
    const int LOOT_LIFETIME_MS = 120000;
    const float LOOT_PICKUP_DIST = 3.0f;

    // --- client presentation caches (milestone step 4) ---
    // Filled by the N_RPG_* handlers in the client; the rpg module is linked
    // into the client too, so the GUI/HUD read these. Never authoritative.
    void setvwep(int clientnum, const char *path);   // "" clears the override
    const char *vwepfor(int clientnum);               // NULL = use the model's default vwep
    void setmana(int mana, int maxmana);
    bool getclientmana(int &mana, int &maxmana);
    void setcooldown(int ability, int waitms, int now);
    int cooldownleft(int ability, int now);           // ms remaining, 0 when ready
    void setinv(const char *invstr, const char *equipstr, int level, int xp); // "id:count;..." / "slot:id;..."
    bool getclientinv(itemstack *out, int maxout, int &n, int equipped[SLOT_COUNT]);
    bool getclientstats(int &level, int &xp); // level/xp from the last N_RPG_INV
    void setclientxp(int xp); // TEST-ONLY: set client XP for HUD verification
    void setloot(int idx, int item, float x, float y, float z);
    void removeloot(int idx);
    void clearloot();
    bool getclientloot(lootdrop *out, int maxout, int &n);

    // --- RPG radar marker plumbing (milestone: radar v1) ---
    // Marker categories for the PvE radar. NPCs, the quest system, and
    // gathering nodes do NOT exist in the game yet: their marker sources are
    // wired here and return 0 today, and light up automatically once those
    // systems land (quest chain v1 is next in the queue). Hostile mobs are
    // enumerated live by the HUD from AI state. Never fake data for the
    // empty categories. Plain float triples (x,y,z); no engine symbols, so
    // the server link is unaffected.
    enum { RPG_MARK_NPC = 0, RPG_MARK_QUESTMOB = 1, RPG_MARK_HOSTILE = 2, RPG_MARK_GATHER = 3, RPG_MARK_N = 4 };
    int npcmarkers(float *out, int maxout);       // 0 today: no NPC system yet
    int questmobmarkers(float *out, int maxout); // 0 today: no quest system yet
    int gathermarkers(float *out, int maxout);    // 0 today: no gathering yet

    // --- quest chain v1: NPC quest givers (static data, shared client+server) ---
    // NPCs are NOT entities: one static table both sides read. The client
    // renders each NPC's model idling at its map position, the pick code
    // tests the same cylinders, the radar reads the same positions, and the
    // server range-checks talk requests against the same coordinates.
    struct npcdef
    {
        const char *name;   // display name ("Elder Maren")
        const char *model;  // model path ("playermodels/peasant_female")
        float x, y, z;      // feet position on the map
        float yaw;          // facing (degrees, engine yaw convention)
    };
    int npccount();
    const npcdef *npcat(int i); // NULL when out of range

    // --- quest chain v1: quest definitions (static, shared) ---
    enum { QUEST_NONE = 0, QUEST_CULL = 1, QUEST_WAVES = 2, QUEST_CHAMPION = 3, QUEST_DONE = 4 };
    enum { QSTATE_OFFERED = 0, QSTATE_ACTIVE = 1, QSTATE_COMPLETE = 2 };
    struct questdef
    {
        const char *name;        // "Cull the Undead"
        const char *objective;   // "Slay 5 zombies"
        const char *mobname;     // monster display name counted ("zombie"), or champion name
        int need;                // kills required
        int xpreward;            // XP on turn-in
        const char *itemreward;  // item id granted on turn-in, or NULL
    };
    const questdef &quest(int qid); // qid 1..3; QUEST_NONE/QUEST_DONE return an empty def

    // --- quest chain v1: server-side per-character state (persisted in rpg.db) ---
    // chainpos: next quest id to offer (1..3), or QUEST_DONE when the chain
    // is fully turned in. queststate: QSTATE_* or -1 when never offered.
    int chainpos(const char *name);
    int queststate(const char *name, int qid);
    int questkills(const char *name, int qid);
    void questsetoffered(const char *name, int qid);
    void questsetactive(const char *name, int qid);   // (re)starts: kills reset to 0
    void questaddkill(const char *name, int qid);     // kills++, write-through
    void questsetcomplete(const char *name, int qid);
    // Called from the server kill path for every monster kill by a player.
    // Returns the quest id when the kill counted toward an active quest, else 0.
    // Server-validated: only counts while that quest is ACTIVE; champion kills
    // only count when the victim is the tracked champion clientnum.
    int questonkill(const char *name, int targetclientnum, bool ismonster);
    // --- party + safety v1 (Oct 19): per-player mute/block lists, persisted ---
    // in rpg.db (additive tables rpg_mutes / rpg_blocks). Used server-side.
    bool ismuted(const char *owner, const char *target);
    bool togglemute(const char *owner, const char *target); // returns true when now muted
    bool isblocked(const char *owner, const char *target);
    bool toggleblock(const char *owner, const char *target); // returns true when now blocked
    // Champion tracking (server-side session state; the champion is a real
    // monster client spawned by the quest system, not a test spawn).
    void setchampioncn(int cn);
    int championcn();          // -1 when none tracked
    void clearchampioncn();

    // --- mouse & targeting v1 (milestone step 6): client presentation state ---
    // Plain data only (no engine symbols) so the server link is unaffected.
    // Selection + auto-attack are client intent; the server re-validates every
    // cast exactly as before. NPC selection rides alongside monster selection
    // (quest chain v1): selecting one clears the other.
    enum { HOVER_DEFAULT = 0, HOVER_LOOT = 1, HOVER_HOSTILE = 2 };
    void cursoraccum(int dx, int dy);              // add relative motion to the free cursor
    void cursorget(float &x, float &y);            // raw accumulated cursor pos (screen px)
    void cursorset(float x, float y);
    void cursorclamp(float &x, float &y, int w, int h);
    int  hoverstate(); void sethoverstate(int s);  // HOVER_* above
    int  selectedtarget(); void selecttarget(int clientnum); // -1 = none
    int  selectednpc(); void selectnpc(int idx);             // -1 = none (quest chain v1)
    bool autoattacking(); void setautoattack(bool on);
    void cleartargeting();                          // deselect + stop auto-attack (map change, death)

    // --- quest chain v1: client presentation caches (never authoritative) ---
    // Active-quest sync from the server (N_RPG_QUEST). Drives the on-screen
    // quest tracker and the radar's orange quest-mob split.
    void setclientquest(int qid, int state, int kills, int need);
    bool getclientquest(int &qid, int &state, int &kills, int &need);
    // True when a monster with this display name is a current quest target.
    // Quest 1/2 count any zombie; quest 3 counts only the Rotting Champion.
    bool isquestmobtarget(const char *monname);
}
