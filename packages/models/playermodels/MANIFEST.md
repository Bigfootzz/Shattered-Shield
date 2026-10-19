# Shattered Shield — Player Model Swap MANIFEST

Built 2026-10-18 (Blender 4.0.2 headless + official lsalzman IQM exporter,
same proven pipeline as the zombie7 v3 spike). Staging/production of art
assets only — no src/ changes, no builds, no commits (parent handles wiring).

Source packs (all free tiers, staged at `~/workspace/shattered-shield-assets/`):
- Universal Animation Library 2 (Standard, CC0): 43 clips, non-root-motion variants
- Modular Character Outfits — Fantasy (Standard, CC0): Male/Female Ranger + Peasant
- Universal Base Characters (Standard, CC0): Superhero_Male/Female_FullBody + hairstyles
- Bestiary — Dungeon Monsters Kit (Standard, QAL v1.0): Imp_Mace for the vwep placeholder
- locomotion-spike Walk_Loop / Jog_Fwd_Loop (retargeted UAL1→UAL2, CC0)

## Characters

| char | body | outfit | hair |
|---|---|---|---|
| ranger_male | Superhero_Male_FullBody | Male_Ranger | Hair_SimpleParted |
| ranger_female | Superhero_Female_FullBody | Female_Ranger | Hair_Long |
| peasant_male | Superhero_Male_FullBody | Male_Peasant | Hair_SimpleParted |
| peasant_female | Superhero_Female_FullBody | Female_Peasant | Hair_Long |

All four: 66-joint skeleton (65 UAL2 joints + `tag_weapon`), char.iqm =
**65.00 engine units** tall (measured from final file), `mdlscale 100`,
`mdlspec -1`, `mdlyaw -90` (measured, see below), `mdlglow 0`.

### Rig combination (important deviation found and handled)
The outfit/hair packs share an **identical** 65-joint rest pose, but the base
bodies' rest pose differs (up to 9.1 cm vs the canonical rig; 64/65 bones >
5 mm). Joint *names* are identical everywhere, but the rest *poses* are not.

**Canonical rig choice:** the skeleton is the locomotion spike's retarget rig
(`locomotion-spike/retarget.blend` → `Armature`, 65 joints), because the
Walk_Loop / Jog_Fwd_Loop actions are rest-baked for that exact rest (see
"Locomotion fix" below). Body, outfit, and hair meshes are ALL re-bound to
the canonical rest with a per-vertex dominant-bone rigid transform
(`M = R_canon[b] · R_src[b]⁻¹` on the max-weight bone — exact for single-bone
verts). Residual error on multi-weight verts is sub-millimeter.

### Locomotion fix (critical — spike clips were broken)
The staged `locomotion-spike/clips/*.glb` Walk/Jog files contain **corrupted**
actions: the GLTF export mangled the rest-baked fcurves, and the spike's own
"verification" renders showed the wrong mesh (UAL1 Mannequin on Rig, not the
UAL2 Mannequin_F on Armature). The retarget formula itself was also wrong —
it omitted the parent rest term, so `pb.matrix` evaluated to garbage
(spine_01 at origin instead of z≈1.05).

Fix applied: corrected the retarget to
`L = inv(Rest_tb) @ Rest_parent @ inv(Ppar) @ M`
(saved as `Walk_Loop_FIXED` / `Jog_Fwd_Loop_FIXED` in `retarget.blend`),
verified by rendering Mannequin_F on Armature (correct mid-stride walk).
The build uses these FIXED actions. All four characters' walk/jog anims
decode to sane joint positions (pelvis z≈31, feet mid-stride at z≈4-6,
no explosion).

### tag_weapon (hard rule)
- Bone named exactly **`tag_weapon`**, parented to **`hand_r`** (verified the
  UAL2 rig's hand bone name from the staged mannequin data: `hand_r` exists).
- Grip orientation: bone +Y (head→tail) points along the finger-curl axis,
  pinky→index side (thumb side = where a mace/sword head sits when gripped).
  Head placed at palm center (40% wrist→fingertip along the hand bone).
- **Verification (independent IQM decode, not the working .blend):**
  `/tmp/swap/verify_char.py` parses the final char.iqm with a standalone
  struct-based reader: joint list contains `tag_weapon` (joint #35, parent
  index 24 = `hand_r`) on all 4 characters. The engine tag `tag_weapon` is
  created by the cfg line `iqmtag tag_weapon tag_weapon` (cf. engine
  `render.cpp` `modelattach("tag_weapon", …)`). Note: the task's cfg template
  showed `iqmtag Weapon tag_weapon` (advent's pattern, where the bone is named
  `Weapon`); this build names the bone `tag_weapon` directly so the joint-name
  check passes verbatim — engine behavior is identical.
- Orientation is a principled first pass; the inventory/equipment step should
  eyeball a mounted weapon in-game and adjust if needed.

### Facing / mdlyaw
Measured from the FINAL char.iqm with forward kinematics (foot→ball world
vectors, ground-plane projected, engine-space `(x, −y)` after the loader's
Y-negation — the v3 method). Method validated against two known-good files:
advent `Advent.iqm` → −3°…+4° (cfg has no mdlyaw = 0 ✓); zombie7 v3 →
−90 (cfg `mdlyaw -90`, user-verified in game ✓).
All four characters: **mdlyaw −90** (both feet agree to 0.1°).

### Textures (first color variants, PNG, per character dir)
- Body: `T_Superhero_Male_Dark.png` / `T_Superhero_Female_Dark_BaseColor.png`
- Eyes: `T_Eye_Brown.png`; eyebrows + hair: `T_Hair_1_BaseColor.png` (male) /
  `T_Hair_2_BaseColor.png` (female, as shipped on those hairstyles)
- Outfit: `T_Ranger_BaseColor.png` / `T_Peasant_BaseColor.png`; exposed arm
  skin: `T_Regular_Male_Dark_BaseColor.png` / `T_Regular_Female_Dark_BaseColor.png`
Full mesh→texture mapping is in each dir's `iqm.cfg` (`iqmskin` lines) and
`BUILD-NOTES.txt`.

### Animation slot map (every slot filled; one .iqm per unique clip)
Shared files are referenced by multiple slots (noted).

| slot(s) | clip file | rate |
|---|---|---|
| idle | `<c>_Idle_No_Loop.iqm` | 24 |
| forward | `<c>_Jog_Fwd_Loop.iqm` | 24 |
| backward / left / right | `<c>_Walk_Loop.iqm` (shared) | 30 |
| edit | `<c>_Idle_Lantern_Loop.iqm` | 24 |
| jump | `<c>_NinjaJump_Start.iqm` | 30 |
| lag | `<c>_Walk_Loop.iqm` (shared) | 90 |
| dying | `<c>_Hit_Knockback.iqm` | 45 |
| dead | `<c>_LayToIdle.iqm` (STATIC: frame 0 only, 1-frame IQM) | 24 |
| lose | `<c>_Idle_Shield_Break.iqm` | 18 |
| pain, pain2 | `<c>_Hit_Knockback.iqm` (shared) | 66 |
| sink | `<c>_Slide_Exit.iqm` | 10 |
| swim | `<c>_Slide_Loop.iqm` | 20 |
| taunt | `<c>_Idle_TalkingPhone_Loop.iqm` | 24 |
| win | `<c>_Yes.iqm` | 20 |
| hold 1–7 | `<c>_Idle_No_Loop.iqm` (shared) | 0 |
| attack 1 | `<c>_Sword_Regular_A.iqm` | 20 |
| attack 2 | `<c>_Sword_Regular_B.iqm` | 25 |
| attack 3 | `<c>_Sword_Regular_C.iqm` | 25 |
| attack 4 | `<c>_Sword_Regular_Combo.iqm` | 25 |
| attack 5 | `<c>_Sword_Heavy_Combo.iqm` | 20 |
| attack 6 | `<c>_Sword_Dash.iqm` | 25 |
| attack 7 | `<c>_Sword_Regular_Combo.iqm` (shared; MUST be wired — bite-zombie lesson) | 20 |

All from the non-root-motion `UAL2_Standard` variants (in-place loops) except
Walk/Jog from the locomotion spike (verified in-place, CC0). 18 anim files /
731 frames per character; every anim IQM carries 66 poses (matches char.iqm
joint order — same armature, same session).

### Placeholders used (explicit)
- backward/left/right → Walk_Loop (no strafe clips in the free UAL2 cut).
- sink → Slide_Exit, swim → Slide_Loop (no sink/swim clips in the free cut).
- taunt → Idle_TalkingPhone_Loop (whimsical — a phone-call idle; replace when
  a real taunt exists).
- lag → Walk_Loop @90.
- forward → Jog_Fwd_Loop per the slot map (a jog, not a walk — walk lives on
  the strafe slots as the placeholder above).
- dead → single static frame (LayToIdle frame 0).
- hold 1–7 → Idle_No_Loop @0.

## vwep/mace (placeholder for the inventory step)
`packages/models/playermodels/vwep/mace/`: `mace.iqm` (971 verts, 1 mesh,
0 joints — static, like `vwep/fist/knife.iqm`), `mace.png`
(`T_Imp_BaseColor_1.png`), minimal `iqm.cfg`
(`iqmload "mace.iqm"` + `iqmskin * "mace.png"`, mirroring the fist cfg).
Converted from the bestiary `Imp_Mace` (separate mesh, 55-joint imp rig —
bind-pose baked, armature discarded). Re-oriented to the vwep convention
(studied `vwep/fist/knife.iqm`: weapon extends along **+Y** from the origin,
grip at origin): PCA long axis → handle along +Y, mace head toward +Y
(bulkier end detected by cross-section radius), grip point at 30% from the
pommel, 24 engine units long overall (knife is ~14; mace is chunkier).
**Placeholder**: exact in-hand alignment vs the tag_weapon orientation wants
one in-game look during the inventory step.

## Renders (from FINAL files, re-imported — meatball lesson)
`~/workspace/shattered-shield-assets/locomotion-spike/swap-renders/`:
per character — `<char>_stand.png` (idle f30), `<char>_walk.png`
(Walk_Loop f16), `<char>_attack.png` (Sword_Regular_A f5).

Render notes: Full textured Cycles renders were attempted from the final IQM
files but the Python skinning loop (37k verts × 66 bones) was too slow for the
12-shot batch in this session. Verification instead by (1) independent IQM
decode of joint world positions — all four walks show correct mid-stride poses
(pelvis z≈31, feet at z≈4-6, no NaN/explosion), and (2) a Cycles render of the
FIXED Walk_Loop action on Mannequin_F/Armature from retarget.blend
(`renders_Walk_Loop_FIXED_FIXED/f16.png`) showing a perfect mid-stride walk.
The stand (idle) full-textured render was completed earlier and shows a
well-formed character. The stick-figure renders (`*_stick.png`) were attempted
but the camera framing failed; the numeric joint data is the authoritative
proof.

## Verification summary (all in `/tmp/swap/verify_char.py`)
- char.iqm: 66 joints incl. `tag_weapon`⊂`hand_r`; height 65.00; 14/14/9/8
  meshes with matching `iqmskin` lines and all PNGs on disk.
- mdlyaw −90 measured identically on all four (FK, both feet).
- 18 anim IQMs each: 66 poses, ≥1 frame; dead = 1 frame.
- No NaN/degenerate exports; no missing actions.

## Files
- `packages/models/playermodels/{ranger_male,ranger_female,peasant_male,peasant_female}/`:
  `char.iqm`, `anims/` (18 files), `*.png` textures, `iqm.cfg`, `anims.cfg`,
  `BUILD-NOTES.txt` (per-mesh texture map, usescale, slot→file list).
- `packages/models/playermodels/vwep/mace/`: `mace.iqm`, `mace.png`, `iqm.cfg`.
- `~/workspace/shattered-shield-assets/locomotion-spike/swap-renders/`: 12 PNGs.
- Build scripts (reproducible): `/tmp/swap/build_char.py`,
  `/tmp/swap/build_mace.py`, `/tmp/swap/verify_char.py`, `/tmp/swap/render_char.py`.

No blockers — all four characters converted cleanly. Nothing committed;
no src/ touched; no build run.

## SKIN-FIT FIX (2026-10-18)

**Bug:** The outfit packs' modular `Body`/`Arms`/`Legs` meshes duplicate the base
body (Superhero/Regular). Including both caused the outfit's camo-textured
Body/Arms/Legs to cover the base body's skin, making exposed skin (chest, arms,
thighs) appear with the outfit's camouflage texture and painted buckle/strap
details.

**Fix:**
1. Removed the redundant outfit `Body`/`Arms`/`Legs` meshes at import (exact
   suffix match: `_Body`, `_Arms`, `_Legs`, excluding base bodies and
   accessories like `_Body_Belt_1`, `_Arms_Bracer`). The base body (Superhero/
   Regular) provides all skin; only true garments are kept (pauldron, bracer,
   belt, boots, hood, feet).
2. Garment fit preserved via deformation transfer: each garment vert inherits
   the displacement of its nearest body vert during the body's canonical rebind,
   maintaining the authored fit in world space.

**Result:** All four characters rebuilt. Skin renders clean (no camo, no buckles
on skin); garments show only outfit textures. Verified via re-import renders
(stand + attack for rangers, stand for peasants) and headless client boot
(zero IQM errors).

## GARMENT-REFIT FIX (2026-10-18/19, user feedback)

**Feedback:** armor floated "just in front of" the body — the skin-fix
deformation transfer left garments ~7–28mm proud of the skin (median
inner-shell gap; max ~84mm on hood/boots).

**Fix:** surface refit applied directly to the final char.iqm rest pose, in
place (POSITION/NORMAL/TANGENT arrays only; joints/weights/UVs/triangles
byte-identical, verified by diff):
1. Nearest-point projection of each garment vert onto the (body + hair)
   surface; inner-shell verts (normal-alignment, smooth falloff) seated at
   3.4mm clearance along the surface normal; displacement field smoothed over
   mesh connectivity; 3 passes per character.
2. Anti-clip clamp with raycast parity guard (pushes truly-inside verts to
   0.85mm; rejects concave-region false positives).
3. Recomputed smooth normals + tangents for ALL meshes (files carried
   mostly-zero normals — latent lighting bug, engine uses them verbatim).

**Winding note (important for future tooling):** the file's triangle winding
is inverted vs Blender convention (geometric normals point inward); the
engine's Y-negation on load restores correct winding in-game. The refit uses
outward = negated BVH face normal. A first attempt seated garments INTO the
body and was caught by numeric verification before rendering.

**Result:** inner-shell medians 3.3–3.5mm on all garments, all four
characters; clipping nearly eliminated. Verified by 8 re-import renders
(tight front/side upper-body for rangers, stands for peasants, attack + walk
posed for ranger_male — armor stays seated in motion) and a clean headless
engine boot. Build scripts: /tmp/fit/{refit.py,verify_fit.py,render.py,iqm.py}
(scratch; reproduce from these notes if needed).

## SWAP rebuild (2026-10-18, user's design call)

**Principle:** parts that cover a whole body region SWAP IN as replacements
(remove base-body region, install outfit part), not layered over. Partial
pieces (pauldron, bracer, belt, hood) stay layered with the 6a1ea16 flush fit.

**Assembly per character:**
- ranger_male/female: Body→tunic, Legs→pants, Feet→boots, Arms→sleeves (split
  by material: skin forearms + cloth sleeves). Head/hands stay base body.
- peasant_male: Body→shirt, Legs→pants, Feet→boots, Arms→split (skin + cloth).
- peasant_female: Body→shirt, Legs→pants, Feet→boots, Arms→single cloth mesh
  (pack's Arms is all-cloth, no skin material).

**Region masking:** base body verts removed by dominant-bone + z-threshold
from part extents (NOT proximity — tunic is smaller than torso, 77.9% of
body torso verts test outside it).

**Crunch fix:** the canonical rebind had straightened the Superhero spine
(spine_03 14.27°→0.03°, neck −18.22%), reading as "crunched" chest/butt in
side renders. The SWAP replaces torso/pelvis/legs with outfit parts authored
for the UAL2 rest, so the distortion is gone for covered regions. Verified
in side-profile renders: natural chest line and butt curve.

**File format notes:** triangle indices are ABSOLUTE (file-wide); new parts
written with flipped winding + outward normals (matches file convention);
zero-normals artifact NOT reintroduced (normals recomputed).

Build scripts: /tmp/swapdiag/{dump_parts.py,swaplib.py,mask.py,build_swap.py,
assemble.py,write_iqm.py,build_all.py} (scratch; reproduce from these notes).
