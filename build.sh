#!/bin/bash
# gw-dos: build the MS-DOS (XT/CGA) external core for the Game & Watch retro-go SD firmware
# (branch "external_cores", firmware ABI v2: cores are /cores/*.bin with a CORE header).
#
#   ./build.sh                                  host test only (no ARM toolchain needed)
#   REPO=/path/to/retro-go-sd-external_cores GCC_PATH=/path/to/arm-gnu-toolchain/bin ./build.sh
#                                               host test + out/dos.bin for the console
#
# REPO must be a checkout of the external_cores branch (same one your firmware was built
# from), with the FatFs submodule and the ST drivers present (`make download_sdk` there).
# It is only read: headers, the core bridge (Core/Src/porting/core_common), ld/ and
# tools/pack_core.py. Nothing is written into it.
# Optional: DOSIMG=/path/to/bootable_floppy.img also boots that disk in the host test.
# Needs: git, gcc, nasm, python3 (+ Pillow for PNG screenshots, optional), github.com access.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
W="$HERE/work"; OUT="$HERE/out"; mkdir -p "$W" "$OUT"

# ---- 1. sources: XTulator core + Sergey Kiselev's 8088 BIOS ----
[ -d "$W/xtulator_src/.git" ] || git clone -q --depth 1 https://github.com/mikechambers84/XTulator "$W/xtulator_src"
[ -d "$W/bios_src/.git" ]     || git clone -q --depth 1 https://github.com/skiselev/8088_bios "$W/bios_src"
rm -rf "$W/xt"; mkdir -p "$W/xt"; cp -r "$W/xtulator_src/XTulator/." "$W/xt/"
python3 -I "$HERE/core/prep.py" "$W/xt" >/dev/null           # sergey_xt machine, SDL stubs
python3 -I "$HERE/core/virtual_time.py" "$W/xt/timing.c" >/dev/null
python3 -I "$HERE/core/static_mem.py" "$W/xt"                # MCU-sized, malloc-free core
python3 -I - "$W/bios_src/binaries/bios-xt.bin" > "$W/bios_rom.c" <<'EOF'
import sys
b = open(sys.argv[1], "rb").read(); assert len(b) == 16384
print("/* Sergey Kiselev 8088 BIOS (GPLv3), https://github.com/skiselev/8088_bios */")
print("#include <stdint.h>\nconst uint8_t bios_xt_rom[16384] = {"); print(",".join(str(x) for x in b)); print("};")
EOF
CORE=""; for f in $(cat "$HERE/core/core_sources.txt"); do CORE="$CORE $W/xt/$f"; done
DEFS="-DXT_MINIMAL -DXT_NO_HOST_RENDER -DXT_TIMING_CATCHUP -DXT_IRQ_EVERY_INSTR -DXT_INT10_EXT -DXT_CGA_STATUS_ICOUNT -DXT_VGA_HLE -DXT_MOUSE_HLE -DXT_SPK_HLE -DXT_ADLIB_HLE -DXT_SB_HLE -DXT_POOL_DEFAULT_SIZE=0 -DXT_RAM_CHUNK=0x1000"
# emu8950 (AdLib): the G&W Doom port's build. -fms-extensions: OPL_SLOT embeds an anonymous struct
DEFS="$DEFS ${EXTRA_DEFS:-} -fms-extensions -DUSE_EMU8950_OPL=1 -DDOOMX=1 -DEMU8950_LINEAR=1 -DEMU8950_LINEAR_SKIP=1 -DEMU8950_NO_RATECONV=1 -DEMU8950_NO_TLL=1 -DEMU8950_NO_TIMER=1 -DEMU8950_NO_TEST_FLAG=1 -DEMU8950_NO_PERCUSSION_MODE=1"
WARN="-Wall -Wno-unused -Wno-parentheses -Wno-attributes -Wno-format-truncation"

# ---- 2. host test: the same frontend + core, on a fake firmware ----
mkdir -p "$W/dos" "$W/shots"
bash "$HERE/core/mkdisk.sh" "$HERE/tests/hello.asm" "$W/dos/a.img"
gcc -O2 $WARN $DEFS -DGWDOS_HOST -DDOS_DIR="\"$W/dos\"" -iquote "$W/xt" -I"$HERE/src" \
    "$HERE/src/dos_main.c" "$HERE/src/cga_render.c" "$HERE/src/gamecfg.c" "$HERE/host/platform_host.c" "$W/bios_rom.c" $CORE \
    -o "$W/gwdos_host" -lm
echo "== host test 1: BIOS POST + boot sector, through the G&W frontend (40 s virtual) =="
OUT1=$(cd "$W" && ./gwdos_host 2400 "" shots/hello_)
echo "$OUT1" | grep -E "Hello from a boot sector|Total Conventional RAM|frames="
echo "$OUT1" | grep -q "Hello from a boot sector" || { echo FAIL; exit 1; }
echo "$OUT1" | grep -qE "(5[0-9][0-9]|6[0-4][0-9]) KiB" || { echo "FAIL: guest RAM not detected (at least 500 KB expected)"; exit 1; }
echo PASS
if [ -n "${DOSIMG:-}" ]; then
  echo "== host test 2: boot $DOSIMG, type DIR on the on-screen keyboard =="
  mkdir -p "$W/dos2"; cp "$DOSIMG" "$W/dos2/a.img"
  gcc -O2 -w $DEFS -DGWDOS_HOST -DDOS_DIR="\"$W/dos2\"" -iquote "$W/xt" -I"$HERE/src" \
      "$HERE/src/dos_main.c" "$HERE/src/cga_render.c" "$HERE/src/gamecfg.c" "$HERE/host/platform_host.c" "$W/bios_rom.c" $CORE -o "$W/gwdos_host2" -lm
  S=$(python3 -I "$HERE/tools/btnscript.py" 2400 time down right right a up right right right right right a left left left left a b a 2>"$W/end.txt")
  OUT2=$(cd "$W" && ./gwdos_host2 $(( $(cat "$W/end.txt") + 900 )) "$S" shots/dos_)
  echo "$OUT2" | tail -6
  echo "$OUT2" | grep -qi "archivo\|file(s)\|bytes" && echo PASS || echo "CHECK: no DIR output found (see work/shots/)"
fi
python3 - "$W/shots" <<'EOF' 2>/dev/null || true
import sys, glob
from PIL import Image
for f in glob.glob(sys.argv[1] + "/*.ppm"):
    Image.open(f).resize((640, 480), Image.NEAREST).save(f[:-4] + ".png")
EOF

# ---- 3. Game & Watch external core ----
ARMGCC=""
if [ -n "${GCC_PATH:-}" ] && [ -x "$GCC_PATH/arm-none-eabi-gcc" ]; then ARMGCC="$GCC_PATH/arm-none-eabi-"
elif command -v arm-none-eabi-gcc >/dev/null; then ARMGCC="arm-none-eabi-"; fi
if [ -z "$ARMGCC" ] || [ -z "${REPO:-}" ]; then
  echo "(set REPO and GCC_PATH to build out/dos.bin for the console)"; exit 0
fi
CC_DIR="$REPO/Core/Src/porting/core_common"
[ -f "$CC_DIR/gw_core_bridge.c" ] && [ -f "$REPO/tools/pack_core.py" ] && [ -f "$REPO/ld/gnw_ram_emu.ld" ] || \
  { echo "REPO is not an external_cores checkout (no core_common bridge / pack_core.py)"; exit 1; }
[ -f "$REPO/Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_hal.h" ] || { echo "REPO has no ST drivers yet: run 'make download_sdk' there"; exit 1; }
[ -f "$REPO/Core/Src/porting/lib/FatFs/ff.h" ] || { echo "REPO submodules missing: git submodule update --init Core/Src/porting/lib/FatFs"; exit 1; }

# same flags as the core SDK's cores/_template/Makefile
MCU="-mcpu=cortex-m7 -mtune=cortex-m7 -mthumb -mno-unaligned-access -mfpu=fpv5-d16 -mfloat-abi=hard"
SDKDEFS="-DUSE_HAL_DRIVER -DSTM32H7B0xx -DSD_CARD=1 -DGNW_DISABLE_COMPRESSION -DIS_LITTLE_ENDIAN -DCOVERFLOW=0 -DCHEAT_CODES=0 -DGNW_CORE_ENTRY_TARGET=app_main_dos"
SDKINC=""
for d in Core/Inc Core/Inc/retro-go Core/Inc/porting Core/Src/porting/core_common Core/Src/porting/lib \
         Core/Src/porting/lib/FatFs Drivers/STM32H7xx_HAL_Driver/Inc Drivers/STM32H7xx_HAL_Driver/Inc/Legacy \
         Drivers/CMSIS/Device/ST/STM32H7xx/Include Drivers/CMSIS/Include; do SDKINC="$SDKINC -I$REPO/$d"; done
CF="$MCU -O2 -Wall -fdata-sections -ffunction-sections -fno-common $SDKDEFS $SDKINC $WARN $DEFS -DXT_NO_STDIO"
REDEF="$REPO/Core/Src/porting/core_common/gw_core_bridge_redefine_syms.txt"
A="$W/arm"; rm -rf "$A"; mkdir -p "$A"
cc_core() {   # <source> <object> [extra flags...]: compile + rename firmware calls to core_* trampolines
  local src="$1" obj="$2"; shift 2
  ${ARMGCC}gcc -c $CF "$@" "$src" -o "$obj"
  ${ARMGCC}objcopy --redefine-syms="$REDEF" "$obj"
}
for f in $(cat "$HERE/core/core_sources.txt"); do cc_core "$W/xt/$f" "$A/$(basename "${f%.c}").o" -iquote "$W/xt"; done
for f in dos_main cga_render gamecfg; do cc_core "$HERE/src/$f.c" "$A/$f.o" -iquote "$W/xt" -I"$HERE/src"; done
cc_core "$HERE/src/platform_core.c" "$A/platform_core.o" -I"$HERE/src"      # firmware headers only
cc_core "$W/bios_rom.c" "$A/bios_rom.o"
cc_core "$CC_DIR/gw_core_i18n.c" "$A/gw_core_i18n.o"
${ARMGCC}gcc -c $CF -fno-builtin-memcpy -fno-builtin-memmove -fno-builtin-memset \
    "$CC_DIR/gw_core_bridge.c" -o "$A/gw_core_bridge.o"               # the bridge itself is not renamed
${ARMGCC}gcc -x assembler-with-cpp -c $MCU $SDKDEFS "$CC_DIR/gw_core_entry.S" -o "$A/gw_core_entry.o"
${ARMGCC}gcc "$A"/*.o $MCU -nostdlib -lgcc -T"$HERE/sdk/core_ram_emu.ld" -L"$REPO/ld" \
    -Wl,-Map="$A/dos_core.map" -Wl,--gc-sections -Wl,--no-check-sections -o "$A/dos_core.elf"
${ARMGCC}objcopy -O binary --only-section=.core_entry --only-section=.data "$A/dos_core.elf" "$A/dos_core.bin"
python3 "$REPO/tools/pack_core.py" --elf "$A/dos_core.elf" --bin "$A/dos_core.bin" \
    --system-name "MS-DOS" --dirname dos --extensions "img ima" \
    --core-name "gw-dos" --version 0.1.0 \
    --header-logo "$HERE/src/assets/header.png" --pad-logo "$HERE/src/assets/pad.png" \
    --nm "${ARMGCC}nm" --objcopy "${ARMGCC}objcopy" --out "$OUT/dos.bin"
echo "copy out/dos.bin to /cores/ and your disk images (.img) to /roms/dos/ on the SD card"
