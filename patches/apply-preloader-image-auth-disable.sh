#!/usr/bin/env bash
# Sunmi V2 T5930 — Apply/rollback the preloader image-auth-disable patch (v2)
#
# ★★★ WARNING ★★★
# Flashing preloader is IRREVERSIBLE if BROM cannot be re-entered.
# See patches/preloader-image-auth-disable.patch for full analysis.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MTKCLIENT_DIR="$REPO_ROOT/tools/mtkclient"
PATCHED_IMG="$REPO_ROOT/scratch/preloader-patch/preloader-image-auth-disable.img"
STOCK_IMG="$REPO_ROOT/dump/preloader-boot0.img"

EXPECTED_PATCHED_SHA="f826ed14a5fa52e96b54cfb083e4af45682a54b89c058779d82d32ec8cbac6c0"
EXPECTED_ORIGINAL_SHA="10d33a52ce7ea88269dccea15cfb3180721b72070bd2a4eb96c1e3f5e86d6424"

usage() {
    cat <<EOF
Usage: $0 <apply|rollback|verify>

  apply     Flash patched preloader (fcn.0021277c + FUN_0020f9b0 both patched, 8 bytes)
  rollback  Flash stock preloader (revert)
  verify    SHA256 check only

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

confirm_apply() {
    cat <<WARN

============================================================
WARNING: PRELOADER FLASH IS IRREVERSIBLE ON BOOTLOOP
============================================================

This is the v2 image-auth-disable patch (Track F).
Skips ALL preloader image_auth (LK RSA verify etc).

If bootloop and BROM does not re-enter, device is BRICKED.

Preflash checklist:
  [ ] dump/preloader-boot0.img verified as stock
  [ ] BROM confirmed working NOW via mtkclient
  [ ] Physical unplug/replug tested for BROM re-entry
  [ ] Track D patched LK also ready (needed for full boot)
  [ ] You have accepted the risk

Type 'I ACCEPT THE RISK' to proceed, anything else to abort:
WARN
    read -r confirmation
    if [[ "$confirmation" != "I ACCEPT THE RISK" ]]; then
        echo "Aborted."
        exit 0
    fi
}

flash() {
    local img="$1"
    check_device
    cd "$MTKCLIENT_DIR"
    exec .venv/bin/python -m mtkclient w preloader "$img" --stock
}

case "${1:-}" in
    verify)
        echo "=== stock preloader ==="
        verify_sha "$STOCK_IMG" "$EXPECTED_ORIGINAL_SHA" || true
        echo "=== patched preloader (v2) ==="
        verify_sha "$PATCHED_IMG" "$EXPECTED_PATCHED_SHA" || true
        ;;
    apply)
        verify_sha "$PATCHED_IMG" "$EXPECTED_PATCHED_SHA"
        verify_sha "$STOCK_IMG" "$EXPECTED_ORIGINAL_SHA"
        confirm_apply
        flash "$PATCHED_IMG"
        ;;
    rollback)
        verify_sha "$STOCK_IMG" "$EXPECTED_ORIGINAL_SHA"
        cat <<WARN
Restoring stock preloader from $STOCK_IMG
Proceed? [y/N]:
WARN
        read -r confirmation
        [[ "$confirmation" == "y" || "$confirmation" == "Y" ]] || exit 0
        flash "$STOCK_IMG"
        ;;
    *)
        usage
        ;;
esac
