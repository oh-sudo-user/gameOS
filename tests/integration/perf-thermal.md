# Integration: performance policies + thermal/battery guard

Doc §7 / §27. Needs hardware (sysfs cpufreq/devfreq, thermal zones, a real
battery and real games). The policy logic itself is host-tested: see
`tests/shell/test-perf-policy.sh`, which runs the real `gameos-perf` against a
fake sysfs tree. It **never** writes a fixed frequency, and the kernel thermal
framework keeps ownership of hardware throttling.

## Policy contract (`/etc/gameos/perf.conf`)

- Strategy names: `ui`, `powersave`, `balanced`, `performance`, `superdrastic`.
  `light` and `heavy` are accepted aliases for `balanced` and `performance`.
- CPU: `ui` / `balanced` prefer `schedutil`; `performance` / `superdrastic`
  prefer `performance`; `powersave` prefers `powersave`. For every policy the
  script reads `scaling_available_governors` and falls back through
  `schedutil -> ondemand -> performance`, logging when a candidate is missing
  or a write fails.
- GPU: `KEEP` (driver default, panfrost `simple_ondemand`) for every strategy
  until a hardware measurement proves a specific governor better. **No forced
  GPU performance** and no CPU governor names applied to GPU.
- `superdrastic` currently maps to the same governor as `performance`: no
  frequency floor has been measured, so none is set.

## Thermal/battery guard

`gameos-sleepd` calls `gameos-perf guard` every 30 seconds while it is awake.

- Hot: any valid thermal zone >= `THERMAL_HOT_MC` (85 °C) or battery <=
  `BATTERY_LOW_PCT` (15%) clamps `performance`/`superdrastic` to `balanced`.
  `ui`/`balanced` are already on schedutil and stay there.
- Critical: >= `THERMAL_CRIT_MC` (95 °C) or battery <= `BATTERY_CRIT_PCT` (8%)
  clamps every strategy to `powersave`.
- Release: only after the temperature is <= `THERMAL_COOL_MC` (80 °C) and the
  battery has recovered to `BATTERY_RECOVER_PCT` (20%), so the device cannot
  oscillate at the threshold.
- Missing sensors never force a clamp (the kernel still protects the SoC).
  `GUARD_ENABLE=0` in `perf.conf` disables the guard for rollback.
- Every transition is appended to `/tmp/gameos-perf.log` with request,
  effective strategy, reason, temperature and battery. The requested strategy
  lives in `/run/gameos/perf-request`; the guard's active/effective pair lives
  in `/run/gameos/perf-guard`. With no request file (fresh boot before the UI
  applies `ui`, or after `restore`) the guard stays idle.

The battery threshold is a policy choice, not a hardware limit. The threshold
also applies while charging (a low battery is still low); only the critical
shutdown path is owned by `gameos-sleepd` itself (`OFF_PCT`).

## Hardware procedure

1. `gameos-perf status` at UI idle → record requested/effective strategy,
   governor per CPU policy, GPU devfreq governor, thermal zones, battery.
   This is the **baseline** (doc §0: performance claims need measurements).
2. Launch a heavy game (NDS) → `launch.cpp` applies `performance` in the
   **parent** (never the child); CPU → performance; GPU untouched (KEEP).
3. Exit to UI → `ui` profile restored (schedutil). Verify the governor actually
   changed back and temperatures fall.
4. Launch a light game (GBA) → `balanced` (schedutil); confirm no unnecessary
   performance lock.
5. Set a per-game override to 省电 (`powersave`) → verify the governor and that
   the guard does not promote it.
6. Crash the UI mid-game → `gameos-session` re-applies `ui` on restart; no heavy
   governor left behind.
7. Sustained heavy load 30 min → record the thermal curve, the exact hot/critical
   transitions from `/tmp/gameos-perf.log`, and frame-time/temperature/battery
   samples. Confirm the kernel still throttles on its own; the guard only
   changes governor strategy.
8. Low battery → discharge or use a lab supply to cross 15% and 8%; confirm the
   clamps and the later release after charging past 20%. Do not run this test on
   a battery you cannot afford to lose.
9. `gameos-perf restore` → boot governors and the saved request/state are
   cleared. `GUARD_ENABLE=0` is the config rollback if a threshold misbehaves.

## Results (NOT RUN)

No device measurements exist for this revision. Record them before claiming
PR-18 performance or power results; all numbers here must be labelled
`measured`.

| date | commit | image SHA-256 | scenario | strategy/governor | avg FPS | P99 frame ms | temp curve | battery | notes |
|------|--------|---------------|----------|-------------------|---------|--------------|------------|---------|-------|
| pending | | | | | | | | | |

## Assertions

- Only governors listed in `scaling_available_governors` are ever written; an
  unavailable configured governor falls back, logs, and never aborts boot.
- No file under `/sys/.../scaling_*_freq` or `devfreq/*/min_freq|max_freq` is
  written by gameOS (grep the script: it only touches `scaling_governor` and
  devfreq `governor`).
- The boot governor is saved once to `/run/gameos/perf-original`; `restore`
  returns to it.
- GPU stays at the driver default unless a measured change is justified.
- The guard clamps and releases with hysteresis and leaves missing sensors
  alone; guard-disable is a one-line rollback.

Pass: heavy games get performance CPU; UI/light idle down; GPU is driver-default;
hot/low-battery clamps happen once, hold, and release; thermals behave; no
hardcoded frequencies anywhere. Attach the baseline + load numbers to the
release verification record.
