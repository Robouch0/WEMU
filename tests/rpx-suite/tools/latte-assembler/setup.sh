#!/bin/sh
# Fetches decaf-emu at a pinned commit and builds latte-assembler into
# tests/rpx-suite/tools/latte-assembler/build/. Usage: setup.sh [checkout-dir]
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=${1:-$HERE/build/decaf-emu}
COMMIT=e6c528a20a41c34e0f9eb91dd3da40f119db2dee
SUBMODULES="libraries/excmd libraries/cpp-peglib libraries/fmt libraries/cnl libraries/gsl-lite"

if [ ! -d "$SRC/.git" ]; then
    mkdir -p "$SRC"
    git -C "$SRC" init -q
    git -C "$SRC" remote add origin https://github.com/decaf-emu/decaf-emu.git
fi
git -C "$SRC" fetch -q --depth 1 origin "$COMMIT"
git -C "$SRC" checkout -q FETCH_HEAD
git -C "$SRC" submodule update --init --depth 1 $SUBMODULES

mkdir -p "$HERE/build"
python3 - "$SRC/tools/latte-assembler/src" "$HERE/build" <<'PY'
import sys
src, out = sys.argv[1], sys.argv[2]
# Drop the GLSL "compile" command and its glslang/spdlog includes.
source = open(f'{src}/main.cpp').read()
for include in ('#include "glsl_compiler.h"\n', '#include <glslang/Public/ShaderLang.h>\n', '#include <spdlog/spdlog.h>\n'):
    source = source.replace(include, '')
start = source.index('      if (options.has("compile")) {')
end = source.index('      } else if (options.has("assemble")) {')
open(f'{out}/main.cpp', 'w').write(source[:start] + '      if (false) {\n' + source[end:])
# The grammar hands comments over without their ';', but the metadata regexes
# require it, so every "; $KEY = value" line was silently ignored.
source = open(f'{src}/gfd.cpp').read()
patched = source.replace('";[[:space:]]*\\\\$', '";?[[:space:]]*\\\\$')
if patched.count(';?[[:space:]]') != 2:
    sys.exit('gfd.cpp comment regex patch did not apply')
open(f'{out}/gfd.cpp', 'w').write(patched)
PY
cmake -S "$HERE" -B "$HERE/build" -DCMAKE_BUILD_TYPE=Release -DDECAF_SOURCE_DIR="$SRC" > /dev/null
cmake --build "$HERE/build" -j"$(nproc)"
echo "OK -> $HERE/build/latte-assembler"
