// Shattered Shield RPG — item definitions (milestone step 4: inventory & equipment v1;
// content data layer: crafting build step 0, Oct 20).
//
// Items are CONTENT, loaded from data/rpg/items.cfg at startup (see items.cpp).
// The compiled-in 6-item table remains as a validated fallback: if the data
// files are missing or fail validation, the game runs on it exactly as before.
// Everything else (inventory, loot, equip, UI) reads the ACTIVE table by item
// id through the accessors below. The server is authoritative — item stats
// never cross the wire from the client.
//
// Equipped ARMOR is stats + icons only; visual outfit swapping on equip is a
// later system (explicitly out of scope).
#pragma once

#include <string>

namespace rpg
{
    // Item types
    enum
    {
        ITEM_WEAPON = 0,
        ITEM_ARMOR,
        ITEM_CONSUMABLE,
        ITEM_MATERIAL,   // crafting ingredient: no use, no equip, stacks
        ITEM_TOOL,       // crafting catalyst: required to craft, never consumed
        ITEM_QUEST       // quest item: carried for quest objectives, no use, no equip, stacks
    };

    // Equipment slots (SLOT_NONE = not equippable, e.g. consumables)
    // Neck/rings are stats-only: no 3D model or vwep is shown for them on
    // the character (Ed, Oct 24) — they exist to add stats and balance to
    // the character sheet. Ring slots are distinct: a ring1 item equips
    // only to ring1, a ring2 item only to ring2 (no first-free-slot logic).
    enum
    {
        SLOT_MAINHAND = 0,
        SLOT_HEAD,
        SLOT_TORSO,
        SLOT_LEGS,
        SLOT_FEET,
        SLOT_NECK,
        SLOT_RING1,
        SLOT_RING2,
        SLOT_COUNT,
        SLOT_NONE = -1
    };

    struct itemdef
    {
        const char *id;    // stable id, used in DB rows, loot, console
        const char *name;  // display name
        int type;          // ITEM_* above
        int slot;          // SLOT_* above (SLOT_NONE for non-gear)
        const char *icon;  // packages/ path to the 64px sliced icon PNG
        const char *vwep;  // weapons only: model path attached at tag_weapon; else NULL
        const char *weaponcat; // weapons only: category for class gating
                           // ("mace"|"axe"|"sword"|"staff"|"wand"|"orb"|
                           // "bow"|"crossbow"), or NULL for legacy weapons
                           // with no category (equippable by all classes).
                           // Data files set it with ss_item_weaponcat.
        int damage;        // weapons: bonus added to melee ability (Power Strike) damage
        int armor;         // armor: flat reduction of incoming monster damage
        int heal;          // consumables: HP restored on use
        bool stackable;    // consumables/materials stack by count; gear does not
        int value;         // base worth: fetch-quest rewards read it now in data;
                           // no gameplay consumer until merchants (crafting design §5.4)
        int mana;          // consumables: mana restored on use (0 = none)
        int tier;          // rarity tier (crafting Phase 1): 0 common, 1 uncommon,
                           // 2 rare, 3 epic, 4 legendary. Default 0; data files
                           // set it with ss_item_tier. Crafting output tier is
                           // computed from ingredient tiers, never stored here.
        int buffattr;      // buff consumables (crafting Phase 2): ATTR_* from stats.h
                           // that this item raises while active, or -1 for non-buff
                           // items. Magnitude comes from the item's tier via
                           // rpg::buffmagnitude(), never stored here. Data files
                           // set it with ss_item_buff; buff items must not heal.
    };

    int itemcount();                    // number of defs in the ACTIVE table
    const itemdef &item(int id);        // id in [0, itemcount())
    int finditem(const char *id);       // -1 when unknown
    const char *itemname(int id);       // display name, "" when invalid
    const char *slotname(int slot);     // "mainhand".."ring2", "" when invalid
    int slotid(const char *name);       // SLOT_* or SLOT_NONE

    // --- content data layer (crafting build step 0) ---
    // Loads the data/rpg/*.cfg content set (items + loot here; recipes via
    // crafting.cpp), validates it as a whole, and swaps it in atomically.
    // Any hard error — or a missing items.cfg — keeps the compiled-in
    // fallback active and logs the reason loudly. Idempotent per process;
    // called from server init and from client connect. Returns true when
    // data content is active afterwards.
    bool loadcontent();
    bool contentfromdata();             // true when the active tables came from data files

    // Content-file plumbing: the definition files use the flat ss_* setter
    // shape from the design (one command per line, quoted string / integer
    // args, // comments). They are parsed strictly — NOT executed: the rpg
    // module is compiled without the script interpreter (in non-client
    // builds ICOMMAND expands to nothing and execfile is not even declared),
    // so a small dedicated parser is the one loading path that behaves
    // identically in every build flavor. Unknown commands and malformed
    // lines are hard errors. parsecontentfile returns false on a read or
    // parse failure and describes it in err (with file:line).
    typedef bool (*sslinehandler)(const char *cmd, int argc, const char *argv[], std::string &err);
    bool parsecontentfile(const char *path, sslinehandler handler, std::string &err);
    bool contentfileexists(const char *path);

    // --- loot tables (crafting build step 0: per-type, from data) ---
    // Weighted tables rolled server-side on RPG monster kills. typekey is
    // the monster class key ("zombie", "imp", "puglin"); a type with no
    // data table falls back to a "default" data table when one exists,
    // else to the compiled-in global table (pre-content-layer behavior).
    // Returns an item index, or -1 for "no drop".
    int rollloot(const char *typekey);
    // v0.1.23: mob coin drops (MMO research: WoW mobs drop coins directly,
    // in addition to items). Returns copper amount for the mob type, 0 for
    // unknown types. Early-game amounts (copper range, not silver/gold):
    // zombie (lvl2) 1-4c, imp (lvl4) 3-8c, puglin (lvl6) 5-12c, champion 25-50c.
    // Coin drops are granted directly to the killer on mob death, separate
    // from the item loot roll (which may also drop an item).
    int rollcoins(const char *typekey);
}
