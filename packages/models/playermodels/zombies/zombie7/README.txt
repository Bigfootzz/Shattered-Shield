QUATERNIUS ZOMBIE SPIKE v3 — drop-in test for the zombie7 slot
=============================================================

WHAT CHANGED FROM v2
1. FACING (fixes "walks sideways"): v2's model faces +Y in model space
   while the engine's monster convention (zombie3) is +X, so the zombie
   rendered 90 degrees off its travel direction. The cfg now carries
   mdlyaw -90, rotating the whole model (all anims) to face +X like
   zombie3. No geometry or anims were re-exported.
2. ATTACK (fixes "never changes when attacking"): bite-zombies spawn with
   gunselect=WEAP_BITE (12), so the engine clamps the attack anim slot to
   ANIM_ATTACK1+12 = "attack 7" — a slot no zombie cfg defines, so the
   engine fell back to a static bind-pose frame and the swing never
   showed. The cfg now wires the real attack clip to "attack 7" as well
   as "attack 1".
3. SKIN (de-pale): gamma lift reduced 1.8 -> 1.55 (whole-image mean
   luminance 0.334 -> 0.291; v1 was 0.173). A touch deeper than v2, far
   from v1's near-black; hue and detail preserved.
   Everything else (size 65.5 units, usescale 18.07, mdlspec -1) is
   unchanged from v2.

INSTALL (your game's folder, NOT this zip's folder)
1. Go to  packages/models/playermodels/zombies/zombie7/
2. MOVE OUT (don't delete, just relocate):  md3.cfg  AND  tris.md3
   The engine loads MD3 preferentially — if those stay, you will NOT
   see the new model.
3. Copy into zombie7/: zombie7.iqm, the 10 *.iqm anims, iqm.cfg,
   skin.jpg, LICENSE.txt (this README can stay out).
4. Launch the game, start/continue a zombie map, compare zombie7
   against zombie1/zombie2 for size.

TO REVERT: move md3.cfg + tris.md3 back, delete the IQM files.

ANIMATION SLOTS (same as v2, rates mirror zombie3's iqm.cfg)
  attack 1 <- ATK        (zombie swipe attack; kept for compatibility)
  attack 7 <- ATK        (THE slot bite-zombies actually trigger)
  dying    <- DEAD 0-60% (fall)
  dead     <- DEAD last 3 frames (lying flat)
  pain     <- HIT        (hit reaction)
  idle     <- IDLE       (zombie idle sway)
  forward  <- WALK       (shamble)
  backward <- WALK reversed (baked; nearest honest match)
  left     <- WALK       (nearest honest match — no strafe clip)
  right    <- WALK       (nearest honest match — no strafe clip)
  jump     <- RUN        (nearest honest match — zombies rarely jump)
  swim/sink <- forward.iqm (same as zombie3's cfg does)

CAVEATS
- backward/left/right/jump are nearest-substitute bakes.
- Source is the Godot Asset Library port of Quaternius's Animated Zombie
  (same CC0 art; port license BSL-1.0, included as LICENSE.txt).
- Jump only triggers when the engine puts the monster airborne
  (timeinair>100); zombies rarely leave the ground, so don't expect to
  see it often — that is engine behavior, not a package defect.
