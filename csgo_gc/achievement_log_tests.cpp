#include "achievement_log.h"

#include <algorithm>
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
    if (!Check(lines.size() == 5, "five complete records")) return false;
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
    return Check(lines[0].find("\"type\":\"unlock\"") != std::string::npos,
            "first unlock")
        && Check(lines[1].find("\"type\":\"progress_call\"") != std::string::npos,
            "real progress API event")
        && Check(lines[1].find("\"current\":25,\"maximum\":100") != std::string::npos,
            "progress counts preserved")
        && Check(lines[2].find("\"success\":true,\"newly_stored\":1") != std::string::npos,
            "successful StoreStats marker")
        && Check(lines[3].find("\"success\":false,\"newly_stored\":0") != std::string::npos,
            "failed StoreStats marker")
        && Check(lines[4].find("ESCAPE\\\\\\\"\\nID") == std::string::npos,
            "literal control characters are not written unescaped")
        && Check(lines[4].find("\\u000aID") != std::string::npos,
            "newline escaped as JSON unicode");
}

bool BoundedAndFailedWritesDoNotThrow(const fs::path &folder)
{
    const fs::path path = folder / "bounded";
    AchievementLog::Writer writer{ path.string(), 220 };
    if (!Check(writer.BeginMatch(), "bounded begin")
        || !Check(writer.Unlock("TEST", 1), "bounded initial record")) return false;
    const auto previous = fs::file_size(path);
    return Check(!writer.Unlock("TEST2", 2), "bound rejects overflow")
        && Check(fs::file_size(path) == previous, "bounded file unchanged")
        && Check(ReadLines(path).size() == 1, "no partial overflow record");
}

bool UnwritableDestinationFailsClosed(const fs::path &folder)
{
    const auto blocker = folder / "not_a_directory";
    {
        std::ofstream out{ blocker, std::ios::binary };
        out << "sentinel";
    }
    AchievementLog::Writer writer{ (blocker / "achievement_log").string() };
    return Check(!writer.Unlock("A", 1), "unwritable journal returns false");
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
        || !Check(lines.size() == 100, "exactly 100 full records"))
        return false;
    for (std::size_t i = 0; i < lines.size(); ++i)
        if (!Check(lines[i].find(
                "\"seq\":" + std::to_string(i + 1) + ",") != std::string::npos,
                "serialized concurrent order"))
            return false;
    return true;
}

bool MatchLifecycleTruncatesAndSuppressesLateWrites(const fs::path &folder)
{
    const auto path = folder / "lifecycle" / "achievement_log";
    AchievementLog::Writer writer{ path.string() };
    if (!Check(!fs::exists(path), "absent before join")
        || !Check(!writer.Unlock("PREMATCH", 1), "no pre-match log")
        || !Check(writer.BeginMatch(), "join creates journal")
        || !Check(fs::exists(path) && fs::file_size(path) == 0, "empty log on join")
        || !Check(writer.BeginMatch(), "idempotent round start")
        || !Check(writer.Unlock("A", 11), "first match award")
        || !Check(writer.BeginMatch(), "next round does not truncate")
        || !Check(ReadLines(path).size() == 1, "round retains award")
        || !Check(writer.ClearCurrentMatch(), "reset truncates match history")
        || !Check(fs::file_size(path) == 0, "reset cleared bytes")
        || !Check(writer.Unlock("B", 12), "post-reset unlock")
        || !Check(writer.EndMatch(), "match end")
        || !Check(fs::file_size(path) == 0, "match end truncates bytes")
        || !Check(!writer.Unlock("POSTMATCH", 13), "no writes after match")) return false;
    return Check(writer.BeginMatch(), "next match begins")
        && Check(fs::file_size(path) == 0, "next match starts empty")
        && Check(writer.Unlock("C", 14), "next match award")
        && Check(ReadLines(path).size() == 1, "only new match data");
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
        && MatchLifecycleTruncatesAndSuppressesLateWrites(folder);
    std::error_code ignored;
    fs::remove_all(folder, ignored);
    if (ok) std::cout << "PASS achievement_log: format, ordering, progress, bounds, concurrent writes\n";
    return ok ? 0 : 1;
}
