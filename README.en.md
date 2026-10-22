# gw-dos: MS-DOS (PC XT + CGA/EGA/VGA) as an external core for the Game & Watch (retro-go SD)

*[Leer en español](README.md)*

An emulated PC XT (XTulator's 8088/V20 CPU, Sergey Kiselev's free BIOS, a CGA card with
EGA/VGA graphics, and a mouse) packaged as an **external core** for the retro-go SD firmware,
branch **`external_cores`** (firmware ABI v2, `CORE` format). It shows up in the menu as the
**MS-DOS** system, and disk images are picked like any ROM. No firmware rebuild is needed.

> Tested on the console: boot, keyboard, on-screen keyboard, disk swapping and CGA games.
> **EGA/VGA and the mouse** have only been tested on the PC, on a "fake firmware" with the same
> frontend and core: Prince of Persia and Monkey Island in VGA; Loom, Rick Dangerous and Ironman
> in EGA/VGA. `dos.bin` is built with the branch's own core SDK (the `core_common` bridge,
> `pack_core.py`) and produces the same symbols as the official `cores/_template`.

**Requirement:** firmware built from the `external_cores` branch with an ABI v2 of at least
908 bytes (commit `b791f15`, 11 Oct 2026, or later). If your firmware is older, the launcher
says "ABI mismatch": reflash the firmware, or rebuild `dos.bin` against your checkout.

## Installing on the SD card

```
/cores/dos.bin                  <- out/dos.bin (the launcher finds it at boot)
/cores/dos/<game>.cfg           <- per-game configuration (optional, see below)
/cores/dos/!BOOT.img            <- DOS boot floppy used by the cfgs (or in /roms/dos)
/roms/dos/<anything>.img        <- your disk images
```

The menu gets an **MS-DOS** tab; pick an image and it boots:
- up to 2.88 MB it is treated as **floppy A:** and boots from A:
  (if a `c.img` hard disk sits in the same folder, it is mounted as C:);
- anything larger is treated as **hard disk C:** and boots from C:.

Settings (CPU speed, 80-column mode) are saved in `/data/dos.cfg`.

### Making a boot floppy from the MS-DOS 6.22 disks

Install disk 1 boots, but it starts the installer (which asks for a hard disk). To make it stop
at `A:\>` with the Spanish keyboard:

```
python3 tools/fat12.py bootdisk Disk01.img "MS-DOS 6.22.img"
```

and copy `MS-DOS 6.22.img` (and, if you like, `Disk02.img`... to swap them from the disk menu)
to `/roms/dos/`.

It copies the disk and removes `instalar` from its `AUTOEXEC.BAT` (keeping `nlsfunc` and
`keyb sp`). The original is not modified. That floppy already has `FORMAT`, `FDISK`, `SYS`,
`EDIT`, `QBASIC`, `MEM`...

### One bootable floppy per game (recommended)

```
python3 tools/fat12.py sysdisk Disk01.img prince.img --run "prince" PoP1/*
```

Creates a floppy with a minimal DOS (IO.SYS, MSDOS.SYS and COMMAND.COM copied from your disk 1)
plus the game's files and an AUTOEXEC.BAT that starts it: pick it in the menu and the game runs.
`--run` takes several commands separated by `;` (e.g. `"loadfix rick"` or `"c:;cd \loom;loom"`).

### Big games: a hard-disk image

```
python3 tools/fat12.py newhd c.img LOOM MONKEY        # each folder becomes C:\LOOM, C:\MONKEY
python3 tools/fat12.py ls c.img
```

16 MB by default; up to 500 MB with `--size` (FAT16 above 16 MB). Name it `c.img` and put it in
`/roms/dos/` next to the floppies: booting any floppy mounts it as C:. Whatever DOS writes to the
disk (and to the floppies) is saved to the card.

### Installing MS-DOS on a hard disk and booting from it

```
python3 tools/fat12.py newhd c.img --size 32          # empty disk, formatted and bootable
```

1. Copy `c.img` and the install disks (`Disk01.img`, `Disk02.img`...) to `/roms/dos/`.
2. Boot `Disk01.img` from the menu: the installer sees `c.img` as C: and offers `C:\DOS`.
3. When it asks for another disk, open the disk menu and pick `Disk02.img` (then `Disk03.img`).
4. When it finishes ("remove the floppy and press ENTER"): in the disk menu choose
   **(eject the disk from A:)** and press ENTER; with no floppy, the PC boots from C:.
5. From then on, boot `c.img` from the menu and your installed DOS comes up. To start a game with
   that DOS, put `boot = c.img` in its cfg: DOS comes from C: and the game is on A:
   (`run = a:;game`). The `:` key is on the on-screen keyboard, next to `;`.

Tested with Spanish MS-DOS 5.0 (INSTALAR). Tip: answer NO to "Run Shell at startup"; the MS-DOS
Shell is slow on an 8088 and does not fit on screen in 80 columns.

### Adding games and programs (.exe, .com)

```
python3 tools/fat12.py add "MS-DOS 6.22.img" ALLEYCAT.EXE      # onto the boot floppy, if it fits (~57 KB free)
python3 tools/fat12.py new games.img ALLEYCAT.EXE OTHER.EXE    # new 1.44 MB floppy with those files
python3 tools/fat12.py ls games.img                            # list what is inside
```

A floppy made with `new` does not boot: boot DOS from your boot floppy, switch to `games.img`
with the **A:** key on the on-screen keyboard, and type the program's name. When the program
exits, DOS may ask for the disk with `COMMAND.COM`: put the boot floppy back.

## Per-game configuration (`.cfg`)

Each game can have a `.cfg` with the same name (`/cores/dos/GAME.cfg`, next to the core, so it does
not show up in the menu). The game can then be a plain data floppy (`fat12.py new`) while DOS boots
from your boot floppy. An example:

```ini
boot   = !BOOT.img     ; boot DOS from this image; the game is on B: (or C: for a hard disk)
run    = b:;prince     ; typed at the DOS prompt; ";" = Enter between commands
cpu    = 10            ; MHz
a      = shift         ; which PC key each button sends
```

- **Every option** (`boot`, `run`, `cpu`, `video`, `vram`, `zoom80`, `mouse` and the buttons), the
  games' own sound options (e.g. `monkey a` for AdLib) and which cards are emulated:
  [`docs/cfg.en.md`](docs/cfg.en.md).
- **Key names** for the buttons, and how to find out which keys a game uses:
  [`docs/teclas.en.md`](docs/teclas.en.md).
- Ready-made examples in [`examples/cfg/`](examples/cfg/) (`plantilla.cfg` has every button).

## EGA and VGA

The video card is a CGA with EGA/VGA graphics added, so games that detect the card pick the best
mode on their own (Prince of Persia and Monkey Island use VGA, Loom and Rick Dangerous EGA). Text
and CGA modes are the usual ones.

| Mode | | Video memory |
|---|---|---|
| 0Dh | 320×200, 16 colours (EGA) | 64 KB (128 KB if the game uses pages or off-screen backgrounds) |
| 0Eh | 640×200, 16 colours (shown 320 wide) | 64 KB |
| 13h | 320×200, 256 colours (VGA) | 64 KB |
| "mode X" | 320×200/240, unchained 256 colours | 256 KB |
| 10h / 12h | 640×350 / 640×480, 16 colours (shown scaled down) | 128 KB / 256 KB |

Video memory comes out of DOS RAM: `vram = 64` leaves 624 KB, **128 (default)** 560 KB and 256
leaves 432 KB. `video = cga` removes EGA/VGA and leaves the full 640 KB (for games that need all
the memory). If a game uses more video memory than there is, the bottom strip says so in red
("Falta memoria de video"): raise `vram` in its cfg. While an EGA/VGA mode is up, the bottom strip
shows which and with how much memory ("EGA 128K", "VGA 128K").

There is no video ROM: the EGA/VGA BIOS services (INT 10h: modes, palette, text and pixels in
graphics modes, detection) are provided by the emulator itself.

## Mouse

MS-DOS 6.22 has no mouse driver, so the emulator carries one (INT 33h, Microsoft-compatible). Turn
it on with `mouse = on` in the cfg: the **d-pad moves the pointer** (slowly at first, faster if you
hold it) and **A / B are the left and right buttons**. It can also be put on any button: `mup`,
`mdown`, `mleft`, `mright`, `click`, `click2`. SCUMM games (Monkey Island, Loom...) play much better
this way; they also work with the arrow keys.

Holding a button mapped to a key **repeats** the key like a real keyboard (after half a second,
20 times a second).

## Controls

| Button | Normal | On-screen keyboard |
|---|---|---|
| D-pad | Arrows (or the mouse pointer with `mouse = on`) | Move |
| A | Enter | Press key |
| B | Space | Close |
| GAME (START on Zelda) | Esc | — |
| TIME (SELECT on Zelda) | **Open keyboard** | Close |
| START / SELECT (Zelda) | Tab / Backspace | — |
| PAUSE/SET | retro-go menu (volume, brightness, quit...) | |

On-screen keyboard (4 rows: two in the top strip and two in the bottom one, the game does not move;
holding the d-pad keeps the cursor moving): letters, digits, F1–F10, arrows, Home/End, Ins/Del,
Ctrl/Alt/Shift (they stay "pressed" until the next key, in green, for Ctrl+C, Shift+letter, etc.;
Shift pressed twice locks, in red, like Caps Lock, and a third press releases it) and special keys:

- **A:** disk menu (opens over the picture): lists the `.img` files in the booted image's folder and
  puts the chosen one in A: (DOS notices the disk change).
- **CPU**: emulated CPU speed in 8088-equivalent MHz: 4.77 (IBM PC/XT), 8 (default, Turbo XT), 10,
  12, 16 and 20 MHz (about 66,000 instructions/s per MHz). Pressing it shows the speed briefly at the
  top; the bottom strip shows the MHz and the console's **load**. The chosen speed is a **maximum**:
  if the console cannot emulate that many instructions in real time (which makes the sound
  stutter), the core lowers the speed by itself to what fits and raises it again when there is room;
  the strip shows it as `16->11.2 MHz`.
- **80c**: 80-column text: a readable 40-column window that follows the cursor (default) or the
  whole line squeezed into 4-pixel letters.

Screen: the picture (320×200) is centred; the 20-pixel strips above and below show the status (cfg
and disks at the top; MHz and load at the bottom) or the keyboard. The status shows for 7 seconds at
boot and then goes away; the bottom line can stay with **Info: ON** in the retro-go menu
(PAUSE/SET → Options). The "Falta memoria de video" warning always shows. CGA modes: 40×25 and 80×25
text, 320×200 (4 colours) and 640×200 (2 colours) graphics; EGA/VGA: see above.
Sound: PC speaker at 48 kHz, computed from port 61h and PIT channel 2 with one-instruction
precision and averaged per sample (without XTulator's buzz or false notes); the speaker driven "by
hand" (digitised sound through bit 1 of 61h) plays too. PAUSE/SET + d-pad shows retro-go's volume
and brightness bars.

**AdLib and Sound Blaster.** The core emulates an AdLib card (OPL2, ports 388h/389h; the emu8950
synthesiser, the same as in the Game & Watch Doom port) and a Sound Blaster 2.0 (port 220h, IRQ 7,
DMA 1: `BLASTER=A220 I7 D1 T3`). Games that detect them use them with nothing to do; in the ones
that ask, pick AdLib or Sound Blaster with those values. The OPL2's percussion mode is not emulated.
AdLib music costs CPU: the speed governor will lower the MHz somewhat in those games.

## How it is built

```
src/platform_core.c  console layer: LCD in LUT8 mode (8 bits with the CGA palette), buttons, audio,
                     pause menu, chosen image (ACTIVE_FILE), settings; uses the firmware's normal
                     names, which the core_common bridge redirects to ABI v2
src/dos_main.c       frontend: loop, virtual time, keyboard, on-screen keyboard, disk menu
src/cga_render.c     CGA -> 320x200 in 8 bits; 8x8 CP437 font (font8x8, public domain)
core/files/vga_hle.c EGA/VGA graphics: registers, planar and chain-4 memory, INT 10h in C
core/files/mouse_hle.c INT 33h mouse driver
sdk/core_ram_emu.ld  linker script from the official core template (removed from the repo in 782d9142)
host/                "fake firmware" to test everything on the PC
core/                patches to the XTulator core (no malloc, paged tables, timers, INT 13h...)
```

Memory: the core takes ≈ 120 KB of code + 37 KB of BSS in RAM_EMU. That leaves ≈ 551 KB of RAM_EMU
(with 16 KB reserved for the firmware), and LUT8 mode frees ≈ 150 KB more from the LCD. EGA/VGA video
memory comes out of that first (128 KB by default) and the rest is DOS RAM, in 16 KB blocks: 560 KB
by default, 640 KB with `video = cga`.

Time: the emulated PC's clock is **virtual** (it advances with the instructions executed), so the
PIT, DOS's IRQ0 and the sound run at the same speed as the emulated CPU whatever the load. That is
why the core is built with `XT_TIMING_CATCHUP` (the timers catch up on late ticks).

## Building

```
./build.sh                                              # PC tests only
REPO=/path/retro-go-sd-external_cores GCC_PATH=/path/arm-gnu-toolchain/bin ./build.sh
DOSIMG=/path/floppy.img ./build.sh                      # also boots your DOS in the test
```

`REPO` is a checkout of the `external_cores` branch (the same one you built the firmware from),
with the FatFs submodule and ST's drivers (`make download_sdk`). It is only read: headers, the
`Core/Src/porting/core_common` bridge, `ld/` and `tools/pack_core.py`.
Needs git, gcc, nasm and python3 (Pillow optional, for the PNG screenshots in `work/shots/`).

## Risks when trying it on the console (what could not be verified here)

- **EGA/VGA**: the cost on the console. Drawing the picture is cheap (mode 13h is a memory copy),
  but VGA games ask for more CPU: Monkey Island runs better at 12–16 MHz. Watch the load.
- **256-colour palette**: every palette change (fades) makes the firmware recompute its table of
  darkened colours. It is only sent when the palette really changes.
- In 256-colour modes the bottom strip uses the closest colours in the game's palette, so it may
  look tinted.
- The mouse driver does not call "event handlers" (INT 33h functions 0Ch/14h): games that only use
  those will not see the mouse (most poll it with function 03h).
- No emulator savestates yet (the games' own saves do work).

## Licences

XTulator: GPLv2 or later (the `core/` patches too). Sergey Kiselev's BIOS: GPLv3, included in
`dos.bin`, so the binary as a whole is **GPLv3**. Font: Daniel Hepper's font8x8, public domain.
**No Microsoft software is included**: you provide the MS-DOS images.
