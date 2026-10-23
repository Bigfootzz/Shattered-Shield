# Vwep Structure (v0.1.16)

View models (weapons) organized by class. Each weapon gets its own folder.

## Layout

```
vwep/
  ranger/       # Ranger class weapons
    bow/        # Wooden Bow
  warrior/      # Warrior class weapons
    mace/       # Imp Mace
  magi/         # Magi class weapons (empty - for future staff/wands)
  stick/        # Generic starter weapon (Puglin Stick)
```

## Adding New Weapons

1. Create a folder under the class: `vwep/<class>/<weapon>/`
2. Add the .iqm model and textures
3. Add an iqm.cfg if needed
4. Reference as `playermodels/vwep/<class>/<weapon>` in items.cpp
5. For the render fallback array, add to src/fpsgame/render.cpp vweps[]

The system is verified working with the current bow/mace/stick. New additions follow the same pattern.
