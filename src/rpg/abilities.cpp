// Shattered Shield RPG — ability table, milestone step 3.
// Tune here; the server enforces everything below.

#include "abilities.h"
#include "stats.h"

namespace rpg
{
    static const abilitydef ABILITIES[ABILITY_COUNT] =
    {
        // key            name            cooldown  mana  range   dmg   melee  type
        // Power Strike range is CENTER-TO-CENTER (client pre-check + server
        // validation both measure origin distance). Two characters in body
        // contact already stand ~7 units apart (model radii 3.5 + 3.5), and a
        // monster actively biting the player holds 7.5-13 in a pack (measured
        // in the real client: zombies 7.5-9.3, the radius-4.2 puglin at 13)
        // — so the original 5.5 could never connect against anything. 15
        // covers real fighting distance with margin and stays clearly melee
        // next to Spark Bolt's 45. (Playtest bug, Oct 20: "weapons do nothing"
        // — every prior proof had masked this with the test range bypasses.)
        { "powerstrike",  "Power Strike", 2000,     0,    15.0f, 35,   true,  "slash" },
        { "sparkbolt",    "Spark Bolt",   3500,     25,   45.0f, 50,   false, "spark" },
        // CLEANUP item 8: the basic weapon swing behind right-click
        // auto-attack. damage 7 = flat base: the server melee formula
        // (base + weapon + Strength) yields 7 + weapon + Strength.
        // TUNING Oct 21 (user-approved): a weaponless level-1 fists
        // (0 + 0 + 1 = 1/tick) could never win starter hunts.
        { "basicattack",  "Basic Attack", 800,      0,    15.0f, 7,    true,  "slash" },
    };

    const abilitydef &ability(int id)
    {
        return ABILITIES[id];
    }

    bool validability(int id)
    {
        return id >= 0 && id < ABILITY_COUNT;
    }

    // Kept for the rpg module's session state; now the step-5 formula.
    int maxmana(int level)
    {
        return stat_maxmana(level);
    }
}
