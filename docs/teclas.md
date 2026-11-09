# Teclas para los botones del `.cfg`

*[Read in English](teclas.en.md)*

*Parte de la [configuración por juego](../README.es.md#configuración-por-juego-cfg).*

Lo que va detrás del `=` de un botón (`a = shift`) es el nombre de una tecla del PC. Da igual
mayúsculas o minúsculas. Se pueden juntar hasta 3 con `+` (`ctrl+c`, `alt+x`, `shift+f1`): se pulsan
en ese orden y se sueltan al revés, como harías con los dedos.

| Tecla del PC | Nombres que acepta |
|---|---|
| Letras y números | `a` … `z`, `0` … `9` |
| F1 … F12 | `f1` … `f12` |
| Intro | `enter`, `return`, `intro` |
| Espacio | `space`, `espacio` |
| Esc | `esc`, `escape` |
| Tabulador | `tab`, `tabulador` |
| Retroceso | `backspace`, `retroceso` |
| Mayúsculas (Shift) | `shift`, `lshift`, `mayus` · la derecha: `rshift` |
| Control | `ctrl`, `control` |
| Alt | `alt` |
| Bloq Mayús / Bloq Num / Bloq Despl | `capslock`, `numlock`, `scrolllock` |
| Flechas | `up`/`arriba`, `down`/`abajo`, `left`/`izquierda`, `right`/`derecha` |
| Inicio / Fin | `home`/`inicio`, `end`/`fin` |
| Re Pág / Av Pág | `pgup`/`repag`, `pgdn`/`avpag` |
| Insert / Supr | `ins`/`insert`, `del`/`delete`/`supr` |
| Teclado numérico | `kp0` … `kp9`, `kpplus`, `kpminus`, `kpmul` (`kp8` es la misma tecla que la flecha arriba, como en un teclado XT) |
| Signos | `-` o `minus`, `=` o `equals`, `[` `]` (`lbracket` `rbracket`), `'` o `quote`, `` ` `` o `backtick`, `\` o `backslash`, `,` o `comma`, `.` o `period`, `/` o `slash`, `*` |
| Punto y coma | **solo `semicolon`**: un `;` suelto empieza un comentario |
| Especiales | `keyboard`/`teclado` (abre el teclado en pantalla), `none`/`nada` (el botón no hace nada) |
| Ratón (con `mouse = on`) | `click`/`clic` (botón izquierdo), `click2`/`clic2` (derecho), `mup` `mdown` `mleft` `mright` (mueven el puntero) |

Un nombre que no esté en la tabla cuenta como error (lo dice la franja de arriba al cargar) y ese
botón se queda con su tecla de siempre.

## ¿Cómo saber qué tecla usa cada juego?

No hay forma automática: cada juego de DOS eligió las
suyas. Por orden, lo que suele funcionar:

1. **Lo que dice el propio juego**: muchos lo muestran al arrancar o en una pantalla de ayuda
   (Alley Cat pregunta "Y/N", Prince of Persia lista las teclas en su demo, los de Sierra y
   LucasArts tienen un menú en F1/F5…).
2. **El manual**: casi todos tienen una tabla de teclas. Se encuentran escaneados buscando
   "<juego> manual" (archive.org, MobyGames, abandonware…).
3. **Probar con el teclado en pantalla**: si una tecla hace algo útil en el juego, ponla en un botón.
4. **Lo típico de la época**: flechas para moverse; Espacio, Ctrl o Alt para saltar o disparar;
   Intro para aceptar; Esc para menú o saltar escenas; F5/F7 o similares para grabar y cargar.

Los que hay en [`examples/cfg/`](../examples/cfg/) salieron así, del manual y la pantalla de cada juego (Prince: Shift para
agarrarse y andar con cuidado; Monkey Island: F5 menú, Espacio pausa).
