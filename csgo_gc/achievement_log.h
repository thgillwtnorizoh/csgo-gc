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
constexpr std::size_t DefaultMaxBytes = 8 * 1024 * 1024;

// Format: UTF-8 JSON Lines v1, one complete line per observed API event.
// Writers are serialized within this process, with sequence numbers per
// process session. Consumers must identify records by (session, seq).
class Writer
{
public:
    explicit Writer(std::string path = LocalPath,
                    std::size_t maxBytes = DefaultMaxBytes);

    // Called only for the local player. Creating the file does not require an unlock.
    // Repeated round starts are idempotent until the match finishes.
    bool BeginMatch();
    bool EndMatch(); // Truncate match journal at completion/disconnect.
    bool ClearCurrentMatch(); // Reset removes past match unlock signals too.
    bool Unlock(std::string_view name, uint32_t unlockSeconds);
    bool Progress(std::string_view name, uint32_t current, uint32_t maximum);
    bool StoreResult(bool success, std::size_t newlyStored);

private:
    bool Append(std::string_view eventType, std::string_view fields);
    static std::string Quote(std::string_view value);

    std::string m_path;
    std::string m_session;
    std::size_t m_maxBytes;
    std::mutex m_mutex;
    bool m_matchActive{};
    uint64_t m_nextSequence{ 1 };
};

// One writer per process, not per Steam interface/proxy.
Writer &Get();

} // namespace AchievementLog
