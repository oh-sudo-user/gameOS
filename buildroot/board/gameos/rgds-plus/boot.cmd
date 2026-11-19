# gameOS A/B slot selector (compiled to boot.scr by post-build.sh).
#
# The boot partition keeps both kernels under /a and /b. /state holds three
# env lines: slot=a|b (next slot to try), fallback=a|b (last slot confirmed
# good by userspace) and n=<unconfirmed boots of the current slot>. n is
# bumped on every boot and reset to 0 by S99update once the UI is really
# running; four bumps without a reset mean the tried slot never came up, so
# we roll back to the fallback slot -- NOT a fixed slot A. Rewriting /state
# before booti also counts boots where the kernel hangs.
#
# The state file is ordinary text, so a lost or corrupt file just restarts
# at slot A with fallback A, and userspace can inspect it after a failed
# update. An old two-line state (slot/n, no fallback) defaults fallback to A,
# which is exactly the pre-upgrade slot when the updater wrote B from A.
#
# @DTB@ is replaced per model by post-build.sh. The mmc device is probed:
# on RG DS the eMMC can enumerate as mmc0, pushing the SD card to mmc1.
#
# Syntax is limited to what U-Boot v2026.01 (HUSH_OLD_PARSER) provides:
# `test` with -a (cmd/test.c OP_AND), env import/export -t (cmd/nvedit.c,
# export sets ${filesize}), setexpr (CONFIG_CMD_SETEXPR), fatwrite
# (CONFIG_FAT_WRITE). Verified against the built source and a sandbox run.

setenv staddr 0x00d00000
setenv mmcdev 0
if fatls mmc 0:1 /a; then setenv mmcdev 0; elif fatls mmc 1:1 /a; then setenv mmcdev 1; fi
if fatload mmc ${mmcdev}:1 ${staddr} /state; then env import -t ${staddr} ${filesize}; fi
if test "${slot}" != "a" -a "${slot}" != "b"; then setenv slot a; fi
if test "${fallback}" != "a" -a "${fallback}" != "b"; then setenv fallback a; fi
if test "${n}" = ""; then setenv n 0; fi
# Normalise n: a valid non-negative counter is incremented; anything else
# (empty handled above, garbage, negative) restarts at 1 so a corrupt state
# can never disable the rollback counter.
if test "${n}" -ge 0; then setexpr n ${n} + 1; else setenv n 1; fi
if test ${n} -gt 3; then
    echo "gameOS: slot ${slot} unconfirmed; rolling back to ${fallback}"
    # Boot the last confirmed-good slot. fallback already names that slot and
    # stays unchanged: the failed slot is NOT confirmed good, so it must not
    # become the new fallback.
    setenv slot ${fallback}
    setenv n 0
fi
setenv rootpart GAMEOS-ROOT-A
if test "${slot}" = "b"; then setenv rootpart GAMEOS-ROOT-B; fi
echo "gameOS: slot=${slot} fallback=${fallback} try=${n} root=${rootpart} mmc=${mmcdev}"
env export -t ${staddr} slot fallback n
fatwrite mmc ${mmcdev}:1 ${staddr} /state ${filesize}
fatload mmc ${mmcdev}:1 ${kernel_addr_r} /${slot}/Image
fatload mmc ${mmcdev}:1 ${fdt_addr_r} /${slot}/@DTB@
setenv bootargs root=PARTLABEL=${rootpart} rootwait rw console=ttyS2,1500000n8 loglevel=7 vt.global_cursor_default=0
booti ${kernel_addr_r} - ${fdt_addr_r}