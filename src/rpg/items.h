// Shattered Shield RPG — item definitions (milestone step 4: inventory & equipment v1).
//
// Data-driven item table: the single place for item tuning. Everything else
// (inventory, loot, equip, UI) reads this table by item id. The server is
// authoritative — item stats never cross the wire from the client.
//
// v1 scope: a small starter set. Equipped ARMOR is stats + icons only;
// visual outfit swapping on equip is a later system (explicitly out of v1).
#pragma once

namespace rpg
{
    // Item types
    enum
    {
        ITEM_WEAPON = 0,
        ITEM_ARMOR,
        ITEM_CONSUMABLE
    };

    // Equipment slots (SLOT_NONE = not equippable, e.g. consumables)
    enum
    {
        SLOT_MAINHAND = 0,
        SLOT_HEAD,
        SLOT_TORSO,
        SLOT_LEGS,
        SLOT_FEET,
        SLOT_COUNT,
        SLOT_NONE = -1
    };

    struct itemdef
    {
        const char *id;    // stable id, used in DB rows, loot, console
        const char *name;  // display name
        int type;          // ITEM_* above
        int slot;          // SLOT_* above (SLOT_NONE for consumables)
        const char *icon;  // packages/ path to the 64px sliced icon PNG
        const char *vwep;  // weapons only: model path attached at tag_weapon; else NULL
        int damage;        // weapons: bonus added to melee ability (Power Strike) damage
        int armor;         // armor: flat reduction of incoming monster damage
        int heal;          // consumables: HP restored on use
        bool stackable;    // consumables stack by count; gear does not
    };

    const int ITEM_COUNT = 6;
    const itemdef &item(int id);        // id in [0, ITEM_COUNT)
    int finditem(const char *id);      // -1 when unknown
    const char *itemname(int id);      // display name, "" when invalid
    const char *slotname(int slot);    // "mainhand".."feet", "" when invalid
    int slotid(const char *name);      // SLOT_* or SLOT_NONE

    // --- loot table v1 (milestone step 4) ---
    // Simple weighted table, rolled server-side on RPG monster kills.
    // Returns an item id, or -1 for "no drop".
    int rollloot();
}
