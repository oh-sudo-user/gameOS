# Integration: performance policy + thermal

Doc §7. Needs hardware (sysfs cpufreq/devfreq, thermal zones, real games).
The policy script `gameos-perf` is POSIX-sh, syntax-checked in CI, and reads
capabilities from sysfs — it **never** writes a fixed frequency.

## Policy contract (`/etc/gameos/perf.conf`)

- UI / LIGHT CPU governor: `schedutil` (fallback `ondemand` → `performance`,
  first available per policy).
- HEAVY CPU governor: `performance` (NDS / PS1 / PSP).
- GPU governor: `KEEP` (driver default, panfrost `simple_ondemand`) until a
  hardware measurement proves a specific governor is better. **No forced GPU
  performance** — that was the unmeasured anomaly removed in this revision.

## Procedure

1. `gameos-perf status` at UI idle → record CPU governor per policy, GPU devfreq
   governor, thermal zone temps. This is the **baseline** (doc §0: performance
   claims need measurements).
2. Launch a heavy game (NDS) → `launch.cpp` applies `heavy` in the **parent**
   (never the child); CPU → performance; GPU untouched (KEEP).
3. Exit to UI → `ui` profile restored (schedutil). Verify the governor actually
   changed back and temps fall.
4. Launch a light game (GBA) → `light` (schedutil). Confirm no unnecessary
   performance lock.
5. Crash the UI mid-game → `gameos-session` re-applies `ui` on restart; no heavy
   governor left behind.
6. Sustained heavy load 10 min → record thermal trend; confirm the device throttles
   via its own thermal framework (we do not pin frequencies) and stays stable.

## Assertions

- Only governors listed in `scaling_available_governors` are ever written; an
  unavailable configured governor falls back, logs, and never aborts boot.
- The boot governor is saved once to `/run/gameos/perf-original`; `restore`
  returns to it.
- No file under `/sys/.../scaling_*_freq` is written by gameOS (grep the script:
  it only touches `scaling_governor` and devfreq `governor`).
- GPU stays at driver default unless a measured change is justified.

Pass: heavy games get performance CPU; UI/light idle down; GPU is driver-default;
thermals behave; no hardcoded frequencies anywhere. Attach the baseline + load
numbers to the release verification record.