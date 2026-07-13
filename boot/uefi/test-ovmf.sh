#!/bin/sh
# TocinBoot OVMF boot test (headless, ~25 s).
#
# Boots build/uefi/esp.img under qemu-system-x86_64 + OVMF and greps the
# serial log for:
#   1. "TocinBoot v0.1"          — the loader ran           (always required)
#   2. "=== TocinOS Booting ===" — kernel banner on COM1, i.e. the full
#      64-bit UEFI -> 32-bit protected-mode handoff worked  (required only
#      when KERNEL.ELF was staged on the image)
#
# The expected kernel banner (check 2) can be overridden for other kernels:
#   KERNEL_BANNER      — fixed string grepped in the serial log
#   KERNEL_BANNER_DESC — suffix of the PASS line (default: "32-bit handoff OK")
# e.g. `make test64` boots the ELF64 M2 stub and expects its 64-bit banner.
#
# Exit 0 iff every applicable check passed. Usage: test-ovmf.sh [esp.img]

set -u

HERE=$(dirname "$0")
IMG=${1:-$HERE/../../build/uefi/esp.img}
BUILD=$(dirname "$IMG")
LOG=$BUILD/serial.log
TIMEOUT=${TIMEOUT:-25}
BANNER=${KERNEL_BANNER:-"=== TocinOS Booting ==="}
BANNER_DESC=${KERNEL_BANNER_DESC:-"32-bit handoff OK"}

[ -f "$IMG" ] || { echo "FAIL: image not found: $IMG (run 'make img')"; exit 1; }

OVMF_CODE=
for c in /usr/share/OVMF/OVMF_CODE_4M.fd /usr/share/OVMF/OVMF_CODE.fd; do
    [ -f "$c" ] && OVMF_CODE=$c && break
done
OVMF_VARS=
for v in /usr/share/OVMF/OVMF_VARS_4M.fd /usr/share/OVMF/OVMF_VARS.fd; do
    [ -f "$v" ] && OVMF_VARS=$v && break
done
[ -n "$OVMF_CODE" ] && [ -n "$OVMF_VARS" ] || {
    echo "FAIL: OVMF firmware not found under /usr/share/OVMF"; exit 1; }

cp "$OVMF_VARS" "$BUILD/ovmf_vars.fd"

# Does the image carry a kernel? (Decides whether check 2 applies.)
HAVE_KERNEL=0
if mdir -i "$IMG" ::/EFI/TOCINOS/ 2>/dev/null | grep -qi 'KERNEL *ELF'; then
    HAVE_KERNEL=1
fi

echo "booting $IMG under OVMF ($OVMF_CODE), timeout ${TIMEOUT}s..."
timeout "$TIMEOUT" qemu-system-x86_64 \
    -machine pc -m 256 \
    -drive if=pflash,format=raw,readonly=on,file="$OVMF_CODE" \
    -drive if=pflash,format=raw,file="$BUILD/ovmf_vars.fd" \
    -drive format=raw,file="$IMG" \
    -display none -serial stdio -no-reboot \
    >"$LOG" 2>&1
# timeout(124) is the normal outcome: loader/kernel never exit on their own.

RC=0

if grep -q "TocinBoot v0.1" "$LOG"; then
    echo "PASS: loader banner 'TocinBoot v0.1' on serial"
else
    echo "FAIL: loader banner 'TocinBoot v0.1' missing (log: $LOG)"
    RC=1
fi

if [ "$HAVE_KERNEL" = 1 ]; then
    if grep -qF "$BANNER" "$LOG"; then
        echo "PASS: kernel banner '$BANNER' — $BANNER_DESC"
    else
        echo "FAIL: kernel banner missing — handoff did not reach kernel_main (log: $LOG)"
        RC=1
    fi
else
    echo "SKIP: no KERNEL.ELF on image — handoff check not applicable"
fi

exit $RC
