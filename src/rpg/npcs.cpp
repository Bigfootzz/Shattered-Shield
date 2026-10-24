// npcs.cpp - NPC content for the quest engine (editor Phase 1, Oct 24, 2026).
//
// NPCs are per-map data: data/rpg/npcs_<map>.cfg, in the flat ss_* setter
// syntax (same parser as the item/quest content files: quoted strings,
// // comments, strict, never executed). Loaded at map change on BOTH client
// and server (game layer calls loadnpcs); the tables are read-only afterwards
// except for editor mutations, which are in-memory until savenpcs.
//
// Fallback: when the data file is missing or fails validation, the hardcoded
// table below (the pre-Phase-1 NPCS[] contents, byte-identical positions)
// is used, so the game still works on maps without an NPC file. The fallback
// is transitional: once every shipped map has an NPC file, it can be removed.
#include "rpg.h"
#include "items.h"
#include "cube.h"
#include <vector>
#include <string>
#include <cstdio>

namespace rpg
{
    // ---- storage (static array, so npcat pointers stay stable across edits) ----
    static npcdef npcs[MAXNPCS];
    static int nnpcs = 0;
    static char loaded_map[128] = "";
    static bool from_file = false;

    // ---- fallback table: pre-Phase-1 NPCS[] contents, verbatim ----
    static const npcdef FALLBACK_NPCS[] =
    {
        { "maren", "Elder Maren", "npcs/female_peasant_static", 2228.0f, 1980.0f, 2116.0f, 135.0f, "maren_main", "questgiver" },
        { "tomas", "Tomas", "npcs/male_peasant_static", 2220.0f, 1980.0f, 2116.0f, 115.0f, "tomas_main", "questgiver" },
        { "corvin", "Corvin", "npcs/male_peasant_static", 3062.0f, 2612.0f, 2116.0f, 270.0f, "corvin_main", "questgiver" },
        { "glimmer", "Glimmer", "npcs/imp_vendor", 2980.0f, 2450.0f, 2116.0f, 180.0f, "vendor_main", "vendor" },
    };
    static const int FALLBACK_COUNT = sizeof(FALLBACK_NPCS)/sizeof(FALLBACK_NPCS[0]);

    static void use_fallback(const char *mapname)
    {
        nnpcs = 0;
        loopi(FALLBACK_COUNT)
        {
            if(nnpcs >= MAXNPCS) break;
            npcs[nnpcs++] = FALLBACK_NPCS[i];
        }
        copystring(loaded_map, mapname ? mapname : "");
        from_file = false;
    }

    // ---- accessors (declared in rpg.h) ----
    int npccount() { return nnpcs; }
    const npcdef *npcat(int i) { return (i >= 0 && i < nnpcs) ? &npcs[i] : NULL; }
    int npcindexbyid(const char *id)
    {
        if(!id || !id[0]) return -1;
        loopi(nnpcs) if(!strcmp(npcs[i].id, id)) return i;
        return -1;
    }
    const char *npcs_loaded_map() { return loaded_map; }
    bool npcs_from_file() { return from_file; }

    // ---- parser (same pattern as quests.cpp) ----
    struct nparse
    {
        int curnpc;
        int errors;
        nparse() : curnpc(-1), errors(0) {}
    };
    static nparse NP;

    static bool nline(const char *cmd, int argc, const char *argv[], std::string &err)
    {
        #define NERR(...) do { \
            char _b[256]; formatstring(_b)(__VA_ARGS__); \
            err = _b; NP.errors++; return false; } while(0)
        #define NEEDNPC() do { if(NP.curnpc < 0) NERR("'%s' outside any ss_npc", cmd); } while(0)

        if(!strcmp(cmd, "ss_npc"))
        {
            if(argc != 1) NERR("ss_npc takes 1 arg");
            if(nnpcs >= MAXNPCS) NERR("too many NPCs (max %d)", MAXNPCS);
            if(npcindexbyid(argv[0]) >= 0) NERR("duplicate npc id '%s'", argv[0]);
            npcdef &nd = npcs[nnpcs];
            memset(&nd, 0, sizeof(nd));
            copystring(nd.id, argv[0], sizeof(nd.id));
            // Defaults: ambient villager, no dialogue. Quest/vendor fields
            // are filled by later setters or by the Phase 2/3 editor.
            copystring(nd.type, "ambient", sizeof(nd.type));
            nd.x = nd.y = nd.z = 0.0f;
            nd.yaw = 0.0f;
            NP.curnpc = nnpcs;
            nnpcs++;
            return true;
        }
        if(!strcmp(cmd, "ss_npc_name"))     { NEEDNPC(); if(argc != 1) NERR("ss_npc_name takes 1 arg"); copystring(npcs[NP.curnpc].name, argv[0], sizeof(npcs[NP.curnpc].name)); return true; }
        if(!strcmp(cmd, "ss_npc_model"))    { NEEDNPC(); if(argc != 1) NERR("ss_npc_model takes 1 arg"); copystring(npcs[NP.curnpc].model, argv[0], sizeof(npcs[NP.curnpc].model)); return true; }
        if(!strcmp(cmd, "ss_npc_dialogue")) { NEEDNPC(); if(argc != 1) NERR("ss_npc_dialogue takes 1 arg"); copystring(npcs[NP.curnpc].dialogue, argv[0], sizeof(npcs[NP.curnpc].dialogue)); return true; }
        if(!strcmp(cmd, "ss_npc_type"))     { NEEDNPC(); if(argc != 1) NERR("ss_npc_type takes 1 arg"); copystring(npcs[NP.curnpc].type, argv[0], sizeof(npcs[NP.curnpc].type)); return true; }
        if(!strcmp(cmd, "ss_npc_pos"))
        {
            NEEDNPC();
            if(argc != 3) NERR("ss_npc_pos takes 3 args (x y z)");
            npcs[NP.curnpc].x = (float)atof(argv[0]);
            npcs[NP.curnpc].y = (float)atof(argv[1]);
            npcs[NP.curnpc].z = (float)atof(argv[2]);
            return true;
        }
        if(!strcmp(cmd, "ss_npc_yaw"))
        {
            NEEDNPC();
            if(argc != 1) NERR("ss_npc_yaw takes 1 arg");
            npcs[NP.curnpc].yaw = (float)atof(argv[0]);
            return true;
        }
        NERR("unknown npc directive '%s'", cmd);
        #undef NERR
        #undef NEEDNPC
    }

    bool loadnpcs(const char *mapname)
    {
        if(!mapname || !mapname[0])
        {
            // No map (menus, empty edit map): keep whatever is loaded, or
            // fall back so early npccount() calls don't return zero.
            if(nnpcs == 0) use_fallback("");
            return false;
        }
        // Already loaded for this map: no-op (map changes call this fresh).
        if(!strcmp(loaded_map, mapname) && (from_file || nnpcs > 0)) return from_file;

        defformatstring(path)("data/rpg/npcs_%s.cfg", mapname);
        if(!contentfileexists(path))
        {
            conoutf("rpg: npcs: %s missing - using hardcoded fallback NPCs", path);
            use_fallback(mapname);
            return false;
        }
        // Parse into a staging copy so a failed file never half-loads.
        npcdef staged[MAXNPCS];
        int staged_n = 0;
        // Temporarily point the parser at the staging area via globals swap.
        // Simplest safe route: parse into npcs[] directly, but snapshot first.
        npcdef backup[MAXNPCS];
        int backup_n = nnpcs;
        loopi(nnpcs) backup[i] = npcs[i];
        char backup_map[128];
        copystring(backup_map, loaded_map, sizeof(backup_map));
        bool backup_from = from_file;

        nnpcs = 0;
        NP = nparse();
        std::string err;
        if(!parsecontentfile(path, nline, err))
        {
            conoutf("rpg: npcs: %s parse failed (%s) - using fallback NPCs", path, err.c_str());
            nnpcs = backup_n;
            loopi(backup_n) npcs[i] = backup[i];
            copystring(loaded_map, backup_map, sizeof(loaded_map));
            from_file = backup_from;
            return false;
        }
        // Validate: every NPC needs an id, name, and model. Position (0,0,0)
        // is allowed (editor may place then move), but warn on it.
        loopi(nnpcs)
        {
            if(!npcs[i].id[0] || !npcs[i].name[0] || !npcs[i].model[0])
            {
                conoutf("rpg: npcs: %s: entry %d missing id/name/model - using fallback NPCs", path, i);
                nnpcs = backup_n;
                loopi(backup_n) npcs[i] = backup[i];
                copystring(loaded_map, backup_map, sizeof(loaded_map));
                from_file = backup_from;
                return false;
            }
            if(npcs[i].x == 0 && npcs[i].y == 0 && npcs[i].z == 0)
                conoutf("rpg: npcs: warning: '%s' at (0,0,0) - move it in the editor", npcs[i].id);
        }
        (void)staged; (void)staged_n;
        copystring(loaded_map, mapname, sizeof(loaded_map));
        from_file = true;
        conoutf("rpg: npcs: loaded %d NPCs for '%s' from %s", nnpcs, mapname, path);
        return true;
    }

    bool savenpcs(const char *mapname)
    {
        if(!mapname || !mapname[0]) mapname = loaded_map;
        if(!mapname || !mapname[0]) { conoutf("rpg: npcs: savenpcs: no map name"); return false; }
        defformatstring(path)("data/rpg/npcs_%s.cfg", mapname);
        FILE *f = fopen(path, "wb");
        if(!f) { conoutf("rpg: npcs: cannot write %s", path); return false; }
        fprintf(f, "// Shattered Shield NPC content - written by the in-editor NPC tool (F7).\n");
        fprintf(f, "// NPCs for %s. Flat ss_* setter lines, // comments, quoted strings.\n", mapname);
        fprintf(f, "// Hand-editable: the game reloads this file on map change.\n\n");
        loopi(nnpcs)
        {
            const npcdef &nd = npcs[i];
            fprintf(f, "ss_npc          \"%s\"\n", nd.id);
            fprintf(f, "ss_npc_name     \"%s\"\n", nd.name);
            fprintf(f, "ss_npc_model    \"%s\"\n", nd.model);
            fprintf(f, "ss_npc_pos      %.1f %.1f %.1f\n", nd.x, nd.y, nd.z);
            fprintf(f, "ss_npc_yaw      %.1f\n", nd.yaw);
            fprintf(f, "ss_npc_type     %s\n", nd.type[0] ? nd.type : "ambient");
            fprintf(f, "ss_npc_dialogue \"%s\"\n", nd.dialogue);
            fprintf(f, "\n");
        }
        fclose(f);
        copystring(loaded_map, mapname, sizeof(loaded_map));
        from_file = true;
        conoutf("rpg: npcs: saved %d NPCs to %s", nnpcs, path);
        return true;
    }

    int npc_add(const char *id, const char *name, const char *model, float x, float y, float z, float yaw, const char *dialogue, const char *type)
    {
        if(!id || !id[0]) return -1;
        if(npcindexbyid(id) >= 0) return -1; // duplicate id
        if(nnpcs >= MAXNPCS) return -1;
        npcdef &nd = npcs[nnpcs];
        memset(&nd, 0, sizeof(nd));
        copystring(nd.id, id, sizeof(nd.id));
        copystring(nd.name, name ? name : id, sizeof(nd.name));
        copystring(nd.model, model && model[0] ? model : "npcs/male_peasant_static", sizeof(nd.model));
        nd.x = x; nd.y = y; nd.z = z; nd.yaw = yaw;
        copystring(nd.dialogue, dialogue ? dialogue : "", sizeof(nd.dialogue));
        copystring(nd.type, type && type[0] ? type : "ambient", sizeof(nd.type));
        return nnpcs++;
    }

    bool npc_move(int idx, float x, float y, float z, float yaw)
    {
        if(idx < 0 || idx >= nnpcs) return false;
        npcs[idx].x = x; npcs[idx].y = y; npcs[idx].z = z; npcs[idx].yaw = yaw;
        return true;
    }

    bool npc_remove(int idx)
    {
        if(idx < 0 || idx >= nnpcs) return false;
        for(int i = idx; i + 1 < nnpcs; i++) npcs[i] = npcs[i+1];
        nnpcs--;
        return true;
    }
}
