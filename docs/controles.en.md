# Controls

*[Leer en español](controles.md) · Back to the [README](../README.en.md)*

## Buttons

| Button | Normal | With the on-screen keyboard |
|---|---|---|
| D-pad | Arrows (or the pointer, with `mouse = on`) | Move |
| A | Enter | Press the key |
| B | Space | Close |
| GAME (START on Zelda) | Esc | — |
| TIME (SELECT on Zelda) | **Open the keyboard** | Close |
| START / SELECT (Zelda) | Tab / Backspace | — |
| PAUSE/SET | retro-go menu (volume, brightness, quit…) | |

All of them can be changed in the `.cfg` (see [cfg.en.md](cfg.en.md)). PAUSE/SET
+ D-pad shows the volume (left/right) and brightness (up/down) bars. Holding a
button that sends a key repeats the key like a real keyboard.

## On-screen keyboard

Four rows, two in the top band and two in the bottom one; the game does not
move. Holding the D-pad keeps the cursor going.

- Letters, numbers, F1–F10, arrows, Home/End, Ins/Del.
- **Ctrl, Alt and Shift** stay pressed for the next key (green), for Ctrl+C,
  Shift+letter, etc. Shift pressed twice locks (red, like Caps Lock); a third
  press releases it.
- **A:** opens the folder's disk list and puts the chosen one in A: (DOS sees
  the disk change).
- **CPU** changes the speed: 4.77 (IBM PC/XT), 8 (default), 10, 12, 16 and
  20 MHz. It is a maximum: if the console cannot keep up, the core lowers it by
  itself and the bottom band shows it (`16->11.2 MHz`).
- **80c** changes how 80-column text looks: a 40-column window that follows
  the cursor (default) or the whole line in small letters.

## Mouse

The emulator carries its own mouse driver (INT 33h, Microsoft compatible). It
is turned on with `mouse = on` in the `.cfg`: the D-pad moves the pointer
(slowly at first, faster if you hold it) and A/B are the buttons. It can go on
any button: `click`, `click2`, `mup`, `mdown`, `mleft`, `mright`. SCUMM games
(Monkey Island, Loom…) play much better that way.

## The screen

The picture (320×200) is centred. The 20-pixel bands at the top and bottom show
the status (the `.cfg` and the disks at the top; MHz and load at the bottom) or
the keyboard. The status shows for 7 seconds at boot; the bottom line can stay
with **Info: ON** in the retro-go menu (PAUSE/SET → Options).
