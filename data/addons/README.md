# Shattered Shield AddOns

Custom UI extensions, written in CubeScript, loaded at startup from
`data/addons/<name>/addon.cfg`. No C++ needed. See `sample/addon.cfg`
for a working example (a small info panel).

## Making an add-on

1. Create a folder: `data/addons/<yourname>/`
2. Write `addon.cfg` in it. This is the entry point; the engine runs it
   once at startup, after the base menus and game config load.
3. `addon.cfg` can `exec` other `.cfg` files in the same folder for
   larger add-ons.
4. Restart the game, or run `/loadaddons` in the console to reload
   without restarting. (`newgui` overwrites a menu of the same name,
   so reloading is safe.)

## What an add-on can do

- **Build UI** with the same GUI commands the game uses: `guiframe`,
  `guilist`, `guibutton`, `guitext`, `guibar`, `guiscroll`, `guibackgroundtex`,
  `guipanel`, `guidockright`, and the rest. Windows you define with
  `newgui` work exactly like the built-in ones.
- **Read game state** with the `rpg*` query commands (the same ones
  `rpg.cfg` uses to render the sheet, inventory, and so on). Never
  invent data; if a value is not exposed by a query, it is not
  available to add-ons.
- **Persist settings** with the stock `persist` command:
  `persist myaddon_showhelp` saves that variable across restarts.
  Use one variable per setting, prefixed with your add-on name.

## The hardware-event rule (WoW's protected-function rule)

Gameplay actions (equip, use, cast, buy, sell, respond to a dialog,
and anything that changes the world or your character) may ONLY run
inside a button or key callback, which is a real hardware event (a
player actually clicked or pressed a key).

- **Allowed:** a `guibutton` whose action equips an item, because the
  player clicked it.
- **Not allowed:** calling a gameplay action at load time (in
  `addon.cfg` top level), from a timer, or from a loop. There is no
  player behind it, so it is blocked by convention and will be
  blocked by the engine in a future hardening pass.

Add-ons are UI-only. If it would play the game for the player, it
does not belong in an add-on.

## Panel slots

`guipanel <slot>` in a window's init script marks it as occupying a
slot. Opening a window closes any other open window in the same slot.
The built-in main windows share slot `main`. Give your panel
`guipanel main` to behave like a built-in panel, or no slot to make
it a floating companion (like the spells panel).

## Docking

`guidockright <width> <gap>` in a window's init script docks its
left edge to the right edge of a centered window of `<width>` FONTW,
plus a `<gap>` FONTW gap. The built-in character sheet is 34 FONTW,
so the spells panel uses `guidockright 34 1`.

## Fixed frames

`guiframe "<name>" <width> <height> [ contents ]` gives a window a
fixed size in FONTW/FONTH. Content taller than the frame scrolls;
content wider than the frame is clipped. Use it instead of letting a
window grow with its content.

## Troubleshooting

- Add-on not loading: check the console for `loading add-on: <name>`.
  A folder without `addon.cfg` is ignored; a missing `data/addons/`
  folder is not an error.
- Syntax error in your script: the console names the file and the
  engine keeps running; fix it and run `/loadaddons`.
- A gameplay action does nothing from your script: it is probably
  running outside a hardware event. Move it into a button callback.
