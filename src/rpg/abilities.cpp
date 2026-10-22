// Shattered Shield RPG — ability table, milestone step 3.
// Tune here; the server enforces everything below.

#include "abilities.h"
#include "stats.h"
#include <cstddef> // NULL
#include <cstring> // strcmp

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
        { "powerstrike",  "Power Strike", 2000,     0,    15.0f, 35,   true,  "slash", DELIVERY_INSTANT,    NULL, "Warrior" },
        // MANA RETUNE Oct 22 (v0.1.8 fix 8): 25 -> 12. Level-1 pool is 80
        // (stat_maxmana base 40 -> 60): ~6.7 raw casts; the 3.5s Spark weave
        // (3.4 mana/s) sits under the 6/s regen, so steady casting is
        // sustainable while Ice dumps still drain the pool.
        { "sparkbolt",    "Spark Bolt",   3500,     12,   120.0f, 50,   false, "spark", DELIVERY_INSTANT,    NULL, "Magi" },
        // CLEANUP item 8: the basic weapon swing behind right-click
        // auto-attack. damage 7 = flat base: the server melee formula
        // (base + weapon + Strength) yields 7 + weapon + Strength.
        // TUNING Oct 21 (user-approved): a weaponless level-1 fists
        // (0 + 0 + 1 = 1/tick) could never win starter hunts.
        { "basicattack",  "Basic Attack", 800,      0,    15.0f, 7,    true,  "slash", DELIVERY_INSTANT,    NULL, NULL },
        // PROJECTILE SPELLS (Oct 21, Ed's request for wizard testing): the
        // first implementation of the RANGED DELIVERY DESIGN. Range + LoS are
        // checked at release exactly like Spark Bolt; the bolt then tracks
        // the target and lands — moving after release does not dodge; it
        // fizzles only if the target dies first. Numbers vs Spark Bolt
        // (25 mana / 3.5s / 50+2xKNOW, instant): Arcane is the spammable
        // workhorse (cheaper, faster, slightly softer, travel time is the
        // tradeoff); Ice is the heavy nuke (longer cooldown, bigger hit).
        // No slow/freeze v1 — damage + visuals only, keep it testable.
        { "arcanebolt",   "Arcane Bolt",  3000,     8,    120.0f, 45,   false, "arcane", DELIVERY_PROJECTILE, "arcane", "Magi" },
        { "icebolt",      "Ice Bolt",     6000,     15,   120.0f, 65,   false, "ice",   DELIVERY_PROJECTILE, "ice", "Magi" },
        // ARROW SHOT (Oct 21, Ed's "perfect world"): the ranger's ranged
        // attack. Uses the same DELIVERY_PROJECTILE system as the spells
        // (range + LoS at release, tracks target, fizzles if target dies) —
        // proving the delivery works for non-spell projectiles. Physical
        // damage: 40 + 2xDexterity (mirrors the spell formula's 2xKnowledge).
        // 0 mana (it's an arrow, not a spell), 2.5s cooldown. Ranger-only.
        { "arrowshot",    "Arrow Shot",   2500,     0,    120.0f, 40,   false, "pierce", DELIVERY_PROJECTILE, "arrow", "Ranger" },
        // VENOM SHOT (v0.1.8 fix 10): the ranger's second starting attack.
        // Poison arrow: upfront physical hit (Dex-scaled like Arrow Shot)
        // plus a 10s poison DoT (see abilitydot() below). 0 mana (it's an
        // arrow), 8s cooldown — an opener, not a spam button.
        { "poisonshot",   "Venom Shot",   8000,     0,    120.0f, 20,   false, "poison", DELIVERY_PROJECTILE, "arrow", "Ranger" },
        // REND (v0.1.8 fix 15): the warrior's second starting attack. Bleed
        // flavor of the same design: upfront melee hit (Strength-scaled)
        // plus a 10s bleed DoT. 8s cooldown, melee range.
        { "rend",         "Rend",         8000,     0,    15.0f, 15,   true,  "bleed", DELIVERY_INSTANT,    NULL, "Warrior" },
    };

    // DoT definitions for the two v0.1.8 DoT attacks. 1s ticks x 10 = 10s.
    // Tick = base 5 + 1x primary attr: level-1 ranger (DEX 1) ticks 6, total
    // 60 over the duration vs 22 upfront (20 + 2xDEX); level-1 warrior (STR 1)
    // ticks 6, total 60 vs ~21 upfront (15 + weapon + STR). DoT total ~2.7x
    // the upfront hit: a strong opener that still needs follow-up against
    // 180-HP starter zombies. Refresh-on-recast, no stacking.
    const dotdef *abilitydot(int id)
    {
        static const dotdef venom = { 10000, 1000, 5, ATTR_DEXTERITY, "poison" };
        static const dotdef rend  = { 10000, 1000, 5, ATTR_STRENGTH,  "bleed"  };
        if(id == 6) return &venom; // "poisonshot"
        if(id == 7) return &rend;  // "rend"
        return NULL;
    }

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

    // v0.1.8: class ability lists for the HUD hotbar and keybinds.
    int classabilities(const char *arch, int *out, int max)
    {
        if(!out || max <= 0) return 0;
        int n = 0;
        if(arch && !strcmp(arch, "Warrior"))
        {
            if(n < max) out[n++] = 0; // Power Strike
            if(n < max) out[n++] = 7; // Rend (bleed DoT, v0.1.8 fix 15)
        }
        else if(arch && !strcmp(arch, "Magi"))
        {
            if(n < max) out[n++] = 1; // Spark Bolt
            if(n < max) out[n++] = 3; // Arcane Bolt
            if(n < max) out[n++] = 4; // Ice Bolt
        }
        else if(arch && !strcmp(arch, "Ranger"))
        {
            if(n < max) out[n++] = 5; // Arrow Shot
            if(n < max) out[n++] = 6; // Venom Shot (poison DoT, v0.1.8 fix 10)
        }
        return n;
    }
}
