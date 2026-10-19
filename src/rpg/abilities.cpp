// Shattered Shield RPG — ability table, milestone step 3.
// Tune here; the server enforces everything below.

#include "abilities.h"
#include "stats.h"

namespace rpg
{
    static const abilitydef ABILITIES[ABILITY_COUNT] =
    {
        // key            name            cooldown  mana  range   dmg   melee  type
        { "powerstrike",  "Power Strike", 2000,     0,    5.5f,  35,   true,  "slash" },
        { "sparkbolt",    "Spark Bolt",   3500,     25,   45.0f, 50,   false, "spark" },
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
