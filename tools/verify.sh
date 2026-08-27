#!/usr/bin/env bash
#
# Everything that can be checked without a human, in one command.
#
#     tools/verify.sh
#
# ---------------------------------------------------------------- the point
#
# Part of this file checks things the RELEASE job checks, and that is
# deliberate. It is the fleet's most expensive lesson: **a check that only ever
# runs in CI, after a tag, is a check that will catch you after the tag.** The
# bundle layout, the plist and the architectures can all be verified here in a
# second; the alternative is a failed release and a force-moved v0.1.0.
#
# The two that have actually bitten this fleet, both in repos started by
# copying another one -- which is exactly how this repo started:
#
#   * `CFBundleExecutable` carrying the PREVIOUS plugin's name, because a plist
#     template was copied. Nothing fails: the bundle assembles, the binary is
#     universal, `nm` finds the entry point, a probe renders a correct frame.
#     Then codesign says "code object is not signed at all" and mentions
#     nothing about a plist.
#
#   * A macOS build that is quietly arm64-only, because CMAKE_OSX_ARCHITECTURES
#     was set after the first target existed. The build log calls that a
#     success. Only `lipo` knows.
#
# ------------------------------------------------------- where the shaders are checked
#
# There is no glslc step here. Every shader in this plugin is concatenated from
# several strings at run time, so what a standalone compiler could be handed is
# not what the plugin runs -- and `astest` compiles the real assembled programs
# through a real driver in a real core-profile context on its way to every GL
# check. A shader that will not compile fails `--read` and `--identity` before
# it can reach a host, which is a stronger check than glslc and not a weaker
# one.
#
set -uo pipefail

cd "$(dirname "$0")/.."

PASS=0
FAIL=0
SKIP=0

ok()    { printf '  \033[32mok\033[0m    %s\n' "$1"; PASS=$((PASS+1)); }
bad()   { printf '  \033[31mFAIL\033[0m  %s\n' "$1"; FAIL=$((FAIL+1)); }
skip()  { printf '  \033[33mskip\033[0m  %s\n' "$1"; SKIP=$((SKIP+1)); }
head_() { printf '\n\033[1m%s\033[0m\n' "$1"; }

BUILD=build-verify
BUNDLE="$BUILD/Astronaught.bundle"
BINARY="$BUNDLE/Contents/MacOS/Astronaught"
ASTEST="$BUILD/astest"

#---------------------------------------------------------------------------
# A clean build, with the architectures that actually ship.
#
# Its own directory, so that a dev build left at -DCMAKE_OSX_ARCHITECTURES=arm64
# cannot be what gets verified -- which would pass the lipo check by testing the
# wrong binary.
#---------------------------------------------------------------------------
head_ "a clean universal build"

rm -rf "$BUILD"
if cmake -B "$BUILD" -DCMAKE_BUILD_TYPE=Release >/dev/null 2>&1 \
   && cmake --build "$BUILD" >/dev/null 2>&1; then
    ok "configures and builds from scratch"
else
    bad "the clean build failed -- rerun without the output suppressed"
    printf '\n%d passed, %d failed\n' "$PASS" "$FAIL"
    exit 1
fi

#---------------------------------------------------------------------------
# The release checks, done here where they are cheap.
#---------------------------------------------------------------------------
head_ "the bundle, as it would ship"

if [ -f "$BINARY" ]; then
    ok "the bundle has a binary at Contents/MacOS/Astronaught"
else
    bad "no binary at $BINARY"
fi

# ☠️ The plist against the binary on DISK, not against a constant. Comparing it
# to the string "Astronaught" would pass on a plist that says Astronaught while
# the file is called something else; comparing it to the file is the check that
# would have caught downpour's hardcoded CFBundleExecutable.
if [ -f "$BUNDLE/Contents/Info.plist" ]; then
    declared=$(/usr/libexec/PlistBuddy -c "Print :CFBundleExecutable" \
               "$BUNDLE/Contents/Info.plist" 2>/dev/null)
    onDisk=$(basename "$BINARY")
    if [ "$declared" = "$onDisk" ]; then
        ok "CFBundleExecutable ($declared) is the file that is actually there"
    else
        bad "CFBundleExecutable is '$declared' but the binary is '$onDisk' -- codesign will fail at release time with a message that mentions no plist"
    fi

    ident=$(/usr/libexec/PlistBuddy -c "Print :CFBundleIdentifier" \
            "$BUNDLE/Contents/Info.plist" 2>/dev/null)
    case "$ident" in
        com.stoatworks.ffgl.astronaught) ok "CFBundleIdentifier is $ident" ;;
        *) bad "CFBundleIdentifier is '$ident' -- a copied identifier collides with the donor plugin" ;;
    esac
else
    bad "no Info.plist in the bundle"
fi

# ☠️ lipo, never the build log. A build that is quietly single-architecture
# reports success.
if [ -f "$BINARY" ]; then
    archs=$(lipo -archs "$BINARY" 2>/dev/null)
    if [[ "$archs" == *arm64* && "$archs" == *x86_64* ]]; then
        ok "universal: $archs"
    else
        bad "not universal -- lipo says '$archs'. Resolume ships both builds."
    fi

    # ☠️ The plugin registers itself from a file-scope constructor that nothing
    # references by name. In a STATIC archive the linker may drop the whole
    # translation unit, giving a bundle that loads, exports plugMain, and
    # reports that it contains no plugins. astronaught_core is an OBJECT
    # library for this reason; these two checks are what would notice if it
    # stopped being one.
    # ☠️ Every one of these captures its output into a variable first, rather
    # than piping into `grep -q`. Under `set -o pipefail` a `grep -q` that
    # matches EARLY exits immediately, the producer is killed by SIGPIPE, and
    # the pipeline's status is that signal -- so the check reports a failure
    # precisely when the string it is looking for is present and near the top.
    # It cost twenty minutes here: `strings | grep -q` failed on two strings
    # that were plainly in the binary, while `nm | grep -q` passed on the line
    # above only because nm's output is small enough to fit the pipe buffer
    # before grep can exit. That difference is what made it look like a linker
    # problem rather than a shell one.
    symbols=$(nm -gU "$BINARY" 2>/dev/null)
    text=$(strings "$BINARY" 2>/dev/null)

    if printf '%s' "$symbols" | grep -q "_plugMain"; then
        ok "exports _plugMain"
    else
        bad "no _plugMain -- the host will dlopen this and find nothing"
    fi

    if printf '%s' "$text" | grep -q "Astronaught FFGL effect"; then
        ok "the CFFGLPluginInfo survived the link"
    else
        bad "the plugin registration was dropped -- is astronaught_core still an OBJECT library?"
    fi

    if printf '%s' "$text" | grep -q "^astronaught 0\."; then
        ok "carries a build stamp ($(printf '%s' "$text" | grep -m1 '^astronaught 0\.'))"
    else
        bad "no build stamp -- PluginEntry.cpp is not in the bundle target"
    fi
fi

#---------------------------------------------------------------------------
# The manifest nothing points at.
#
# CMakeLists.txt says find_package(GLEW REQUIRED) on non-Apple, and GLEW
# arrives through vcpkg.json -- which nothing in CMakeLists mentions. Delete it
# and every macOS build stays green while the Windows job fails at CONFIGURE.
#---------------------------------------------------------------------------
head_ "the Windows build's dependencies"

if [ -f vcpkg.json ] && grep -q glew vcpkg.json; then
    ok "vcpkg.json is present and names GLEW"
else
    bad "vcpkg.json is missing or does not name GLEW -- the Windows job fails at configure and no macOS build notices"
fi

#---------------------------------------------------------------------------
# The model and the picture.
#---------------------------------------------------------------------------
head_ "the machine"

if [ ! -x "$ASTEST" ]; then
    bad "astest was not built"
else
    for check in modes ratios delay rate drag chorus names read identity presets guard; do
        if "$ASTEST" "--$check" >/tmp/astest.$check.log 2>&1; then
            ok "--$check"
        else
            bad "--$check -- see /tmp/astest.$check.log"
        fi
    done
fi

#---------------------------------------------------------------------------
# Dead controls.
#
# Against the binary just built, not against ./build -- sweeping a different
# build from the one being verified is the kind of mistake that only shows up
# much later.
#---------------------------------------------------------------------------
head_ "every control reaches the picture"

if [ ! -x "$ASTEST" ]; then
    skip "astest was not built"
elif ASTEST="$ASTEST" python3 tools/sweep.py >/tmp/astest.sweep.log 2>&1; then
    ok "$(tail -1 /tmp/astest.sweep.log)"
else
    bad "dead controls -- $(grep 'DEAD CONTROLS' /tmp/astest.sweep.log || echo 'see /tmp/astest.sweep.log')"
fi

#---------------------------------------------------------------------------
head_ "result"
printf '  %d passed, %d failed, %d skipped\n\n' "$PASS" "$FAIL" "$SKIP"
[ "$FAIL" -eq 0 ]
