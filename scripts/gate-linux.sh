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

echo
if [ "$failed" -ne 0 ]; then
    echo "GATE LINUX: FAIL"
    exit 1
fi
echo "GATE LINUX: PASS"
