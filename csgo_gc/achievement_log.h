#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>

// Independent, opt-in read-side journal for RASC. This is NOT Steam user
// stats persistence. Never change Steam's return values or callbacks based
// on whether writing this diagnostic journal succeeds.
namespace AchievementLog
{

constexpr const char *LocalPath = "csgo_gc/achievement_log";
// Persistent opt-in research trace. Unlike LocalPath, this is never cleared
// by match join/end or reset. RASC must NOT treat it as an award source.
constexpr const char *ApiTracePath = "csgo_gc/achievement_api_trace.jsonl";
constexpr std::size_t DefaultMaxBytes = 8 * 1024 * 1024;

// Format: UTF-8 JSON Lines v1, one complete line per observed API event.
// Writers are serialized within this process, with sequence numbers per
// process session. Consumers must identify records by (session, seq).
enum class Retention { MatchScoped, Persistent };

class Writer
{
public:
    explicit Writer(std::string path = LocalPath,
                    std::size_t maxBytes = DefaultMaxBytes,
                    Retention retention = Retention::MatchScoped);

    // Called only for the local player. Creating the file does not require an unlock.
    // Repeated round starts are idempotent until the match finishes.
    bool BeginMatch();
    bool EndMatch(); // Truncate match journal at completion/disconnect.
    bool ClearCurrentMatch(); // Reset removes past match unlock signals too.
    bool Unlock(std::string_view name, uint32_t unlockSeconds);
    bool Progress(std::string_view name, uint32_t current, uint32_t maximum);
    bool StoreResult(bool success, std::size_t newlyStored);

    // Research only: actual game-facing SteamUserStatsProxy method attempts,
    // including failures and post-reset suppression. No synthetic events are
    // recorded by this API. Use an independent persistent Writer instance.
    bool TraceStart();
    bool StatCall(std::string_view name, int32_t before, int32_t requested,
                  int32_t after, bool beforeValid, bool afterValid,
                  bool accepted, bool blocked);
    bool FloatStatCall(std::string_view name, float before, float requested,
                       float after, bool beforeValid, bool afterValid,
                       bool accepted, bool blocked);
    bool AchievementCall(std::string_view name, bool beforeValid, bool beforeUnlocked,
                         bool accepted, bool newlyUnlocked, bool blocked);
    bool ProgressAttempt(std::string_view name, uint32_t current, uint32_t maximum,
                         bool accepted, bool callbackQueued, bool blocked);

private:
    bool Append(std::string_view eventType, std::string_view fields);
    bool AppendLocked(std::string_view eventType, std::string_view fields);
    static std::string Quote(std::string_view value);
    static std::string BaseFields(std::string_view name, bool accepted, bool blocked);

    std::string m_path;
    std::string m_session;
    std::size_t m_maxBytes;
    Retention m_retention;
    std::mutex m_mutex;
    bool m_matchActive{};
    uint64_t m_nextSequence{ 1 };
};

// One writer per process, not per Steam interface/proxy.
Writer &Get();
// Produces a standalone capped diagnostic trace. Only call when the explicit
// achievement_api_trace config is enabled on the custom-AppID local proxy.
Writer &GetApiTrace();

} // namespace AchievementLog
