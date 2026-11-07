#!/usr/bin/env bash
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/common.sh"

[[ $# -le 1 ]] || fail 'Usage: bash scripts/run.sh [--smoke]'
append='root=/dev/vda rw console=ttyS0,115200'
case "${1:-}" in
    '') ;;
    --smoke) append+=' myos.smoke=1' ;;
    *) fail "Unknown option: $1" ;;
esac
require_command qemu-system-x86_64
for name in bzImage rootfs.ext4; do
    [[ -s $OUTPUT_DIR/images/$name ]] || fail "Missing $name. Run bash scripts/build.sh first."
done
[[ $OUTPUT_DIR != *,* ]] || fail 'QEMU image paths must not contain commas.'

exec qemu-system-x86_64 \
    -machine pc -accel tcg -cpu qemu64 -m 256M -smp 2 \
    -kernel "$OUTPUT_DIR/images/bzImage" \
    -drive "file=$OUTPUT_DIR/images/rootfs.ext4,if=virtio,format=raw" \
    -append "$append" \
    -display none -monitor none -serial stdio \
    -nic none -snapshot -no-reboot
