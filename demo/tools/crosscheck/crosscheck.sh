#!/usr/bin/env bash
#
# The browser demo's JavaScript port of the CPU half, against the plugin's C++.
#
#     demo/tools/crosscheck/crosscheck.sh        exit 1 on any difference
#
# uniforms.cpp drives the REAL plugin class (Astronaught.cpp and everything it
# calls, with the FFGL SDK) in a headless GL 4.1 context through scenario.mjs's
# frames, and after every frame prints its tape position, stride and slot table,
# every parameter, and every uniform of its four programs read back out of GL.
# frames.mjs drives demo/port.js -- the module the page imports -- through the
# same frames and prints the same. compare.py holds the two together: integers
# and parameters exactly, floats to a relative tolerance it states.
#
# The C++ is built twice. -ffp-contract=off is the arithmetic of the plugin's
# x86_64 slice, which has no fused multiply-add and is the arithmetic
# JavaScript has; the native build is what Apple Silicon runs, where clang fuses
# a*b+c. Both must pass. Neither can share JavaScript's libm, so the tape
# position, which integrates sinf and friends over thousands of frames, is the
# value that moves most; compare.py reports how far.
#
# Run by tools/verify.sh. Needs node, a C++ compiler, the FFGL submodule and an
# OpenGL context (the plugin's shaders are compiled and run for real).
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../../.." && pwd)"
CXX="${CXX:-c++}"

command -v node >/dev/null 2>&1 || { echo "crosscheck: node is not installed"; exit 2; }
[[ -f "$REPO/external/ffgl/source/lib/FFGLSDK.cpp" ]] || { echo "crosscheck: FFGL SDK missing -- git submodule update --init --recursive"; exit 2; }

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

version="$( sed -n 's/^[[:space:]]*VERSION \([0-9.]*\)$/\1/p' "$REPO/CMakeLists.txt" | head -1 )"
sources=( "$REPO"/source/Diag.cpp "$REPO"/source/Heads.cpp "$REPO"/source/Transport.cpp "$REPO"/source/Tape.cpp
          "$REPO"/source/Spring.cpp "$REPO"/source/Controls.cpp "$REPO"/source/PassBuffer.cpp "$REPO"/source/Store.cpp
          "$REPO"/source/shaders/Vertex.cpp "$REPO"/source/shaders/Tape.cpp "$REPO"/source/shaders/Medium.cpp
          "$REPO"/source/shaders/Passes.cpp "$REPO"/source/Astronaught.cpp "$REPO/external/ffgl/source/lib/FFGLSDK.cpp" )
flags=( -std=c++17 -O2 -DGL_SILENCE_DEPRECATION -DASTRONAUGHT_VERSION="\"$version\"" -I "$REPO/source"
        -I "$REPO/external/ffgl/source/lib" -framework OpenGL -framework AppKit -w )

"$CXX" -ffp-contract=off "${flags[@]}" "$HERE/uniforms.cpp" "${sources[@]}" -o "$tmp/uniforms-strict"
"$CXX" "${flags[@]}" "$HERE/uniforms.cpp" "${sources[@]}" -o "$tmp/uniforms-native"

node "$HERE/scenario.mjs" > "$tmp/scenario.txt"
node "$HERE/frames.mjs" < "$tmp/scenario.txt" > "$tmp/js.txt"

rc=0
for build in strict native; do
	"$tmp/uniforms-$build" < "$tmp/scenario.txt" > "$tmp/cpp-$build.txt"
	echo "plugin built ${build/strict/-ffp-contract=off} against the port:"
	python3 "$HERE/compare.py" "$tmp/cpp-$build.txt" "$tmp/js.txt" | sed 's/^/  /' || rc=1
done
exit $rc
