#!/bin/sh
# Builds the WEMU RPX test suite into build/game/{code,content,meta}.
# --install also copies it to games/wemu-suite so the GUI library lists it
# (launched from the GUI it opens the interactive viewer).
#
# Needs devkitPPC + wut (DEVKITPRO, default /opt/devkitpro) and latte-assembler
# (LATTE_ASSEMBLER, default tools/latte-assembler/build/latte-assembler; build
# it once with tools/latte-assembler/setup.sh).
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
DKP=${DEVKITPRO:-/opt/devkitpro}
export DEVKITPRO=$DKP  # wut.specs resolves its paths through it
LATTE_ASSEMBLER=${LATTE_ASSEMBLER:-$HERE/tools/latte-assembler/build/latte-assembler}
OUT=$HERE/build
GEN=$OUT/gen
GAME=$OUT/game

if [ ! -x "$LATTE_ASSEMBLER" ]; then
    echo "latte-assembler not found at $LATTE_ASSEMBLER (run tools/latte-assembler/setup.sh)" >&2
    exit 1
fi
mkdir -p "$GEN/shaders" "$OUT/obj" "$GAME/code" "$GAME/content" "$GAME/meta"

# Latte assembly -> GFD -> C arrays (256-byte aligned like shader programs).
PROGRAMS=$(python3 "$HERE/shaders/programs.py" "$GEN/shaders")
BLOBS=$GEN/shader_blobs.c
echo '#include <stdint.h>' > "$BLOBS"
for program in $PROGRAMS; do
    "$LATTE_ASSEMBLER" assemble "$GEN/shaders/$program.gsh" \
        --vsh "$GEN/shaders/$program.vsh" --psh "$GEN/shaders/$program.psh"
    python3 - "$GEN/shaders/$program.gsh" "wcs_gsh_$program" >> "$BLOBS" <<'PY'
import sys
data = open(sys.argv[1], 'rb').read()
rows = [', '.join(f'0x{b:02x}' for b in data[i:i + 16]) for i in range(0, len(data), 16)]
print(f'const uint8_t {sys.argv[2]}[] __attribute__((aligned(256))) = {{\n    ' + ',\n    '.join(rows) + '\n};')
PY
done

# The core's public-domain 8x16 font, reused for the on-screen overlay.
python3 - "$HERE/../../core/src/gfx/BitmapFont.hpp" > "$GEN/font8x16.c" <<'PY'
import re, sys
text = open(sys.argv[1]).read()
body = text[text.index('kFont8x16[128][16] = {'):]
rows = []
for line in body.splitlines()[1:]:
    line = line.strip()
    if line.startswith('BLK'):
        rows.append([0] * 16)
    elif line.startswith('{'):
        rows.append([int(v, 0) for v in re.findall(r'0x[0-9A-Fa-f]+|\d+', line.split('}')[0])])
    elif line.startswith('};'):
        break
rows += [[0] * 16] * (128 - len(rows))
print('#include <stdint.h>\nconst uint8_t wcs_font8x16[128][16] = {')
print(',\n'.join('    {' + ', '.join(f'0x{v:02X}' for v in row) + '}' for row in rows[:128]))
print('};')
PY

CC=$DKP/devkitPPC/bin/powerpc-eabi-gcc
# -O2 on purpose: retail code is optimised, and the suite should exercise the
# instruction mix (paired loads/stores, fmadd, record forms) that comes with it.
FLAGS="-O2 -g -std=gnu11 -Wall -Wextra -Werror -Wno-missing-field-initializers -mcpu=750 -meabi -mhard-float \
       -ffunction-sections -fdata-sections -DESPRESSO -D__WIIU__ -D__WUT__"
OBJECTS=
for source in "$HERE"/src/*.c "$BLOBS" "$GEN/font8x16.c"; do
    object=$OUT/obj/$(basename "$source" .c).o
    $CC $FLAGS -I"$DKP/wut/include" -I"$HERE/src" -c "$source" -o "$object"
    OBJECTS="$OBJECTS $object"
done
$CC $FLAGS -L"$DKP/wut/lib" -specs="$DKP/wut/share/wut.specs" $OBJECTS -o "$OUT/wcs.elf" -lwut -lm
"$DKP/tools/bin/elf2rpl" "$OUT/wcs.elf" "$GAME/code/wcs.rpx"

cat > "$GAME/meta/meta.xml" <<'XML'
<?xml version="1.0" encoding="utf-8"?>
<menu>
    <longname_en>WEMU RPX Test Suite</longname_en>
    <shortname_en>WEMU Suite</shortname_en>
    <publisher_en>WEMU</publisher_en>
    <title_version>1</title_version>
</menu>
XML
echo "OK -> $GAME/code/wcs.rpx"

if [ "$1" = "--install" ]; then
    DEST=$HERE/../../games/wemu-suite
    mkdir -p "$DEST/code" "$DEST/meta"
    cp "$GAME/code/wcs.rpx" "$DEST/code/"
    cp "$GAME/meta/meta.xml" "$DEST/meta/"
    mkdir -p "$DEST/content"  # no suite.cfg: the GUI launch is interactive
    echo "Installed -> $(cd "$DEST" && pwd)"
fi
