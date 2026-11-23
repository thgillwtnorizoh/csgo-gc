#include "achievement_log.h"

#include <algorithm>
#include <bit>
#include <limits>
#include <chrono>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace
{
namespace fs = std::filesystem;

bool Check(bool condition, const char *message)
{
    if (!condition) std::cerr << "FAILED: " << message << '\n';
    return condition;
}

std::vector<std::string> ReadLines(const fs::path &path)
{
    std::ifstream file{ path, std::ios::binary };
    std::vector<std::string> lines;
    for (std::string line; std::getline(file, line);)
        lines.push_back(std::move(line));
    return lines;
}

bool BasicEventFormatAndOrder(const fs::path &folder)
{
    const fs::path path = folder / "journal";
    AchievementLog::Writer writer{ path.string() };
    if (!Check(writer.BeginMatch(), "begin match")
        || !Check(writer.Unlock("KILL_ENEMY_LOW", 1795260000), "initial unlock")
        || !Check(writer.Progress("KILL_ENEMY_HIGH", 25, 100), "native progress")
        || !Check(writer.StoreResult(true, 1), "stored marker")
        || !Check(writer.StoreResult(false, 0), "failed store marker")
        || !Check(writer.Unlock("ESCAPE\\\"\nID", 14), "escaped id"))
        return false;

    const auto lines = ReadLines(path);
    if (!Check(lines.size() == 6, "match marker plus five complete records")) return false;
    const auto firstSessionPos = lines[0].find("\"session\":");
    if (!Check(firstSessionPos != std::string::npos, "session present")) return false;
    for (std::size_t i = 0; i < lines.size(); ++i)
    {
        if (!Check(lines[i].starts_with("{\"v\":1,\"session\":"),
                "each record declares version and session")
            || !Check(lines[i].find(
                "\"seq\":" + std::to_string(i + 1) + ",") != std::string::npos,
                "record sequence strictly increasing")
            || !Check(lines[i].find("\"time_ms\":") != std::string::npos,
                "wall time present")
            || !Check(lines[i].find("\"steady_us\":") != std::string::npos,
                "monotonic time present")
            || !Check(!lines[i].empty() && lines[i].back() == '}', "complete JSON object"))
            return false;
    }
    return Check(lines[0].find("\"type\":\"match_start\"") != std::string::npos,
            "explicit match start")
        && Check(lines[1].find("\"type\":\"unlock\"") != std::string::npos,
            "first unlock")
        && Check(lines[2].find("\"type\":\"progress_call\"") != std::string::npos,
            "real progress API event")
        && Check(lines[2].find("\"current\":25,\"maximum\":100") != std::string::npos,
            "progress counts preserved")
        && Check(lines[3].find("\"success\":true,\"newly_stored\":1") != std::string::npos,
            "successful StoreStats marker")
        && Check(lines[4].find("\"success\":false,\"newly_stored\":0") != std::string::npos,
            "failed StoreStats marker")
        && Check(lines[5].find("ESCAPE\\\\\\\"\\nID") == std::string::npos,
            "literal control characters are not written unescaped")
        && Check(lines[5].find("\\u000aID") != std::string::npos,
            "newline escaped as JSON unicode");
}

bool BoundedAndFailedWritesDoNotThrow(const fs::path &folder)
{
    const fs::path path = folder / "bounded";
    AchievementLog::Writer writer{ path.string(), 410 };
    if (!Check(writer.BeginMatch(), "bounded begin")
        || !Check(writer.Unlock("TEST", 1), "bounded initial record")) return false;
    const auto previous = fs::file_size(path);
    return Check(!writer.Unlock("TEST2", 2), "bound rejects overflow")
        && Check(fs::file_size(path) == previous, "bounded file unchanged")
        && Check(ReadLines(path).size() == 2, "no partial overflow record");
}

bool UnwritableDestinationFailsClosed(const fs::path &folder)
{
    const auto blocker = folder / "not_a_directory";
    {
        std::ofstream out{ blocker, std::ios::binary };
        out << "sentinel";
    }
    AchievementLog::Writer writer{ (blocker / "achievement_log").string() };
    return Check(!writer.BeginMatch(), "unwritable match journal start fails")
        && Check(!writer.Unlock("A", 1), "unwritable journal refuses append");
}

bool ConcurrentAppendIsSerialized(const fs::path &folder)
{
    const fs::path path = folder / "concurrent";
    AchievementLog::Writer writer{ path.string() };
    if (!Check(writer.BeginMatch(), "concurrent begin")) return false;
    std::atomic<bool> ok{ true };
    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i)
    {
        threads.emplace_back([&, i]
        {
            for (int j = 0; j < 25; ++j)
                if (!writer.Unlock("E" + std::to_string(i) + "_" +
                     std::to_string(j), 120))
                    ok.store(false);
        });
    }
    for (auto &thread : threads) thread.join();
    const auto lines = ReadLines(path);
    if (!Check(ok.load(), "all concurrent events written")
        || !Check(lines.size() == 101, "marker and exactly 100 full records"))
        return false;
    for (std::size_t i = 0; i < lines.size(); ++i)
        if (!Check(lines[i].find(
                "\"seq\":" + std::to_string(i + 1) + ",") != std::string::npos,
                "serialized concurrent order"))
            return false;
    return true;
}

bool PersistentApiTraceCapturesCallsOutsideMatchAndRetainsOnEnd(const fs::path &folder)
{
    const auto path = folder / "trace" / "achievement_api_trace.jsonl";
    using AchievementLog::Writer;
    using AchievementLog::Retention;

    Writer trace{ path.string(), AchievementLog::DefaultMaxBytes,
                  Retention::Persistent };
    if (!Check(trace.TraceStart(), "persistent trace starts without match")
        || !Check(trace.StatCall("TOTAL_KILLS", 19, 20, 20,
                true, true, true, false), "accepted stat write")
        || !Check(trace.FloatStatCall("RATIO", 1.25f,
                std::numeric_limits<float>::infinity(),
                std::numeric_limits<float>::infinity(),
                true, true, true, false), "exact float bits")
        || !Check(trace.AchievementCall("BODY_BAGGER", true, false,
                true, true, false), "new achievement call")
        || !Check(trace.ProgressAttempt("KILL_ENEMY_HIGH", 25, 100,
                false, false, false), "rejected progress observed")
        || !Check(trace.ProgressAttempt("KILL_ENEMY_HIGH", 50, 100,
                true, false, true), "reset-suppressed progress observed"))
        return false;

    const auto before = ReadLines(path);
    if (!Check(before.size() == 6, "trace and five attempts present")
        || !Check(before[0].find("\"type\":\"trace_start\"") != std::string::npos,
                "explicit trace enabled proof")
        || !Check(before[1].find("\"stat_type\":\"int32\"") != std::string::npos,
                "integer stat typed")
        || !Check(before[1].find("\"before\":19,\"requested\":20") != std::string::npos,
                "original and requested counter recorded")
        || !Check(before[1].find("\"accepted\":true,\"blocked_by_reset\":false")
            != std::string::npos, "accepted vs blocked distinct")
        || !Check(before[2].find("\"stat_type\":\"float32\"") != std::string::npos,
                "float typed")
        || !Check(before[2].find("\"requested_bits\":2139095040") != std::string::npos,
                "infinity serialized as IEEE 754 bits not invalid JSON literal")
        || !Check(before[3].find("\"type\":\"achievement_call\"") != std::string::npos,
                "achievement API attempt recorded")
        || !Check(before[3].find("\"newly_unlocked\":true") != std::string::npos,
                "newly unlocked status recorded")
        || !Check(before[4].find("\"type\":\"progress_attempt\"") != std::string::npos,
                "rejected progress call captured")
        || !Check(before[4].find("\"accepted\":false") != std::string::npos,
                "rejection distinctly recorded")
        || !Check(before[5].find("\"blocked_by_reset\":true") != std::string::npos,
                "reset suppression distinctly recorded")
        || !Check(before[5].find("\"callback_queued\":false") != std::string::npos,
                "no Steam callback on suppressed call"))
        return false;

    if (!Check(trace.EndMatch(), "persistent trace ignores match end")
        || !Check(trace.ClearCurrentMatch(), "persistent trace ignores reset")
        || !Check(trace.BeginMatch(), "persistent trace ignores join")
        || !Check(ReadLines(path) == before, "no truncated research evidence"))
        return false;

    // A later process reopens the same path with another session marker.
    Writer restart{ path.string(), AchievementLog::DefaultMaxBytes,
                    Retention::Persistent };
    if (!Check(restart.TraceStart(), "new trace session persists")
        || !Check(restart.StatCall("TOTAL_KILLS", 0, 1, 1,
                true, true, true, false), "new process appends"))
        return false;
    const auto after = ReadLines(path);
    return Check(after.size() == 8, "old trace retained across processes")
        && Check(after[6].find("\"type\":\"trace_start\"") != std::string::npos,
                 "new session marker appended")
        && Check(after[7].find("\"requested\":1") != std::string::npos,
                 "new session writes visible");
}

bool MatchLifecycleTruncatesAndSuppressesLateWrites(const fs::path &folder)
{
    const auto path = folder / "lifecycle" / "achievement_log";
    AchievementLog::Writer writer{ path.string() };
    if (!Check(!fs::exists(path), "absent before join")
        || !Check(!writer.Unlock("PREMATCH", 1), "no pre-match log")
        || !Check(writer.BeginMatch(), "join creates journal")
        || !Check(fs::exists(path) && ReadLines(path).size() == 1, "join-only marker")
        || !Check(writer.BeginMatch(), "idempotent round start")
        || !Check(writer.Unlock("A", 11), "first match award")
        || !Check(writer.BeginMatch(), "next round does not truncate")
        || !Check(ReadLines(path).size() == 2, "round retains marker plus award")
        || !Check(writer.ClearCurrentMatch(), "reset truncates match history")
        || !Check(ReadLines(path).size() == 1 &&
                 ReadLines(path)[0].find("\"reason\":\"reset\"") != std::string::npos,
                 "reset replaced old events with fresh baseline marker")
        || !Check(writer.Unlock("B", 12), "post-reset unlock")
        || !Check(writer.EndMatch(), "match end")
        || !Check(fs::file_size(path) == 0, "match end truncates bytes")
        || !Check(!writer.Unlock("POSTMATCH", 13), "no writes after match")) return false;
    return Check(writer.BeginMatch(), "next match begins")
        && Check(ReadLines(path).size() == 1, "next match starts with a marker")
        && Check(writer.Unlock("C", 14), "next match award")
        && Check(ReadLines(path).size() == 2, "next match marker and new award only");
}
} // namespace

int main()
{
    const auto folder = fs::temp_directory_path() /
        ("rasc_achievement_log_test_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(folder);
    const bool ok = BasicEventFormatAndOrder(folder)
        && BoundedAndFailedWritesDoNotThrow(folder)
        && UnwritableDestinationFailsClosed(folder)
        && ConcurrentAppendIsSerialized(folder)
        && PersistentApiTraceCapturesCallsOutsideMatchAndRetainsOnEnd(folder)
        && MatchLifecycleTruncatesAndSuppressesLateWrites(folder);
    std::error_code ignored;
    fs::remove_all(folder, ignored);
    if (ok) std::cout << "PASS achievement_log: format, ordering, progress, persistent API trace, bounds, concurrent writes\n";
    return ok ? 0 : 1;
}
