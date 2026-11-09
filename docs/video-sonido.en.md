# Video and sound

*[Leer en español](video-sonido.md) · Back to the [README](../README.md)*

## EGA and VGA

The card is a CGA with EGA/VGA graphics added, so games that detect the card
pick the best mode by themselves (Prince of Persia and Monkey Island use VGA;
Loom and Rick Dangerous, EGA).

| Mode | | Video memory |
|---|---|---|
| 0Dh | 320×200, 16 colours (EGA) | 64 KB (128 KB if the game uses off-screen pages) |
| 0Eh | 640×200, 16 colours (shown 320 wide) | 64 KB |
| 13h | 320×200, 256 colours (VGA) | 64 KB |
| "mode X" | 320×200/240, 256 colours unchained | 256 KB |
| 10h / 12h | 640×350 / 640×480, 16 colours (shown scaled down) | 128 KB / 256 KB |

Video memory comes from the same pool as DOS RAM: with `vram = 64` and with
**128 (default)** DOS has its full 640 KB; with 256 it has 512 KB. `video = cga`
removes EGA/VGA, for games that only work on CGA. If a game uses more video
memory than there is, the bottom band warns in red ("Falta memoria de video"):
raise `vram` in its `.cfg`.

## Sound

- **PC speaker** at 48 kHz, including sound digitised "by hand" (bit 1 of
  port 61h).
- **AdLib** (OPL2, ports 388h/389h; emu8950 synthesiser). Percussion mode is not
  emulated.
- **Sound Blaster 2.0**: port 220h, IRQ 7, DMA 1 (`BLASTER=A220 I7 D1 T3`).

Games that detect the card use it by themselves; in those that ask, pick AdLib
or Sound Blaster with those values. AdLib music costs CPU: the speed regulator
will lower the MHz somewhat in those games.

## What is missing

- The mouse driver does not call "event handlers" (INT 33h 0Ch/14h): games that
  only use those do not see the mouse (most use function 03h).
- No emulator save states (the game's own saved games do work).
