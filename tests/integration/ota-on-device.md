# Integration: on-device OTA end-to-end

Doc §4.5. The download/verify/write/switch ordering and every failure path are
automated in `tests/shell/test-gameos-update.sh` (56 cases). This plan covers the
parts that need a real device, real network and real block writes.

## Happy path

1. Device running gameOS on slot A, WiFi up, `/userdata` mounted.
2. Settings > System update > check → offers the newer release (version compare
   ignores the `+g<sha>` suffix; a same-version rebuild is **not** offered).
3. Install → progress states `downloading → installing → ready`.
4. Verify after `ready`, **before** reboot:
   - `/boot/state` = `slot=b fallback=a n=0` (switch happened LAST).
   - idle slot B rootfs written; `/boot/b/{Image,both dtbs}` present;
     `/boot/boot.scr` matches the running model.
   - downloads + staging removed from `/userdata/system/update`.
5. Reboot → boots slot B; UI confirms → `slot=b fallback=b n=0`.
6. Repeat to go back B→A. Both directions work.

## Failure / safety paths on hardware

- Pull WiFi mid-download → `failed`, `/boot/state` unchanged, device still boots
  the running slot.
- Serve a manifest whose rootfs sha256 is wrong (or a truncated `.gz`) →
  `failed` at verify, **no** block write, state unchanged.
- Fill `/userdata` so the decompress space gate trips → `failed`, state unchanged.
- Interrupt power during the block write → on next boot the device still runs the
  old slot (state was never switched); the half-written idle slot is overwritten
  by the next OTA.
- Old release whose boot tar lacks `boot.scr.*` → rejected (member whitelist);
  after a successful new OTA, `reconcile_bootscr` deploys the fixed selector on
  first confirmed boot even if an old updater never replaced it.

Pass: no failure ever switches `/boot/state` early; the running slot is never
written; a power loss mid-OTA never bricks the device.

## Security note

HTTPS + same-origin SHA256 detects corruption but is **not** publisher
authentication. Manifest signing is a separate, deferred task (doc §4.4); no
signing private key may ever appear in the repo, logs or images.