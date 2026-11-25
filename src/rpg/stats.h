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
    int stat_maxmana(int level, int wisbonus); // Phase 3: + gear/buff Wisdom
    int stat_manaregen(int level);         // 5 + Wisdom per second
    int stat_manaregen(int level, int wisbonus); // Phase 3: + gear/buff Wisdom
    int stat_powerstrike(int level, int weapondmg); // 35 + weapon + Strength
    int stat_powerstrike(int level, int weapondmg, int strbonus); // Phase 3: + gear/buff Strength
    int stat_sparkbolt(int level);         // 50 + 2 * Knowledge
    int stat_sparkbolt(int level, int knowbonus); // Phase 3: + gear/buff Knowledge
    float stat_critchance(int level);      // %: 5 + 0.5 * Dexterity
    float stat_critchance(int level, int dexbonus); // Phase 3: + gear/buff Dexterity
    float stat_critmult(int level);        // x: 1.50 + 0.05 * Cunning
    float stat_critmult(int level, int cunbonus); // Phase 3: + gear/buff Cunning

    // --- Phase 3 class weighting (gear bonuses only) ---
    // Favored attribute per class, full value; every other gear bonus
    // counts at half, rounded down. Base attributes (= level) and timed
    // buffs are NOT weighted (full value for every class).
    int favoredattr(const char *cls);      // ATTR_* for "Warrior"/"Ranger"/"Magi", -1 when unknown
    int gearweight(int rawbonus, int attr, const char *cls); // weighted bonus (floor on halves)

    // Server-side crit roll: true when a uniform 0-100 roll beats the chance.
    bool stat_rollcrit(float chancepct);

    // DDO-style percentage mitigation, attacker-level independent:
    //   taken = raw * 100 / (100 + armor * 10), minimum 1.
    // armor 3 -> 23% reduction; armor 6 -> 37.5%. RPG monster path only.
    int stat_mitigate(int rawdmg, int armor);
    int stat_armorreductionpct(int armor); // whole-percent reduction, for display

    // --- PvP gear (Oct 31, SPEC: pvpreduce / maxhp itemdef fields) ---
    // Equipped-item sums mirroring armorvalue() (rpg.cpp) over SLOT_COUNT
    // via the public equippeditem() accessor; placed here, not in rpg.cpp,
    // because SPEC file ownership gives this workstream stats.cpp/stats.h
    // + the server call sites only (rpg.cpp is the vendor workstream's
    // file). Unknown/missing characters sum to 0. These read the
    // item-defs workstream's fields (item(id).pvpreduce / item(id).maxhp).
    int pvpreducevalue(const char *name); // summed PvP reduction %, clamped 0..100
    int maxhpbonusvalue(const char *name); // summed flat max-HP bonus, >= 0
    // PvP damage formula: armor mitigation first, then the summed PvP
    // percent off the mitigated value (integer division, minimum 1):
    //   taken = stat_mitigate(raw, armor) * (100 - pvpreducepct) / 100
    // pvpreducepct <= 0 returns the plain mitigated value unchanged.
    int stat_mitigatepvp(int rawdmg, int armor, int pvpreducepct);

    // --- WoW-style attack table (v0.1.146) ---
    // White damage (auto-attacks + "quick" fillers): ONE roll against a
    // precedence table. Yellow damage (specials): TWO rolls (miss, then
    // crit); block is a separate concurrent roll for physical attacks.
    // All chances are percents (0..100). The roll functions take the roll
    // value (0..100) so a harness can test them deterministically; the
    // server generates the roll with rand() (same pattern as
    // stat_rollcrit). Crushing Blow: 15% when the attacker is 3+ levels
    // above the defender (WoW rule); the server wrapper only ever feeds
    // it a nonzero value for monster attackers, so players never crush.
    enum
    {
        ATK_MISS = 0,
        ATK_DODGE,
        ATK_PARRY,
        ATK_GLANCING,
        ATK_BLOCK,
        ATK_CRIT,
        ATK_CRUSHING,
        ATK_HIT,
        ATK_COUNT
    };
    const char *atkoutcomename(int outcome); // "miss", "dodge", ...; "" when out of range
    float atk_misschance(int attlevel, int deflevel);    // 5 + 1 per level the defender is above the attacker
    float atk_dodgechance(int defdex);                   // 5 + 0.1 per Dexterity
    float atk_parrychance(int defstr, bool canparry);    // 5 + 0.1 per Strength, 0 when !canparry
    float atk_glancechance(int attlevel, int deflevel);  // 10 per level the defender is above, cap 30, else 0
    float atk_blockchance(bool hasshield);               // 5 when a shield is equipped, else 0
    float atk_crushchance(int attlevel, int deflevel);   // 15% when the attacker is 3+ levels above the defender, else 0 (WoW rule)
    // Glancing blows land for 75% of the rolled damage; a block shaves 30%.
    const float ATK_GLANCING_DMG_PCT = 75.0f;
    const float ATK_BLOCK_REDUCE_PCT = 30.0f;
    // White: single roll 0..100, precedence miss > dodge > parry > glancing
    // > block > crit > crushing > hit. All outcomes mutually exclusive, so
    // crit is capped by what is left after the bands above it, and crushing
    // takes only what is left after crit. The chance parameters are
    // percents (use the atk_*chance functions above).
    // outdmg is the adjusted damage (0 for miss/dodge/parry). Returns the
    // ATK_* outcome.
    int atk_rollwhite(float roll, float miss, float dodge, float parry, float glance, float block, float crit, float crush, float critmult, int damage, int &outdmg);
    // Yellow: first roll is the miss check (true = the attack misses).
    bool atk_rollyellowmiss(float roll, int attlevel, int deflevel);
    // Yellow: second roll is the crit check (true = critical hit).
    bool atk_rollyellowcrit(float roll, float critchance);
    // Yellow: dodge and parry are checked after the miss roll (WoW: a
    // special from the front can be dodged or parried). Dodge applies to
    // any physical special (melee or ranged); parry is melee-only
    // (canparry is Warrior players and monsters). Spells are never
    // dodged or parried. A dodged/parried special deals no damage and
    // does not crit.
    bool atk_rollyellowdodge(float roll, int defdex);
    bool atk_rollyellowparry(float roll, int defstr, bool canparry);
    // Glancing is PvE melee-only (WoW): only a player/bot attacker vs a
    // monster target with a melee attack can glance. PvP, mob attackers
    // (they crush instead), and ranged attacks never glance.
    bool atk_glanceapplies(bool attisplayer, bool defismonster, bool ismelee);
    // Yellow: block is a separate concurrent roll for PHYSICAL attacks only
    // (melee / pierce / poison / quick); spells never block. True = blocked.
    bool atk_rollyellowblock(float roll, bool hasshield);

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
