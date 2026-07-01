#!/usr/bin/env bash
# Sunmi V2 T5930 — Apply/rollback the preloader SBC-disable patch.
#
# ★★★ WARNING ★★★
# Flashing preloader is IRREVERSIBLE if BROM cannot be re-entered.
# Ensure the following BEFORE running "apply":
#   - dump/preloader-boot0.img is a verified copy of the STOCK preloader
#     (SHA256 must match EXPECTED_ORIGINAL_SHA below)
#   - You have physically tested BROM re-entry (unplug USB, plug back in,
#     lsusb should show 0e8d:0003)
#   - mtkclient handshake is confirmed working NOW (not in a stale state)

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MTKCLIENT_DIR="$REPO_ROOT/tools/mtkclient"
PATCHED_IMG="$REPO_ROOT/scratch/preloader-patch/preloader-sbc-disable.img"
STOCK_IMG="$REPO_ROOT/dump/preloader-boot0.img"

EXPECTED_PATCHED_SHA="025256e4e129019374b7aa9fce6a9d965291ed35f02c88baa7a47c5675b33b5d"
EXPECTED_ORIGINAL_SHA="10d33a52ce7ea88269dccea15cfb3180721b72070bd2a4eb96c1e3f5e86d6424"

usage() {
    cat <<EOF
Usage: $0 <apply|rollback|verify>

  apply     Flash patched preloader (SBC-disable)
  rollback  Flash stock preloader (revert)
  verify    Only check SHA256 of both files, no flash

Preconditions (apply/rollback):
  - Device is in BROM mode (0e8d:0003 visible via lsusb)
  - mtkclient BROM handshake is currently working
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
        echo "Enter BROM mode: power off device, then USB connect (no buttons)"
        exit 3
    fi
    echo "Device detected in BROM mode (0e8d:0003)"
}

confirm_apply() {
    cat <<WARN

============================================================
WARNING: PRELOADER FLASH IS IRREVERSIBLE ON BOOTLOOP
============================================================

If this patched preloader does not boot, and BROM does not
re-enter, the device is BRICKED.

Preflash checklist:
  [ ] dump/preloader-boot0.img verified as stock (see 'verify' subcommand)
  [ ] BROM confirmed working NOW via handshake test
  [ ] Physical unplug/replug tested for BROM re-entry
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
    # Preloader is on boot0 partition; mtkclient's w preloader writes there
    exec .venv/bin/python -m mtkclient w preloader "$img" --stock
}

case "${1:-}" in
    verify)
        echo "=== stock preloader ==="
        verify_sha "$STOCK_IMG" "$EXPECTED_ORIGINAL_SHA" || true
        echo "=== patched preloader ==="
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
This should be safe if BROM handshake is currently working.
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
