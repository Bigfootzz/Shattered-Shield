// Shattered Shield RPG — item table + loot tables.
//
// Content data layer (crafting build step 0, Oct 20): item and loot
// definitions are CONTENT. At startup the loader parses the definition
// files under data/rpg/ (flat ss_* setter lines, the design's format),
// validates the whole set, and swaps it in atomically. The compiled-in
// table further down is the fallback: content can break, the game never
// runs on a half-loaded content set. The files keep the CubeScript setter
// shape but are parsed strictly, never executed — this module is built
// without the script interpreter, and a data file that names an unknown
// command is a hard error, so content can never smuggle in behavior.
// The server loads its own copy and is the only side whose numbers mean
// anything; the client loads the same files for display.
#include "items.h"
#include "crafting.h"
#include "stats.h"
#include "cube.h"

#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <string>
#include <vector>

namespace rpg
{
    // ---------- string pool for data-loaded defs ----------
    // itemdef holds const char*; strings defined by data files are copied
    // into this deque (push_back never invalidates existing c_str()s) and
    // live for the process lifetime, like the compiled-in literals.
    static std::deque<std::string> strpool;
    static const char *poolstr(const std::string &s)
    {
        strpool.push_back(s);
        return strpool.back().c_str();
    }

    // ---------- compiled-in fallback (pre-content-layer behavior, verbatim) ----------
    // Icon art: System G6/Qoma RPG Icons Set (CC0), sliced to 64px cells under
    // packages/ui/rpg/icons/. Weapon models: Quaternius Bestiary (Imp mace,
    // Puglin stick) converted to IQM vweps on the tag_weapon path.
    static const itemdef FALLBACK_ITEMS[] =
    {
        // id              name            type            slot           icon                                        vwep                         wcat     dmg  arm  heal  stack  value mana
        { "imp_mace",      "Imp Mace",     ITEM_WEAPON,    SLOT_MAINHAND, "packages/ui/rpg/icons/icon_mace.png",     "playermodels/vwep/warrior/mace",     "mace",  15,  0,   0,   false, 60,   0 , 0, -1 },
        { "warrior_sword", "Warrior Sword",ITEM_WEAPON,    SLOT_MAINHAND, "packages/ui/rpg/icons/icon_empty.png",    "playermodels/vwep/warrior/sword",    "sword", 15,  0,   0,   false, 60,   0 , 0, -1 },
        { "puglin_stick",  "Puglin Stick", ITEM_WEAPON,    SLOT_MAINHAND, "packages/ui/rpg/icons/icon_stick.png",    "playermodels/vwep/stick",    NULL,    8,   0,   0,   false, 10,   0 , 0, -1 },
        { "ranger_tunic",  "Ranger Tunic", ITEM_ARMOR,     SLOT_TORSO,    "packages/ui/rpg/icons/icon_tunic.png",    NULL,                          NULL,    0,  3,   0,   false, 30,   0 , 0, -1 },
        { "leather_helm",  "Leather Helm", ITEM_ARMOR,     SLOT_HEAD,     "packages/ui/rpg/icons/icon_helm.png",     NULL,                          NULL,    0,  1,   0,   false, 15,   0 , 0, -1 },
        { "iron_boots",    "Iron Boots",   ITEM_ARMOR,     SLOT_FEET,     "packages/ui/rpg/icons/icon_boots.png",    NULL,                          NULL,    0,  2,   0,   false, 20,   0 , 0, -1 },
        { "copper_ring",   "Copper Ring",  ITEM_ARMOR,     SLOT_RING1,    "packages/ui/rpg/icons/icon_ring.png",     NULL,                          NULL,    0,  1,   0,   false, 12,   0 , 0, -1 },
        { "silver_ring",   "Silver Ring",  ITEM_ARMOR,     SLOT_RING2,    "packages/ui/rpg/icons/icon_ring.png",     NULL,                          NULL,    0,  1,   0,   false, 18,   0 , 0, -1 },
        { "copper_necklace","Copper Necklace",ITEM_ARMOR,   SLOT_NECK,     "packages/ui/rpg/icons/icon_necklace.png", NULL,                          NULL,    0,  1,   0,   false, 15,   0 , 0, -1 },
        { "leather_leggings","Leather Leggings",ITEM_ARMOR, SLOT_LEGS,     "packages/ui/rpg/icons/icon_boots.png",    NULL,                          NULL,    0,  1,   0,   false, 15,   0 , 0, -1 },
        { "iron_leggings", "Iron Leggings",ITEM_ARMOR,     SLOT_LEGS,     "packages/ui/rpg/icons/icon_boots.png",    NULL,                          NULL,    0,  2,   0,   false, 25,   0 , 0, -1 },
        { "iron_helm",     "Iron Helm",    ITEM_ARMOR,     SLOT_HEAD,     "packages/ui/rpg/icons/icon_helm.png",     NULL,                          NULL,    0,  2,   0,   false, 25,   0 , 0, -1 },
        { "chain_tunic",   "Chain Tunic",  ITEM_ARMOR,     SLOT_TORSO,    "packages/ui/rpg/icons/icon_tunic.png",    NULL,                          NULL,    0,  4,   0,   false, 45,   0 , 0, -1 },
        { "leather_boots", "Leather Boots",ITEM_ARMOR,     SLOT_FEET,     "packages/ui/rpg/icons/icon_boots.png",    NULL,                          NULL,    0,  1,   0,   false, 12,   0 , 0, -1 },
        { "health_potion", "Health Potion",ITEM_CONSUMABLE,SLOT_NONE,     "packages/ui/rpg/icons/icon_potion.png",   NULL,                          NULL,    0,  0,   40,  true,  10,   0 , 0, -1 },
        { "wooden_bow",    "Wooden Bow",   ITEM_WEAPON,    SLOT_MAINHAND, "packages/ui/rpg/icons/icon_bow.png",     "playermodels/vwep/ranger/bow",      "bow",   12,  0,   0,   false, 25,   0 , 0, -1 },
    };
    static const int FALLBACK_COUNT = sizeof(FALLBACK_ITEMS)/sizeof(FALLBACK_ITEMS[0]);

    // Fallback loot: the original global table (weights per 100 rolls,
    // remainder = no drop), item ids resolved against the ACTIVE table.
    static const struct { const char *id; int weight; } FALLBACK_LOOT[] =
    {
        { "health_potion", 30 },
        { "puglin_stick",  10 },
        { "leather_helm",  10 },
        { "iron_boots",     6 },
        { "ranger_tunic",   6 },
        { "imp_mace",       5 },
    };

    // ---------- active (data-loaded) tables ----------
    static std::vector<itemdef> dataitems;
    static std::map<std::string, std::vector<std::pair<int, int> > > dataloot; // typekey -> (item index, weight)
    static bool usingdata = false;
    static bool contentloaded = false;

    int itemcount()
    {
        return usingdata ? (int)dataitems.size() : FALLBACK_COUNT;
    }

    const itemdef &item(int id)
    {
        static const itemdef invalid = { "", "", ITEM_CONSUMABLE, SLOT_NONE, "", NULL, NULL, 0, 0, 0, false, 0, 0, 0, -1 };
        if(id < 0 || id >= itemcount()) return invalid;
        return usingdata ? dataitems[id] : FALLBACK_ITEMS[id];
    }

    int finditem(const char *id)
    {
        if(!id || !id[0]) return -1;
        int n = itemcount();
        for(int i = 0; i < n; i++) if(!strcmp(item(i).id, id)) return i;
        return -1;
    }

    const char *itemname(int id)
    {
        if(id < 0 || id >= itemcount()) return "";
        return item(id).name;
    }

    static const char *SLOTNAMES[SLOT_COUNT] = { "mainhand", "head", "torso", "legs", "feet", "neck", "ring1", "ring2" };

    const char *slotname(int slot)
    {
        if(slot < 0 || slot >= SLOT_COUNT) return "";
        return SLOTNAMES[slot];
    }

    int slotid(const char *name)
    {
        if(!name || !name[0]) return SLOT_NONE;
        for(int i = 0; i < SLOT_COUNT; i++) if(!strcmp(SLOTNAMES[i], name)) return i;
        return SLOT_NONE;
    }

    // ---------- content builders (filled by the ss_* commands during exec) ----------
    struct itembuild
    {
        bool active = false;
        std::string id, name, type, slot, icon, vwep, weaponcat;
        int damage = 0, armor = 0, heal = 0, mana = 0, value = 0, tier = 0;
        int buffattr = -1;
        bool stack = false;
    };
    static itembuild curitem;
    static std::vector<itembuild> builtitems;

    static void commitpendingitem()
    {
        if(!curitem.active) return;
        builtitems.push_back(curitem);
        curitem = itembuild();
    }

    static std::string curlootkey;
    static bool havecurloot = false;
    static std::map<std::string, std::vector<std::pair<std::string, int> > > builtloot;

    // ---------- ss_* setter handlers (driven by parsecontentfile) ----------
    static bool parseintarg(const char *cmd, const char *s, int &out, std::string &err)
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

    static bool itemsethandler(const char *cmd, int argc, const char *argv[], std::string &err)
    {
        std::string c = cmd ? cmd : "";
        if(c == "ss_item")
        {
            if(argc != 1) { err = "ss_item: expected exactly 1 argument (item id)"; return false; }
            commitpendingitem();
            curitem = itembuild();
            curitem.active = true;
            curitem.id = argv[0];
            return true;
        }
        if(c == "ss_loot")
        {
            if(argc != 1) { err = "ss_loot: expected exactly 1 argument (monster class key)"; return false; }
            curlootkey = argv[0];
            havecurloot = true;
            builtloot[curlootkey]; // ensure the table exists even if left empty
            return true;
        }
        if(c == "ss_loot_add")
        {
            if(argc != 2) { err = "ss_loot_add: expected 2 arguments (item id, weight)"; return false; }
            if(!havecurloot) { err = "ss_loot_add with no preceding ss_loot"; return false; }
            int w = 0;
            if(!parseintarg(cmd, argv[1], w, err)) return false;
            builtloot[curlootkey].push_back(std::make_pair(std::string(argv[0]), w));
            return true;
        }
        // everything below sets a field on the item being defined
        if(!curitem.active) { err = c + " with no preceding ss_item"; return false; }
        if(c == "ss_item_name") { if(argc != 1) { err = "ss_item_name: expected 1 argument"; return false; } curitem.name = argv[0]; return true; }
        if(c == "ss_item_type") { if(argc != 1) { err = "ss_item_type: expected 1 argument"; return false; } curitem.type = argv[0]; return true; }
        if(c == "ss_item_slot") { if(argc != 1) { err = "ss_item_slot: expected 1 argument"; return false; } curitem.slot = argv[0]; return true; }
        if(c == "ss_item_icon") { if(argc != 1) { err = "ss_item_icon: expected 1 argument"; return false; } curitem.icon = argv[0]; return true; }
        if(c == "ss_item_vwep") { if(argc != 1) { err = "ss_item_vwep: expected 1 argument"; return false; } curitem.vwep = argv[0]; return true; }
        if(c == "ss_item_weaponcat") { if(argc != 1) { err = "ss_item_weaponcat: expected 1 argument (weapon category)"; return false; } curitem.weaponcat = argv[0]; return true; }
        if(c == "ss_item_damage") { if(argc != 1 || !parseintarg(cmd, argv[0], curitem.damage, err)) return false; return true; }
        if(c == "ss_item_armor") { if(argc != 1 || !parseintarg(cmd, argv[0], curitem.armor, err)) return false; return true; }
        if(c == "ss_item_heal") { if(argc != 1 || !parseintarg(cmd, argv[0], curitem.heal, err)) return false; return true; }
        if(c == "ss_item_mana") { if(argc != 1 || !parseintarg(cmd, argv[0], curitem.mana, err)) return false; return true; }
        if(c == "ss_item_value") { if(argc != 1 || !parseintarg(cmd, argv[0], curitem.value, err)) return false; return true; }
        if(c == "ss_item_tier")
        {
            if(argc != 1 || !parseintarg(cmd, argv[0], curitem.tier, err)) return false;
            if(curitem.tier < 0 || curitem.tier > 4) { err = "ss_item_tier out of range 0..4"; return false; }
            return true;
        }
        if(c == "ss_item_buff")
        {
            if(argc != 1) { err = "ss_item_buff: expected 1 argument (attribute name)"; return false; }
            if(argv[0] == std::string("strength")) curitem.buffattr = ATTR_STRENGTH;
            else if(argv[0] == std::string("dexterity")) curitem.buffattr = ATTR_DEXTERITY;
            else if(argv[0] == std::string("vitality")) curitem.buffattr = ATTR_VITALITY;
            else if(argv[0] == std::string("knowledge")) curitem.buffattr = ATTR_KNOWLEDGE;
            else if(argv[0] == std::string("wisdom")) curitem.buffattr = ATTR_WISDOM;
            else if(argv[0] == std::string("cunning")) curitem.buffattr = ATTR_CUNNING;
            else { err = std::string("ss_item_buff: unknown attribute '") + argv[0] + "'"; return false; }
            return true;
        }
        if(c == "ss_item_stack")
        {
            int v = 0;
            if(argc != 1 || !parseintarg(cmd, argv[0], v, err)) return false;
            curitem.stack = v != 0;
            return true;
        }
        err = "unknown content command '" + c + "'";
        return false;
    }

    // ---------- the content-file parser ----------
    // findfile("e") only checks homedir + packagedirs, never cwd — so also
    // try a cwd-relative access() or data/rpg/*.cfg are silently "missing"
    // and quests/dialogue stay disabled (bug found Oct 22).
    bool contentfileexists(const char *path)
    {
        return path && (findfile(path, "e") != NULL || fileexists(path, "r"));
    }

    bool parsecontentfile(const char *path, sslinehandler handler, std::string &err)
    {
        char *buf = loadfile(path, NULL);
        if(!buf)
        {
            err = std::string("cannot read ") + (path ? path : "");
            return false;
        }
        std::string text(buf);
        delete[] buf;
        int lineno = 0;
        size_t pos = 0;
        while(pos <= text.size())
        {
            size_t eol = text.find('\n', pos);
            std::string line = text.substr(pos, eol == std::string::npos ? std::string::npos : eol - pos);
            lineno++;
            // tokenize: bare words, "quoted strings" (with \-escapes), // comments
            std::vector<std::string> toks;
            bool badquote = false;
            size_t i = 0;
            while(i < line.size())
            {
                char ch = line[i];
                if(ch == ' ' || ch == '\t' || ch == '\r') { i++; continue; }
                if(ch == '/' && i + 1 < line.size() && line[i + 1] == '/') break;
                if(ch == '"')
                {
                    i++;
                    std::string tok;
                    bool closed = false;
                    while(i < line.size())
                    {
                        if(line[i] == '\\' && i + 1 < line.size()) { tok += line[i + 1]; i += 2; continue; }
                        if(line[i] == '"') { closed = true; i++; break; }
                        tok += line[i++];
                    }
                    if(!closed) { badquote = true; break; }
                    toks.push_back(tok);
                }
                else
                {
                    size_t start = i;
                    while(i < line.size() && line[i] != ' ' && line[i] != '\t' && line[i] != '\r' &&
                          !(line[i] == '/' && i + 1 < line.size() && line[i + 1] == '/')) i++;
                    toks.push_back(line.substr(start, i - start));
                }
            }
            if(badquote)
            {
                err = std::string(path) + ":" + std::to_string(lineno) + ": unterminated string";
                return false;
            }
            if(!toks.empty())
            {
                std::vector<const char *> argv;
                for(size_t k = 1; k < toks.size(); k++) argv.push_back(toks[k].c_str());
                if(!handler(toks[0].c_str(), (int)argv.size(), argv.empty() ? NULL : &argv[0], err))
                {
                    err = std::string(path) + ":" + std::to_string(lineno) + ": " + err;
                    return false;
                }
            }
            if(eol == std::string::npos) break;
            pos = eol + 1;
        }
        return true;
    }

    // ---------- validation + commit ----------
    static int typefromname(const std::string &t)
    {
        if(t == "weapon") return ITEM_WEAPON;
        if(t == "armor") return ITEM_ARMOR;
        if(t == "consumable") return ITEM_CONSUMABLE;
        if(t == "material") return ITEM_MATERIAL;
        if(t == "tool") return ITEM_TOOL;
        if(t == "quest") return ITEM_QUEST;
        return -1;
    }

    static int slotfromname(const std::string &s, bool &ok)
    {
        ok = true;
        if(s.empty() || s == "none") return SLOT_NONE;
        for(int i = 0; i < SLOT_COUNT; i++) if(s == SLOTNAMES[i]) return i;
        ok = false;
        return SLOT_NONE;
    }

    static bool validateitems(std::string &err)
    {
        if(builtitems.empty()) { err = "no items defined"; return false; }
        std::map<std::string, int> seen;
        for(size_t i = 0; i < builtitems.size(); i++)
        {
            const itembuild &b = builtitems[i];
            if(b.id.empty()) { err = "an item definition has an empty id"; return false; }
            if(seen.count(b.id)) { err = "duplicate item id '" + b.id + "'"; return false; }
            seen[b.id] = 1;
            if(b.name.empty()) { err = "item '" + b.id + "' is missing ss_item_name"; return false; }
            int type = typefromname(b.type);
            if(type < 0) { err = "item '" + b.id + "' has unknown type '" + b.type + "'"; return false; }
            bool slotok = true;
            int slot = slotfromname(b.slot, slotok);
            if(!slotok) { err = "item '" + b.id + "' has unknown slot '" + b.slot + "'"; return false; }
            if(type == ITEM_WEAPON && slot != SLOT_MAINHAND)
            { err = "weapon '" + b.id + "' must use slot mainhand"; return false; }
            if(type == ITEM_ARMOR && (slot == SLOT_NONE || slot == SLOT_MAINHAND))
            { err = "armor '" + b.id + "' must name a body slot (head/torso/legs/feet/neck/ring1/ring2)"; return false; }
            if(type != ITEM_WEAPON && type != ITEM_ARMOR && slot != SLOT_NONE)
            { err = "item '" + b.id + "' is not gear and cannot name a slot"; return false; }
            if(b.damage < 0 || b.armor < 0 || b.heal < 0 || b.mana < 0 || b.value < 0)
            { err = "item '" + b.id + "' has a negative stat"; return false; }
            if(type != ITEM_WEAPON && b.damage != 0)
            { err = "item '" + b.id + "' sets damage but is not a weapon"; return false; }
            if(!b.weaponcat.empty())
            {
                if(type != ITEM_WEAPON)
                { err = "item '" + b.id + "' sets a weapon category but is not a weapon"; return false; }
                static const char *cats[] = { "mace", "axe", "sword", "staff", "wand", "orb", "bow", "crossbow", NULL };
                bool catok = false;
                for(int k = 0; cats[k]; k++) if(b.weaponcat == cats[k]) { catok = true; break; }
                if(!catok)
                { err = "item '" + b.id + "' has unknown weapon category '" + b.weaponcat + "'"; return false; }
            }
            if(type != ITEM_ARMOR && b.armor != 0)
            { err = "item '" + b.id + "' sets armor but is not armor"; return false; }
            if(type != ITEM_CONSUMABLE && (b.heal != 0 || b.mana != 0))
            { err = "item '" + b.id + "' sets heal/mana but is not a consumable"; return false; }
            if(b.buffattr < -1 || b.buffattr >= ATTR_COUNT)
            { err = "item '" + b.id + "' has an out-of-range buff attribute"; return false; }
            if(b.buffattr >= 0 && type != ITEM_CONSUMABLE)
            { err = "item '" + b.id + "' sets a buff but is not a consumable"; return false; }
            if(b.buffattr >= 0 && b.heal != 0)
            { err = "item '" + b.id + "' sets both a buff and a heal; buff items must not heal"; return false; }
        }
        return true;
    }

    static void commititems()
    {
        dataitems.clear();
        for(size_t i = 0; i < builtitems.size(); i++)
        {
            const itembuild &b = builtitems[i];
            bool slotok = true;
            itemdef d;
            d.id = poolstr(b.id);
            d.name = poolstr(b.name);
            d.type = typefromname(b.type);
            d.slot = slotfromname(b.slot, slotok);
            d.icon = poolstr(b.icon.empty() ? "packages/ui/rpg/icons/icon_empty.png" : b.icon);
            d.vwep = b.vwep.empty() ? NULL : poolstr(b.vwep);
            d.weaponcat = b.weaponcat.empty() ? NULL : poolstr(b.weaponcat);
            d.damage = b.damage;
            d.armor = b.armor;
            d.heal = b.heal;
            d.stackable = b.stack;
            d.value = b.value;
            d.mana = b.mana;
            d.tier = b.tier;
            d.buffattr = b.buffattr;
            dataitems.push_back(d);
            // a missing icon must not stop a server (design §1.3): warn only
            if(!findfile(d.icon, "e"))
                conoutf("rpg: content: warning: icon '%s' for item '%s' not found", d.icon, d.id);
        }
        builtitems.clear();
        curitem = itembuild();
    }

    static bool validateloot(std::string &err)
    {
        for(auto &kv : builtloot)
        {
            int total = 0;
            for(auto &e : kv.second)
            {
                if(finditem(e.first.c_str()) < 0)
                { err = "loot table '" + kv.first + "' names unknown item '" + e.first + "'"; return false; }
                if(e.second < 1)
                { err = "loot table '" + kv.first + "' has a non-positive weight for '" + e.first + "'"; return false; }
                total += e.second;
            }
            if(total > 100)
            { err = "loot table '" + kv.first + "' weights sum past 100 (later entries could never roll)"; return false; }
        }
        return true;
    }

    static void commitloot()
    {
        dataloot.clear();
        for(auto &kv : builtloot)
        {
            std::vector<std::pair<int, int> > &tab = dataloot[kv.first];
            for(auto &e : kv.second) tab.push_back(std::make_pair(finditem(e.first.c_str()), e.second));
        }
        builtloot.clear();
        havecurloot = false;
        curlootkey.clear();
    }

    static void rollbackcontent(const char *file, const std::string &err)
    {
        conoutf(CON_ERROR, "rpg: content: %s rejected (%s) - whole content set discarded, compiled-in fallback stays active", file, err.c_str());
        usingdata = false;
        dataitems.clear();
        dataloot.clear();
        builtitems.clear();
        builtloot.clear();
    }

    static unsigned fnv1a(const char *s, unsigned h)
    {
        while(*s) { h ^= (unsigned char)*s++; h *= 16777619u; }
        return h;
    }

    static unsigned contentchecksum()
    {
        unsigned h = 2166136261u;
        int n = itemcount();
        for(int i = 0; i < n; i++)
        {
            const itemdef &d = item(i);
            h = fnv1a(d.id, h);
            h ^= (unsigned)(d.type * 131 + d.slot * 17 + d.damage * 7 + d.armor * 5 + d.heal * 3 + d.mana * 11 + d.value);
            h *= 16777619u;
        }
        for(auto &kv : dataloot)
        {
            h = fnv1a(kv.first.c_str(), h);
            for(auto &e : kv.second)
            {
                h = fnv1a(item(e.first).id, h);
                h ^= (unsigned)e.second;
                h *= 16777619u;
            }
        }
        return h;
    }

    bool contentfromdata()
    {
        return usingdata;
    }

    bool loadcontent()
    {
        if(contentloaded) return usingdata;
        contentloaded = true;
        builtitems.clear();
        builtloot.clear();
        curitem = itembuild();
        havecurloot = false;
        curlootkey.clear();

        std::string err;
        if(!contentfileexists("data/rpg/items.cfg"))
        {
            conoutf(CON_ERROR, "rpg: content: data/rpg/items.cfg missing - compiled-in fallback content stays active");
            return false;
        }
        if(!parsecontentfile("data/rpg/items.cfg", itemsethandler, err))
        {
            conoutf(CON_ERROR, "rpg: content: items.cfg rejected (%s) - compiled-in fallback content stays active", err.c_str());
            builtitems.clear();
            return false;
        }
        commitpendingitem();
        if(!validateitems(err))
        {
            conoutf(CON_ERROR, "rpg: content: items.cfg rejected (%s) - compiled-in fallback content stays active", err.c_str());
            builtitems.clear();
            return false;
        }
        commititems();
        usingdata = true; // item table live; loot validation below may still roll the whole set back

        if(contentfileexists("data/rpg/loot.cfg"))
        {
            if(!parsecontentfile("data/rpg/loot.cfg", itemsethandler, err)) { rollbackcontent("loot.cfg", err); return false; }
            if(!validateloot(err)) { rollbackcontent("loot.cfg", err); return false; }
            commitloot();
        }
        else conoutf("rpg: content: warning: data/rpg/loot.cfg missing - no per-type loot tables (global fallback applies)");

        // Crafting recipes (Phase 1): additive - a missing or rejected
        // crafting.cfg leaves the game running with no recipes, and the
        // reason is logged loudly above. It never rolls back items/loot.
        loadcrafting();

        conoutf("rpg: content: %d items, %d loot tables loaded from data/rpg (checksum %08x)",
                itemcount(), (int)dataloot.size(), contentchecksum());
        return true;
    }

    // ---------- loot rolling ----------
    static int rolltable(const std::vector<std::pair<int, int> > &t)
    {
        int roll = std::rand() % 100;
        for(size_t i = 0; i < t.size(); i++)
        {
            if(roll < t[i].second) return t[i].first;
            roll -= t[i].second;
        }
        return -1;
    }

    int rollloot(const char *typekey)
    {
        if(usingdata)
        {
            if(typekey && typekey[0])
            {
                auto it = dataloot.find(typekey);
                if(it != dataloot.end()) return rolltable(it->second);
            }
            auto d = dataloot.find("default");
            if(d != dataloot.end()) return rolltable(d->second);
        }
        // compiled-in global table (pre-content-layer behavior)
        int roll = std::rand() % 100;
        for(size_t i = 0; i < sizeof(FALLBACK_LOOT)/sizeof(FALLBACK_LOOT[0]); i++)
        {
            if(roll < FALLBACK_LOOT[i].weight) return finditem(FALLBACK_LOOT[i].id);
            roll -= FALLBACK_LOOT[i].weight;
        }
        return -1;
    }

    // v0.1.23: mob coin drops. Amounts are copper range for early game
    // (WoW research: lvl1-10 mobs drop copper/small silver, players report
    // never more than 30 silver leaving the starting zone). Coin drops are
    // always granted (not a chance roll) in addition to item loot, so every
    // kill feels rewarding. Champion is a boss, drops more.
    int rollcoins(const char *typekey)
    {
        if(!typekey || !typekey[0]) return 0;
        if(!strcmp(typekey, "zombie")) return 1 + (std::rand() % 4); // 1-4c
        if(!strcmp(typekey, "imp")) return 3 + (std::rand() % 6); // 3-8c
        if(!strcmp(typekey, "puglin")) return 5 + (std::rand() % 8); // 5-12c
        if(!strcmp(typekey, "champion")) return 25 + (std::rand() % 26); // 25-50c
        return 0;
    }
}
