# RASC GC-I04 — Reset write quarantine after stat/achievement reset

Project / Campaign / Actor / Assignment: CS:GO Achievements Resurrection / GC-I04 / Work 2 / suppress stale in-process grind-stat resubmission after reset.
Starting SHA: `1818eea10344afb19bcbb87847a13552422fe74c`.
Status: ENGINEERING INTEGRATION; exact branch CI and real-game runtime NOT VERIFIED at writing.
Evidence: owner observed Body Bagger, Newb World Order, War Bonds, Points in Your Favor (persistent grind achievements) appearing again immediately following successful `reset_achievements confirm`. Existing local Store::ResetAchievements clears and persists integer/float/achievement maps, but SteamUserStatsProxy::SetStat and SetAchievement still accept cached game-side calls. This is **RECONSTRUCTED** root cause, not confirmed native evidence.

New behavior: after successful custom-AppID local reset, activate process-wide and Store-local write fences. Subsequent SetStat(int/float) and SetAchievement calls return Steam-compatible valid-name successes but do NOT mutate progress or unlocks, create pending stored achievements, or generate journal events. IndicateAchievementProgress returns success for valid name but produces no callback. Fence remains until the legacy CS:GO process exits; fresh process starts with persisted zero stats and can earn new achievements. No Steam official AppID 730 mutation, and no remote inventory mutation. Failed persistence does not activate fence. RCON success includes `writes=blocked_until_restart`.

Consequence: genuine post-reset progress is intentionally suppressed until restarting the game. Do not reset mid-match if user wants new earnable progress before restart.
Tests: initial numeric and achievement progress, persisted reset, stale stat and achievement calls suppressed, fresh Store allows normal new progress, failed-save rollback. Full CI Windows/macOS/Linux and 32-bit gate required.
Limitations: CS:GO can maintain additional native achievement values outside this GC. No assertion of runtime equivalence. The guard prevents this proxy from re-recording cached awards; owner must smoke test in original client.
Discrepancies: exact game cache provenance not independently reconstructed; runtime unverified.
Next task: review exact CI, merge if passed, owner restarts game after reset to observe grind behavior.
Subsystem: OPEN pending owner acceptance.
