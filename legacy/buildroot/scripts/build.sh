#!/usr/bin/env bash
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/common.sh"

action=${1:-build}
[[ $# -le 1 ]] || fail 'Usage: bash scripts/build.sh [configure|build|menuconfig|savedefconfig|legal-info]'
case "$action" in
    configure|build|menuconfig|savedefconfig|legal-info) ;;
    *) fail "Unknown action: $action" ;;
esac
require_linux
for cmd in make gcc g++ patch perl python3 rsync cpio unzip bc bison flex file wget; do
    require_command "$cmd"
done
export LC_ALL=C
bash "$PROJECT_ROOT/scripts/fetch.sh"

if [[ $action == configure ]]; then
    [[ ! -e $OUTPUT_DIR/.config ]] || fail 'Already configured. Use menuconfig, or a fresh output directory.'
    brmake myos_qemu_x86_64_defconfig
    exit 0
fi
if [[ ! -f $OUTPUT_DIR/.config ]]; then
    brmake myos_qemu_x86_64_defconfig
fi
case "$action" in
    build)
        # Reinstall local package sources on every incremental build.
        if grep -qx 'BR2_PACKAGE_MYOS_BASE=y' "$OUTPUT_DIR/.config"; then
            brmake myos-base-rebuild
        fi
        brmake
        printf '\nImages: %s/images\nRun: bash scripts/run.sh\n' "$OUTPUT_DIR"
        ;;
    menuconfig) brmake menuconfig ;;
    savedefconfig)
        brmake savedefconfig BR2_DEFCONFIG="$PROJECT_ROOT/configs/myos_qemu_x86_64_defconfig"
        ;;
    legal-info) brmake legal-info ;;
esac
