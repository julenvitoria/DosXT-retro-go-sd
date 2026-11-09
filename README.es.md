# DosXT: MS-DOS (PC XT) para la Game & Watch (retro-go SD)

*[Read in English](README.md)*

Un PC XT emulado como núcleo externo del firmware **retro-go SD**: CPU 8088 de
XTulator, BIOS libre de Sergey Kiselev, gráficos CGA, EGA y VGA, ratón, AdLib y
Sound Blaster. Aparece en el menú como **DosXT** y cada imagen de disco se
elige como una ROM.

- Arranca un **MS-DOS de verdad**, que pones tú (no se incluye ninguno).
- Para juegos que piden un 286/386 (VGA exigente, memoria extendida) está el
  otro núcleo, [DosAT-retro-go-sd](https://github.com/julenvitoria/DosAT-retro-go-sd)
  (`dosat.bin`). Los dos conviven en la misma tarjeta.
- Necesita un firmware de la rama `external_cores` (ABI v2, `b791f15` del
  11-10-2026 o posterior). Con uno más antiguo el lanzador dice "ABI mismatch".

## En la tarjeta SD

```
/cores/dosxt.bin           <- el núcleo (out/dosxt.bin)
/cores/dosxt/!BOOT.img     <- disquete de arranque con tu MS-DOS
/cores/dosxt/MONKEY.cfg    <- configuración de cada juego (opcional), mismo nombre
/roms/dos/MONKEY.img       <- cada juego, como imagen de disco
```

## Crear las imágenes

Todo se hace con `tools/fat12.py`, que solo necesita Python 3 (Windows, Linux o
macOS).

**1. El disquete de arranque**, una sola vez, desde el disco 1 de MS-DOS 6.22:

```
python3 tools/fat12.py bootdisk Disk01.img !BOOT.img
```

**2. La imagen de cada juego**, desde la carpeta del juego:

```
python3 tools/fat12.py new prince.img PRINCE/*            # disquete de 1,44 MB
python3 tools/fat12.py newhd monkey.img --size 8 MONKEY   # disco duro, juegos grandes
```

Más formas (un disquete arrancable por juego, instalar MS-DOS en un disco duro,
meter archivos en una imagen…): [**docs/discos.md**](docs/discos.md).

## Configurar un juego (`.cfg`)

El `.cfg` le dice al núcleo con qué DOS arrancar y qué escribir en el prompt.
Un ejemplo completo:

```ini
; /cores/dosxt/MONKEY.cfg  -- el juego es el disco duro /roms/dos/MONKEY.img
boot   = !BOOT.img      ; arranca el DOS desde este disquete (A:); el juego queda en C:
run    = c:;cd \monkey;monkey a   ; lo que se escribe en el prompt (";" = Intro)
cpu    = 16             ; MHz: 4.77, 8, 10, 12, 16 o 20
mouse  = on             ; la cruceta mueve el puntero del ratón
b      = click          ; B: botón izquierdo del ratón
a      = click2         ; A: botón derecho
start  = f5             ; START: F5 (en Monkey Island, guardar partida)
select = space          ; SELECT: Espacio (pausa)
```

- Con un disquete de arranque, el juego queda en **B:** si es un disquete y en
  **C:** si es un disco duro.
- Sin `.cfg`, la imagen arranca sola si es un disquete de arranque.
- **Todas las opciones**: [**docs/cfg.md**](docs/cfg.md). **Nombres de las
  teclas**: [docs/teclas.md](docs/teclas.md). Ejemplos listos:
  [examples/cfg/](examples/cfg/).

## Controles

| Botón | Qué hace |
|---|---|
| Cruceta | Flechas (o el puntero, con `mouse = on`) |
| A / B | Intro / Espacio |
| GAME | Esc |
| **TIME** | **Abre el teclado en pantalla** (CPU, cambio de disco…) |
| PAUSE/SET | Menú de retro-go; con la cruceta, volumen y brillo |

Teclado en pantalla, cambio de disco, velocidad de la CPU y ratón:
[docs/controles.md](docs/controles.md). Vídeo y sonido:
[docs/video-sonido.md](docs/video-sonido.md).

## Partidas guardadas

Desde el menú de retro-go (**PAUSE/SET**) puedes guardar la partida en
cualquiera de sus huecos y volver a ella cuando quieras. Al lanzar un juego que
tiene partidas guardadas, retro-go te deja elegir entre **Continuar** y empezar
de nuevo.

- **Qué se guarda:** el PC XT entero: CPU, toda la memoria (con el DOS y el
  juego cargados), vídeo, sonido y qué disquete había en A:. Cada guardado
  ocupa alrededor de 1 MB y tarda unos segundos en guardarse y en cargarse.
- **Solo vale con el mismo `dosxt.bin`.** Si actualizas el núcleo, los
  guardados de antes se rechazan: no se carga nada y el juego sigue como
  estaba.
- **Las imágenes de disco no vuelven atrás.** Lo que el juego haya escrito en
  su `.img` (sus propias partidas, récords) se queda como esté, y el DOS
  recuperado puede no saber de ello hasta que vuelva a leer el disco. Lo más
  seguro es usar o los guardados de retro-go o las partidas del propio juego,
  no mezclarlos.

## Más

- [docs/desarrollo.md](docs/desarrollo.md): cómo está hecho y cómo compilarlo.
- Licencia: XTulator es GPLv2+ y la BIOS de Sergey Kiselev GPLv3, así que
  `dosxt.bin` es **GPLv3**. Fuente font8x8 de Daniel Hepper (dominio público).
  No incluye ningún software de Microsoft.
