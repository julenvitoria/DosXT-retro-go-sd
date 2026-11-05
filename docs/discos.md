# Crear las imágenes de disco

*[Read in English](discos.en.md) · Vuelve al [README](../README.md)*

Todo se hace con `tools/fat12.py`, que solo necesita Python 3. Para ver todas
sus órdenes: `python3 tools/fat12.py`; para la ayuda de una:
`python3 tools/fat12.py help <orden>`.

## Cómo las trata el núcleo

- Una imagen de **hasta 2,88 MB** es un disquete: se arranca desde A:.
- Una **más grande** es un disco duro: se arranca desde C:.
- Lo que el DOS escribe (partidas guardadas, configuración) se guarda en la
  imagen, en la tarjeta.

## El disquete de arranque (`!BOOT.img`)

El disco 1 de instalación de MS-DOS 6.22 arranca, pero lanza el instalador.
Esto hace una copia que se queda en `A:\>` con el teclado español:

```
python3 tools/fat12.py bootdisk Disk01.img !BOOT.img
```

Ponlo en `/cores/dosxt/` (o en `/roms/dos/` si quieres elegirlo en el menú). Trae
`FORMAT`, `FDISK`, `SYS`, `EDIT`, `QBASIC`, `MEM`… El original no se modifica.

## Un juego en disquete

```
python3 tools/fat12.py new prince.img PRINCE/*
```

Crea un disquete de 1,44 MB con esos archivos. No arranca solo: en su `.cfg`
pon `boot = !BOOT.img` y `run = b:;prince` (ver [cfg.md](cfg.md)).

**O todo en uno**, un disquete que arranca y lanza el juego sin `.cfg`:

```
python3 tools/fat12.py sysdisk Disk01.img prince.img --run "prince" PRINCE/*
```

Lleva el DOS mínimo (IO.SYS, MSDOS.SYS y COMMAND.COM de tu disco 1), los
archivos del juego y un AUTOEXEC.BAT que lo arranca. `--run` admite varias
órdenes separadas por `;` (`"loadfix rick"`, `"c:;cd \loom;loom"`).

## Un juego grande en disco duro

```
python3 tools/fat12.py newhd monkey.img --size 8 MONKEY
```

Cada carpeta pasa a ser un directorio del disco (`C:\MONKEY`). Por defecto
16 MB, hasta 500 MB con `--size`. Arrancando desde `!BOOT.img`, el disco queda
en C: (`run = c:;cd \monkey;monkey`).

## Meter o ver archivos

```
python3 tools/fat12.py add juego.img ALLEYCAT.EXE    # añadir (también carpetas)
python3 tools/fat12.py ls juego.img                  # ver qué hay dentro
python3 tools/fat12.py extract juego.img carpeta     # sacarlo todo
```

## Instalar MS-DOS en un disco duro

Para arrancar con un DOS instalado en lugar de un disquete:

1. Crea un disco vacío: `python3 tools/fat12.py newhd c.img --size 32`.
2. Copia `c.img` y los discos de instalación (`Disk01.img`, `Disk02.img`…) a
   `/roms/dos/`.
3. Arranca `Disk01.img` desde el menú: el instalador ve `c.img` como C:.
4. Cuando pida otro disco, ábrelo con la tecla **A:** del teclado en pantalla
   y elige el siguiente.
5. Al terminar, en la lista de discos elige **(sacar el disco de A:)** y pulsa
   Intro: el PC arranca desde C:.

Desde entonces, `boot = c.img` en el `.cfg` de un juego arranca ese DOS, y el
juego queda en A: (`run = a:;juego`). Probado con MS-DOS 5.0 en español.
Consejo: a "Ejecutar Shell al inicio" contesta NO; en un 8088 es lento.

## Para el otro núcleo (`dosat.bin`)

`todsk` convierte una imagen o una carpeta en el `.dsk` que usa DosAT:

```
python3 tools/fat12.py todsk monkey MONKEY.dsk --run "CD MONKEY;MONKEY"
```
