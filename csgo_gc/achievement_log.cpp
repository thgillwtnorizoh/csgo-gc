#include "achievement_log.h"

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <string>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#include <process.h>
#else
#include <unistd.h>
#endif

namespace AchievementLog
{
namespace
{
// The GC supports macOS 10.13. libc++ std::filesystem needs macOS 10.15,
// so use the OS directory and stat functions already available on 10.13.
bool EnsureParentDirectories(std::string_view path)
{
    const size_t lastSeparator = path.find_last_of("/\\");
    if (lastSeparator == std::string_view::npos)
        return true;

    // Walk each path component to support both a relative GC directory and
    // nested/absolute paths used by standalone test fixtures.
    for (size_t end = 0; end <= lastSeparator; ++end)
    {
        if (end != lastSeparator && path[end] != '/' && path[end] != '\\')
            continue;

        const std::string directory{ path.substr(0, end) };
        if (directory.empty() || directory == "." || directory == ".."
            || directory.back() == ':')
            continue;
#ifdef _WIN32
        const int result = _mkdir(directory.c_str());
#else
        const int result = mkdir(directory.c_str(), 0755);
#endif
        if (result != 0 && errno != EEXIST)
            return false;
    }
    return true;
}

bool GetExistingSize(const std::string &path, uint64_t &size)
{
#ifdef _WIN32
    struct _stat64 info{};
    const int result = _stat64(path.c_str(), &info);
#else
    struct stat info{};
    const int result = stat(path.c_str(), &info);
#endif
    if (result != 0)
    {
        if (errno != ENOENT)
            return false;
        size = 0;
        return true;
    }
    if (info.st_size < 0)
        return false;
    size = static_cast<uint64_t>(info.st_size);
    return true;
}

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

bool Writer::BeginMatch()
{
    std::lock_guard<std::mutex> lock{ m_mutex };
    if (m_matchActive) return true;
    if (!EnsureParentDirectories(m_path)) return false;
    FILE *file = std::fopen(m_path.c_str(), "wb");
    if (!file) return false;
    if (std::fclose(file) != 0) return false;
    m_matchActive = true;
    // Explicit join marker gives read-only consumers a reliable trigger for
    // a ONE-TIME achievements.txt baseline without polling it every second.
    if (AppendLocked("match_start", "\"reason\":\"join\"")) return true;
    m_matchActive = false;
    return false;
}

bool Writer::EndMatch()
{
    std::lock_guard<std::mutex> lock{ m_mutex };
    if (!m_matchActive) return true;
    // Stop writes even if the file cannot be cleared; report failure upstream.
    m_matchActive = false;
    FILE *file = std::fopen(m_path.c_str(), "wb");
    return file && std::fclose(file) == 0;
}

bool Writer::ClearCurrentMatch()
{
    std::lock_guard<std::mutex> lock{ m_mutex };
    if (!m_matchActive) return true;
    FILE *file = std::fopen(m_path.c_str(), "wb");
    if (!file || std::fclose(file) != 0) return false;
    // The reset is a new earned-state baseline within the same match.
    return AppendLocked("match_start", "\"reason\":\"reset\"");
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
    if (!m_matchActive) return false;
    return AppendLocked(eventType, fields);
}

// Caller owns m_mutex. Both the join marker and regular API events use the
// same sequence generator and atomic line-format rules.
bool Writer::AppendLocked(std::string_view eventType, std::string_view fields)
{
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
    if (!EnsureParentDirectories(m_path) || line.size() > m_maxBytes)
        return false;

    uint64_t currentSize{};
    if (!GetExistingSize(m_path, currentSize) ||
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
