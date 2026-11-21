#include "achievement_log.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace AchievementLog
{
namespace
{
std::string MakeSessionId()
{
    const auto wall = std::chrono::system_clock::now().time_since_epoch().count();
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
#ifdef _WIN32
    const unsigned long pid = static_cast<unsigned long>(_getpid());
#else
    const unsigned long pid = static_cast<unsigned long>(getpid());
#endif
    char buffer[96]{};
    std::snprintf(buffer, sizeof(buffer), "%llx-%lx-%llx",
        static_cast<unsigned long long>(wall), pid,
        static_cast<unsigned long long>(ticks));
    return buffer;
}
} // namespace

Writer::Writer(std::string path, std::size_t maxBytes)
    : m_path{ std::move(path) }, m_session{ MakeSessionId() },
      m_maxBytes{ maxBytes }
{
}

std::string Writer::Quote(std::string_view input)
{
    static constexpr char Hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(input.size() + 2);
    result.push_back('"');
    for (unsigned char ch : input)
    {
        if (ch == '"' || ch == '\\')
        {
            result.push_back('\\');
            result.push_back(static_cast<char>(ch));
        }
        else if (ch < 0x20)
        {
            result.append("\\u00");
            result.push_back(Hex[ch >> 4]);
            result.push_back(Hex[ch & 15]);
        }
        else
        {
            result.push_back(static_cast<char>(ch));
        }
    }
    result.push_back('"');
    return result;
}

bool Writer::Unlock(std::string_view name, uint32_t unlockSeconds)
{
    return Append("unlock", "\"id\":" + Quote(name) +
        ",\"unlock_seconds\":" + std::to_string(unlockSeconds));
}

bool Writer::Progress(std::string_view name, uint32_t current, uint32_t maximum)
{
    return Append("progress_call", "\"id\":" + Quote(name) +
        ",\"current\":" + std::to_string(current) +
        ",\"maximum\":" + std::to_string(maximum));
}

bool Writer::StoreResult(bool success, std::size_t newlyStored)
{
    return Append("stats_store", std::string{ "\"success\":" } +
        (success ? "true" : "false") +
        ",\"newly_stored\":" + std::to_string(newlyStored));
}

bool Writer::Append(std::string_view eventType, std::string_view fields)
{
    std::lock_guard<std::mutex> lock{ m_mutex };

    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    const auto steady = std::chrono::steady_clock::now().time_since_epoch();
    const auto steadyUs = std::chrono::duration_cast<std::chrono::microseconds>(steady).count();
    const uint64_t seq = m_nextSequence++;
    std::string line = "{\"v\":1,\"session\":" + Quote(m_session) +
        ",\"seq\":" + std::to_string(seq) +
        ",\"time_ms\":" + std::to_string(ms) +
        ",\"steady_us\":" + std::to_string(steadyUs) +
        ",\"type\":" + Quote(eventType) + "," + std::string{ fields } + "}\n";

    // If logging fails, NEVER interfere with CS:GO's SetAchievement /
    // IndicateAchievementProgress / StoreStats return values or callbacks.
    // Cap growth rather than silently rotating data RASC might not yet read.
    std::error_code error;
    const std::filesystem::path path{ m_path };
    const auto parent = path.parent_path();
    if (!parent.empty())
    {
        std::filesystem::create_directories(parent, error);
        if (error) return false;
    }
    const bool exists = std::filesystem::exists(path, error);
    if (error) return false;
    const auto currentSize = exists ? std::filesystem::file_size(path, error) : 0;
    if (error || line.size() > m_maxBytes ||
        currentSize > m_maxBytes - line.size())
        return false;

    FILE *file = std::fopen(m_path.c_str(), "ab");
    if (!file) return false;
    const bool written = std::fwrite(line.data(), 1, line.size(), file) == line.size();
    // fclose flushes the record; we deliberately avoid a per-event fsync on
    // the game thread. Sudden power loss may lose the newest journal record,
    // independently of Steam/GC persistence.
    const bool closed = std::fclose(file) == 0;
    return written && closed;
}

Writer &Get()
{
    static Writer writer;
    return writer;
}
} // namespace AchievementLog
