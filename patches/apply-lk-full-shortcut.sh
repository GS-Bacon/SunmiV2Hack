#!/usr/bin/env bash
# Sunmi V2 T5930 — Apply/rollback the LK full-shortcut patch (v2)
# Contains Track D (seccfg) + Track G (SEC_POLICY wrapper x2) = 10 bytes total
#
# LK flash is reversible via mtkclient as long as BROM is accessible.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MTKCLIENT_DIR="$REPO_ROOT/tools/mtkclient"
PATCHED_IMG="$REPO_ROOT/scratch/lk-patch/lk-full-shortcut.img"
STOCK_IMG="$REPO_ROOT/dump/lk.img"

EXPECTED_PATCHED_SHA="c33e893a10f6c38128aacef2912954b1ce737c67993d07af04b262823a28ec50"
EXPECTED_ORIGINAL_SHA="fa9e3290118ed58d331d41a37050f59e9eeab203f570487f8d8c8e022a860926"

usage() {
    cat <<EOF
Usage: $0 <apply|rollback|verify>

  apply     Flash patched LK (seccfg + SEC_POLICY wrappers short-circuited, 10 bytes)
  rollback  Flash stock LK (revert)
  verify    SHA256 check only, no flash

Preconditions (apply/rollback):
  - Device is in BROM mode (0e8d:0003 visible via lsusb)
  - mtkclient BROM handshake currently working
  - Running with sudo (raw USB access)
EOF
    exit 1
}

verify_sha() {
    local file="$1"
    local expected="$2"
    if [[ ! -f "$file" ]]; then
        echo "MISSING: $file"
        return 1
    fi
    local actual
    actual=$(sha256sum "$file" | awk '{print $1}')
    if [[ "$actual" != "$expected" ]]; then
        echo "SHA256 MISMATCH for $file"
        echo "  expected: $expected"
        echo "  actual:   $actual"
        return 2
    fi
    echo "SHA256 OK: $file"
    return 0
}

check_device() {
    if ! lsusb | grep -q "0e8d:0003"; then
        echo "ERROR: Device not in BROM mode (0e8d:0003 not found)"
        exit 3
    fi
    echo "Device detected in BROM mode (0e8d:0003)"
}

flash() {
    local img="$1"
    check_device
    cd "$MTKCLIENT_DIR"
    exec .venv/bin/python -m mtkclient w lk "$img" --stock
}

case "${1:-}" in
    verify)
        echo "=== stock lk.img ==="
        verify_sha "$STOCK_IMG" "$EXPECTED_ORIGINAL_SHA" || true
        echo "=== patched lk.img (v2) ==="
        verify_sha "$PATCHED_IMG" "$EXPECTED_PATCHED_SHA" || true
        ;;
    apply)
        verify_sha "$PATCHED_IMG" "$EXPECTED_PATCHED_SHA"
        verify_sha "$STOCK_IMG" "$EXPECTED_ORIGINAL_SHA"
        echo ""
        echo "About to flash: $PATCHED_IMG"
        echo "Contains Track D + Track G patches (10 bytes total)."
        echo ""
        read -p "Continue? [y/N] " confirm
        [[ "$confirm" == "y" || "$confirm" == "Y" ]] || exit 0
        flash "$PATCHED_IMG"
        ;;
    rollback)
        verify_sha "$STOCK_IMG" "$EXPECTED_ORIGINAL_SHA"
        echo ""
        echo "About to flash stock: $STOCK_IMG"
        read -p "Continue? [y/N] " confirm
        [[ "$confirm" == "y" || "$confirm" == "Y" ]] || exit 0
        flash "$STOCK_IMG"
        ;;
    *)
        usage
        ;;
esac
