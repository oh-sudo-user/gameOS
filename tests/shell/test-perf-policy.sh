#!/bin/sh
# Host test for gameos-perf strategy names, governor fallback, thermal/battery
# guard hysteresis and restore. Needs no hardware: sysfs, /run and the log are
# redirected into a temporary sandbox through the GAMEOS_PERF_* variables.
set -u

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
SCRIPT="$ROOT/buildroot/board/gameos/rgds-plus/rootfs-overlay/usr/bin/gameos-perf"

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT INT TERM

SYS="$TMP/sys"
RUN="$TMP/run"
CONF="$TMP/perf.conf"
LOG="$TMP/perf.log"
PASS=0
FAIL=0

ok() { PASS=$((PASS + 1)); }
bad() { FAIL=$((FAIL + 1)); echo "FAIL: $*"; }
eq() { if [ "$1" = "$2" ]; then ok; else bad "$3 (got '$1' want '$2')"; fi; }
file_eq() {
    if [ "$(cat "$1" 2>/dev/null)" = "$2" ]; then ok
    else bad "$3 (got '$(cat "$1" 2>/dev/null)' want '$2')"; fi
}
has() {
    if grep -q "$2" "$1" 2>/dev/null; then ok
    else bad "$3 (missing '$2')"; fi
}

# mk <path> <value>
mk() {
    mkdir -p "${1%/*}"
    printf '%s\n' "$2" >"$1"
}

setup() {
    rm -rf "$SYS" "$RUN" "$LOG" "$CONF"
    mkdir -p "$SYS/cpufreq/policy0" "$SYS/cpufreq/policy1" \
             "$SYS/devfreq/gpu" "$SYS/thermal/thermal_zone0" \
             "$SYS/power/battery" "$RUN"
    mk "$SYS/cpufreq/policy0/scaling_available_governors" \
       "powersave schedutil performance"
    mk "$SYS/cpufreq/policy0/scaling_governor" "schedutil"
    mk "$SYS/cpufreq/policy1/scaling_available_governors" "performance"
    mk "$SYS/cpufreq/policy1/scaling_governor" "performance"
    mk "$SYS/devfreq/gpu/name" "mali"
    mk "$SYS/devfreq/gpu/available_governors" "simple_ondemand performance"
    mk "$SYS/devfreq/gpu/governor" "simple_ondemand"
    mk "$SYS/thermal/thermal_zone0/type" "soc-thermal"
    mk "$SYS/thermal/thermal_zone0/temp" "70000"
    mk "$SYS/power/battery/type" "Battery"
    mk "$SYS/power/battery/capacity" "80"
    mk "$SYS/power/battery/status" "Discharging"
    cat >"$CONF" <<'EOF'
UI_CPU_GOVERNOR=schedutil
POWERSAVE_CPU_GOVERNOR=powersave
LIGHT_CPU_GOVERNOR=schedutil
HEAVY_CPU_GOVERNOR=performance
SUPERDRASTIC_CPU_GOVERNOR=performance
UI_GPU_GOVERNOR=KEEP
POWERSAVE_GPU_GOVERNOR=KEEP
LIGHT_GPU_GOVERNOR=KEEP
HEAVY_GPU_GOVERNOR=KEEP
SUPERDRASTIC_GPU_GOVERNOR=KEEP
GUARD_ENABLE=1
THERMAL_HOT_MC=85000
THERMAL_CRIT_MC=95000
THERMAL_COOL_MC=80000
BATTERY_LOW_PCT=15
BATTERY_CRIT_PCT=8
BATTERY_RECOVER_PCT=20
EOF
    : >"$LOG"
}

run_perf() {
    GAMEOS_PERF_CONF="$CONF" \
    GAMEOS_PERF_RUN="$RUN" \
    GAMEOS_PERF_LOG="$LOG" \
    GAMEOS_PERF_CPUFREQ="$SYS/cpufreq" \
    GAMEOS_PERF_DEVFREQ="$SYS/devfreq" \
    GAMEOS_PERF_THERMAL="$SYS/thermal" \
    GAMEOS_PERF_POWER="$SYS/power" \
    sh "$SCRIPT" "$@"
}

# --- strategy names and request state ------------------------------------
setup
run_perf balanced >/dev/null
file_eq "$SYS/cpufreq/policy0/scaling_governor" schedutil \
    "balanced selects schedutil"
file_eq "$RUN/perf-request" balanced "request file records balanced"
file_eq "$RUN/perf-guard" "0 balanced" "guard starts inactive"

run_perf performance >/dev/null
file_eq "$SYS/cpufreq/policy0/scaling_governor" performance \
    "performance selects performance"
file_eq "$RUN/perf-request" performance "request file records performance"

# --- thermal guard: clamp, hold, release --------------------------------
mk "$SYS/thermal/thermal_zone0/temp" "90000"
run_perf guard >/dev/null
file_eq "$SYS/cpufreq/policy0/scaling_governor" schedutil \
    "hot performance request clamps to balanced"
file_eq "$RUN/perf-request" performance "clamping keeps the request"
file_eq "$RUN/perf-guard" "1 balanced" "guard state records the clamp"
has "$LOG" "state=hot" "hot transition is logged"

mk "$SYS/thermal/thermal_zone0/temp" "82000"
run_perf guard >/dev/null
file_eq "$SYS/cpufreq/policy0/scaling_governor" schedutil \
    "hysteresis holds until the cool threshold"
file_eq "$RUN/perf-guard" "1 balanced" "held clamp keeps the state"

mk "$SYS/thermal/thermal_zone0/temp" "78000"
run_perf guard >/dev/null
file_eq "$SYS/cpufreq/policy0/scaling_governor" performance \
    "cool release restores the requested strategy"
file_eq "$RUN/perf-guard" "0 performance" "guard state clears on release"
has "$LOG" "state=released" "release transition is logged"

# --- low battery and critical states ------------------------------------
mk "$SYS/power/battery/capacity" "10"
run_perf guard >/dev/null
file_eq "$SYS/cpufreq/policy0/scaling_governor" schedutil \
    "low battery clamps a top tier"
file_eq "$RUN/perf-guard" "1 balanced" "low battery records the clamp"

mk "$SYS/power/battery/capacity" "25"
run_perf guard >/dev/null
file_eq "$SYS/cpufreq/policy0/scaling_governor" performance \
    "battery recovery releases the clamp"

mk "$SYS/power/battery/capacity" "80"
mk "$SYS/thermal/thermal_zone0/temp" "96000"
run_perf guard >/dev/null
file_eq "$SYS/cpufreq/policy0/scaling_governor" powersave \
    "critical temperature forces powersave"
file_eq "$RUN/perf-guard" "1 powersave" "critical state is recorded"
has "$LOG" "state=critical" "critical transition is logged"

mk "$SYS/thermal/thermal_zone0/temp" "70000"
mk "$SYS/power/battery/capacity" "7"
run_perf guard >/dev/null
file_eq "$SYS/cpufreq/policy0/scaling_governor" powersave \
    "critical battery forces powersave"

# Leaving the critical band while still hot/low must stay at powersave until
# the full cool/recover hysteresis is crossed.
mk "$SYS/power/battery/capacity" "10"
run_perf guard >/dev/null
file_eq "$SYS/cpufreq/policy0/scaling_governor" powersave     "critical clamp holds through the hot/low band"

# Missing sensors must not latch a clamp forever.
rm -rf "$SYS/thermal" "$SYS/power"
run_perf performance >/dev/null
file_eq "$SYS/cpufreq/policy0/scaling_governor" performance \
    "missing sensors do not clamp"
file_eq "$RUN/perf-guard" "0 performance" "missing sensors release the guard"

# A driver reporting whole degrees is normalised to millidegrees.
setup
mk "$SYS/thermal/thermal_zone0/temp" "90"
run_perf performance >/dev/null
run_perf guard >/dev/null
file_eq "$RUN/perf-guard" "1 balanced" "whole-degree temperatures are normalised"

# --- explicit strategies, aliases and invalid input ---------------------
setup
run_perf powersave >/dev/null
file_eq "$SYS/cpufreq/policy0/scaling_governor" powersave \
    "powersave selects the powersave governor"
file_eq "$RUN/perf-request" powersave "request file records powersave"

run_perf superdrastic >/dev/null
file_eq "$SYS/cpufreq/policy0/scaling_governor" performance \
    "superdrastic currently uses the performance governor"
file_eq "$RUN/perf-request" superdrastic "request file records superdrastic"

run_perf light >/dev/null
file_eq "$RUN/perf-request" balanced "light aliases balanced"
run_perf heavy >/dev/null
file_eq "$RUN/perf-request" performance "heavy aliases performance"

run_perf turbo >/dev/null 2>&1
eq "$?" 2 "unknown strategy exits 2"
file_eq "$RUN/perf-request" performance "invalid strategy keeps the request"

# --- fallback and restore ------------------------------------------------
setup
mk "$SYS/cpufreq/policy0/scaling_available_governors" "schedutil performance"
mk "$SYS/cpufreq/policy0/scaling_governor" "schedutil"
run_perf powersave >/dev/null
file_eq "$SYS/cpufreq/policy0/scaling_governor" schedutil \
    "unavailable powersave falls back to schedutil"

setup
run_perf performance >/dev/null
run_perf restore >/dev/null
file_eq "$SYS/cpufreq/policy0/scaling_governor" schedutil \
    "restore returns the boot governor"
if [ ! -e "$RUN/perf-request" ] && [ ! -e "$RUN/perf-guard" ]; then ok
else bad "restore removes request and guard state"; fi
run_perf guard >/dev/null
file_eq "$SYS/cpufreq/policy0/scaling_governor" schedutil     "guard stays idle after restore"
if [ ! -e "$RUN/perf-guard" ]; then ok
else bad "idle guard does not create state"; fi

# --- GPU stays at the driver default ------------------------------------
setup
file_eq "$SYS/devfreq/gpu/governor" simple_ondemand \
    "GPU starts at the driver default"
run_perf performance >/dev/null
file_eq "$SYS/devfreq/gpu/governor" simple_ondemand \
    "GPU governor is untouched"

# --- guard can be rolled back from perf.conf ----------------------------
setup
echo "GUARD_ENABLE=0" >>"$CONF"
mk "$SYS/thermal/thermal_zone0/temp" "100000"
run_perf performance >/dev/null
file_eq "$SYS/cpufreq/policy0/scaling_governor" performance \
    "disabled guard keeps the requested strategy"
file_eq "$RUN/perf-guard" "0 performance" "disabled guard records inactive"

# --- status is read-only and reports the guard --------------------------
setup
run_perf balanced >/dev/null
OUT=$(run_perf status)
case "$OUT" in
    *"guard enabled   = 1"*) ok ;;
    *) bad "status reports the guard switch" ;;
esac
case "$OUT" in
    *"battery         = 80"*) ok ;;
    *) bad "status reports the battery" ;;
esac
case "$OUT" in
    *"simple_ondemand"*) ok ;;
    *) bad "status reports the GPU governor" ;;
esac

echo "test-perf-policy: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
