# Development

*[Leer en español](desarrollo.md) · Back to the [README](../README.en.md)*

## Building

```
./build.sh                                              # host tests only
REPO=/path/retro-go-sd-external_cores GCC_PATH=/path/arm-gnu-toolchain/bin ./build.sh
DOSIMG=/path/floppy.img ./build.sh                      # also boots your DOS in the test
```

`REPO` is a checkout of the `external_cores` branch (the one the firmware was
built from), with the FatFs submodule and ST's drivers (`make download_sdk`).
It is only read: headers, the `Core/Src/porting/core_common` bridge, `ld/` and
`tools/pack_core.py`. It needs git, gcc, nasm and python3 (Pillow, optional, for
the PNG captures in `work/shots/`).

## How it is built

```
src/platform_core.c   console layer: LCD in LUT8 mode, buttons, audio, pause menu,
                      chosen image, settings (through the core_common bridge)
src/dos_main.c        loop, virtual time, keyboard, on-screen keyboard, disks
src/cga_render.c      CGA -> 320x200 at 8 bits; CP437 8x8 font (font8x8)
core/files/vga_hle.c  EGA/VGA graphics: registers, planar and chain-4 memory, INT 10h
core/files/mouse_hle.c INT 33h mouse driver
sdk/core_ram_emu.ld   linker script from the official core template
host/                 "fake firmware" to test everything on the PC
core/                 patches to the XTulator core (no malloc, timers, INT 13h…)
```

**Memory.** The core takes ≈ 127 KB of code and 57 KB of BSS in RAM_EMU. That
leaves ≈ 524 KB of RAM_EMU (16 KB kept for the firmware), LUT8 mode frees
≈ 148 KB of the LCD and DTCM adds 96 KB: 768 KB. EGA/VGA video memory comes out
of that first (128 KB by default) and the rest is DOS RAM, in 4 KB blocks: the
full 640 KB by default.

**Time.** The emulated PC's clock is virtual (it advances with executed
instructions), so the PIT, DOS's IRQ0 and the sound run at the emulated CPU's
speed whatever the load. That is why the core is built with
`XT_TIMING_CATCHUP`.

**256-colour palette.** Every palette change makes the firmware recompute its
darkened-colour table, so it is only sent when it really changes. In those
modes the bottom band uses the closest colours of the game's palette.
