#!/usr/bin/env bash
# Sunmi V2 T5930 — Apply lk-seccfg-shortcut patch and flash to device.
# Requires: mtkclient + Sunmi V2 T5930 in BROM mode (0e8d:0003)
#
# Verifies SHA256 before flashing to prevent accidents.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MTKCLIENT_DIR="$REPO_ROOT/tools/mtkclient"
PATCHED_LK="$REPO_ROOT/scratch/lk-patch/lk-seccfg-shortcut.img"
ORIGINAL_LK="$REPO_ROOT/dump/lk.img"

EXPECTED_PATCHED_SHA="4d70cd923678b952f733424832f43cc532c1b301781440817afdc2b9460c1381"
EXPECTED_ORIGINAL_SHA="fa9e3290118ed58d331d41a37050f59e9eeab203f570487f8d8c8e022a860926"

usage() {
    cat <<EOF
Usage: $0 <apply|rollback>

  apply     Flash patched LK (lk-seccfg-shortcut.img) to device
  rollback  Flash original stock LK (dump/lk.img) to device

Preconditions:
  - Device is in BROM mode (0e8d:0003 visible via lsusb)
  - mtkclient venv available at $MTKCLIENT_DIR/.venv
  - This script must be run with sudo (raw USB access)
EOF
    exit 1
}

verify_sha() {
    local file="$1"
    local expected="$2"
    local actual
    actual=$(sha256sum "$file" | awk '{print $1}')
    if [[ "$actual" != "$expected" ]]; then
        echo "SHA256 mismatch for $file"
        echo "  expected: $expected"
        echo "  actual:   $actual"
        exit 2
    fi
    echo "SHA256 OK: $file"
}

check_device() {
    if ! lsusb | grep -q "0e8d:0003"; then
        echo "Device not in BROM mode (0e8d:0003 not found in lsusb)"
        echo "Enter BROM mode: power off device, then connect USB while holding no buttons"
        exit 3
    fi
    echo "Device detected in BROM mode"
}

flash_lk() {
    local img="$1"
    check_device
    cd "$MTKCLIENT_DIR"
    exec .venv/bin/python -m mtkclient w lk "$img" --stock
}

case "${1:-}" in
    apply)
        verify_sha "$PATCHED_LK" "$EXPECTED_PATCHED_SHA"
        echo ""
        echo "About to flash: $PATCHED_LK"
        echo "This applies the seccfg-shortcut 2-byte patch."
        echo ""
        read -p "Continue? [y/N] " confirm
        [[ "$confirm" == "y" || "$confirm" == "Y" ]] || exit 0
        flash_lk "$PATCHED_LK"
        ;;
    rollback)
        verify_sha "$ORIGINAL_LK" "$EXPECTED_ORIGINAL_SHA"
        echo ""
        echo "About to flash: $ORIGINAL_LK (stock)"
        echo ""
        read -p "Continue? [y/N] " confirm
        [[ "$confirm" == "y" || "$confirm" == "Y" ]] || exit 0
        flash_lk "$ORIGINAL_LK"
        ;;
    *)
        usage
        ;;
esac
