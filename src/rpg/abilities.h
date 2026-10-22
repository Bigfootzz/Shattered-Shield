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

    // --- damage-over-time (v0.1.8, Oct 22) ---
    // Applied server-side when a DoT ability's hit lands (instant or
    // projectile). Ticks are server-authoritative: the server owns the
    // per-target list and applies damage through the normal damage path,
    // so kills feed XP exactly like direct hits. Re-casting on a target
    // that already has this DoT REFRESHES the duration (no stacking).
    // tickdmg is the base per-tick damage; attr adds per-tick scaling
    // (attrscore(level, attr), -1 = flat). resist is data for the future
    // resist behavior (stats.h RESIST_*); it changes nothing yet.
    struct dotdef
    {
        int durationms;    // total DoT duration
        int tickms;        // ms between ticks
        int tickdmg;       // base damage per tick
        int attr;          // ATTR_* for per-tick scaling, -1 = flat
        const char *resist;// "poison", "bleed", ...
    };
    const dotdef *abilitydot(int id); // NULL when the ability has no DoT

    const int ABILITY_COUNT = 8;

    // Melee: short range, free, fast cooldown.
    // Spell: long range, costs mana, slower cooldown.
    // Index 2 is the basic weapon swing (CLEANUP item 8): the generic
    // attack the right-click auto-attack loop uses. No mana, short
    // cooldown, damage = ability base (0) + weapon + Strength.
    const abilitydef &ability(int id); // id in [0, ABILITY_COUNT)
    bool validability(int id);
    // v0.1.8: class ability list for the HUD and keybinds. Fills out[] with
    // ability ids in display order for the archetype ("Warrior", "Magi",
    // "Ranger"); returns count. Basic Attack (2) is intentionally excluded —
    // it's the right-click auto-attack, not a hotbar slot.
    int classabilities(const char *arch, int *out, int max);

    // --- mana model (session state only, NOT persisted — see BUILD-STEP-3.md) ---
    // Formulas live in stats.h (milestone step 5): stat_maxmana / stat_manaregen.
    int maxmana(int level);            // stat_maxmana: 60 + 10*level + 10*Wisdom
}
