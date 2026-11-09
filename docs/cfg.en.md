# Each game's `.cfg`

*[Leer en español](cfg.md)*

*Everything that can go in a game's `.cfg` for `dosxt.bin`. Back to the
[README](../README.md).*

## Where it goes

For the image `/roms/dos/GAME.img`: `/cores/dosxt/GAME.cfg`.

Next to the core, so it does not show up in the game menu, and apart from the
other core (`dosat.bin`, which reads `/cores/dosat/`), so each keeps its own
`.cfg` for the same game.

All of it is optional: with no `.cfg`, the image boots by itself if it is a boot
floppy, with the default buttons.

## Format

- One option per line: `key = value`. Upper or lower case, it does not matter.
- `;` and `#` start a comment anywhere on the line. In `run` a `;` with no
  space before it separates commands (`b:;game`) and a `;` after a space starts
  the comment (`run = b:;game   ; comment`). That is why a keyboard `;` is
  written `semicolon`.
- If a line is not understood, the top strip says so when loading
  ("N errores") and that option stays as it was.

## Options

| Key | Values | What it does |
|---|---|---|
| `boot` | image name (`!BOOT.img`, `c.img`…) | DOS boots from that image. It is looked for in `/cores/dosxt`, then in `/roms/dos`. If it is a floppy, it boots on A: and the game is on B: (floppy) or C: (hard disk). If it is a hard disk with DOS installed, it boots on C: and the game is on A: (floppy) or D: (hard disk). |
| `run` | commands separated by `;` | Typed at the DOS prompt, each one when the prompt (`A:\>`) appears, followed by Enter. E.g. `b:;cd \game;game`. |
| `cpu` | `4.77`, `8`, `10`, `12`, `16`, `20` | CPU speed in MHz at boot (also changed with the `CPU` key on the on-screen keyboard). |
| `video` | `vga` (default) or `cga` | `cga` removes the EGA/VGA graphics and leaves the full 640 KB to DOS. |
| `vram` | `64`, `128` (default), `256` | EGA/VGA video memory in KB. If a game warns "Falta memoria de video", raise it. |
| `zoom80` | `1` / `0` | 80-column text: `1` = 40-column window that follows the cursor, `0` = the whole line. |
| `mouse` (or `raton`) | `on` / `off` | Mouse: the d-pad moves the pointer and A/B are the buttons (see [controles.en.md](controles.en.md#mouse)). |
| `up` `down` `left` `right` (or `arriba` `abajo` `izquierda` `derecha`), `a`, `b`, `game`, `time`, `start` (or `x`), `select` (or `y`) | key name, or up to 3 joined with `+` | Which PC key each button sends. List of names: [`teclas.en.md`](teclas.en.md). Also: `keyboard`/`teclado` (opens the on-screen keyboard), `none`/`nada` (does nothing) and, for the mouse, `click` `click2` `mup` `mdown` `mleft` `mright`. |

If no button is left as `keyboard`, TIME still opens the on-screen keyboard.

## The games' own options (sound, video…)

This is not part of the `.cfg` but of each game: many are configured with a
letter or a word after the executable's name, or with a setup program. In
`dosxt.bin` they go in `run` (for example `run = b:;monkey a`).

- **LucasArts games of the period (Monkey Island 1, Loom, Indiana Jones…)**:
  a letter after the executable picks the sound card: `a` = AdLib,
  `r` = Roland MT-32 (not emulated: no music), `i` = PC speaker. With no letter
  the game looks for the card itself. With a Sound Blaster use `a`: these games'
  music is FM, and the Sound Blaster carries the same chip as the AdLib.
- **Games with a setup program** (`SETUP.EXE`, `INSTALL.EXE`, `SOUND.EXE`,
  `CONFIG.EXE`…): run it once from the prompt, pick the card (AdLib, or Sound
  Blaster at 220h / IRQ 7 / DMA 1) and the game saves the choice in its folder.
- **To find out what options a game has**: the manual or the `README`/`LEEME`
  in its folder, or try `GAME /?`.

What `dosxt.bin` emulates: AdLib, Sound Blaster 2.0 (220h, IRQ 7, DMA 1:
`BLASTER=A220 I7 D1 T3`) and the PC speaker. It does not emulate Roland MT-32,
Gravis Ultrasound or Tandy/PCjr.
