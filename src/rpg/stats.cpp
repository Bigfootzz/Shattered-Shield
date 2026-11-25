// Shattered Shield RPG — stat system formulas (milestone step 5).
// Pure functions; the proposal's exact numbers.

#include "stats.h"
#include "rpg.h"   // MAX_LEVEL
#include <cstdlib> // rand
#include <cstring> // strcmp (Phase 3 class weighting)

namespace rpg
{
    static const char *ATTR_NAMES[ATTR_COUNT] =
    {
        "Strength",
        "Dexterity",
        "Vitality",
        "Knowledge",
        "Wisdom",
        "Cunning",
    };

    static const char *RESIST_NAMES[RESIST_COUNT] =
    {
        "slash",
        "pierce",
        "blunt",
        "bleed",
        "poison",
        "control",
    };

    const char *attrname(int attr)
    {
        return attr >= 0 && attr < ATTR_COUNT ? ATTR_NAMES[attr] : "";
    }

    int attrscore(int level, int attr)
    {
        if(attr < 0 || attr >= ATTR_COUNT) return 0;
        if(level < 1) level = 1;
        if(level > MAX_LEVEL) level = MAX_LEVEL;
        return level; // automatic growth: score == level
    }

    int stat_maxhp(int level)
    {
        return 90 + 10*attrscore(level, ATTR_VITALITY);
    }

    int stat_maxhp(int level, int vitbonus)
    {
        if(vitbonus < 0) vitbonus = 0;
        return 90 + 10*(attrscore(level, ATTR_VITALITY) + vitbonus);
    }

    // MANA RETUNE Oct 22 (v0.1.8 fix 8): base 40 -> 60. Level-1 pool is now
    // 80 (was 60): Spark Bolt (12) x ~6.7 casts raw, Arcane (8) x 10,
    // Ice (15) x ~5.3. A disciplined 6s rotation (Ice+Arcane+Spark = 35 mana
    // = 5.8/s) sits just under the 6/s regen and is sustainable; spamming
    // every cooldown drains slowly, so spell choice still matters. Per Ed's
    // MMO model, skill ranks (stronger + costlier) will retune this again
    // later; the pool still grows 10/level + 10/Wisdom.
    int stat_maxmana(int level)
    {
        return 60 + 10*level + 10*attrscore(level, ATTR_WISDOM);
    }

    int stat_maxmana(int level, int wisbonus)
    {
        if(wisbonus < 0) wisbonus = 0;
        return 60 + 10*level + 10*(attrscore(level, ATTR_WISDOM) + wisbonus);
    }

    int stat_manaregen(int level)
    {
        return 5 + attrscore(level, ATTR_WISDOM);
    }

    int stat_manaregen(int level, int wisbonus)
    {
        if(wisbonus < 0) wisbonus = 0;
        return 5 + attrscore(level, ATTR_WISDOM) + wisbonus;
    }

    int stat_powerstrike(int level, int weapondmg)
    {
        return 35 + weapondmg + attrscore(level, ATTR_STRENGTH);
    }

    int stat_powerstrike(int level, int weapondmg, int strbonus)
    {
        if(strbonus < 0) strbonus = 0;
        return 35 + weapondmg + attrscore(level, ATTR_STRENGTH) + strbonus;
    }

    int stat_sparkbolt(int level)
    {
        return 50 + 2*attrscore(level, ATTR_KNOWLEDGE);
    }

    int stat_sparkbolt(int level, int knowbonus)
    {
        if(knowbonus < 0) knowbonus = 0;
        return 50 + 2*(attrscore(level, ATTR_KNOWLEDGE) + knowbonus);
    }

    float stat_critchance(int level)
    {
        return 5.0f + 0.5f*attrscore(level, ATTR_DEXTERITY);
    }

    float stat_critchance(int level, int dexbonus)
    {
        if(dexbonus < 0) dexbonus = 0;
        return 5.0f + 0.5f*(attrscore(level, ATTR_DEXTERITY) + dexbonus);
    }

    float stat_critmult(int level)
    {
        return 1.50f + 0.05f*attrscore(level, ATTR_CUNNING);
    }

    float stat_critmult(int level, int cunbonus)
    {
        if(cunbonus < 0) cunbonus = 0;
        return 1.50f + 0.05f*(attrscore(level, ATTR_CUNNING) + cunbonus);
    }

    // Phase 3 class weighting (gear bonuses only; pure, shared server+client).
    // Unknown class fails closed to half value for every attribute: no
    // class ever gets full value it did not earn, and no crash on "".
    int favoredattr(const char *cls)
    {
        if(!cls) return -1;
        if(!strcmp(cls, "Warrior")) return ATTR_STRENGTH;
        if(!strcmp(cls, "Ranger")) return ATTR_DEXTERITY;
        if(!strcmp(cls, "Magi")) return ATTR_KNOWLEDGE;
        return -1;
    }

    int gearweight(int rawbonus, int attr, const char *cls)
    {
        if(rawbonus <= 0) return 0;
        if(attr < 0 || attr >= ATTR_COUNT) return 0;
        if(attr == favoredattr(cls)) return rawbonus;
        return rawbonus / 2; // integer division: half rounded down (3 -> 1)
    }

    bool stat_rollcrit(float chancepct)
    {
        // uniform 0..100 roll; server-side only (server rolls, never the client)
        return (rand() % 10000) < int(chancepct*100.0f + 0.5f);
    }

    int stat_mitigate(int rawdmg, int armor)
    {
        if(rawdmg <= 0) return 0;
        if(armor <= 0) return rawdmg;
        int taken = rawdmg*100/(100 + armor*10);
        return taken < 1 ? 1 : taken;
    }

    // PvP gear (Oct 31): equipped sums mirroring armorvalue() in rpg.cpp
    // (same SLOT_COUNT loop, same -1 slot guard), but through the public
    // equippeditem() accessor so they can live in this file per SPEC
    // ownership. Unlike the pure formulas above, these read the session
    // equipment cache; on a client without the character cached they sum
    // to 0. Field names are the SPEC-fixed itemdef fields owned by the
    // item-defs workstream (pvpreduce = percent, maxhp = flat HP).
    int pvpreducevalue(const char *name)
    {
        if(!name || !name[0]) return 0;
        int total = 0;
        for(int i = 0; i < SLOT_COUNT; i++)
        {
            int id = equippeditem(name, i);
            if(id >= 0) total += item(id).pvpreduce;
        }
        if(total < 0) total = 0;
        if(total > 100) total = 100;
        return total;
    }

    int maxhpbonusvalue(const char *name)
    {
        if(!name || !name[0]) return 0;
        int total = 0;
        for(int i = 0; i < SLOT_COUNT; i++)
        {
            int id = equippeditem(name, i);
            if(id >= 0) total += item(id).maxhp;
        }
        if(total < 0) total = 0;
        return total;
    }

    int stat_mitigatepvp(int rawdmg, int armor, int pvpreducepct)
    {
        int taken = stat_mitigate(rawdmg, armor);
        if(pvpreducepct <= 0) return taken;
        if(pvpreducepct > 100) pvpreducepct = 100;
        int reduced = taken*(100 - pvpreducepct)/100;
        return reduced < 1 ? 1 : reduced;
    }

    int stat_armorreductionpct(int armor)
    {
        if(armor <= 0) return 0;
        return 100 - 100*100/(100 + armor*10);
    }

    const char *resistname(int resist)
    {
        return resist >= 0 && resist < RESIST_COUNT ? RESIST_NAMES[resist] : "";
    }

    // --- WoW-style attack table (v0.1.146) ---
    // White damage: one roll, precedence miss > dodge > parry > glancing >
    // block > crit > crushing > hit. Yellow damage: two rolls (miss, then
    // crit) + a concurrent block roll for physical attacks. All chances
    // are percents; the roll functions take the roll value so a harness can
    // test them deterministically (the server passes rand()%10000/100).
    const char *atkoutcomename(int outcome)
    {
        static const char *names[ATK_COUNT] =
        {
            "miss", "dodge", "parry", "glancing", "block", "crit", "crushing", "hit"
        };
        return outcome >= 0 && outcome < ATK_COUNT ? names[outcome] : "";
    }

    float atk_misschance(int attlevel, int deflevel)
    {
        float c = 5.0f + float(deflevel > attlevel ? deflevel - attlevel : 0);
        return c > 100.0f ? 100.0f : c;
    }

    float atk_dodgechance(int defdex)
    {
        if(defdex < 0) defdex = 0;
        return 5.0f + 0.1f*defdex;
    }

    float atk_parrychance(int defstr, bool canparry)
    {
        if(!canparry) return 0.0f;
        if(defstr < 0) defstr = 0;
        return 5.0f + 0.1f*defstr;
    }

    float atk_glancechance(int attlevel, int deflevel)
    {
        if(deflevel <= attlevel) return 0.0f;
        float c = 10.0f*(deflevel - attlevel);
        return c > 30.0f ? 30.0f : c;
    }

    // Glancing is PvE melee-only (WoW): only a player/bot attacker vs a
    // monster target with a melee attack can glance. PvP (both sides
    // players/bots), mob attackers (they crush instead), and ranged
    // attacks (arrows) never glance.
    bool atk_glanceapplies(bool attisplayer, bool defismonster, bool ismelee)
    {
        return attisplayer && defismonster && ismelee;
    }

    float atk_blockchance(bool hasshield)
    {
        return hasshield ? 5.0f : 0.0f;
    }

    // Crushing Blow (WoW rule): a mob 3+ levels above its target crushes
    // 15% of the time for 150% damage. Flat 15% once the threshold is met
    // (vanilla WoW does not scale it further with the gap); below the
    // threshold the chance is 0. Mob-only is enforced by the server
    // wrapper (rpg_atk_rollwhite), which feeds 0 for player attackers.
    float atk_crushchance(int attlevel, int deflevel)
    {
        if(attlevel < deflevel + 3) return 0.0f;
        return 15.0f;
    }

    int atk_rollwhite(float roll, float miss, float dodge, float parry, float glance, float block, float crit, float crush, float critmult, int damage, int &outdmg)
    {
        if(damage < 0) damage = 0;
        if(crit < 0.0f) crit = 0.0f;
        if(crush < 0.0f) crush = 0.0f;
        if(critmult < 1.0f) critmult = 1.0f;
        float band = 0.0f;
        band += miss;
        if(roll < band) { outdmg = 0; return ATK_MISS; }
        band += dodge;
        if(roll < band) { outdmg = 0; return ATK_DODGE; }
        band += parry;
        if(roll < band) { outdmg = 0; return ATK_PARRY; }
        band += glance;
        if(roll < band)
        {
            outdmg = int(damage*ATK_GLANCING_DMG_PCT/100.0f + 0.5f);
            if(outdmg < 1 && damage > 0) outdmg = 1;
            return ATK_GLANCING;
        }
        band += block;
        if(roll < band)
        {
            outdmg = int(damage*(100.0f - ATK_BLOCK_REDUCE_PCT)/100.0f + 0.5f);
            if(outdmg < 1 && damage > 0) outdmg = 1;
            return ATK_BLOCK;
        }
        band += crit;
        if(roll < band) { outdmg = int(damage*critmult + 0.5f); return ATK_CRIT; }
        band += crush; // crushing sits below crit: it takes only what is
                       // left after the crit band, never alongside a crit.
        if(roll < band) { outdmg = int(damage*1.5f + 0.5f); return ATK_CRUSHING; }
        outdmg = damage;
        return ATK_HIT;
    }

    bool atk_rollyellowmiss(float roll, int attlevel, int deflevel)
    {
        return roll < atk_misschance(attlevel, deflevel);
    }

    bool atk_rollyellowcrit(float roll, float critchance)
    {
        if(critchance < 0.0f) critchance = 0.0f;
        return roll < critchance;
    }

    bool atk_rollyellowblock(float roll, bool hasshield)
    {
        return roll < atk_blockchance(hasshield);
    }

    bool atk_rollyellowdodge(float roll, int defdex)
    {
        return roll < atk_dodgechance(defdex);
    }

    bool atk_rollyellowparry(float roll, int defstr, bool canparry)
    {
        return roll < atk_parrychance(defstr, canparry);
    }
}
