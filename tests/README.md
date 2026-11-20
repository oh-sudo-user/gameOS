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
| `test-gameos-update.sh` | strict manifest validation matrix; version compare (no downgrade / no same-release rebuild); `do_apply` fault injection at **every** stage — the invariant is `/boot/state` is switched **LAST** and **never** on any failure; malicious boot tars (traversal / symlink / missing / extra / empty member) | §4.5 |
| `test-firstboot-data.sh` | an **existing** p4 is **NEVER** auto-formatted (exFAT preserved, corrupt/foreign preserved); format only after create+verify succeed; sgdisk/parted/node-timeout/mkfs/verify failures all keep the first-boot marker | §3.3 / §0 |
| `test-boot-state.sh` | `confirm()` records slot=fallback=current, n=0; `reconcile_bootscr()` atomically deploys the model-matched script; the start gate runs confirm **only** after `ui-ok` exists | §2.3 |
| `test-ui-perf.sh` | compiles `tests/host/test-ui-perf.cpp` against the real `src/perf.cpp`; nearest-rank percentiles, window cap/dropped/reset, and the disabled-path no-op | §10 |

Run locally (WSL example):

```sh
cd gameOS
for t in tests/shell/test-*.sh; do dash "$t"; done
```

CI runs the same suite (`sh "$t"`) in the **build** job, before the long compile,
so a regression fails fast. A non-zero exit from any test blocks the build.

## 2. Integration / hardware plans (`tests/integration/`)

These need real hardware or a U-Boot sandbox and are **not** run in CI. Each file
lists the procedure, the pass criteria, and what is already covered by the
automated layer. They are the "needs-hardware" checklist required by §0.

## Fixtures (`tests/fixtures/`)

`manifest-valid.json`, `manifest-invalid-hash.json`, `manifest-invalid-size.json`
match the exact one-line-per-section format emitted by `post-image.sh`. The
malicious boot archives are generated deterministically at test runtime (no
binary blobs committed).