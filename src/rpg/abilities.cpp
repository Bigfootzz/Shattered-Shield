// Shattered Shield RPG — ability table, milestone step 3.
// Tune here; the server enforces everything below.

#include "abilities.h"

namespace rpg
{
    static const abilitydef ABILITIES[ABILITY_COUNT] =
    {
        // key            name            cooldown  mana  range   dmg   melee
        { "powerstrike",  "Power Strike", 2000,     0,    5.5f,  35,   true  },
        { "sparkbolt",    "Spark Bolt",   3500,     25,   45.0f, 50,   false },
    };

    const abilitydef &ability(int id)
    {
        return ABILITIES[id];
    }

    bool validability(int id)
    {
        return id >= 0 && id < ABILITY_COUNT;
    }

    int maxmana(int level)
    {
        return 40 + 10*level;
    }
}
