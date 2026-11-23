# GC-I05 — Persistent API-call trace and future progress derivation boundary

**Project / Campaign / Actor / Assignment:** CS:GO Achievements Resurrection (RASC) + csgo-gc / GC-I05 / Work 2 Engineering / owner-authorized, opt-in diagnostic tracing before progress-popup implementation.

**Starting main SHA:** `77b118bfbd18f83b4055e19fbe367f523dc7d893`.
**Status:** VERIFIED CHECKPOINT (exact-source cross-platform CI); original CS:GO runtime and independent Work 1 QA **NOT VERIFIED**; subsystem OPEN.
**Exact tested production SHA:** `888ff10eb7b02c7fcd193c151addc84bb956c425`.
**Published merge SHA:** `88800813f9afc4ec7779e4f591003f1a870135bc`.
**Exact tested/merged source-tree SHA:** `a2eeed27b206940401aaeaa43529a25248630bd2` (identical).
**Original runtime evidence:** Owner provided a match-scoped `achievement_log` with unlocks and stat stores but no `progress_call`, and an empty PowerShell-filtered capture. This absence **does not prove** CS:GO never calls the original Steam API: the previous journal records only validated local-proxy calls while a match is active, and is truncated at match end. Game may use `SetStat` and direct unlocks instead.

## New opt-in research trace — independently retained

Add to `csgo_gc/config.txt` and **restart the game**:

```text
"achievement_api_trace" "1"
```

Existing `"achievement_log" "1"` is optional and independent. New `csgo_gc/achievement_api_trace.jsonl` is UTF-8 JSON Lines v1, capped at 8 MiB; **not reset at match join, round start, match end, disconnect, or successful achievement reset**. It also captures local custom-AppID calls *outside active matches*. Separate writer instance, per-process `session`, monotonic `seq`, `time_ms`, `steady_us` same conventions as `achievement_log`. A `trace_start` marker is emitted when the custom-AppID local proxy initializes via `RequestCurrentStats` (or on its first traced API call). All observed events carry `source:"observed_api"`.

Types:
- `stat_call` for actual `ISteamUserStats::SetStat` int32 and float32 attempts: stat ID/name, `before_valid`, `before`/ `before_bits`, requested numeric value/`requested_bits`, `after_valid`, `after`/`after_bits`, `accepted`, `blocked_by_reset`. Float bit fields preserve IEEE-754 NaNs, +/- infinity and -0.0 without invalid JSON. **A valid GetStat returning 0 does not prove that the stat was explicitly stored**; it may be the default.
- `achievement_call` for actual `SetAchievement` attempts: ID, `before_valid`, `before_unlocked`, `accepted`, `newly_unlocked`, `blocked_by_reset`. Repeated SetAchievement calls can be accepted without being newly unlocked.
- `progress_attempt` for actual `IndicateAchievementProgress` attempts: ID, the exact game's `current`/`maximum` arguments, `accepted`, `callback_queued`, `blocked_by_reset`. Unlike existing `achievement_log`'s `progress_call` (success-only), this detects invalid and post-reset-suppressed attempts. **A record proves only that the instrumented custom-AppID local proxy was invoked**, not that official Steam displayed a progress toast.
- `trace_start` confirms instrumentation began, not that any progress API call happened.

The existing match-scoped `achievement_log` semantics stay exactly unchanged. RASC must **never** read this research file as a source for notifications. Genuine AppID 730 original Steam User Stats path is forwarded unchanged; server-side proxies are excluded from new tracing. When disabled (default), avoid extra stat reads and trace I/O in the hot path.

## For the user's next capture

1. Back up the current `achievements.txt` before destructive reset. Set `achievement_api_trace=1` and restart CS:GO. Confirm `trace_start` in `csgo_gc/achievement_api_trace.jsonl`.
2. Play one or more local matches involving counter achievements (kills, economy, round wins). Exit normally. The new trace **survives** match-end truncation.
3. Search with PowerShell: `Select-String -Path ".\\csgo_gc\\achievement_api_trace.jsonl" -Pattern '"type":"progress_attempt"'`. Also inspect `stat_call` and `achievement_call` to build independently verified stat→award mappings.
4. Stop the game before backing up/removing a full trace. Disable the extra config after studying a few matches because synchronous per-event writes add I/O on the calling game thread. No native gameplay performance profiling yet.
5. Review source trace before sharing because local stat and achievement identifiers are logged. No Steam IDs or chat intentionally included.

## Preparing an OWN progress signal — no invented original calls

RASC already has a **presentation-only** `AchievementProgressMilestonePolicy.HighestNewMilestone(previous,current,target,hasPriorValidCount)` with 25/50/75 thresholds, never generating live notifications by itself. This campaign retains that safety boundary and declares future **`derived_progress`** events as a **separate provenance class**, not `progress_call` or `progress_attempt`.

Planned (not activated) contract:
- `source: "derived_from_verified_stat_mapping"`, achievement ID, exact stat ID, observed prior/current stat values and goal, crossed percent, source evidence reference, independent series/event identity. Require explicit per-achievement stat+target mapping with evidence; never derive from names/descriptions alone.
- Only derive from two *accepted*, non-reset-blocked observations of the **same named integer stat** in the **same session/baseline**; first observation is silent. Block on invalid/missing values, counter decreases/reset, invalid target, already-earned achievement, or progress >= completion.
- Deduplicate 25/50/75 milestones per achievement and match profile, and prefer the highest threshold on a jump; no backlog of missed startup progress.
- Preserve authoritative `progress_attempt` observations separately. Never rewrite game files, call Steam methods or label a synthetic notification as an original game API call.
- Integration of inference to RASC's popup/notification queue remains **OFF** until owner reviews actual source fixtures and approves mapping and display policy. Stat names and targets are currently **NOT VERIFIED**.

## Tests / remaining gates

C++ standalone journal writer test: persistent path retains records across match start/end/reset, all 3 API attempt classes, accepted vs blocked status, float infinity bit fidelity, multiple GC process sessions, source type; previous match-journal tests preserved.

**Exact-source CI:** [Full Build Actions 45111842581](https://github.com/thgillwtnorizoh/csgo-gc/actions/runs/45111842581) **SUCCESS** at `888ff10eb7b02c7fcd193c151addc84bb956c425` — CTest **23/23 Windows, 23/23 macOS, 23/23 Linux, plus 1/1 Linux 32-bit**, **70/70 executions** with no failures. All three native platform builds and packaging succeeded. Artifacts: Windows `14790352028`, macOS `14790217634`, Linux `14789594498`. [Fast Achievement Log Checks 45111842588](https://github.com/thgillwtnorizoh/csgo-gc/actions/runs/45111842588) **SUCCESS**: Windows, macOS, Linux (3/3 additional standalone platform checks). No historical tests deleted. Tested tree identical to published main merge tree. No physical original game execution or independent Work 1 QA was performed.

**Discrepancies:** No proof of original CS:GO `IndicateAchievementProgress` calls. Research trace is capped; if 8 MiB reached it refuses further appends; it is not a reliable high-volume lossless audit. Multithreaded same-process writes serialized; other processes or sudden power loss may still lose data. No host-thread/pipe IDs exposed to avoid privacy/fragility. Subsystem **OPEN**; no owner acceptance claimed.

**Next bounded task / actor:** Owner capture `csgo_gc/achievement_api_trace.jsonl` during an actual local CS:GO match after enabling config and restarting; Work 1 independently identify/verify exact named stat→achievement ID and goal mappings and examine absence/presence of `progress_attempt`; Work 2 implements an explicitly owner-approved derived progress producer only after mapping evidence and acceptance thresholds. Live popup integration and physical runtime remain **OPEN / NOT VERIFIED**.
