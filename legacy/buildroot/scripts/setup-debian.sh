#!/usr/bin/env bash
set -euo pipefail

[[ $(uname -s) == Linux ]] || { printf 'This script requires Debian or Ubuntu Linux.\n' >&2; exit 1; }
[[ $(id -u) == 0 ]] || { printf 'Run: sudo bash scripts/setup-debian.sh\n' >&2; exit 1; }
. /etc/os-release
case "$ID" in
    debian|ubuntu) ;;
    *) printf 'Supported build hosts: Debian and Ubuntu.\n' >&2; exit 1 ;;
esac

apt-get update
apt-get install -y --no-install-recommends \
    build-essential bc bison flex cpio file git rsync unzip wget curl \
    ca-certificates xz-utils patch perl python3 libncurses-dev \
    qemu-system-x86
