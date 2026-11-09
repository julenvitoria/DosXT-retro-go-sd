# Controles

*[Read in English](controles.en.md) · Vuelve al [README](../README.es.md)*

## Botones

| Botón | Normal | Con el teclado en pantalla |
|---|---|---|
| Cruceta | Flechas (o el puntero, con `mouse = on`) | Mover |
| A | Intro | Pulsar la tecla |
| B | Espacio | Cerrar |
| GAME (START en Zelda) | **Abrir el teclado** | Cerrar |
| TIME (SELECT en Zelda) | Esc | — |
| START / SELECT (Zelda) | Tab / Retroceso | — |
| PAUSE/SET | Menú de retro-go (volumen, brillo, salir…) | |

Todos se pueden cambiar en el `.cfg` (ver [cfg.md](cfg.md)). Con PAUSE/SET +
cruceta salen las barras de volumen (izquierda/derecha) y brillo
(arriba/abajo). Al mantener un botón que manda una tecla, la tecla se repite
como en un teclado de verdad.

## Teclado en pantalla

Cuatro filas, dos en la franja de arriba y dos en la de abajo; el juego no se
mueve. Manteniendo la cruceta el cursor sigue avanzando.

- Letras, números, F1–F10, flechas, Inicio/Fin, Ins/Supr.
- **Ctrl, Alt y Shift** se quedan pulsados para la siguiente tecla (en verde),
  para Ctrl+C, Shift+letra, etc. Shift pulsado dos veces se queda bloqueado (en
  rojo, como Bloq Mayús); una tercera vez lo suelta.
- **A:** abre la lista de discos de la carpeta y mete el elegido en A: (el DOS
  ve el cambio de disco).
- **CPU** cambia la velocidad: 4,77 (IBM PC/XT), 8 (por defecto), 10, 12, 16
  y 20 MHz, solo para esa partida (al volver a arrancar el juego vale la del
  `.cfg`, o 8 MHz si no pone nada). Es un máximo: si la consola no llega, el núcleo baja solo y la
  franja de abajo lo indica (`16->11.2 MHz`).
- **80c** cambia cómo se ve el texto de 80 columnas: una ventana de 40 que sigue
  al cursor (por defecto) o la línea entera con letras pequeñas.

## Ratón

El emulador trae su propio driver de ratón (INT 33h, compatible con el de
Microsoft). Se activa con `mouse = on` en el `.cfg`: la cruceta mueve el
puntero (despacio al principio, más rápido si la mantienes) y A/B son los
botones. Se puede poner en cualquier botón: `click`, `click2`, `mup`, `mdown`,
`mleft`, `mright`. Los juegos SCUMM (Monkey Island, Loom…) se juegan mucho
mejor así.

## La pantalla

La imagen (320×200) va centrada. Las franjas de 20 píxeles de arriba y abajo
muestran el estado (el `.cfg` y los discos arriba; MHz y carga abajo) o el
teclado. El estado se ve 7 segundos al arrancar; la línea de abajo puede
quedarse fija con **Info: ON** en el menú de retro-go (PAUSE/SET → Options).
