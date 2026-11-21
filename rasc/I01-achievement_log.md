# RASC I01 — achievement_log implementation and evidence

**Project / Campaign / Actor / Assignment:** RASC (CS:GO Achievements Resurrection) / GC-I01 / combined owner-authorized Work 1 requirements/source analysis and Work 2 implementation / live Steam User Stats achievement-event journal.

**Repository:** https://github.com/thgillwtnorizoh/csgo-gc
**Upstream:** https://github.com/GT-610/csgo-gc
**Starting fork and upstream SHA:** `048a0f00ae3584bc87e636292118251724b4e0fc` (identical at 2026-11-21).
**Branch:** `feature/rasc-i01-achievement_log` / draft PR, not main.

## Behavior and config

An explicit opt-in in `csgo_gc/config.txt`:

```text
"achievement_log" "1"
```

Disabled by default. Output is exactly `csgo_gc/achievement_log`, **no extension**, separate from both `gc_log.txt` and existing `csgo_gc/achievements.txt`. Logs only the custom AppID local-proxy path, not the AppID 730 forwarding path.

- `SteamUserStatsProxy::SetAchievement`: journal one `unlock` per *newly inserted* achievement. Existing repeated calls continue to return success without duplicate events. Preserve the original Unix-second timestamp stored in `LocalUserStats::Store`.
- `SteamUserStatsProxy::IndicateAchievementProgress`: journal a `progress_call` with exactly the game's provided ID/current/max after passing existing validation. No synthetic 25/50/75% milestones or stat-to-achievement inference.
- `SteamUserStatsProxy::StoreStats`: journal `stats_store` with successful/failed save status and count of saved achievement callbacks. Native `UserStatsStored_t` and `UserAchievementStored_t` queuing and Save semantics untouched.
- No `GetStat`/`SetStat` event tracing in I01; requires a separate authorized targeted mapping campaign.

## JSON Lines v1 (synthetic example only)

```json
{"v":1,"session":"session-generated-on-start","seq":1,"time_ms":1795260000125,"steady_us":74261230000,"type":"unlock","id":"KILL_ENEMY_LOW","unlock_seconds":1795260000}
{"v":1,"session":"session-generated-on-start","seq":2,"time_ms":1795260000150,"steady_us":74261255000,"type":"progress_call","id":"KILL_ENEMY_HIGH","current":2500,"maximum":10000}
{"v":1,"session":"session-generated-on-start","seq":3,"time_ms":1795260000900,"steady_us":74262005000,"type":"stats_store","success":true,"newly_stored":1}
```

`session`: per-process identifier generated from wall/steady clocks and PID. `seq`: strictly incrementing number **per attempted append** within one process, preserving serialized journal order and making gaps detectable. `time_ms`: UTC Unix milliseconds when writer creates the event, not necessarily exactly the original call entry; `steady_us`: monotonic microsecond clock value for within-session intervals. `unlock_seconds`: the GT-610 timestamp used in the achievement store.

RASC future consumer should use complete newline-delimited records; event identity `(session, seq)`, not `unlock_seconds`. It must reconcile live unlocks against `achievements.txt` for persisted truth and deduplicate after restarts. A missing journal line does not mean the game rejected an unlock.

## Safety, performance and limitations

- Synchronous compact fopen/fwrite/fclose per event **on the intercepting game thread**; no full snapshot rewriting. Actual gameplay impact has **NOT BEEN PROFILED** and is a remaining runtime qualification gate.
- Process-level mutex, JSON string escaping, bounded file size of **8 MiB**; on saturation the writer refuses new entries rather than rotating away data the companion has not consumed. Archive/reset only with game stopped after retaining unread evidence.
- No per-event forced disk `fsync`; a power loss may lose latest buffered records. Independent GC state remains authoritative.
- Journal failure is *nonfatal* and does not change Steam API returns, GC callbacks, or `StoreStats`. Failure produces a detectable potential sequence gap if more events are later written.
- If two separate GC processes write the same journal simultaneously, cross-process append ordering is not qualified.
- The native game may or may not call `IndicateAchievementProgress`; do not claim original progress notification behavior without runtime observation.
- No asset bundling, user identity, credentials, gameplay chat, or account identifiers.

## Verification and remaining gates

- Independent C++20 `achievement_log_tests`: JSONL structure, ordinal sequence, native progress fields, failed/successful StoreStats marker, escaping, bounded growth/failure, concurrent same-process append serialization.
- Historical `local_user_stats_tests` preserved, plus `FreshUnlockFlagPreservesStoreStatsSemantics`: new vs repeated insert flags, first timestamp, successful repeats and existing pending callback save behavior.
- Standalone Windows/Linux/macOS GitHub Actions gate: inspect exact commit and CI run before marking verified.
- Full original CMake/shared `csgo_gc` builds and historical CTest suites: run via original GitHub PR workflows; don't assume these pass from standalone writer tests.
- **NOT RUN:** actual original CS:GO app and native callback comparison, RASC JSONL consumer, visual popup timing, source stat mapping, independent second actor QA.

## Upstream continuity

Daily conditional monitoring of GT-610 `main` compares upstream changes to the owner's fork; notify only on new changes, explain each important behavioral modification and how it affects the achievement journal and fork integration. Never auto-merge, force push or overwrite upstream work.

**Subsystem:** OPEN until independent development and owner physical verification; owner acceptance not implied by a green CI checkpoint.
