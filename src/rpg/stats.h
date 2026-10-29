// Shattered Shield RPG — character stat system (milestone step 5).
//
// Approved by the user Oct 19 from the research proposal
// (~/workspace/your_files/shattered-shield-stat-system-proposal.md), modeled
// on DDO / Warhammer Online / WoW: six primary attributes with the kit's
// flavor names, DDO-style percentage armor mitigation, server-side crit.
//
// Growth is automatic: every attribute equals the character's level
// (level 1 -> all 1, level 10 -> all 10). Because attributes derive from
// level, no database schema change is needed — level persists, everything
// else is computed. Player-allocated points are a future option.
//
// Server-authoritative: the server computes damage, crits, mitigation and
// max health/mana. The client uses the same pure functions to display the
// character sheet (it knows the level from the last N_RPG_INV).

#pragma once

namespace rpg
{
    // --- primary attributes (kit flavor names) ---
    enum
    {
        ATTR_STRENGTH = 0,
        ATTR_DEXTERITY,
        ATTR_VITALITY,
        ATTR_KNOWLEDGE,
        ATTR_WISDOM,
        ATTR_CUNNING,
        ATTR_COUNT
    };

    const char *attrname(int attr);        // "Strength", ...; "" when out of range
    int attrscore(int level, int attr);    // automatic growth: == clamped level

    // --- derived stats ---
    int stat_maxhp(int level);             // 90 + 10 * Vitality
    int stat_maxhp(int level, int vitbonus); // 90 + 10 * (Vitality + bonus); Phase 2 buffs
    int stat_maxmana(int level);           // 40 + 10 * level + 10 * Wisdom
    int stat_manaregen(int level);         // 5 + Wisdom per second
    int stat_powerstrike(int level, int weapondmg); // 35 + weapon + Strength
    int stat_sparkbolt(int level);         // 50 + 2 * Knowledge
    float stat_critchance(int level);      // %: 5 + 0.5 * Dexterity
    float stat_critmult(int level);        // x: 1.50 + 0.05 * Cunning

    // Server-side crit roll: true when a uniform 0-100 roll beats the chance.
    bool stat_rollcrit(float chancepct);

    // DDO-style percentage mitigation, attacker-level independent:
    //   taken = raw * 100 / (100 + armor * 10), minimum 1.
    // armor 3 -> 23% reduction; armor 6 -> 37.5%. RPG monster path only.
    int stat_mitigate(int rawdmg, int armor);
    int stat_armorreductionpct(int armor); // whole-percent reduction, for display

    // --- damage types & resists: data now, behavior later ---
    // Every ability records its type; every character carries six resists
    // defaulting to 0, which change nothing. Typed monster damage and the
    // elemental list (earth/water/fire/air/divine/chaos) arrive in a later
    // pass; the Defense tab displays the honest zeros meanwhile.
    enum
    {
        RESIST_SLASH = 0,
        RESIST_PIERCE,
        RESIST_BLUNT,
        RESIST_BLEED,
        RESIST_POISON,
        RESIST_CONTROL,
        RESIST_COUNT
    };
    const char *resistname(int resist); // "slash", ...; "" when out of range
}
