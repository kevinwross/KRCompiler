#!/bin/sh
# Report the tools a repository's gates need, with the version each one resolved
# to, and stop before the first compile if one is missing.
#
#   doctor.sh <repo> [--need krc,krld]
#
# Every gate calls this first (J5). The value is not the list -- it is that a
# missing tool fails with a sentence naming the tool and the repository that
# needs it, instead of a compiler error twenty lines into a build.
set -u

repo="${1:-unknown}"
shift 2>/dev/null || true
extra=""
for argument in "$@"; do
    case "$argument" in
        --need=*|--need) if [ "$argument" = "--need" ]; then shift; extra="${1:-}"; else extra="${argument#--need=}"; fi ;;
    esac
done

# The tools every repository's gate needs, and what each is for.
missing=0
report() {
    tool="$1"
    why="$2"
    path="$(command -v "$tool" 2>/dev/null || true)"
    if [ -z "$path" ]; then
        echo "doctor: $repo needs '$tool' ($why) and it is not on PATH" >&2
        missing=$((missing + 1))
        return
    fi
    version="$("$tool" --version 2>/dev/null | head -1)"
    if [ -z "$version" ]; then version="$("$tool" -v 2>/dev/null | head -1)"; fi
    printf '  %-10s %-40s %s\n' "$tool" "${version:-unknown version}" "$path"
}

echo "doctor: $repo"
report cmake "configuring the build"
report ninja "the gate generator"
cc="$(command -v cc 2>/dev/null || command -v gcc 2>/dev/null || command -v cl 2>/dev/null || true)"
if [ -z "$cc" ]; then
    echo "doctor: $repo needs a C compiler (cc, gcc or cl) and none is on PATH" >&2
    missing=$((missing + 1))
else
    printf '  %-10s %-40s %s\n' "c compiler" "$("$cc" --version 2>/dev/null | head -1)" "$cc"
fi
report rsync "refreshing the Linux mirror"
node="$(command -v node 2>/dev/null || true)"
if [ -n "$node" ]; then
    printf '  %-10s %-40s %s\n' node "$("$node" --version 2>/dev/null)" "$node"
fi

# Tools a repository names explicitly, e.g. --need krc,krld for the guest builds.
if [ -n "$extra" ]; then
    old_ifs="$IFS"
    IFS=','
    for tool in $extra; do
        IFS="$old_ifs"
        report "$tool" "building guest code"
        IFS=','
    done
    IFS="$old_ifs"
fi

if [ "$missing" -ne 0 ]; then
    echo "doctor: $repo is missing $missing tool(s); nothing was built" >&2
    exit 1
fi
echo "doctor: $repo has every tool it needs"
