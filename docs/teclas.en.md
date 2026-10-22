# Keys for the `.cfg` buttons

*[Leer en español](teclas.md)*

*Part of the [per-game configuration](../README.en.md#per-game-configuration-cfg).*

What goes after a button's `=` (`a = shift`) is the name of a PC key. Upper or lower case does not
matter. Up to 3 can be joined with `+` (`ctrl+c`, `alt+x`, `shift+f1`): they are pressed in that
order and released in reverse, as your fingers would.

| PC key | Accepted names |
|---|---|
| Letters and digits | `a` … `z`, `0` … `9` |
| F1 … F12 | `f1` … `f12` |
| Enter | `enter`, `return`, `intro` |
| Space | `space`, `espacio` |
| Esc | `esc`, `escape` |
| Tab | `tab`, `tabulador` |
| Backspace | `backspace`, `retroceso` |
| Shift | `shift`, `lshift`, `mayus` · the right one: `rshift` |
| Control | `ctrl`, `control` |
| Alt | `alt` |
| Caps Lock / Num Lock / Scroll Lock | `capslock`, `numlock`, `scrolllock` |
| Arrows | `up`/`arriba`, `down`/`abajo`, `left`/`izquierda`, `right`/`derecha` |
| Home / End | `home`/`inicio`, `end`/`fin` |
| Page Up / Page Down | `pgup`/`repag`, `pgdn`/`avpag` |
| Insert / Delete | `ins`/`insert`, `del`/`delete`/`supr` |
| Numeric keypad | `kp0` … `kp9`, `kpplus`, `kpminus`, `kpmul` (`kp8` is the same key as the up arrow, as on an XT keyboard) |
| Symbols | `-` or `minus`, `=` or `equals`, `[` `]` (`lbracket` `rbracket`), `'` or `quote`, `` ` `` or `backtick`, `\` or `backslash`, `,` or `comma`, `.` or `period`, `/` or `slash`, `*` |
| Semicolon | **only `semicolon`**: a bare `;` starts a comment |
| Special | `keyboard`/`teclado` (opens the on-screen keyboard), `none`/`nada` (the button does nothing) |
| Mouse (with `mouse = on`) | `click`/`clic` (left button), `click2`/`clic2` (right), `mup` `mdown` `mleft` `mright` (move the pointer) |

A name that is not in the table counts as an error (the top strip says so when loading) and that
button keeps its usual key.

## How do I find out which keys a game uses?

There is no automatic way: every DOS game chose its own. In order, what usually works:

1. **What the game itself says**: many show it at start-up or on a help screen (Alley Cat asks
   "Y/N", Prince of Persia lists its keys in its demo, Sierra and LucasArts games have a menu on
   F1/F5…).
2. **The manual**: almost all of them have a table of keys. Scans can be found by searching
   "<game> manual" (archive.org, MobyGames, abandonware sites…).
3. **Trying with the on-screen keyboard**: if a key does something useful in the game, put it on a
   button.
4. **The usual keys of the period**: arrows to move; Space, Ctrl or Alt to jump or fire; Enter to
   accept; Esc for the menu or to skip scenes; F5/F7 or similar to save and load.

The ones in [`examples/cfg/`](../examples/cfg/) were made that way, from each game's manual and
screens (Prince: Shift to grab and step carefully; Monkey Island: F5 menu, Space pause).
