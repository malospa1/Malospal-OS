#!/usr/bin/env bash
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/common.sh"

require_command curl
require_command sha256sum
require_command tar
require_command xz
archive="$PROJECT_ROOT/.cache/buildroot-$BUILDROOT_VERSION.tar.xz"
mkdir -p "$PROJECT_ROOT/.cache"

if [[ ! -f $archive ]]; then
    curl --fail --location --retry 3 --proto '=https' \
        "https://buildroot.org/downloads/buildroot-$BUILDROOT_VERSION.tar.xz" \
        --output "$archive.part"
    mv -- "$archive.part" "$archive"
fi
printf '%s  %s\n' "$BUILDROOT_SHA256" "$archive" | sha256sum --check --status || \
    fail "Checksum mismatch: $archive. Remove this archive and fetch again."

if [[ ! -d $BUILDROOT_DIR ]]; then
    staging=$(mktemp -d "$PROJECT_ROOT/.cache/extract.XXXXXX")
    trap 'rm -rf -- "$staging"' EXIT
    tar -xJf "$archive" -C "$staging"
    mv -- "$staging/buildroot-$BUILDROOT_VERSION" "$BUILDROOT_DIR"
fi
printf 'Buildroot %s is available at %s\n' "$BUILDROOT_VERSION" "$BUILDROOT_DIR"
