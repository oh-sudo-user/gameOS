# Integration: dual-model flash + smooth operation

Doc §0 / §11. The release ships **two** images from one kernel + rootfs; only the
boot partition's dtb / boot.scr differ. Both models must flash and run smoothly.

## Images

- `gameOS-rgds-plus.img.gz` → Anbernic RG DS Plus
- `gameOS-rgds.img.gz` → Anbernic RG DS

`post-image.sh` builds both; CI uploads/publishes both (7 assets total). The
manifest carries `image_rgds_plus` and `image_rgds` hashes/sizes.

## Flash + first boot (each model)

1. Back up the card first (docs/FLASHING.md). Write the matching `.img.gz` with
   balenaEtcher.
2. Boot → `gameos-splash` on the panels (console is serial-only, no boot text on
   the game panels).
3. First boot creates GAMEOS-DATA (p4) filling the card, formats exFAT **only
   because p4 was absent**, verifies ro-mount, clears the first-boot marker.
   - On a card that already has a p4, it is **preserved** (see
     `test-firstboot-data.sh`); never auto-formatted.
4. UI comes up; `/userdata` mounted; ROM skeleton present; model detected at
   runtime from `/proc/device-tree/compatible`.

## Smooth-operation checklist (each model)

- NDS: dual-screen DRM/KMS, both panels correct orientation, full speed.
- GBA / GBC / PS1 / PSP: launch, run, exit back to UI cleanly; no graphics
  re-init crash (the session backoff handles a <3 s exit).
- Input: buttons / stick / lid all work; merged input daemon stable.
- WiFi + Bluetooth: connect, persist across reboot; BT off by default, on-demand.
- TF2: second card hot-plug mounts/exFAT, ROMs scanned.
- Sleep / resume: see `suspend-resume.md`.
- OTA: see `ota-on-device.md`; A/B rollback see `boot-state-uboot-sandbox.md`.
- Performance: see `perf-thermal.md`; no hardcoded frequencies.

Pass: both models boot to a responsive UI, run every supported console smoothly,
and survive sleep / OTA / reboot without anomalies. Record any model-specific
delta (panel timing, dtb) here.