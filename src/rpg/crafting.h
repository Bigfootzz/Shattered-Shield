// Shattered Shield RPG — crafting recipes, Phase 1 (Oct 27).
//
// Content data layer, same strict-parser shape as items/quests: recipes
// live in data/rpg/crafting.cfg and are parsed by loadcrafting(), called
// from loadcontent() after the item table is live. The server is the only
// side that resolves crafts; the client never resolves anything.
//
// Phase 1 scope: exactly two ingredients per recipe, both materials, one
// consumable output. Gear outputs, buff timers and catalysts are later
// phases and are rejected by validation here, so they cannot sneak in
// through content alone.
//
// Recipe identity is the canonical ingredient key (sorted, counts folded),
// not the ss_recipe label: slot order never matters, and the same pair in
// either order is the same recipe. Discovery is keyed the same way.
#pragma once

namespace rpg
{
    // Tier math (settled design, Oct 27): output tier is drawn from the
    // range between the lowest and highest ingredient tiers, weighted
    // toward the midpoint (triangular weights); same-tier ingredients
    // guarantee that tier; output never above the highest or below the
    // lowest ingredient. Pure function of tiers + seed (no engine state).
    int craft_tier(const int *tiers, int n, unsigned int seed);
    const char *crafttiername(int tier); // "common".."legendary", "" when bad

    // Canonical key for two craft slots (order-insensitive, 1 unit per
    // slot; same id in both slots folds to count 2). Returns false when
    // either id is empty or unknown.
    bool craftkey(const char *a, const char *b, char *out, int outlen);

    struct craftrecipe
    {
        const char *key;        // canonical ingredient key
        const char *ing[2];     // ingredient item ids (sorted order for material
                                // pairs; declared order — gear, material — for
                                // enhancement recipes, so ing[0] is the gear)
        int ingcount[2];        // required count per ingredient
        const char *out;        // base (tier 0) output item id
        int outcount;           // granted count
        const char *outtier[5]; // per-tier output variants, NULL when undefined
        bool enhance;           // Phase 3: gear + material -> enhanced gear
                                // (output tier = material tier, tier-gated)
    };
    int craftrecipecount();
    const craftrecipe *craftrecipebykey(const char *key);
    const craftrecipe *craftrecipebyindex(int i);
    // Output item id for a recipe at a computed tier: the tier variant
    // when defined, else the highest defined tier at or below it (tier 0
    // base always exists, so this never returns NULL for a valid recipe).
    const char *craftoutfortier(const craftrecipe *r, int tier);

    // Parse + validate data/rpg/crafting.cfg. False = no recipes loaded
    // (file missing or rejected; the reason is logged loudly). Idempotent
    // per process; called from loadcontent().
    bool loadcrafting();
}
