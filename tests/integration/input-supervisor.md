# Integration: input daemon supervision

Doc §5.1. Needs hardware (real input devices, UI, emulator). The supervisor
script logic is POSIX-sh and syntax-checked in CI; respawn timing and the
"input never freezes" property need a device.

## Crash respawn

1. Boot to UI; `inputd` running under `gameos-inputd-supervisor`.
2. `kill -9` the `inputd` pid → supervisor respawns it within ~1 s; controller /
   buttons work again without a UI restart.
3. Kill `inputd` 12× rapidly → backoff escalates (1,2,4,8,15,15 s cap) so a
   permanently-failing inputd never spins the CPU; exactly one supervisor and one
   inputd exist at any time (check pid files + `ps`).
4. Kill the **supervisor** → `gameos-session` detects it (zombie-aware check) and
   starts a fresh one on the next UI loop; no double supervisor.

## UI crash mid-game

1. Launch a game; `kill -9` the UI → `gameos-session` restarts it; the supervisor
   kept `inputd` alive across the UI downtime, so input has **no gap**.
2. Verify only one live supervisor after the restart (the session's
   `start_inputd_supervisor` stops any old one first).

## Lid close does not freeze input

Covered in `suspend-resume.md`: the emulator's private process group is STOPped,
but inputd/supervisor share the UI's group and keep running.

Pass: input survives UI and inputd crashes; backoff prevents spin; lid sleep
never freezes input; no duplicate supervisors.