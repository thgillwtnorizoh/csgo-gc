# RASC I02 — Integrated achievement journal and reset

Project / Campaign / Actor / Assignment: CS:GO Achievements Resurrection / I02 / Work 2 / integrate owner-authorized PR #1 (achievement_log) and PR #2 (achievement-only reset) onto latest upstream-derived main.

Status: **ENGINEERING CHECKPOINT; CI NOT YET VERIFIED**.
Starting main SHA: `40d419993201c16d263d90ef490fe903c847c247` (includes GT-610 #156 `round_mvp` server listener lifetime fix).
Feature parents: PR #1 `d083c8dd097f8453b7f342a1fc0f0cb1e171f424` and PR #2 `feeb441c5fb5606c56032349a556b2bb7ced9978`.
Published SHA / exact integration tested SHA: Inspect PR #3 head and Actions run. This file itself **does not claim test success**.

## Integrated behavior
- Preserve all upstream listener lifecycle mutexes, server registration/unregistration, and client/server callback split.
- Opt-in UTF-8 JSONL `csgo_gc/achievement_log` with native new-unlock, progress and StoreStats event records; disabled by default, not an on-disk snapshot.
- Local-only, custom-AppID RCON `reset_achievements confirm`; clears unlocked achievements and pending callbacks while preserving integer/float stats, inventory and genuine Steam achievements. Persists immediately via existing store and rolls back memory on save failure.
- Preserve journal and reset fixtures in local stats and GC command suites.
- Preserve 10.13 GC production macOS deployment minimum. Use 10.15 compiler/linker minimum for the standalone `achievement_log_tests` CMake target under Makefile generator.

## Evidence / confidence
RECONSTRUCTED + ENGINEERING: composite source and original branch tests.
VERIFIED COMPONENT (prior independent branch only): PR #2 Build Actions 44892569235 (22/22 each Windows, macOS, Linux; 1/1 Linux 32-bit build gate).
PR #1 fast checks Actions 44913329513 green on all three platforms. PR #1 full Build 44913329511 failed on macOS test-only `std::filesystem` availability (Makefile generator), cancelling Windows; fixed explicitly in this integration.
Combined build, CTest and independent QA: **NOT RUN / NOT VERIFIED at creation**.

## Remaining gates
- Full Build on this exact commit with `ctest --output-on-failure`; verify `local_user_stats_tests`, `gc_message_tests`, `achievement_log_tests` on each platform and Linux 32-bit build check.
- Separate fast journal checks and upstream-only regression preservation.
- Legacy September 2023 CS:GO client runtime smoke with custom AppID, local GC RCON and achievement logger enabled; check unlock→journal→reset→re-unlock, status and storage, plus UI state.
- Journal file writes can fail; writer does not change original Steam callback/return values. Reset does not erase journal.
- Original KeyValue save is not crash-safe atomic. Existing cumulative game stats may automatically re-award achievements after reset. Back up `csgo_gc/achievements.txt` before using the command.

Coverage: combined sources, original branch tests and upstream listener changes.
Discrepancies: runtime integration and independent human verification pending.
Updated records: this file, both original feature checkpoints and original PR history.
Reproduction: build the integration PR head, run all configured CTests, inspect `csgo_gc/achievement_log`, then use local GC Source RCON `reset_achievements confirm` with custom AppID and validate reload.
Next task: CI decision on exact commit, merge into main if green and upstream head unchanged; human runtime qualification afterward.
Subsystem: **OPEN**, owner acceptance pending.
