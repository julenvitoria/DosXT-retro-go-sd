# Vídeo y sonido

*[Read in English](video-sonido.en.md) · Vuelve al [README](../README.md)*

## EGA y VGA

La tarjeta es una CGA a la que se le han añadido los gráficos EGA/VGA, así que
los juegos que detectan la tarjeta eligen solos el mejor modo (Prince of Persia
y Monkey Island usan VGA; Loom y Rick Dangerous, EGA).

| Modo | | Memoria de vídeo |
|---|---|---|
| 0Dh | 320×200, 16 colores (EGA) | 64 KB (128 KB si el juego usa páginas fuera de pantalla) |
| 0Eh | 640×200, 16 colores (se ve a 320 de ancho) | 64 KB |
| 13h | 320×200, 256 colores (VGA) | 64 KB |
| "modo X" | 320×200/240, 256 colores sin encadenar | 256 KB |
| 10h / 12h | 640×350 / 640×480, 16 colores (se ven reducidos) | 128 KB / 256 KB |

La memoria de vídeo sale de la misma reserva que la RAM del DOS: con
`vram = 64` y con **128 (por defecto)** el DOS tiene sus 640 KB enteros; con 256
quedan 512 KB. `video = cga` quita la EGA/VGA, para juegos que solo funcionan en
CGA. Si un juego usa más memoria de vídeo de la que hay, la franja de abajo lo
avisa en rojo ("Falta memoria de video"): sube `vram` en su `.cfg`.

## Sonido

- **Altavoz del PC** a 48 kHz, también el sonido digitalizado "a mano" (bit 1
  del puerto 61h).
- **AdLib** (OPL2, puertos 388h/389h; sintetizador emu8950). El modo percusión
  no está emulado.
- **Sound Blaster 2.0**: puerto 220h, IRQ 7, DMA 1
  (`BLASTER=A220 I7 D1 T3`).

Los juegos que detectan la tarjeta la usan solos; en los que preguntan, elige
AdLib o Sound Blaster con esos valores. La música AdLib pide CPU: el regulador
bajará algo los MHz en esos juegos.

## Lo que no está

- El driver de ratón no llama a los "event handlers" (INT 33h 0Ch/14h): los
  juegos que solo usan eso no ven el ratón (la mayoría usan la función 03h).
- Sin estados guardados del emulador (las partidas del propio juego sí se
  guardan).
