#!/bin/bash
# The krcompiler gate: every Linux configuration this host can reach.
#
# The repository's checks are presets, and AGENTS.local.md requires the matrix when
# a change crosses a boundary. This is that matrix as one command. The sanitizer
# preset is not in the default set because it is CPU-bound and slow; pass
# --sanitize to include it, which is what a boundary change deserves.
#
# A failing preset does not stop the others: the whole picture is more useful than
# the first error, and the exit code says whether anything failed.
#
# Usage: scripts/gate-linux.sh [--preset <name>] [--skip-tests] [--sanitize]
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1

# I2: build in the native mirror, never on the mount. This tree owns the mirror
# tool, so the default path is its own script; a caller can still override it.
IN_PLACE=0
for argument in "$@"; do
    if [ "$argument" = "--in-place" ]; then IN_PLACE=1; fi
done
case "$ROOT" in
    /mnt/*)
        if [ "$IN_PLACE" = "1" ]; then
            echo "gate-linux: building on $ROOT (--in-place): debug paths will carry the mount" >&2
        else
            REPO="$(basename "$ROOT")"
            MIRROR_TOOL="${KRC_MIRROR_TOOL:-$ROOT/scripts/kr-mirror.sh}"
            if [ ! -f "$MIRROR_TOOL" ]; then
                echo "gate-linux: $ROOT is a /mnt path and no mirror tool was found at '$MIRROR_TOOL'; set KRC_MIRROR_TOOL or pass --in-place" >&2
                exit 2
            fi
            bash "$MIRROR_TOOL" "$REPO" --wait 600 >&2
            MIRROR="$HOME/mirrors/$REPO"
            echo "gate-linux: source was $ROOT; building from the mirror at $MIRROR" >&2
            exec bash "$MIRROR/scripts/gate-linux.sh" "$@"
        fi
        ;;
esac

# The job count lives here because a test preset cannot compute one; the same rule
# is in scripts/gate-windows.ps1.
CORES="$(nproc 2>/dev/null || echo 4)"
JOBS=$(( CORES * 8 / 10 ))
[ "$JOBS" -lt 1 ] && JOBS=1

PRESETS="linux-gcc-debug linux-gcc-release linux-clang-debug"
SKIP_TESTS=0
while [ $# -gt 0 ]; do
    case "$1" in
        --preset) shift; PRESETS="${1:-}";;
        --skip-tests) SKIP_TESTS=1;;
        --sanitize) PRESETS="$PRESETS linux-sanitize";;
        *) echo "unknown option: $1" >&2; exit 2;;
    esac
    shift
done

failed=0
step() {
    echo "== $1"
    shift
    "$@"
    local code=$?
    if [ "$code" -ne 0 ]; then
        echo "   FAIL: exit $code"
        failed=1
        return 1
    fi
    return 0
}

for name in $PRESETS; do
    echo
    echo "== preset $name"
    step "$name configure" cmake --preset "$name" || continue
    step "$name build" cmake --build --preset "$name" --parallel "$JOBS" || continue
    [ "$SKIP_TESTS" -eq 1 ] && continue
    step "$name ctest" ctest --preset "$name" --output-on-failure --no-tests=error || continue
    # krc_tests is a binary, not a ctest row.
    config=Debug
    case "$name" in *release*) config=Release;; esac
    unit="build/$name/host/bin/$config/krc_tests"
    if [ -x "$unit" ]; then
        step "$name krc_tests" "$unit"
    else
        echo "   FAIL: $name wrote no krc_tests at $unit"
        failed=1
    fi
done

# The staged sysroot is what the install ships, and a header missing from it is
# invisible until a consumer includes one: stdarg.h was absent from both installed
# prefixes because the staging target only ran when named. Every source header
# must reach the stage.
echo
echo "== staged sysroot carries every source header"
stage="$ROOT/build/linux-gcc-debug/stage/sysroot"
if [ ! -d "$stage" ]; then
    echo "   FAIL: no staged sysroot at $stage"
    failed=1
else
    missing=""
    while IFS= read -r file; do
        rel="${file#$ROOT/sysroot/}"
        [ -e "$stage/$rel" ] || missing="$missing $rel"
    done <<EOF
$(find "$ROOT/sysroot" -type f \( -name '*.h' -o -name '*.hpp' \))
EOF
    if [ -n "$missing" ]; then
        echo "   FAIL: headers missing from the stage:$missing"
        failed=1
    else
        echo "   every source header is staged"
    fi
fi

echo
if [ "$failed" -ne 0 ]; then
    echo "GATE LINUX: FAIL"
    exit 1
fi
echo "GATE LINUX: PASS"
