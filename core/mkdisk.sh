#!/bin/bash
# usage: mkdisk.sh <boot.asm> <out.img>  -> 1.44 MB floppy image with the assembled boot sector
set -e
TMP=$(mktemp)
trap 'rm -f "$TMP"' EXIT
nasm -f bin "$1" -o "$TMP"
dd if=/dev/zero of="$2" bs=512 count=2880 status=none
dd if="$TMP" of="$2" conv=notrunc status=none
