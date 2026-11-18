# gameOS A/B slot selector (compiled to boot.scr by post-image.sh).
#
# The boot partition keeps both kernels under /a and /b. /state holds two
# env lines, slot=a|b and n=<unconfirmed boots>. n is bumped on every boot
# and reset to 0 by S99update once the UI is actually running; four bumps
# without a reset mean the new slot never came up, so we roll back to A.
# Rewriting /state before booti also counts boots where the kernel hangs.
#
# The state file is ordinary text, so a lost or corrupt file just restarts
# at slot A, and userspace can inspect it after a failed update.

setenv staddr 0x00d00000
if fatload mmc 0:1 ${staddr} /state; then env import -t ${staddr} ${filesize}; fi
if test "${slot}" = ""; then setenv slot a; fi
if test "${n}" = ""; then setenv n 0; fi
setexpr n ${n} + 1
if test ${n} -gt 3; then
    setenv slot a
    setenv n 0
    echo "gameOS: 4 boots without confirmation - rolling back to slot A"
fi
setenv rootpart GAMEOS-ROOT-A
if test "${slot}" = "b"; then setenv rootpart GAMEOS-ROOT-B; fi
echo "gameOS: slot ${slot} try ${n} root=${rootpart}"
env export -t ${staddr} slot n
fatwrite mmc 0:1 ${staddr} /state ${filesize}
fatload mmc 0:1 ${kernel_addr_r} /${slot}/Image
fatload mmc 0:1 ${fdt_addr_r} /${slot}/rk3568-anbernic-rg-ds-plus.dtb
setenv bootargs root=PARTLABEL=${rootpart} rootwait rw console=ttyS2,1500000n8 loglevel=7 vt.global_cursor_default=0
booti ${kernel_addr_r} - ${fdt_addr_r}
