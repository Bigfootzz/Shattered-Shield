// Shattered Shield RPG — ability table, milestone step 3.
// Tune here; the server enforces everything below.

#include "abilities.h"
#include "stats.h"
#include <cstddef> // NULL

namespace rpg
{
    static const abilitydef ABILITIES[ABILITY_COUNT] =
    {
        // key            name            cooldown  mana  range   dmg   melee  type       delivery             projectile
        // Power Strike range is CENTER-TO-CENTER (client pre-check + server
        // validation both measure origin distance). Two characters in body
        // contact already stand ~7 units apart (model radii 3.5 + 3.5), and a
        // monster actively biting the player holds 7.5-13 in a pack (measured
        // in the real client: zombies 7.5-9.3, the radius-4.2 puglin at 13)
        // — so the original 5.5 could never connect against anything. 15
        // covers real fighting distance with margin and stays clearly melee
        // next to Spark Bolt's 45. (Playtest bug, Oct 20: "weapons do nothing"
        // — every prior proof had masked this with the test range bypasses.)
        { "powerstrike",  "Power Strike", 2000,     0,    15.0f, 35,   true,  "slash", DELIVERY_INSTANT,    NULL },
        { "sparkbolt",    "Spark Bolt",   3500,     25,   45.0f, 50,   false, "spark", DELIVERY_INSTANT,    NULL },
        // CLEANUP item 8: the basic weapon swing behind right-click
        // auto-attack. damage 7 = flat base: the server melee formula
        // (base + weapon + Strength) yields 7 + weapon + Strength.
        // TUNING Oct 21 (user-approved): a weaponless level-1 fists
        // (0 + 0 + 1 = 1/tick) could never win starter hunts.
        { "basicattack",  "Basic Attack", 800,      0,    15.0f, 7,    true,  "slash", DELIVERY_INSTANT,    NULL },
        // PROJECTILE SPELLS (Oct 21, Ed's request for wizard testing): the
        // first implementation of the RANGED DELIVERY DESIGN. Range + LoS are
        // checked at release exactly like Spark Bolt; the bolt then tracks
        // the target and lands — moving after release does not dodge; it
        // fizzles only if the target dies first. Numbers vs Spark Bolt
        // (25 mana / 3.5s / 50+2xKNOW, instant): Arcane is the spammable
        // workhorse (cheaper, faster, slightly softer, travel time is the
        // tradeoff); Ice is the heavy nuke (longer cooldown, bigger hit).
        // No slow/freeze v1 — damage + visuals only, keep it testable.
        { "arcanebolt",   "Arcane Bolt",  3000,     20,   45.0f, 45,   false, "arcane", DELIVERY_PROJECTILE, "arcane" },
        { "icebolt",      "Ice Bolt",     6000,     30,   45.0f, 65,   false, "ice",   DELIVERY_PROJECTILE, "ice" },
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
