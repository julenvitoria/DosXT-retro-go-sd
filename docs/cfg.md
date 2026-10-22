# El `.cfg` de cada juego

*[Read in English](cfg.en.md)*

*Todo lo que se puede poner en el `.cfg` de un juego para `dos.bin`. Vuelve al
[README](../README.md#configuración-por-juego-cfg).*

## Dónde va

Para la imagen `/roms/dos/JUEGO.img` se busca, por este orden:

1. `/cores/dos/JUEGO.cfg` (junto al núcleo, así no sale en el menú de juegos)
2. `/roms/dos/cfg/JUEGO.cfg`
3. `/roms/dos/JUEGO.cfg`

Todo es opcional: sin `.cfg`, la imagen arranca sola si es un disquete de
arranque, con los botones por defecto.

## Formato

- Una opción por línea: `clave = valor`. Mayúsculas o minúsculas, da igual.
- `;` y `#` empiezan un comentario en cualquier sitio de la línea (salvo el `;`
  dentro del valor de `run`, que separa órdenes). Por eso un `;` de teclado se
  escribe `semicolon`.
- Si una línea no se entiende, la franja de arriba lo avisa al cargar
  ("N errores") y esa opción se queda como estaba.

## Opciones

| Clave | Valores | Qué hace |
|---|---|---|
| `boot` | nombre de imagen (`!BOOT.img`, `c.img`…) | El DOS arranca desde esa imagen. Se busca en `/cores/dos` y luego en `/roms/dos`. Si es un disquete, arranca en A: y el juego queda en B: (disquete) o C: (disco duro). Si es un disco duro con DOS instalado, arranca en C: y el juego queda en A: (disquete) o D: (disco duro). |
| `run` | órdenes separadas por `;` | Se escriben en el prompt del DOS, cada una cuando aparece el prompt (`A:\>`), con Intro detrás. Ej.: `b:;cd \juego;juego`. |
| `cpu` | `4.77`, `8`, `10`, `12`, `16`, `20` | Velocidad de la CPU en MHz al arrancar (también se cambia con la tecla `CPU` del teclado en pantalla). |
| `video` | `vga` (por defecto) o `cga` | `cga` quita los gráficos EGA/VGA y deja 640 KB enteros para el DOS. |
| `vram` | `64`, `128` (por defecto), `256` | Memoria de vídeo EGA/VGA en KB. Si un juego avisa de "Falta memoria de video", súbela. |
| `zoom80` | `1` / `0` | Texto de 80 columnas: `1` = ventana de 40 columnas que sigue al cursor, `0` = la línea entera. |
| `mouse` (o `raton`) | `on` / `off` | Ratón: la cruceta mueve el puntero y A/B son los botones (ver el README, "Ratón"). |
| `up` `down` `left` `right` (o `arriba` `abajo` `izquierda` `derecha`), `a`, `b`, `game`, `time`, `start` (o `x`), `select` (o `y`) | nombre de tecla, o hasta 3 con `+` | Qué tecla del PC manda cada botón. Lista de nombres: [`teclas.md`](teclas.md). Además: `keyboard`/`teclado` (abre el teclado en pantalla), `none`/`nada` (sin función) y, para el ratón, `click` `click2` `mup` `mdown` `mleft` `mright`. |

Si ningún botón queda como `keyboard`, TIME sigue abriendo el teclado en
pantalla.

## Opciones de los propios juegos (sonido, vídeo…)

Esto ya no es del `.cfg` sino de cada juego: muchos se configuran con una letra
o una palabra detrás del nombre del ejecutable, o con un programa de
instalación. En `dos.bin` se ponen en `run` (por ejemplo `run = b:;monkey a`).

- **Juegos de LucasArts de la época (Monkey Island 1, Loom, Indiana Jones…)**:
  una letra detrás del ejecutable elige la tarjeta de sonido: `a` = AdLib,
  `r` = Roland MT-32 (no está emulada: sin música), `i` = altavoz del PC. Sin
  letra, el juego busca la tarjeta solo. Con una Sound Blaster se usa `a`: la
  música de estos juegos es FM, y la Sound Blaster lleva el mismo chip que la
  AdLib.
- **Juegos con programa de configuración** (`SETUP.EXE`, `INSTALL.EXE`,
  `SOUND.EXE`, `CONFIG.EXE`…): se ejecuta una vez desde el prompt, se elige la
  tarjeta (AdLib, o Sound Blaster en 220h / IRQ 7 / DMA 1) y el juego guarda la
  elección en su carpeta.
- **Para saber qué opciones tiene un juego**: el manual o el `README`/`LEEME`
  de su carpeta, o probar `JUEGO /?`.

Qué emula `dos.bin`: AdLib, Sound Blaster 2.0 (220h, IRQ 7, DMA 1:
`BLASTER=A220 I7 D1 T3`) y altavoz del PC. No emula Roland MT-32, Gravis Ultrasound ni Tandy/PCjr.
