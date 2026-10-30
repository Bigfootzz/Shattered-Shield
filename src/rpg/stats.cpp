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

    int stat_armorreductionpct(int armor)
    {
        if(armor <= 0) return 0;
        return 100 - 100*100/(100 + armor*10);
    }

    const char *resistname(int resist)
    {
        return resist >= 0 && resist < RESIST_COUNT ? RESIST_NAMES[resist] : "";
    }
}
