# Making the disk images

*[Leer en español](discos.md) · Back to the [README](../README.md)*

Everything is done with `tools/fat12.py`, which only needs Python 3. To see
all its commands: `python3 tools/fat12.py`; for one command's help:
`python3 tools/fat12.py help <command>`.

## How the core treats them

- An image **up to 2.88 MB** is a floppy: it boots from A:.
- A **larger** one is a hard disk: it boots from C:.
- Whatever DOS writes (saved games, settings) is saved into the image, on the
  card.

## The boot floppy (`!BOOT.img`)

MS-DOS 6.22 install disk 1 boots, but it starts the installer. This makes a
copy that stops at `A:\>` with the Spanish keyboard:

```
python3 tools/fat12.py bootdisk Disk01.img !BOOT.img
```

Put it in `/cores/dosxt/` (or in `/roms/dos/` if you want to pick it from the
menu). It carries `FORMAT`, `FDISK`, `SYS`, `EDIT`, `QBASIC`, `MEM`… The
original is not modified.

## A game on a floppy

```
python3 tools/fat12.py new prince.img PRINCE/*
```

Makes a 1.44 MB floppy with those files. It does not boot by itself: in its
`.cfg` put `boot = !BOOT.img` and `run = b:;prince` (see [cfg.en.md](cfg.en.md)).

**Or all in one**, a floppy that boots and starts the game with no `.cfg`:

```
python3 tools/fat12.py sysdisk Disk01.img prince.img --run "prince" PRINCE/*
```

It holds the minimal DOS (IO.SYS, MSDOS.SYS and COMMAND.COM from your disk 1),
the game's files and an AUTOEXEC.BAT that starts it. `--run` takes several
commands separated by `;` (`"loadfix rick"`, `"c:;cd \loom;loom"`).

## A large game on a hard disk

```
python3 tools/fat12.py newhd monkey.img --size 8 MONKEY
```

Each folder becomes a directory on the disk (`C:\MONKEY`). 16 MB by default, up
to 500 MB with `--size`. Booting from `!BOOT.img`, the disk is C:
(`run = c:;cd \monkey;monkey`).

## Adding or listing files

```
python3 tools/fat12.py add game.img ALLEYCAT.EXE     # add (folders too)
python3 tools/fat12.py ls game.img                   # see what is inside
python3 tools/fat12.py extract game.img folder       # take everything out
```

## Installing MS-DOS on a hard disk

To boot an installed DOS instead of a floppy:

1. Make an empty disk: `python3 tools/fat12.py newhd c.img --size 32`.
2. Copy `c.img` and the install disks (`Disk01.img`, `Disk02.img`…) to
   `/roms/dos/`.
3. Boot `Disk01.img` from the menu: the installer sees `c.img` as C:.
4. When it asks for another disk, open the list with the **A:** key of the
   on-screen keyboard and pick the next one.
5. When it is done, pick **(sacar el disco de A:)** (eject) in the disk list and
   press Enter: the PC boots from C:.

From then on, `boot = c.img` in a game's `.cfg` boots that DOS, and the game is
on A: (`run = a:;game`). Tested with Spanish MS-DOS 5.0. Tip: answer NO to
"Run Shell at startup"; on an 8088 it is slow.

## For the other core (`dosat.bin`)

`todsk` turns an image or a folder into the `.dsk` DosAT uses:

```
python3 tools/fat12.py todsk monkey MONKEY.dsk --run "CD MONKEY;MONKEY"
```
