#!/usr/bin/env bash
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/common.sh"

require_command timeout
mkdir -p "$OUTPUT_DIR"
log="$OUTPUT_DIR/smoke.log"
if timeout 120s bash "$PROJECT_ROOT/scripts/run.sh" --smoke >"$log" 2>&1; then
    if grep -q '^MYOS_SMOKE_OK' "$log"; then
        printf 'PASS: Linux booted, myos-info ran, devices were mounted, and the VM shut down.\n'
        exit 0
    fi
fi
tail -n 60 "$log" >&2
fail "Boot test failed. Full log: $log"
