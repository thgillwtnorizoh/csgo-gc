# RASC — Achievement-Only Reset / Engineering Checkpoint

Project / Campaign / Actor / Assignment: CS:GO Achievements Resurrection / local user-stat testing / Work 2 / achievement-only reset RCON command.

Status: **UNVERIFIED IMPLEMENTATION — CI/CLIENT RUNTIME PENDING** (do not call this a verified checkpoint until tests pass).

Starting SHA: `048a0f00ae3584bc87e636292118251724b4e0fc` (fork `main` when work began).
Published SHA: inspect the tip of `rasc/achievement-reset`; this file is part of that changeset.
Exact tested SHA: **NOT RUN** in an instrumented build environment.

## Scope and supported profile

On the fork's custom AppID, enable authenticated local RCON and call:

```text
reset_achievements confirm
```

An exact `confirm` token is required. Success looks like:

```text
OK reset_achievements removed=N stats=preserved
```

This acts on the **locally emulated** Steam User Stats store at `csgo_gc/achievements.txt`. It empties unlocked achievement entries and pending achievement-stored notifications and immediately saves, without resetting `integer_stats`, `float_stats`, inventory or the actual Steam account. It routes through the live user-stats proxy rather than editing the file behind the game's back. Successful reset queues a `UserStatsStored_t` callback; it does not fabricate unlock callbacks.

Failures are explicit if the original Steam AppID is in use, the local stats proxy is absent, loading fails, or saving fails. A failed save restores in-memory achievements and pending callbacks. Existing `KeyValue::WriteToFile` does **not** provide crash-safe atomic file replacement; backup `csgo_gc/achievements.txt` before experimenting. Existing game logic can immediately re-award achievements if preserved stats already satisfy their conditions; a frontend may also need reopening to repaint cached achievement state.

The command is restricted by the normal Source RCON enabled/password/bind-address policy. Do not enable unauthenticated RCON on a public interface. No action resets a Steam cloud achievement.

## Evidence / Confidence

- RECONSTRUCTED: underlying local achievement, stat, and callback implementation from `csgo_gc/local_user_stats.*` and `csgo_gc/steam_hook.cpp`.
- ENGINEERING: achievement-only transactional-in-memory reset; RCON explicit confirmation; mutex-protected cross-thread access and callback queues; live proxy binding.
- NOT VERIFIED: legacy CS:GO frontend live refresh, on-disk fault atomicity, cross-platform compilation, game runtime, and build CI.

## Tests

Added isolated `local_user_stats_tests` scenarios for persisted achievement-only reset, integer and float stat preservation, clearing unsaved achievement callbacks, idempotence, and in-memory rollback after a simulated write failure.

Added `gc_message_tests` command-dispatch checks for help discovery, exact confirmation, no handler, and successful injected reset handler.

Expected commands in a properly provisioned build environment:

```sh
cmake --build build --config Release --target csgo_gc local_user_stats_tests gc_message_tests
ctest --test-dir build -C Release --output-on-failure
```

**NOT RUN** locally: no complete compiler/dependency/game runtime checkout in this execution. GitHub Actions PR build/ctest is the next verification gate. The above is not a claim of passing tests.

## Coverage / Discrepancies / Next bounded task

Coverage gained: local-store reset API, RCON command, live store coordination, command and persistence test fixtures.

Remaining: CI build/ctest on supported platforms; independent review of live UserStats callback synchronization; human smoke test against final September 2023 CS:GO client and custom AppID; integration with future `achievement_log` (not present at starting SHA). No `achievement_log` state is edited here.

Reproduction: use a test copy of `achievements.txt`, unlock at least one achievement locally, run the confirmed command via RCON, verify `removed=N`, reopen/reload and inspect achievement state while ensuring integer and float stat values remain unchanged.

Subsystem: **OPEN**, awaiting verified CI and owner runtime acceptance.
