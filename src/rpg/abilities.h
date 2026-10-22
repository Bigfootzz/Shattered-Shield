// Shattered Shield RPG — ability layer v1 (milestone step 3).
//
// Data-driven ability table: the single obvious place for ability tuning.
// The server is authoritative — cooldowns, mana costs and damage all live
// here and are enforced server-side. The client only ASKS to cast
// (ability id + client-advisory yaw); it never supplies damage, cooldown
// or resource values.
#pragma once

namespace rpg
{
    struct abilitydef
    {
        const char *key;   // stable id, used in logs and console
        const char *name;  // display name
        int cooldownms;    // server-enforced cooldown between successful casts
        int manacost;      // mana cost (0 = free)
        float range;       // max target distance, world units
        int damage;        // base damage, applied through the engine damage path
        bool melee;        // true: front-cone target check; false: line-of-sight check
        const char *damagetype; // data only (behavior later): "slash", "spark" ...
        int delivery;      // DELIVERY_INSTANT or DELIVERY_PROJECTILE (below)
        const char *projectile; // visual style for projectiles: "arcane", "ice", "arrow", or NULL
        const char *reqclass;   // class gate (Oct 21): NULL = universal (all classes);
                                // otherwise a space-separated list of allowed archetype
                                // display names, e.g. "Magi", "Warrior", "Ranger".
    };

    const int DELIVERY_INSTANT = 0;    // damage applied at cast (existing)
    const int DELIVERY_PROJECTILE = 1; // target-following projectile (Oct 21):
                                       // range + LoS checked at release, then
                                       // tracks and lands; fizzles if target
                                       // dies first. Spark Bolt stays instant.

    const int ABILITY_COUNT = 6;

    // Melee: short range, free, fast cooldown.
    // Spell: long range, costs mana, slower cooldown.
    // Index 2 is the basic weapon swing (CLEANUP item 8): the generic
    // attack the right-click auto-attack loop uses. No mana, short
    // cooldown, damage = ability base (0) + weapon + Strength.
    const abilitydef &ability(int id); // id in [0, ABILITY_COUNT)
    bool validability(int id);

    // --- mana model (session state only, NOT persisted — see BUILD-STEP-3.md) ---
    // Formulas live in stats.h (milestone step 5): stat_maxmana / stat_manaregen.
    int maxmana(int level);            // stat_maxmana: 40 + 10*level + 10*Wisdom
}
