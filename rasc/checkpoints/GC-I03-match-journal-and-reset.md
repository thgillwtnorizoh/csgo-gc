# RASC / GC-I03 — Match-scoped achievement journal and full local stats reset

Project / Campaign / Actor / Assignment: CS:GO Achievements Resurrection / GC-I03 / Work 2 / journal lifecycle and reset semantics.
Starting SHA: `c2b4da73442f9220f1f410845c22ba88343c50ec`. Branch: `rasc/match-journal-and-full-stats-reset`. Published SHA / tested SHA: consult PR build job and branch head.

Status: ENGINEERING IMPLEMENTED; automated CI and legacy game runtime NOT YET VERIFIED when authored.

Functional changes:
- Opt-in `achievement_log` creates/truncates an empty log on the first local `player_connect_full` (matching local userid), or first `round_start` fallback. Round starts do not truncate active matches.
- `cs_win_panel_match`, `cs_match_end_restart`, and `cs_game_disconnected` truncate the journal and stop appends until next match.
- In the custom AppID path, `reset_achievements confirm` clears both all local unlocked achievements AND stored integer/float stats, with in-memory rollback on failed persistence. Pending unlock callbacks are cleared and journal is truncated on success. Genuine Steam achievements and inventory remain untouched.
- Back up `csgo_gc/achievements.txt`. This reset is intentionally destructive to locally recorded numeric progress. Stale stats cached by the game itself may still require restarting the game to prevent immediate re-awards; this cannot be asserted solved without original runtime proof.
- Non-atomic existing KeyValue save, journal truncation failure, event delivery latency, and unknown original client end-event availability are disclosed limitations.

Development tests extended for begin/idempotence/end/next-match journal lifecycle and zero stats after reset/reload, plus failure rollback.
Tests: GitHub Actions full cross-platform Build / CTest and standalone C++20 journal fast checks REQUIRED. Exact commands: repository Build workflow and `ctest --test-dir build --output-on-failure`.
Independent original-client GUI qualification: NOT RUN.
Subsystem: OPEN; owner acceptance pending.
