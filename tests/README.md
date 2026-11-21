# gameOS test suite

Two layers, per the整改与优化实施文档 §10:

## 1. Runnable fault-injection tests (`tests/shell/`)

Execute on any POSIX host (dash / BusyBox ash) with **no hardware**. Block-device,
mount and network I/O are stubbed; `gzip`, `sha256sum` and `tar` are the real
tools so integrity checks are exercised for real. Each test extracts the logic
under test verbatim from the shipped script and redirects only absolute paths /
device-presence predicates into a temp sandbox.

| file | covers | doc |
|------|--------|-----|
| `test-gameos-update.sh` | strict manifest validation matrix; version compare (no downgrade / no same-release rebuild); Ed25519 manifest signature required; `do_apply` fault injection at **every** stage — the invariant is `/boot/state` is switched **LAST** and **never** on any failure; malicious boot tars (traversal / symlink / missing / extra / empty member) | §4.5 |
| `test-ota-signature.sh` | extracts `verify_manifest_signature()` verbatim; valid signature, one-byte tamper, wrong key, 63-byte truncated signature, missing signature and missing public key | §4.3 |
| `test-firstboot-data.sh` | an **existing** p4 is **NEVER** auto-formatted (exFAT preserved, corrupt/foreign preserved); format only after create+verify succeed; sgdisk/parted/node-timeout/mkfs/verify failures all keep the first-boot marker | §3.3 / §0 |
| `test-boot-state.sh` | `confirm()` records slot=fallback=current, n=0; `reconcile_bootscr()` atomically deploys the model-matched script; the start gate runs confirm **only** after `ui-ok` exists | §2.3 |
| `test-ui-perf.sh` | compiles `tests/host/test-ui-perf.cpp` against the real `src/perf.cpp`; nearest-rank percentiles, window cap/dropped/reset, and the disabled-path no-op | §10 |
| `test-ui-drawcall.sh` | compiles `tests/host/test-drawcall.cpp` against the real `src/drawcall.cpp`; rectangle intersection/merge/area limits and geometry hash stability (skips when SDL2 dev files are absent) | §10 |
| `test-theme-tokens.sh` | compiles `tests/host/test-theme-tokens.cpp` against the real `src/i18n.cpp`; the palette schema accepts valid/CRLF files and rejects missing, duplicate, malformed or out-of-range fields as a whole; shipped palettes must match the built-in themes; also runs `scripts/lint-themes.sh` | §10 |
| `test-motion.sh` | compiles `tests/host/test-motion.cpp` against the real `src/motion.cpp`; sampled easing curves match the spec table, the animator interpolates/retargets/stops, and the L0–L3 frame-budget ladder escalates, ignores isolated spikes and recovers after calm windows | §10 |
| `test-widget.sh` | compiles `tests/host/test-widget.cpp` against the header-only widget helpers; list-row geometry/hit-testing, switch and slider mapping, dialog panel/button layout, and toast replace/expiry/fade timing | §10 |
| `test-touch.sh` | compiles `tests/host/test-touch.cpp` against the header-only touch helpers; 12 DP drag threshold and the page-transition touch lock window | §10 |
| `test-netparse.sh` | compiles `tests/host/test-netparse.cpp` with the real `src/netparse.cpp`; MAC validation, Bluetooth device lines (CRLF, extra tabs, bad rows), WiFi known/status/nets formats, duplicate SSID merge and saved/keyed flags | §10 |
| `test-overrides.sh` | compiles `tests/host/test-overrides.cpp` with the real `src/overrides.cpp`; schema validation (missing / invalid / oversized / trailing documents get moved to `.bak`), per-entry skipping, atomic save + roundtrip + removal, hot reload by mtime/size, key format and the RetroArch config mapping | §10 |
| `test-fileops.sh` | compiles `tests/host/test-fileops.cpp` with the real `src/fileops.cpp`; path guard (traversal / absolute / symlink escapes), listing order, mkdir / rename / copy / overwrite / cancel, recursive remove, stored + deflate ZIP extraction and the rejection matrix (traversal, absolute names, symlinks, encryption, unsupported method, ZIP64, duplicates, truncated archives), staging cleanup, and the TaskQueue worker with progress and cancel | §25 |

The override tests need the host `json-c` development files and print `SKIP`
when they are absent.
The file-manager tests need the host `zlib` development files and print `SKIP`
when they are absent.

The OTA tests need `openssl` with Ed25519 support (OpenSSL 3.x); they print
`SKIP` only when the host has no openssl at all.

Run locally (WSL example):

```sh
cd gameOS
for t in tests/shell/test-*.sh; do dash "$t"; done
```

`scripts/ci.sh` is the single entry point: it runs every test above, the theme
lint, `sh -n` over shipped scripts, and a package-pin audit that rejects floating
git refs. Set `GAMEOS_IMAGES` to also verify a finished image set with
`scripts/verify-image.sh` (required artifacts, sha256, gzip, boot payload,
target binaries, and the theme palettes inside `rootfs.ext4` via `debugfs`).
Run it before the long compile in any CI job so a regression fails fast.

## 2. Integration / hardware plans (`tests/integration/`)

These need real hardware or a U-Boot sandbox and are **not** run in CI. Each file
lists the procedure, the pass criteria, and what is already covered by the
automated layer. They are the "needs-hardware" checklist required by §0.

## Fixtures (`tests/fixtures/`)

`manifest-valid.json`, `manifest-invalid-hash.json`, `manifest-invalid-size.json`
match the exact one-line-per-section format emitted by `post-image.sh`. The
malicious boot archives are generated deterministically at test runtime (no
binary blobs committed).