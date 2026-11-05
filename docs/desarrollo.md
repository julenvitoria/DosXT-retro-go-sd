# Desarrollo

*[Read in English](desarrollo.en.md) · Vuelve al [README](../README.md)*

## Compilar

```
./build.sh                                              # solo pruebas en el PC
REPO=/ruta/retro-go-sd-external_cores GCC_PATH=/ruta/arm-gnu-toolchain/bin ./build.sh
DOSIMG=/ruta/disquete.img ./build.sh                    # además arranca tu DOS en la prueba
```

`REPO` es un checkout de la rama `external_cores` (la misma con la que se
compiló el firmware), con el submódulo FatFs y los drivers de ST
(`make download_sdk`). Solo se lee: cabeceras, el puente
`Core/Src/porting/core_common`, `ld/` y `tools/pack_core.py`. Necesita git, gcc,
nasm y python3 (Pillow, opcional, para las capturas PNG de `work/shots/`).

## Cómo está hecho

```
src/platform_core.c   capa de la consola: LCD en modo LUT8, botones, audio, menú de
                      pausa, imagen elegida, ajustes (vía el puente core_common)
src/dos_main.c        bucle, tiempo virtual, teclado, teclado en pantalla, discos
src/cga_render.c      CGA -> 320x200 en 8 bits; fuente CP437 8x8 (font8x8)
core/files/vga_hle.c  gráficos EGA/VGA: registros, memoria planar y chain-4, INT 10h
core/files/mouse_hle.c driver de ratón INT 33h
sdk/core_ram_emu.ld   linker script de la plantilla oficial de cores
host/                 "falso firmware" para probar todo en el PC
core/                 parches del núcleo XTulator (sin malloc, timers, INT 13h…)
```

**Memoria.** El núcleo ocupa ≈ 127 KB de código y 57 KB de BSS en RAM_EMU.
Quedan ≈ 524 KB de RAM_EMU (con 16 KB de reserva para el firmware), el modo LUT8
libera ≈ 148 KB del LCD y la DTCM aporta 96 KB: 768 KB. De ahí sale primero la
memoria de vídeo EGA/VGA (128 KB por defecto) y el resto es la RAM del DOS, en
bloques de 4 KB: 640 KB enteros por defecto.

**Tiempo.** El reloj del PC emulado es virtual (avanza con las instrucciones
ejecutadas), así que el PIT, la IRQ0 del DOS y el sonido van a la velocidad de
la CPU emulada, sea cual sea la carga. Por eso el núcleo se compila con
`XT_TIMING_CATCHUP`.

**Paleta de 256 colores.** Cada cambio de paleta hace que el firmware
recalcule su tabla de colores oscurecidos, así que solo se envía cuando cambia
de verdad. En esos modos la franja de abajo usa los colores más parecidos de la
paleta del juego.
