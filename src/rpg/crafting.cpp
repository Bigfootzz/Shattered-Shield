// Shattered Shield RPG — crafting recipes, Phase 1 (Oct 27).
//
// Strict-parser content file (data/rpg/crafting.cfg), same shape as the
// item/quest files: one ss_* command per line, // comments, quoted
// strings. Unknown commands and malformed lines are hard errors.
//
//   ss_recipe "label"              // starts a recipe block (label is free text, unique)
//   ss_recipe_ing <itemid> <count> // exactly 2 per recipe, materials only
//   ss_recipe_out <itemid> <count> // exactly 1 per recipe, consumable only
//   ss_recipe_out_tier <tier> <itemid> <count> // optional tier variants (1..4)
//
// Tier math lives here (pure, seedable): craft_tier() draws from the
// range between the lowest and highest ingredient tiers with triangular
// weights peaking at the midpoint, so mixed-tier crafts land near the
// middle without ever exceeding the best or dropping below the worst
// ingredient. Same-tier ingredients collapse the range to one tier.
#include "crafting.h"
#include "items.h"
#include "cube.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

namespace rpg
{
    const char *crafttiername(int tier)
    {
        static const char *names[5] = { "common", "uncommon", "rare", "epic", "legendary" };
        if(tier < 0 || tier > 4) return "";
        return names[tier];
    }

    int craft_tier(const int *tiers, int n, unsigned int seed)
    {
        if(!tiers || n < 1) return 0;
        int lo = tiers[0], hi = tiers[0];
        for(int i = 1; i < n; i++) { if(tiers[i] < lo) lo = tiers[i]; if(tiers[i] > hi) hi = tiers[i]; }
        if(lo < 0) lo = 0;
        if(hi > 4) hi = 4;
        if(lo >= hi) return lo;
        // Triangular weights over [lo, hi]: weight(t) = span+1 - |2*(t-lo)-span|.
        // span 3 -> 1,3,3,1; span 4 -> 1,3,5,3,1; span 1 -> 1,1. Peak at the
        // midpoint, endpoints possible but least likely, range never left.
        int span = hi - lo, total = 0;
        for(int t = lo; t <= hi; t++) total += span + 1 - abs(2 * (t - lo) - span);
        unsigned int s = seed * 1664525u + 1013904223u; // LCG step, high bits
        int roll = (int)((s >> 16) % (unsigned int)total);
        for(int t = lo; t <= hi; t++)
        {
            int w = span + 1 - abs(2 * (t - lo) - span);
            if(roll < w) return t;
            roll -= w;
        }
        return hi; // unreachable (roll < total), kept for compiler warnings
    }

    bool craftkey(const char *a, const char *b, char *out, int outlen)
    {
        if(!a || !a[0] || !b || !b[0] || !out || outlen < 8) return false;
        if(finditem(a) < 0 || finditem(b) < 0) return false;
        // Bounded write honoring outlen: the old formatstring(out) defaulted
        // to MAXSTRLEN (260) and wrote d[259] = 0 unconditionally, which
        // overflowed every 256-byte caller buffer by 1 byte (stack overwrite
        // that faulted on Windows MinGW x64, harmless padding on Linux).
        if(!strcmp(a, b)) { snprintf(out, outlen, "%s:2", a); return true; }
        if(strcmp(a, b) < 0) snprintf(out, outlen, "%s:1+%s:1", a, b);
        else snprintf(out, outlen, "%s:1+%s:1", b, a);
        return true;
    }

    // ---------- recipe table (built by the parser, committed on validate) --
    struct recipbuild
    {
        bool active = false;
        bool enhance = false; // Phase 3: ss_recipe_enh flag (gear + material)
        std::string label;
        std::vector<std::pair<std::string, int> > ings;
        std::string out;
        int outcount = 0;
        bool hasout = false;
        std::string outtier[5];
        bool hastier[5];
        recipbuild() { for(int i = 0; i < 5; i++) hastier[i] = false; }
    };
    static std::deque<std::string> craftstrpool;
    static const char *craftpoolstr(const std::string &s)
    {
        craftstrpool.push_back(s);
        return craftstrpool.back().c_str();
    }
    static std::deque<craftrecipe> craftrecipes;
    static std::vector<recipbuild> builtrecipes;
    static recipbuild currecipe;
    static bool craftloaded = false;

    static void commitpendingrecipe()
    {
        if(!currecipe.active) return;
        builtrecipes.push_back(currecipe);
        currecipe = recipbuild();
    }

    static bool parseintarg2(const char *cmd, const char *s, int &out, std::string &err)
    {
        char *end = NULL;
        long v = s ? std::strtol(s, &end, 10) : 0;
        if(!s || !s[0] || !end || *end)
        {
            err = std::string(cmd) + ": expected an integer, got '" + (s ? s : "") + "'";
            return false;
        }
        out = (int)v;
        return true;
    }

    static bool craftlinehandler(const char *cmd, int argc, const char *argv[], std::string &err)
    {
        std::string c = cmd ? cmd : "";
        if(c == "ss_recipe")
        {
            if(argc != 1) { err = "ss_recipe: expected exactly 1 argument (label)"; return false; }
            commitpendingrecipe();
            currecipe = recipbuild();
            currecipe.active = true;
            currecipe.label = argv[0];
            return true;
        }
        if(!currecipe.active) { err = c + " with no preceding ss_recipe"; return false; }
        if(c == "ss_recipe_enh")
        {
            if(argc != 0) { err = "ss_recipe_enh: expected no arguments"; return false; }
            currecipe.enhance = true;
            return true;
        }
        if(c == "ss_recipe_ing")
        {
            if(argc != 2) { err = "ss_recipe_ing: expected 2 arguments (item id, count)"; return false; }
            int cnt = 0;
            if(!parseintarg2(cmd, argv[1], cnt, err)) return false;
            if(cnt < 1) { err = "ss_recipe_ing: count must be >= 1"; return false; }
            currecipe.ings.push_back(std::make_pair(std::string(argv[0]), cnt));
            return true;
        }
        if(c == "ss_recipe_out")
        {
            if(argc != 2) { err = "ss_recipe_out: expected 2 arguments (item id, count)"; return false; }
            int cnt = 0;
            if(!parseintarg2(cmd, argv[1], cnt, err)) return false;
            if(cnt < 1) { err = "ss_recipe_out: count must be >= 1"; return false; }
            if(currecipe.hasout) { err = "ss_recipe_out: recipe already has an output"; return false; }
            currecipe.out = argv[0];
            currecipe.outcount = cnt;
            currecipe.hasout = true;
            return true;
        }
        if(c == "ss_recipe_out_tier")
        {
            if(argc != 3) { err = "ss_recipe_out_tier: expected 3 arguments (tier, item id, count)"; return false; }
            int tier = 0, cnt = 0;
            if(!parseintarg2(cmd, argv[0], tier, err)) return false;
            if(!parseintarg2(cmd, argv[2], cnt, err)) return false;
            if(tier < 1 || tier > 4) { err = "ss_recipe_out_tier: tier must be 1..4 (tier 0 is the base output)"; return false; }
            if(cnt < 1) { err = "ss_recipe_out_tier: count must be >= 1"; return false; }
            if(currecipe.hastier[tier]) { err = "ss_recipe_out_tier: duplicate tier variant"; return false; }
            currecipe.outtier[tier] = argv[1];
            currecipe.hastier[tier] = true;
            return true;
        }
        err = "unknown crafting command '" + c + "'";
        return false;
    }

    static bool validatecrafting(std::string &err)
    {
        std::vector<std::string> seenkeys, seenlabels;
        for(size_t i = 0; i < builtrecipes.size(); i++)
        {
            const recipbuild &r = builtrecipes[i];
            if(r.label.empty()) { err = "a recipe has an empty label"; return false; }
            for(size_t j = 0; j < seenlabels.size(); j++)
                if(seenlabels[j] == r.label) { err = "duplicate recipe label '" + r.label + "'"; return false; }
            seenlabels.push_back(r.label);
            // Exactly two ingredients and one output for every recipe kind.
            if(r.ings.size() != 2) { err = "recipe '" + r.label + "' must have exactly 2 ingredients"; return false; }
            if(!r.hasout) { err = "recipe '" + r.label + "' has no output"; return false; }
            if(r.enhance)
            {
                // Phase 3 enhancement: declared order is gear, material.
                // The output (all 5 tiers) is the same piece enhanced:
                // armor, same slot as the input gear, gearstat set,
                // enhbase pointing at the input gear, bonus = tier + 1.
                // ALL tier variants are required: output tier equals the
                // material tier exactly, so a missing variant would
                // silently downgrade a rare-material craft via fallback.
                int gid = finditem(r.ings[0].first.c_str());
                int mid = finditem(r.ings[1].first.c_str());
                if(gid < 0) { err = "recipe '" + r.label + "' names unknown gear '" + r.ings[0].first + "'"; return false; }
                if(mid < 0) { err = "recipe '" + r.label + "' names unknown material '" + r.ings[1].first + "'"; return false; }
                if(item(gid).type != ITEM_ARMOR) { err = "recipe '" + r.label + "' gear '" + r.ings[0].first + "' is not armor (Phase 3)"; return false; }
                if(item(mid).type != ITEM_MATERIAL) { err = "recipe '" + r.label + "' material '" + r.ings[1].first + "' is not a material (Phase 3)"; return false; }
                if(r.ings[0].second != 1 || r.ings[1].second != 1)
                { err = "recipe '" + r.label + "' must use count 1 per ingredient (Phase 3)"; return false; }
                const char *outs[5] = { r.out.c_str(), NULL, NULL, NULL, NULL };
                for(int t = 1; t <= 4; t++)
                {
                    if(!r.hastier[t]) { err = "recipe '" + r.label + "' is missing tier variant " + std::string(crafttiername(t)) + " (Phase 3 requires all 5 tiers)"; return false; }
                    outs[t] = r.outtier[t].c_str();
                }
                for(int t = 0; t <= 4; t++)
                {
                    int oid = finditem(outs[t]);
                    if(oid < 0) { err = "recipe '" + r.label + "' names unknown output '" + std::string(outs[t]) + "'"; return false; }
                    const itemdef &od = item(oid);
                    if(od.type != ITEM_ARMOR) { err = "recipe '" + r.label + "' output '" + std::string(outs[t]) + "' is not armor (Phase 3)"; return false; }
                    if(od.slot != item(gid).slot) { err = "recipe '" + r.label + "' output '" + std::string(outs[t]) + "' is not in the input gear's slot (Phase 3)"; return false; }
                    if(od.gearstat < 0) { err = "recipe '" + r.label + "' output '" + std::string(outs[t]) + "' has no gear bonus (Phase 3)"; return false; }
                    if(!od.enhbase || strcmp(od.enhbase, r.ings[0].first.c_str()))
                    { err = "recipe '" + r.label + "' output '" + std::string(outs[t]) + "' does not derive from '" + r.ings[0].first + "' (Phase 3)"; return false; }
                    if(od.tier != t) { err = "recipe '" + r.label + "' output '" + std::string(outs[t]) + "' tier does not match its variant tier (Phase 3)"; return false; }
                    if(od.gearbonus != t + 1) { err = "recipe '" + r.label + "' output '" + std::string(outs[t]) + "' gear bonus is not tier + 1 (Phase 3)"; return false; }
                }
            }
            else
            {
            // Phase 1: exactly two material ingredients, one consumable output.
            for(size_t j = 0; j < r.ings.size(); j++)
            {
                int id = finditem(r.ings[j].first.c_str());
                if(id < 0) { err = "recipe '" + r.label + "' names unknown ingredient '" + r.ings[j].first + "'"; return false; }
                if(item(id).type != ITEM_MATERIAL) { err = "recipe '" + r.label + "' ingredient '" + r.ings[j].first + "' is not a material (Phase 1)"; return false; }
            }
            int oid = finditem(r.out.c_str());
            if(oid < 0) { err = "recipe '" + r.label + "' names unknown output '" + r.out + "'"; return false; }
            if(item(oid).type != ITEM_CONSUMABLE) { err = "recipe '" + r.label + "' output '" + r.out + "' is not a consumable (Phase 1)"; return false; }
            for(int t = 1; t <= 4; t++) if(r.hastier[t])
            {
                int vid = finditem(r.outtier[t].c_str());
                if(vid < 0) { err = "recipe '" + r.label + "' names unknown tier output '" + r.outtier[t] + "'"; return false; }
                if(item(vid).type != ITEM_CONSUMABLE) { err = "recipe '" + r.label + "' tier output '" + r.outtier[t] + "' is not a consumable (Phase 1)"; return false; }
            }
            }
            char key[MAXSTRLEN];
            if(!craftkey(r.ings[0].first.c_str(), r.ings[1].first.c_str(), key, sizeof(key)))
            { err = "recipe '" + r.label + "' has an unbuildable ingredient key"; return false; }
            // The canonical key assumes 1 unit per slot; a recipe whose
            // counts differ from the slot shape cannot be reached from the
            // two-slot window, so Phase 1 recipes must use count 1 each.
            // (Same-id pairs fold to count 2 and are reachable.)
            bool same = r.ings[0].first == r.ings[1].first;
            if(!same && (r.ings[0].second != 1 || r.ings[1].second != 1))
            { err = "recipe '" + r.label + "' must use count 1 per ingredient (Phase 1 two-slot window)"; return false; }
            if(same && r.ings[0].second + r.ings[1].second != 2)
            { err = "recipe '" + r.label + "' same-item pair must total count 2"; return false; }
            for(size_t j = 0; j < seenkeys.size(); j++)
                if(seenkeys[j] == key) { err = "duplicate recipe for ingredient key '" + std::string(key) + "'"; return false; }
            seenkeys.push_back(key);
        }
        return true;
    }

    static void commitcrafting()
    {
        craftrecipes.clear();
        for(size_t i = 0; i < builtrecipes.size(); i++)
        {
            const recipbuild &b = builtrecipes[i];
            craftrecipe r;
            // Material pairs sort ingredients for the canonical key +
            // display order. Enhancement recipes keep declared order
            // (gear, material) so ing[0] is always the gear piece.
            std::pair<std::string, int> a = b.ings[0], c = b.ings[1];
            if(!b.enhance && c.first < a.first) { std::pair<std::string, int> t = a; a = c; c = t; }
            char key[MAXSTRLEN];
            if(!craftkey(a.first.c_str(), c.first.c_str(), key, sizeof(key))) continue; // validated above; never pool an unbuilt key
            r.key = craftpoolstr(key);
            r.ing[0] = craftpoolstr(a.first);
            r.ing[1] = craftpoolstr(c.first);
            r.ingcount[0] = a.second;
            r.ingcount[1] = c.second;
            r.out = craftpoolstr(b.out);
            r.outcount = b.outcount;
            for(int t = 0; t < 5; t++) r.outtier[t] = b.hastier[t] ? craftpoolstr(b.outtier[t]) : NULL;
            r.enhance = b.enhance;
            craftrecipes.push_back(r);
        }
        builtrecipes.clear();
        currecipe = recipbuild();
    }

    bool loadcrafting()
    {
        if(craftloaded) return craftrecipes.size() > 0;
        craftloaded = true;
        std::string err;
        if(!contentfileexists("data/rpg/crafting.cfg"))
        {
            conoutf(CON_ERROR, "rpg: crafting: data/rpg/crafting.cfg missing - no recipes available");
            return false;
        }
        if(!parsecontentfile("data/rpg/crafting.cfg", craftlinehandler, err))
        {
            conoutf(CON_ERROR, "rpg: crafting: crafting.cfg rejected (%s) - no recipes available", err.c_str());
            builtrecipes.clear();
            return false;
        }
        commitpendingrecipe();
        if(!validatecrafting(err))
        {
            conoutf(CON_ERROR, "rpg: crafting: crafting.cfg rejected (%s) - no recipes available", err.c_str());
            builtrecipes.clear();
            return false;
        }
        commitcrafting();
        conoutf("rpg: crafting: %d recipes loaded from data/rpg/crafting.cfg", (int)craftrecipes.size());
        return true;
    }

    int craftrecipecount() { return (int)craftrecipes.size(); }

    const craftrecipe *craftrecipebykey(const char *key)
    {
        if(!key) return NULL;
        for(size_t i = 0; i < craftrecipes.size(); i++)
            if(!strcmp(craftrecipes[i].key, key)) return &craftrecipes[i];
        return NULL;
    }

    const craftrecipe *craftrecipebyindex(int i)
    {
        if(i < 0 || i >= (int)craftrecipes.size()) return NULL;
        return &craftrecipes[i];
    }

    const char *craftoutfortier(const craftrecipe *r, int tier)
    {
        if(!r) return NULL;
        if(tier < 0) tier = 0;
        if(tier > 4) tier = 4;
        for(int t = tier; t >= 0; t--) if(t == 0 || r->outtier[t]) return t == 0 ? r->out : r->outtier[t];
        return r->out;
    }
}
