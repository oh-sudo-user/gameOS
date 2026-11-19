# Integration: suspend / resume + low-battery shutdown

Doc §5. Needs hardware (lid switch, battery gauge, panels, emulator running).

## Lid close / open (sleep / resume)

1. Launch an NDS game (dual-screen, emulator in its own process group).
2. Close the lid → `gameos-sleepd` SIGSTOPs the **emulator's** process group only.
   - Panels blank; audio stops; CPU drops.
   - **Input must stay alive**: `inputd` and its supervisor are children of
     `gameos-session` (the UI's group), NOT the emulator group, so they are never
     frozen. Verify the lid-open button / power button still respond.
3. Open the lid → SIGCONT the emulator group; game resumes from the same frame;
   dual-screen DRM/KMS state restored; no flicker/crash.
4. Repeat 20×; no input gap, no leaked STOPped processes, no UI restart.

Pass: resume is instant and lossless; input never freezes during sleep.

## Low-battery auto-shutdown (the §5.2 fix)

1. Discharge (or simulate) the gauge to ≤ OFF_PCT (5%).
2. `suspend_now` low-battery branch must, in order:
   SIGCONT → SIGTERM the emulator group → **wait up to 5 s** for it to exit →
   if still alive SIGKILL + wait 2 s → `sync()` → `reboot(RB_POWER_OFF)`.
3. Verify the emulator gets a chance to flush its in-game save before the power
   off (SIGTERM is best-effort, **not** a guaranteed save — documented).
4. If `reboot()` fails (no CAP_SYS_BOOT), it logs errno and falls back to
   `kill(1, SIGUSR2)` → BusyBox init runs shutdown scripts then powers off.

Pass: no abrupt cut while the emulator is mid-write; the device powers off
cleanly; the fallback path also powers off (verified against BusyBox init.c:
SIGUSR2 → run_shutdown_and_kill_processes → RB_POWER_OFF).

## Automated coverage

Process-group existence / wait logic is in `gameos-sleepd/src/main.c`
(`process_group_exists`, `wait_group_exit`). The signal ordering is reviewed
statically; the actual save-flush timing and panel resume need this hardware run.