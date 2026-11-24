#!/bin/sh
# Refresh a native-ext4 mirror of a repository checkout, for Linux builds.
#
#   kr-mirror.sh <repo> [--source DIR] [--mirror-root DIR] [--wait SECONDS]
#
# A WSL build on /mnt/c carries host paths in its debug data and its timestamps
# are too coarse for an incremental build, so Linux gates build in a mirror and
# edit on Windows. The mirror is disposable: this script owns its contents and
# deletes whatever is not in the source. It is never committed.
#
# One mirror is refreshed by one process at a time. The lock is a file beside the
# mirror, taken with flock and held for the whole rsync, so a second agent either
# waits (--wait) or stops with a sentence naming the holder's pid -- never a
# half-written tree in the middle of another agent's build.
set -eu

repo=""
source_root=""
mirror_root="${HOME}/mirrors"
wait_s=0

while [ $# -gt 0 ]; do
    case "$1" in
        --source) source_root="${2:-}"; shift 2 ;;
        --mirror-root) mirror_root="${2:-}"; shift 2 ;;
        --wait) wait_s="${2:-}"; shift 2 ;;
        -h|--help)
            echo "usage: kr-mirror.sh <repo> [--source DIR] [--mirror-root DIR] [--wait SECONDS]" >&2
            exit 0 ;;
        -*) echo "kr-mirror: unknown option '$1'" >&2; exit 2 ;;
        *) repo="$1"; shift ;;
    esac
done

if [ -z "$repo" ]; then
    echo "kr-mirror: the repository name is required, e.g. kr-mirror.sh kr232" >&2
    exit 2
fi
if ! command -v rsync >/dev/null 2>&1; then
    echo "kr-mirror: rsync was not found on PATH" >&2
    exit 1
fi

# The default source is the Windows checkout of the same name. The Windows user
# is not the Linux user, so it is discovered rather than assumed: exactly one
# /mnt/c/Users/*/repos/<repo> must exist, or the caller has to say which.
if [ -z "$source_root" ]; then
    matches=$(ls -d /mnt/c/Users/*/repos/"$repo" 2>/dev/null || true)
    count=$(printf '%s\n' "$matches" | grep -c . || true)
    if [ "$count" -ne 1 ]; then
        echo "kr-mirror: could not pick a Windows checkout of '$repo' ($count candidates); pass --source DIR" >&2
        exit 1
    fi
    source_root="$matches"
fi
if [ ! -d "$source_root" ]; then
    echo "kr-mirror: source '$source_root' does not exist; pass --source" >&2
    exit 1
fi
case "$source_root" in
    /mnt/*) ;;
    *)
        echo "kr-mirror: source '$source_root' is already native; there is nothing to mirror" >&2
        exit 2 ;;
esac

mirror="$mirror_root/$repo"
lock="$mirror_root/$repo.lock"
mkdir -p "$mirror_root"

exec 9>"$lock"
if ! flock -w "$wait_s" 9; then
    holder=$(cat "$lock" 2>/dev/null || true)
    echo "kr-mirror: '$mirror' is being refreshed by another process (pid ${holder:-unknown}); pass --wait SECONDS to queue instead of failing" >&2
    exit 1
fi
echo "$$" > "$lock"

# A repository may need some of its ignored-at-first-glance trees: krdbg vendors
# its kdbg reader under third_party and cannot build without it, while kr32's
# third_party is a fetched SDL2 tree. So the exclusions are per repository, read
# from <source>/.kr-mirror-exclude, with only build/ and .git/ unconditional.
extra=""
if [ -f "$source_root/.kr-mirror-exclude" ]; then
    extra="$source_root/.kr-mirror-exclude"
fi

echo "kr-mirror: $source_root -> $mirror"
if [ -n "$extra" ]; then
    echo "kr-mirror: extra exclusions from $extra"
    # --checksum --no-times: compare content, and stamp a transferred file with the
    # current time. With -t the mirror keeps the source's mtime, so a refreshed
    # file can look older than the object built from its previous version and
    # ninja skips it -- which is how a stale krc kept rejecting .GLOBL after the
    # source had been fixed and synced.
    rsync -a --checksum --no-times --delete \
        --exclude 'build/' --exclude '.git/' --exclude '*.tmp' \
        --exclude-from "$extra" \
        "$source_root"/ "$mirror"/
else
    rsync -a --checksum --no-times --delete \
        --exclude 'build/' --exclude '.git/' --exclude '*.tmp' \
        --exclude 'third_party/' \
        "$source_root"/ "$mirror"/
fi

echo "kr-mirror: refreshed (lock held by $$)"
