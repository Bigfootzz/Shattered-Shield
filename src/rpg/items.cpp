// Shattered Shield RPG — item table + loot table (milestone step 4).
#include "items.h"

#include <cstdlib>
#include <cstring>

namespace rpg
{
    // Icon art: System G6/Qoma RPG Icons Set (CC0), sliced to 64px cells under
    // packages/ui/rpg/icons/. Weapon models: Quaternius Bestiary (Imp mace,
    // Puglin stick) converted to IQM vweps on the tag_weapon path.
    static const itemdef ITEMS[ITEM_COUNT] =
    {
        // id              name            type            slot           icon                                        vwep                         dmg  arm  heal  stack
        { "imp_mace",      "Imp Mace",     ITEM_WEAPON,    SLOT_MAINHAND, "packages/ui/rpg/icons/icon_mace.png",     "playermodels/vwep/mace",     15,  0,   0,   false },
        { "puglin_stick",  "Puglin Stick", ITEM_WEAPON,    SLOT_MAINHAND, "packages/ui/rpg/icons/icon_stick.png",    "playermodels/vwep/stick",    8,   0,   0,   false },
        { "ranger_tunic",  "Ranger Tunic", ITEM_ARMOR,     SLOT_TORSO,    "packages/ui/rpg/icons/icon_tunic.png",    NULL,                          0,  3,   0,   false },
        { "leather_helm",  "Leather Helm", ITEM_ARMOR,     SLOT_HEAD,     "packages/ui/rpg/icons/icon_helm.png",     NULL,                          0,  1,   0,   false },
        { "iron_boots",    "Iron Boots",   ITEM_ARMOR,     SLOT_FEET,     "packages/ui/rpg/icons/icon_boots.png",    NULL,                          0,  2,   0,   false },
        { "health_potion", "Health Potion",ITEM_CONSUMABLE,SLOT_NONE,     "packages/ui/rpg/icons/icon_potion.png",   NULL,                          0,  0,   40,  true  },
    };

    const itemdef &item(int id)
    {
        static const itemdef invalid = { "", "", ITEM_CONSUMABLE, SLOT_NONE, "", NULL, 0, 0, 0, false };
        if(id < 0 || id >= ITEM_COUNT) return invalid;
        return ITEMS[id];
    }

    int finditem(const char *id)
    {
        if(!id || !id[0]) return -1;
        for(int i = 0; i < ITEM_COUNT; i++) if(!strcmp(ITEMS[i].id, id)) return i;
        return -1;
    }

    const char *itemname(int id)
    {
        if(id < 0 || id >= ITEM_COUNT) return "";
        return ITEMS[id].name;
    }

    static const char *SLOTNAMES[SLOT_COUNT] = { "mainhand", "head", "torso", "legs", "feet" };

    const char *slotname(int slot)
    {
        if(slot < 0 || slot >= SLOT_COUNT) return "";
        return SLOTNAMES[slot];
    }

    int slotid(const char *name)
    {
        if(!name || !name[0]) return SLOT_NONE;
        for(int i = 0; i < SLOT_COUNT; i++) if(!strcmp(SLOTNAMES[i], name)) return i;
        return SLOT_NONE;
    }

    // --- loot table v1 ---
    // (item index, weight); weights are per-100 rolls, remainder = no drop.
    static const struct { int item; int weight; } LOOT[] =
    {
        { 5, 30 }, // health_potion
        { 1, 10 }, // puglin_stick
        { 3, 10 }, // leather_helm
        { 4,  6 }, // iron_boots
        { 2,  6 }, // ranger_tunic
        { 0,  5 }, // imp_mace
    };
    static const int LOOT_COUNT = sizeof(LOOT)/sizeof(LOOT[0]);

    int rollloot()
    {
        int roll = std::rand() % 100;
        for(int i = 0; i < LOOT_COUNT; i++)
        {
            if(roll < LOOT[i].weight) return LOOT[i].item;
            roll -= LOOT[i].weight;
        }
        return -1;
    }
}
