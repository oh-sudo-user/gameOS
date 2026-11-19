# Integration: A/B boot-state rollback (U-Boot sandbox + on-device)

Doc §2.3. The Linux-side `confirm()` / `reconcile_bootscr()` / ui-ok gate are
already automated in `tests/shell/test-boot-state.sh`. The **U-Boot-side** state
machine lives in `board/gameos/rgds-plus/boot.cmd` (HUSH parser, not POSIX sh) and
must be exercised in a U-Boot sandbox or on hardware.

## State file contract (`/boot/state`, FAT)

```
slot=a|b        # slot to try this boot
fallback=a|b    # last confirmed-good slot
n=<integer>     # unconfirmed boots of `slot`
```

`boot.cmd` must: load state; validate `slot`/`fallback` (default A); increment
`n` (normalising garbage/negative to 1); if the **incremented** `n > 3` switch
`slot` to `fallback` and reset `n=0`; boot the chosen slot's `/a/Image` or
`/b/Image` with the model dtb; `root=PARTLABEL=GAMEOS-ROOT-<A|B>`. Because `n`
is incremented before the check, rollback fires on the 4th unconfirmed power-on
(stored `n=3` becomes 4).

## U-Boot sandbox procedure

1. Build U-Boot for sandbox with the gameOS boot script embedded:
   `make sandbox_defconfig && make -j$(nproc)`; convert `boot.cmd` with
   `mkimage -A arm64 -O linux -T script -C none -d boot.cmd boot.scr`.
2. Drive the HUSH script with a fake `state` file and a fake FAT load, stepping
   `n` from 0..4. Use `setenv` / `printenv` to inspect the chosen slot.
3. Cases (`n` is incremented before the `>3` check):
   - `slot=a fallback=a n=0` → boots A, n becomes 1.
   - `slot=b fallback=a n=2` → boots B, n becomes 3 (not >3, no rollback yet).
   - `slot=b fallback=a n=3` → n becomes 4 (>3) → **rolls back to A**, n reset 0.
   - `slot=b fallback=a n=4` → n becomes 5 (>3) → **rolls back to A**, n reset 0.
   - `slot=b fallback=b n=4` → **rolls back to B**, n reset 0 (fallback honoured).
   - missing/garbage `slot`/`fallback` → safe default A, never an empty target.
   - missing/corrupt `state` → default slot A, fallback A, n 0→1.
   - garbage/negative `n` → normalised to 1, never disables the counter.

Pass: rollback fires exactly when the incremented n>3 (the 4th unconfirmed
power-on); the fallback slot is always a valid, previously-confirmed slot and is
left unchanged by a rollback; a malformed state never selects an empty target.

## On-device procedure (both models)

1. Flash a known-good image; confirm boot (UI up) → `state` shows
   `slot=fallback=<current>, n=0`.
2. OTA to the idle slot (see `ota-on-device.md`); reboot. New slot boots; before
   the UI confirms, `n` climbs each power cycle.
3. Force the new slot to fail (e.g. corrupt its `/b/Image` after OTA, or hold the
   UI from starting): after 4 unconfirmed power-ons the device **returns to the
   fallback slot** and boots normally.
4. Confirm the recovered slot → `n=0`, fallback updated.

Pass: a bad OTA never bricks the device; it auto-returns to the last good slot
within 4 power cycles, on **both** RG DS Plus and RG DS.