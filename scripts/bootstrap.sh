#!/bin/sh
# Configure, build and install KRCompiler into a named prefix.
#
# The bootstrap for a machine that has no compiler: given a source tree and a
# prefix it leaves a working `krc` whose --version reports the build it came
# from, and stops at the first failure. It does not install CMake, a compiler or
# a generator; it refuses to run without them.
#
#   ./scripts/bootstrap.sh --prefix /opt/krc [--config Debug] [--build build/bootstrap]
set -eu

prefix=""
config="Debug"
build="build/bootstrap"
while [ $# -gt 0 ]; do
    case "$1" in
        --prefix) prefix="${2:-}"; shift 2 ;;
        --config) config="${2:-}"; shift 2 ;;
        --build) build="${2:-}"; shift 2 ;;
        *) echo "bootstrap: unknown argument '$1'" >&2; exit 2 ;;
    esac
done
if [ -z "$prefix" ]; then
    echo "bootstrap: --prefix is required; a default would scatter prefixes" >&2
    exit 2
fi
case "$config" in
    Debug|Release) ;;
    *) echo "bootstrap: --config must be Debug or Release, not '$config'" >&2; exit 2 ;;
esac

command -v cmake >/dev/null 2>&1 || {
    echo "bootstrap: cmake was not found on PATH" >&2
    exit 1
}
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
echo "bootstrap: $root -> $prefix ($config)"

cmake -S "$root" -B "$root/$build" -G Ninja -DCMAKE_BUILD_TYPE="$config"
cmake --build "$root/$build"
cmake --install "$root/$build" --prefix "$prefix"

if [ ! -x "$prefix/bin/krc" ]; then
    echo "bootstrap: install reported success but $prefix/bin/krc does not exist" >&2
    exit 1
fi
"$prefix/bin/krc" --version
if [ ! -f "$prefix/lib/cmake/krc/krc-stamp.cmake" ]; then
    echo "bootstrap: no stamp under $prefix/lib/cmake/krc" >&2
    exit 1
fi
echo "bootstrap: done, prefix describes itself as:"
cat "$prefix/lib/cmake/krc/krc-stamp.cmake"
