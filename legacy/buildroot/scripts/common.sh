#!/usr/bin/env bash

PROJECT_ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
# This is a version-controlled file, never data from a remote server.
source "$PROJECT_ROOT/buildroot.lock"
BUILDROOT_DIR="$PROJECT_ROOT/.cache/buildroot-$BUILDROOT_VERSION"
OUTPUT_DIR="$PROJECT_ROOT/output"

fail() { printf 'Error: %s\n' "$*" >&2; exit 1; }
require_command() { command -v "$1" >/dev/null 2>&1 || fail "Missing command: $1"; }

require_linux() {
    [[ $(uname -s) == Linux ]] || fail 'Build on Linux or WSL2. See README.md.'
    [[ $(id -u) != 0 ]] || fail 'Run the build as a regular user, without sudo.'
    [[ $PROJECT_ROOT != *[[:space:]]* ]] || fail 'The project path must not contain whitespace.'
    case "$(stat -f -c %T "$PROJECT_ROOT")" in
        9p|drvfs|ntfs|fuseblk)
            fail 'Copy the project into the Linux filesystem (for example ~/src/myos).'
            ;;
    esac
}

brmake() {
    make -C "$BUILDROOT_DIR" O="$OUTPUT_DIR" BR2_EXTERNAL="$PROJECT_ROOT" "$@"
}
