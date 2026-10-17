# gw-dos: MS-DOS (PC XT + CGA/EGA/VGA) como core externo para la Game & Watch (retro-go SD)

Un PC XT emulado (CPU 8088/V20 de XTulator, BIOS libre de Sergey Kiselev, tarjeta CGA con
gráficos EGA/VGA y ratón) empaquetado como **core externo** del firmware retro-go SD, rama **`external_cores`**
(ABI de firmware v2, formato `CORE`). Aparece en el menú como sistema **MS-DOS** y las
imágenes de disco se eligen como cualquier ROM. No hace falta recompilar el firmware.

> Probado en la consola: arranque, teclado, teclado en pantalla, cambio de disco y juegos CGA.
> **EGA/VGA y ratón** solo están probados en el PC sobre un "falso firmware" con el mismo frontend
> y núcleo: Prince of Persia y Monkey Island en VGA, Loom, Rick Dangerous e Ironman en EGA/VGA.
> El `dos.bin` se compila con el SDK de cores de la propia rama (puente `core_common`,
> `pack_core.py`) y da los mismos símbolos que la plantilla oficial `cores/_template`.

**Requisito:** firmware compilado desde la rama `external_cores` con ABI v2 de al menos
908 bytes (commit `b791f15`, 11-oct-2026, o posterior). Si el firmware es más antiguo, el
lanzador dirá "ABI mismatch": reflashea el firmware o recompila `dos.bin` contra tu checkout.

## Instalación en la tarjeta SD

```
/cores/dos.bin                  <- out/dos.bin (el lanzador lo detecta al arrancar)
/cores/dos/<juego>.cfg          <- configuración por juego (opcional, ver más abajo)
/cores/dos/!BOOT.img        <- disquete de arranque del DOS para los cfg (o en /roms/dos)
/roms/dos/<lo que quieras>.img  <- tus imágenes de disco
```

En el menú aparece la pestaña **MS-DOS**; eliges una imagen y arranca:
- hasta 2,88 MB se trata como **disquete A:** y se arranca desde A:
  (si en la misma carpeta hay un `c.img` de disco duro, se monta como C:);
- más grande se trata como **disco duro C:** y se arranca desde C:.

Los ajustes (velocidad de CPU, modo de 80 columnas) se guardan en `/data/dos.cfg`.

### Preparar un disquete de arranque desde los discos de MS-DOS 6.22

El disco 1 de instalación arranca, pero lanza el instalador (que pide un disco duro).
Para que pare en `A:\>` con el teclado español:

```
python3 tools/fat12.py bootdisk Disk01.img "MS-DOS 6.22.img"
```

y copia `MS-DOS 6.22.img` (y, si quieres, `Disk02.img`... para cambiarlos desde el menú de discos)
a `/roms/dos/`.

Copia el disco y quita `instalar` de su `AUTOEXEC.BAT` (deja `nlsfunc` y `keyb sp`).
El original no se modifica. Ese disquete ya trae `FORMAT`, `FDISK`, `SYS`, `EDIT`, `QBASIC`, `MEM`...

### Un disquete arrancable por juego (recomendado)

```
python3 tools/fat12.py sysdisk Disk01.img prince.img --run "prince" PoP1/*
```

Crea un disquete con el DOS mínimo (IO.SYS, MSDOS.SYS y COMMAND.COM copiados de tu disco 1)
más los ficheros del juego y un AUTOEXEC.BAT que lo lanza: lo eliges en el menú y arranca el juego.
`--run` admite varias órdenes separadas por `;` (por ejemplo `"loadfix rick"` o `"c:;cd \loom;loom"`).

### Juegos grandes: imagen de disco duro

```
python3 tools/fat12.py newhd c.img LOOM MONKEY        # cada carpeta pasa a ser C:\LOOM, C:\MONKEY
python3 tools/fat12.py ls c.img
```

Por defecto 16 MB; con `--size` hasta 500 MB (FAT16 por encima de 16 MB). Llámala `c.img` y
ponla en `/roms/dos/` junto a los disquetes: al arrancar desde cualquier disquete se monta como C:.
Lo que DOS escribe en el disco (y en los disquetes) se guarda en la tarjeta.

### Instalar MS-DOS en un disco duro y arrancar desde él

```
python3 tools/fat12.py newhd c.img --size 32          # disco vacío, formateado y arrancable
```

1. Copia `c.img` y los discos de instalación (`Disk01.img`, `Disk02.img`...) a `/roms/dos/`.
2. Arranca `Disk01.img` desde el menú: el instalador ve `c.img` como C: y propone `C:\DOS`.
3. Cuando pida otro disco, abre el menú de discos y elige `Disk02.img` (luego `Disk03.img`).
4. Al terminar ("retire el disquete y presione ENTRAR"): en el menú de discos elige
   **(sacar el disco de A:)** y pulsa ENTRAR; sin disquete, el PC arranca desde C:.
5. A partir de ahí arranca `c.img` desde el menú y sale tu DOS instalado. Para que un juego
   arranque con ese DOS, pon `boot = c.img` en su cfg: el DOS sale de C: y el juego queda en A:
   (`run = a:;juego`). La tecla `:` está en el teclado de pantalla, junto a `;`.

Probado con MS-DOS 5.0 en español (INSTALAR). Consejo: en "Ejecutar Shell al inicio" elige NO;
el MS-DOS Shell es lento en un 8088 y en 80 columnas no cabe entero en la pantalla.

### Meter juegos y programas (.exe, .com)

```
python3 tools/fat12.py add "MS-DOS 6.22.img" ALLEYCAT.EXE      # al disquete de arranque, si cabe (~57 KB libres)
python3 tools/fat12.py new juegos.img ALLEYCAT.EXE OTRO.EXE    # disquete nuevo de 1,44 MB con esos ficheros
python3 tools/fat12.py ls juegos.img                           # ver qué hay dentro
```

Un disquete creado con `new` no arranca: arranca el DOS con tu disquete de arranque, cambia a
`juegos.img` con la tecla **A:** del teclado en pantalla y escribe el nombre del programa.
Al salir del programa, el DOS puede pedir el disco con `COMMAND.COM`: vuelve a poner el de arranque.

## Configuración por juego (`.cfg`)

Para cada imagen `/roms/dos/JUEGO.img` se busca `/cores/dos/JUEGO.cfg`, junto al core, para que
no salga en el menú de juegos (si no existe, también `/roms/dos/cfg/JUEGO.cfg` y `/roms/dos/JUEGO.cfg`). Así el juego puede ser un disquete de datos normal (`fat12.py new`) y el
DOS arranca desde tu disquete de arranque. Ejemplos en `examples/cfg/` (`plantilla.cfg` tiene todos los botones):

```ini
; comentarios con ; o #
boot   = !BOOT.img     ; arranca el DOS desde esta imagen (A:); el juego queda en B: (disquete)
                           ; o C: (disco duro). Se busca en /cores/dos y luego en /roms/dos.
                           ; Si es un disco duro (boot = c.img, con DOS instalado) arranca desde
                           ; C: y el juego queda en A: (disquete) o D: (disco duro): run = a:;juego
run    = b:;prince         ; se escribe en el prompt del DOS; ";" = Intro entre órdenes
cpu    = 10                ; MHz: 4.77, 8, 10, 12, 16, 20
zoom80 = 0                 ; texto de 80 columnas: 1 = ventana de 40, 0 = línea entera
video  = vga               ; vga (EGA/VGA, por defecto) o cga (solo CGA, 640 KB para el DOS)
vram   = 128               ; memoria EGA/VGA en KB: 64, 128 (por defecto) o 256
mouse  = on                ; ratón: cruceta = puntero, A/B = botones (ver "Ratón")
up     = q                 ; cruceta: up down left right (por defecto, las flechas)
down   = a
left   = o
right  = p
a      = shift             ; botones: a b game time start select
b      = ctrl+a            ;   (también arriba, abajo, izquierda, derecha)
time   = keyboard          ; teclado en pantalla ("teclado"); "none"/"nada" = sin función
```

- `#` y `;` empiezan un comentario en cualquier sitio de la línea, así que no los uses en nombres de fichero (`!BOOT.img` sí vale).
- **Teclas**: la lista de nombres que se pueden poner en cada botón (y cómo saber qué teclas usa un juego) está en [`docs/teclas.md`](docs/teclas.md).
- **`run`** escribe directamente en el búfer de teclado de la BIOS (da igual el `KEYB` cargado) y
  espera a ver el prompt (`A:\>`) antes de cada orden. El disquete de `boot` debe acabar en el prompt
  (el de `fat12.py bootdisk` lo hace).
- Si ningún botón queda como `keyboard`, TIME sigue abriendo el teclado en pantalla.
- La franja de arriba muestra el `.cfg` cargado; si alguna línea no se entiende indica cuántas ("N errores").

## EGA y VGA

La tarjeta de vídeo es una CGA a la que se le han añadido los gráficos EGA/VGA, así que los juegos
que detectan la tarjeta eligen solos el mejor modo (Prince of Persia y Monkey Island usan VGA, Loom y
Rick Dangerous EGA). Los modos de texto y CGA siguen siendo los de siempre.

| Modo | | Memoria de vídeo |
|---|---|---|
| 0Dh | 320×200, 16 colores (EGA) | 64 KB (128 KB si el juego usa páginas o fondos fuera de pantalla) |
| 0Eh | 640×200, 16 colores (se ve a 320 de ancho) | 64 KB |
| 13h | 320×200, 256 colores (VGA) | 64 KB |
| "modo X" | 320×200/240, 256 colores sin encadenar | 256 KB |
| 10h / 12h | 640×350 / 640×480, 16 colores (se ven reducidos) | 128 KB / 256 KB |

La memoria de vídeo se resta de la RAM del DOS: con `vram = 64` quedan 624 KB, con **128 (por
defecto)** 560 KB y con 256 quedan 432 KB. `video = cga` quita la EGA/VGA y deja los 640 KB enteros
(para juegos que necesitan toda la memoria). Si un juego usa más memoria de vídeo de la que hay, la
franja inferior lo avisa en rojo ("Falta memoria de video"): sube `vram` en su cfg. Mientras hay un
modo EGA/VGA, la franja inferior indica cuál y con cuánta memoria ("EGA 128K", "VGA 128K").

No hay ROM de vídeo: los servicios de la BIOS EGA/VGA (INT 10h: modos, paleta, texto y píxeles en
modos gráficos, detección) los da el propio emulador.

## Ratón

MS-DOS 6.22 no trae driver de ratón, así que el emulador lleva uno (INT 33h, compatible con el de
Microsoft). Se activa con `mouse = on` en el cfg: la **cruceta mueve el puntero** (despacio al
principio, más rápido si la mantienes) y **A / B son los botones izquierdo y derecho**. También se
puede asignar a cualquier botón: `mup`, `mdown`, `mleft`, `mright`, `click`, `click2`.
Los juegos SCUMM (Monkey Island, Loom...) se juegan mucho mejor así; con las flechas también funcionan.

Al mantener pulsado un botón asignado a una tecla, la tecla se **repite** como en un teclado de
verdad (tras medio segundo, 20 veces por segundo).

## Controles

| Botón | Normal | Teclado en pantalla |
|---|---|---|
| Cruceta | Flechas (o puntero del ratón con `mouse = on`) | Mover |
| A | Intro | Pulsar tecla |
| B | Espacio | Cerrar |
| GAME (START en Zelda) | Esc | — |
| TIME (SELECT en Zelda) | **Abrir teclado** | Cerrar |
| START / SELECT (Zelda) | Tab / Retroceso | — |
| PAUSE/SET | Menú de retro-go (volumen, brillo, salir...) | |

Teclado en pantalla (4 filas: dos en la franja de arriba y dos en la de abajo, el juego no se mueve; manteniendo la cruceta el cursor sigue avanzando): letras, números, F1–F10, flechas, Inicio/Fin, Ins/Supr, Ctrl/Alt/Shift
(se quedan "pulsadas" hasta la siguiente tecla, en verde, para Ctrl+C, Shift+letra, etc.; Shift pulsado dos veces queda
bloqueado, en rojo, como Bloq Mayús, y una tercera vez lo suelta) y teclas especiales:

- **A:** menú de discos (se abre encima de la imagen): lista los `.img` de la carpeta de la imagen arrancada y mete el elegido en A:
  (el DOS detecta el cambio de disco).
- **CPU**: velocidad de la CPU emulada en MHz equivalentes de un 8088: 4,77 (IBM PC/XT), 8 (por defecto, Turbo XT), 10, 12, 16 y 20 MHz (aprox. 66.000 instrucciones/s por MHz). Al pulsarla se muestra un momento arriba; la franja inferior indica los MHz y la **carga** de la consola. La velocidad elegida es un **máximo**: si la consola no llega a emular tantas instrucciones en tiempo real (con lo que el sonido se entrecorta), el core baja la velocidad sola hasta lo que cabe y la vuelve a subir cuando hay margen; la franja lo indica como `16->11.2 MHz`.
- **80c**: texto en 80 columnas: ventana de 40 columnas legible que sigue al cursor (por defecto)
  o la línea entera comprimida a letras de 4 píxeles.

Pantalla: la imagen (320×200) va centrada; las franjas de 20 píxeles de arriba y abajo muestran el
estado (cfg y discos arriba; MHz y carga abajo) o el teclado. El estado se ve 7 segundos al arrancar
y luego desaparece; la línea de abajo puede quedarse fija con **Info: ON** en el menú de retro-go
(PAUSE/SET → Options). El aviso de "Falta memoria de video" se ve siempre. Modos CGA: texto 40×25 y 80×25, gráficos 320×200 (4 colores) y 640×200
(2 colores); EGA/VGA: ver arriba.
Sonido: altavoz del PC a 48 kHz, calculado a partir del puerto 61h y del canal 2 del PIT con la
precisión de una instrucción y promediado en cada muestra (sin el zumbido ni las notas falsas de
XTulator); también suena el altavoz manejado "a mano" (sonido digitalizado por el bit 1 del 61h).
Con PAUSE/SET + cruceta se ven las barras de volumen y brillo de retro-go.

**AdLib y Sound Blaster.** El core emula una tarjeta AdLib (OPL2, puertos 388h/389h; sintetizador
emu8950, el mismo del port de Doom para la Game & Watch) y una Sound Blaster 2.0 (puerto 220h, IRQ 7,
DMA 1: `BLASTER=A220 I7 D1 T3`). Los juegos que las detectan solos las usan sin hacer nada; en los que
preguntan, elige AdLib o Sound Blaster con esos valores. El modo percusión del OPL2 no está
emulado. La música AdLib pide CPU: el regulador de velocidad bajará algo los MHz en esos juegos.

## Cómo está hecho

```
src/platform_core.c  capa de la consola: LCD en modo LUT8 (8 bits con paleta CGA), botones, audio,
                     menú de pausa, imagen elegida (ACTIVE_FILE), ajustes; usa los nombres normales
                     del firmware, que el puente core_common redirige a la ABI v2
src/dos_main.c       frontend: bucle, tiempo virtual, teclado, teclado en pantalla, menú de discos
src/cga_render.c     CGA -> 320x200 en 8 bits; fuente CP437 8x8 (font8x8, dominio público)
core/files/vga_hle.c gráficos EGA/VGA: registros, memoria planar y chain-4, INT 10h en C
core/files/mouse_hle.c driver de ratón INT 33h
sdk/core_ram_emu.ld  linker script de la plantilla oficial de cores (eliminada del repo en 782d9142)
host/                "falso firmware" para probar todo en el PC
core/                parches del núcleo XTulator (sin malloc, tablas por páginas, timers, INT 13h...)
```

Memoria: el core ocupa ≈ 120 KB de código + 37 KB de BSS en RAM_EMU. Quedan ≈ 551 KB de RAM_EMU (con
16 KB de reserva para el firmware) y el modo LUT8 libera ≈ 150 KB más del LCD. De ahí sale primero la
memoria de vídeo EGA/VGA (128 KB por defecto) y el resto es la RAM del DOS, en bloques de 16 KB:
560 KB por defecto, 640 KB con `video = cga`.

Tiempo: el reloj del PC emulado es **virtual** (avanza con las instrucciones ejecutadas), así que
el PIT, la IRQ0 del DOS y el sonido van a la misma velocidad que la CPU emulada, sea cual sea la
carga. Por eso el núcleo se compila con `XT_TIMING_CATCHUP` (los timers recuperan los ticks atrasados).

## Compilar

```
./build.sh                                              # solo pruebas en el PC
REPO=/ruta/retro-go-sd-external_cores GCC_PATH=/ruta/arm-gnu-toolchain/bin ./build.sh
DOSIMG=/ruta/disquete.img ./build.sh                    # además arranca tu DOS en la prueba
```

`REPO` es un checkout de la rama `external_cores` (la misma con la que compilaste el firmware),
con el submódulo FatFs y los drivers de ST (`make download_sdk`). Solo se lee: cabeceras, el
puente `Core/Src/porting/core_common`, `ld/` y `tools/pack_core.py`.
Necesita git, gcc, nasm y python3 (Pillow opcional para las capturas PNG en `work/shots/`).

## Riesgos al probarlo en la consola (lo que no se ha podido verificar aquí)

- **EGA/VGA**: el coste en la consola. Dibujar la imagen es barato (el modo 13h es una copia de
  memoria), pero los juegos VGA piden más CPU: Monkey Island va mejor a 12–16 MHz. Mira la carga.
- **Paleta de 256 colores**: cada cambio de paleta (fundidos) hace que el firmware recalcule su tabla
  de colores oscurecidos. Solo se envía cuando la paleta cambia de verdad.
- En los modos de 256 colores la barra inferior usa los colores más parecidos de la paleta del juego,
  así que puede verse con otros tonos.
- El driver de ratón no llama a los "event handlers" (INT 33h funciones 0Ch/14h): los juegos que solo
  usan eso no verán el ratón (la mayoría lo consultan con la función 03h).
- Sin partidas guardadas (savestates) del emulador todavía (las del propio juego sí funcionan).

## Licencias

XTulator: GPLv2 o posterior (los parches de `core/` también). BIOS de Sergey Kiselev: GPLv3,
incluida en `dos.bin`, así que el binario en conjunto es **GPLv3**. Fuente: font8x8 de Daniel Hepper,
dominio público. **No se incluye ningún software de Microsoft**: las imágenes de MS-DOS las pones tú.
