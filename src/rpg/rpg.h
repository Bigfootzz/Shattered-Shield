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
    // Reload quest + repeatable caches from the DB for an already-connected
    // client. Needed because onconnect runs before the first map change, so
    // the connect-time cache load ran with zero quest definitions and the
    // validquest filter dropped every stored row. Called after loadquests()
    // in changemap, once definitions exist; safe to repeat on later map
    // changes because quest/repeatable writes are write-through.
    void reloadquestcaches(const char *name);
    void savepos(const char *name, const char *map, float x, float y, float z, float yaw);

    // Award XP. Returns the new level, or -1 when the character is unknown
    // or the DB is closed. *leveledup is set true when a level threshold
    // was crossed (may be NULL). Persists immediately (crash-safe).
    int awardxp(const char *name, int amount, bool *leveledup = NULL);
    bool getstats(const char *name, int &level, int &xp);

    // Cumulative XP required to REACH the given level (2..MAX_LEVEL); -1 out of range.
    int xpforlevel(int level);

    // --- currency v1 (v0.1.23): copper/silver/gold, stored as total copper ---
    // 100 copper = 1 silver, 100 silver = 1 gold (1 gold = 10,000 copper).
    // Item `value` fields are in copper (potion 10c, sword 60c, etc.).
    // Sell price = value / 2 (rounded down), tunable later.
    // Starting copper for new characters: 50 (enough for potions, not gear).
    const int COPPER_PER_SILVER = 100;
    const int SILVER_PER_GOLD = 100;
    const int COPPER_PER_GOLD = 10000;
    const int STARTING_COPPER = 50;
    const int SELL_DIVISOR = 2; // sell price = value / SELL_DIVISOR
    int getcopper(const char *name);              // total copper, -1 when unknown
    int addcopper(const char *name, int amount);  // returns new total, -1 when unknown
    bool spendcopper(const char *name, int amount); // false when insufficient
    int sellprice(int itemidx);                   // value / SELL_DIVISOR, 0 when invalid

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

    // --- mob behaviour + player regen (Oct 20 design rules, additive) ---
    // World mobs on RPG maps are anchored to their spawn point: they stay
    // within MOB_ROAM_RADIUS of home while idle, engage only when damaged
    // (level <= 10 types), and break off + reset once a fight carries them
    // or their foe past MOB_LEASH_RANGE from home. AGGRO is the design unit
    // the leash derives from (leash = 2 x aggro, measured from the spawn
    // point, per the user's rule). Shared by the client AI (movement) and
    // the server (authoritative reset/regen), hence constants live here.
    const float MOB_AGGRO_RANGE = 30.0f;  // combat-zone unit around a mob's home
    const float MOB_LEASH_RANGE = 60.0f;  // 2 x aggro from the spawn point: break off + reset
    const float MOB_ROAM_RADIUS = 12.0f;  // anchored home roam radius while idle
    // Out-of-combat regen: no damage dealt or taken for REGEN_DELAY_MS, then
    // health and mana climb at REGEN_RATE_PCT of max per second (min
    // REGEN_RATE_MIN points/s) back to full. Wisdom's in-combat mana regen
    // is separate and unchanged.
    const int REGEN_DELAY_MS = 10000;
    const int REGEN_RATE_PCT = 2;
    const int REGEN_RATE_MIN = 2;
    // Combat bookkeeping for the regen rule (session-only, like mana).
    void notecombat(const char *name, int now); // stamp last damage dealt/taken
    int lastcombat(const char *name);           // millis of last combat, -1 when unknown

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
    const char *equipdeny(); // reason for the last equipitem refusal ("" when it did not refuse on class grounds)
    bool unequipslot(const char *name, int slot);
    int equippeditem(const char *name, int slot);          // item index, or -1
    int weapondamage(const char *name);                    // equipped mainhand bonus (0 when none)
    int armorvalue(const char *name);                      // sum of equipped armor values
    int usestack(const char *name, const char *itemid);     // consumable: heal amount, -1 when unusable
    // --- crafting Phase 2: timed stat buffs (buff consumables) ---
    // Buffs are session-bound: they persist through death (death never
    // touches this table) but are cleared on logout/disconnect, and a
    // fresh login never inherits prior-session rows. Expiry is server
    // clock (Unix time), so map changes and battleground mode changes
    // never reset the timer. Different buff types stack; the same type
    // never stacks (re-use refreshes the remaining time).
    const int BUFF_DURATION_SEC = 3600; // approved default (1 hour); the server
                                        // passes its rpg_buff_duration_sec VAR per use
    int buffmagnitude(int tier); // tier 0..4 -> 2/3/5/7/10, clamped
    bool applybuff(const char *name, int attr, int magnitude, int durationsec); // set/refresh one buff type
    int buffbonus(const char *name, int attr); // active magnitude, 0 when none/expired
    // --- crafting Phase 3: persistent gear bonuses (armor only) ---
    // gearbonus() sums equipped armor pieces' gearstat bonuses for one
    // attribute AFTER class weighting (favored full, rest half rounded
    // down); 0 when nothing equipped or class unknown. effectiveattr()
    // is the single stacking rule: base (= level) + gear (weighted) +
    // timed buff (full). Server-authoritative; recomputed on every read,
    // so equip/unequip/level/buff changes need no invalidation.
    int gearbonus(const char *name, int attr);
    int effectiveattr(const char *name, int level, int attr);
    int maxmanafor(const char *name, int level); // stat_maxmana with gear+buff Wisdom
    bool buffactive(const char *name, int attr, int &magnitude, int &secondsleft);
    int buffprune(const char *name, int *expired, int maxexpired); // drop expired rows, return count + attrs
    void clearbuffs(const char *name); // logout: drop all rows + cache
    int bufflist(const char *name, int *attrs, int *mags, int *secsleft, int max); // active buffs (debug/display)
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
    // Client copper cache (from N_RPG_GOLD, never authoritative).
    void setclientcopper(int copper);
    int getclientcopper(); // total copper, 0 when unset
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

    // --- quest chain v1: NPC quest givers (data-driven since editor Phase 1) ---
    // NPCs are NOT entities: one per-map data file (data/rpg/npcs_<map>.cfg)
    // is loaded at map change on both client and server. The client renders
    // each NPC's model idling at its map position, the pick code tests the
    // same cylinders, the radar reads the same positions, and the server
    // range-checks talk requests against the same coordinates. A hardcoded
    // fallback table (in rpg.cpp) is used ONLY when the data file is missing.
    struct npcdef
    {
        char id[64];       // stable id ("maren") used by quest data
        char name[96];     // display name ("Elder Maren")
        char model[128];   // model path ("npcs/female_peasant_static")
        float x, y, z;     // feet position on the map
        float yaw;         // facing (degrees, engine yaw convention)
        char dialogue[64]; // root dialogue graph name ("maren_main"); "vendor_main" = vendor marker
        char type[32];     // "questgiver", "vendor", "ambient" (editor Phase 1; behaviour in Phase 2/3)
    };
    int npccount();
    const npcdef *npcat(int i); // NULL when out of range
    int npcindexbyid(const char *id); // -1 when unknown
    // Editor Phase 1: load NPCs for a map from data/rpg/npcs_<map>.cfg.
    // Falls back to the hardcoded table when the file is missing or invalid.
    // Returns true when the data file was loaded, false on fallback.
    // Called by the game layer at map change (client changemapserv, server changemap).
    bool loadnpcs(const char *mapname);
    // The map name the NPC table was last loaded for ("" when never loaded).
    const char *npcs_loaded_map();
    // True when the current NPC table came from a data file (not fallback).
    bool npcs_from_file();
    // Editor Phase 1: write the current NPC table back to data/rpg/npcs_<map>.cfg
    // in ss_* syntax. Returns false on write failure. Does not touch the fallback.
    bool savenpcs(const char *mapname);
    // Editor Phase 1 mutations (in-memory only until savenpcs). Returns the new
    // index on add, or -1 when the table is full / id already exists.
    int npc_add(const char *id, const char *name, const char *model, float x, float y, float z, float yaw, const char *dialogue, const char *type);
    // Move/rotate an NPC in memory. Returns false when idx is out of range.
    bool npc_move(int idx, float x, float y, float z, float yaw);
    // Remove an NPC in memory (shifts later entries down). Returns false when idx is out of range.
    bool npc_remove(int idx);
    // Maximum NPCs per map (static storage, so npcat pointers stay stable).
    enum { MAXNPCS = 64 };

    // --- ambient monster spawns (world map): static spawn-point table ---
    // Each row is one spawn point. Adding monster types later is a DATA
    // change: put the type key in `type`; the server resolves it to a
    // monster type id ("zombie" = random zombie today; bestiary keys will
    // resolve in the same place when those types land in monster.cpp).
    struct spawndef
    {
        const char *type;   // monster set key, e.g. "zombie"
        float x, y, z;      // feet position on the map (spawner nudges +2)
        const char *area;   // human label for logs/docs, e.g. "south fields"
    };
    int spawnpointcount();
    const spawndef *spawnpoint(int i); // NULL when out of range

    // --- quest engine v2 (data-driven): quest + dialogue definitions ---
    // Quests, their staged objectives, and NPC dialogue node graphs are
    // CONTENT: data/rpg/quests.cfg, parsed once at init on both client and
    // server (same ss_* setter syntax as the item content files). Stages are
    // plain integers per quest; stage 0 = never started, `finalstage` is the
    // terminal stage. Dialogue is server-authoritative: the server picks the
    // node, sends node text + response buttons; the client sends back only a
    // button index. Conditions/actions are the CLOSED vocabularies below -
    // dialogue data can never execute arbitrary script.
    enum { QOBJ_NONE = 0, QOBJ_KILL, QOBJ_FETCH, QOBJ_TALK };
    struct queststage
    {
        int stage;            // stage number (>= 1)
        int objtype;          // QOBJ_*
        char objarg[64];      // kill: monster class ("any"/"champion"/"imp"...);
                              // fetch: unused (roll from fetch pool); talk: npc id
        int objcount;         // kill/fetch count
        char journal[256];    // journal line for this stage
    };
    struct questdef
    {
        int qid;              // permanent append-only id (1..3 Maren, 4 relay, 5 errand, 6 fetch)
        char key[64];         // "cull_the_imps"
        char name[96];        // "Cull the Imps"
        int giver;            // npc index (resolved from ss_quest_giver id)
        int finalstage;       // terminal stage value
        int repeatsec;        // >0: repeatable template (fetch), cooldown seconds after turn-in
        char donetext[256];   // journal line on completion
        int nstages;
        queststage stages[8];
        int npool;            // fetch pool item ids
        char pool[8][64];
        int poolmin, poolmax; // roll amount range
    };
    // Dialogue condition/action vocabularies (closed - add to the enum AND
    // the server executor; never arbitrary script).
    enum { CND_STAGE = 0, CND_HASITEM, CND_LEVEL, CND_QUESTDONE, CND_OBJREADY, CND_COOLDOWNREADY, CND_RECIPES };
    enum { CMP_EQ = 0, CMP_GE, CMP_LE, CMP_LT, CMP_GT };
    struct dlgcond
    {
        int verb;             // CND_*
        int qid;
        int op;               // CMP_* (stage/hasitem/level)
        int n;
        char item[64];        // hasitem
    };
    enum { ACT_SETSTAGE = 0, ACT_COMPLETEQUEST, ACT_GIVEITEM, ACT_TAKEITEM,
           ACT_GIVEXP, ACT_GIVEFETCHXP, ACT_SPAWNCHAMPION, ACT_ROLLFETCH,
           ACT_GIVEMONEY };
    struct dlgaction
    {
        int verb;             // ACT_*
        int qid;
        int n;                // setstage stage / givexp amount / givemoney copper
        char item[64];        // giveitem/takeitem ("rolled" = the fetch roll)
        int count;
    };
    struct dlgresponse
    {
        char text[160];
        int ncond; dlgcond cond[2];
        int nact;  dlgaction act[6];
        char gotonode[64];    // empty = none
        bool close;
    };
    struct dlgnode
    {
        char id[64];
        int quest;            // qid for $var substitution (0 = none)
        int nwhen; dlgcond when[4];
        int nsay;  char say[6][256];
        int nresp; dlgresponse resp[4];
    };
    struct dlggraph
    {
        char name[64];
        int nnodes; dlgnode nodes[48];
    };
    // Content API (both sides). loadquests() runs after NPCs load at map
    // change, retried until success; defs are read-only afterwards.
    // Returns false when data/rpg/quests.cfg failed validation
    // (the server logs the reason and keeps running with no quest content).
    bool loadquests();
    int questdefcount();
    const questdef *questdefbyid(int qid);      // NULL when unknown
    const questdef *questdefbyindex(int i);     // 0..count-1
    const dlggraph *questgraph(const char *name); // NULL when unknown
    const queststage *queststageof(const questdef *qd, int stage); // NULL when no such stage
    bool validquest(int qid); // true when a quest def exists for qid
    // Entry node selection: first node (definition order) whose when-clauses
    // all hold; no-when nodes only match at entry when they are the graph's
    // last node (the fallback). Goto-only nodes never match at entry.
    // Returns the node index, or -1 when the graph is empty (never when valid).
    int dlgnode_selectentry(const dlggraph *g, const char *name);
    // $var substitution for node text: $progress/$target (kill), $amount/$item
    // (fetch), $name. Server-side (needs the character's repeatable row).
    void questtextsubst(const char *name, int qid, const char *in, char *out, int outlen);

    // --- quest engine v2: server-side per-character state (persisted in rpg.db) ---
    // Stages are integers: 0 = never started (no row yet), 1..N in progress,
    // finalstage = done. questsetstage upserts and resets progress; write-through.
    int queststate(const char *name, int qid);          // stage, 0 when never started
    int questkills(const char *name, int qid);          // progress counter for kill stages
    void questsetstage(const char *name, int qid, int stage);
    void questaddprogress(const char *name, int qid, int n = 1); // kill stages only
    bool questiscomplete(const char *name, int qid);    // stage == finalstage
    // Objective bookkeeping for sync/display. have/need: kill -> kills/count;
    // fetch -> min(have,amount)/amount; talk/none -> 0/0.
    void questprogress(const char *name, int qid, int &have, int &need);
    // Server-evaluated condition (needs character state). Client never evaluates.
    bool dlgcondmet(const char *name, const dlgcond &c);
    // Called from the server kill path for every monster kill by a player.
    // Counts the kill toward EVERY active quest whose current stage has a
    // matching kill objective ("any" matches any monster; "champion" only the
    // tracked champion clientnum; otherwise the victim's class name). Party
    // credit is preserved: the caller loops party members. Appends counted
    // qids to `counted` (up to maxcounted) and returns how many counted.
    int questonkill(const char *name, const char *victimclass, int targetclientnum,
                    bool ismonster, int *counted, int maxcounted);
    // --- crafting recipes (Phase 1, Oct 27): per-character discovered
    // recipes, additive table rpg_recipes. One row per (character, recipe
    // key); the key is the canonical ingredient key from crafting.h, so a
    // recipe discovered in either slot order is one row. Server-side only;
    // the client displays the server's formatted lines, never its own rows.
    bool recipediscovered(const char *name, const char *key);
    void discoverrecipe(const char *name, const char *key);
    // Discovered keys in discovery order (rowid order). Returns count filled.
    int recipelist(const char *name, char keys[][256], int maxkeys);
    // One row per (character, quest): the rolled item/amount and the earliest
    // re-accept time. Server-side only.
    bool repeatableready(const char *name, int qid); // cooldown elapsed (or never rolled)
    bool repeatableroll(const char *name, int qid);  // roll item+amount from the pool
    bool repeatableget(const char *name, int qid, char *item, int itemlen, int *amount);
    void repeatablesetcooldown(const char *name, int qid); // readyat = now + repeatsec
    // --- party + safety v1 (Oct 19): per-player mute/block lists, persisted ---
    // in rpg.db (additive tables rpg_mutes / rpg_blocks). Used server-side.
    bool ismuted(const char *owner, const char *target);
    bool togglemute(const char *owner, const char *target); // returns true when now muted
    bool isblocked(const char *owner, const char *target);
    bool toggleblock(const char *owner, const char *target); // returns true when now blocked
    // --- clan system v1 (Oct 19): persisted in rpg.db, server-validated ---
    // ranks: 0 = Leader, 1 = Officer, 2 = Member
    enum { CLAN_RANK_LEADER = 0, CLAN_RANK_OFFICER = 1, CLAN_RANK_MEMBER = 2 };
    int clan_create(const char *name, const char *tag, const char *leader); // returns id, -1 if name taken
    int clan_of(const char *name); // clan id, 0 if none
    int clan_rank(const char *name); // rank, -1 if none
    bool clan_info(int id, char *name, char *tag, char *leader); // true if found
    bool clan_addmember(int id, const char *name, int rank);
    void clan_removemember(const char *name);
    bool clan_setrank(const char *name, int rank);
    void clan_disband(int id);
    int clan_membercount(int id);
    void clan_tagof(const char *name, char *tag); // empty if none
    int clan_memberlist(int id, char names[][64], int *ranks, int max); // returns count
    // --- clan system v1: client presentation cache (never authoritative) ---
    void setclientclan(int id, int rank, const char *name, const char *tag);
    bool getclientclan(int &id, int &rank, char *name, char *tag); // true when in a clan
    // --- character select (ITEM 5): client-side direct rpg.db access ---
    // Lists up to max characters (name, level, archetype, model). Returns count.
    // Opens its own SQLite handle; safe to call from the main menu.
    int charlist(char names[][64], int *levels, char archetypes[][32], char models[][64], int max);
    bool chararchetype(const char *name, char *out, int max); // archetype for one character ("" if unknown)
    bool charmodel(const char *name, char *out, int max); // model dir for one character ("" if unknown)
    bool charcreate(const char *name, const char *archetype, const char *model); // false if name taken or 3+ chars
    bool chardelete(const char *name);
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
    enum { RPG_HOVER_DEFAULT = 0, RPG_HOVER_LOOT = 1, RPG_HOVER_HOSTILE = 2, RPG_HOVER_QUEST = 3, RPG_HOVER_TURNIN = 4 };
    void cursoraccum(int dx, int dy);              // add relative motion to the free cursor
    void cursorget(float &x, float &y);            // raw accumulated cursor pos (screen px)
    void cursorset(float x, float y);
    void cursorclamp(float &x, float &y, int w, int h);
    int  hoverstate(); void sethoverstate(int s);  // HOVER_* above
    int  selectedtarget(); void selecttarget(int clientnum); // -1 = none
    int  selectednpc(); void selectnpc(int idx);             // -1 = none (quest chain v1)
    bool autoattacking(); void setautoattack(bool on);
    void cleartargeting();                          // deselect + stop auto-attack (map change, death)

    // --- quest engine v2: client presentation caches (never authoritative) ---
    // Per-quest sync from the server (N_RPG_QUEST + N_RPG_QTEXT). Drives the
    // on-screen quest tracker, the radar's quest-mob highlight, and the J-key
    // quest log (a derived view - no new table).
    void setclientquest(int qid, int state, int kills, int need);
    void setclientquesttext(int qid, const char *text); // objective display text
    // Bundle (Oct 28): HUD buff display, client presentation cache (never
    // authoritative). One entry per active buff type the server has told
    // us about (attr -> server-clock expiry + magnitude). The server syncs
    // on use, expiry, and every map change; buffs clear server-side on logout.
    // v0.1.37: magnitude rides the sync so the buff-row hover tooltip can
    // show "+N Strength" (tier backgrounds stay inventory-only).
    void setclientbuff(int attr, int expires, int mag);
    void clearclientbuffs();
    int clientbufflist(int *attrs, int *exps, int *mags, int max); // active, expiry-pruned, attr order
    bool getclientquest(int &qid, int &state, int &kills, int &need); // tracked quest
    bool getclientquesttext(int qid, char *buf, int buflen);
    int clientqueststate(int qid); // -1 when the client has no row for qid
    // True when a monster with this display name is a current quest target.
    bool isquestmobtarget(const char *monname);
    // --- radar combat tracking (Oct 21): client-side "in combat with" set ---
    // The client learns about combat from N_DAMAGE (player damages a mob, or
    // a mob damages the player). A mob counts as in combat with the player
    // while damage was exchanged within REGEN_DELAY_MS (the game's own
    // out-of-combat window). The radar uses this for its aggro override: a
    // mob marker is drawn when the mob is an active quest objective OR in
    // combat with the player - never hide something that is attacking you.
    void notecombatsignal(int clientnum); // record a damage exchange with this mob
    bool incombatwith(int clientnum);     // damage exchanged within REGEN_DELAY_MS
    void rpg_buildjournal(); // rebuild the J-key log from the quest caches
    int rpg_journal_lines();
    void rpg_journal_line(int i, char *buf, int buflen);
}
