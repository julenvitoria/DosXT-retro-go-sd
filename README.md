# DosXT: MS-DOS (PC XT) for the Game & Watch (retro-go SD)

*[Leer en español](README.es.md)*

An emulated PC XT as an external core for the **retro-go SD** firmware:
XTulator's 8088 CPU, Sergey Kiselev's free BIOS, CGA, EGA and VGA graphics,
mouse, AdLib and Sound Blaster. It shows up in the menu as **DosXT** and each
disk image is picked like a ROM.

- It boots a **real MS-DOS**, which you supply (none is included).
- For games that want a 286/386 (demanding VGA, extended memory) there is the
  other core, [DosAT-retro-go-sd](https://github.com/julenvitoria/DosAT-retro-go-sd)
  (`dosat.bin`). Both live on the same card.
- It needs a firmware from the `external_cores` branch (ABI v2, `b791f15` of
  11-10-2026 or later). With an older one the launcher says "ABI mismatch".

## On the SD card

```
/cores/dosxt.bin           <- the core (out/dosxt.bin)
/cores/dosxt/!BOOT.img     <- boot floppy with your MS-DOS
/cores/dosxt/MONKEY.cfg    <- each game's configuration (optional), same name
/roms/dos/MONKEY.img       <- each game, as a disk image
```

## Making the images

Everything is done with `tools/fat12.py`, which only needs Python 3 (Windows,
Linux or macOS).

**1. The boot floppy**, once, from MS-DOS 6.22 disk 1:

```
python3 tools/fat12.py bootdisk Disk01.img !BOOT.img
```

**2. Each game's image**, from the game's folder:

```
python3 tools/fat12.py new prince.img PRINCE/*            # 1.44 MB floppy
python3 tools/fat12.py newhd monkey.img --size 8 MONKEY   # hard disk, for large games
```

More ways (a bootable floppy per game, installing MS-DOS on a hard disk, adding
files to an image…): [**docs/discos.en.md**](docs/discos.en.md).

## Configuring a game (`.cfg`)

The `.cfg` tells the core which DOS to boot and what to type at the prompt. A
complete example:

```ini
; /cores/dosxt/MONKEY.cfg  -- the game is the hard disk /roms/dos/MONKEY.img
boot   = !BOOT.img      ; boot DOS from this floppy (A:); the game is on C:
run    = c:;cd \monkey;monkey a   ; typed at the prompt (";" = Enter)
cpu    = 16             ; MHz: 4.77, 8, 10, 12, 16 or 20
mouse  = on             ; the D-pad moves the mouse pointer
b      = click          ; B: left mouse button
a      = click2         ; A: right mouse button
start  = f5             ; START: F5 (in Monkey Island, save game)
select = space          ; SELECT: Space (pause)
```

- With a boot floppy, the game is on **B:** if it is a floppy and on **C:** if
  it is a hard disk.
- Without a `.cfg`, the image boots by itself if it is a boot floppy.
- **Every option**: [**docs/cfg.en.md**](docs/cfg.en.md). **Key names**:
  [docs/teclas.en.md](docs/teclas.en.md). Ready-made examples:
  [examples/cfg/](examples/cfg/).

## Controls

| Button | What it does |
|---|---|
| D-pad | Arrows (or the pointer, with `mouse = on`) |
| A / B | Enter / Space |
| GAME | Esc |
| **TIME** | **Opens the on-screen keyboard** (CPU, disk swap…) |
| PAUSE/SET | retro-go menu; with the D-pad, volume and brightness |

On-screen keyboard, disk swapping, CPU speed and mouse:
[docs/controles.en.md](docs/controles.en.md). Video and sound:
[docs/video-sonido.en.md](docs/video-sonido.en.md).

## More

- [docs/desarrollo.en.md](docs/desarrollo.en.md): how it is built and how to
  build it.
- Licence: XTulator is GPLv2+ and Sergey Kiselev's BIOS GPLv3, so `dosxt.bin` is
  **GPLv3**. font8x8 by Daniel Hepper (public domain). No Microsoft software is
  included.
