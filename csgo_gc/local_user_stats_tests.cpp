#include "stdafx.h"
#include "local_user_stats.h"
#include "test_filesystem.h"

#include <cstdio>

namespace Platform
{

void Print(const char *, ...)
{
}

}

namespace
{

constexpr const char *TestDirectory = "local_user_stats_test";
constexpr const char *TestPath = "local_user_stats_test/user_stats.txt";

bool Check(bool condition, const char *expression, int line)
{
    if (!condition)
    {
        fprintf(stderr, "Check failed at line %d: %s\n", line, expression);
    }
    return condition;
}

#define CHECK(expression) Check((expression), #expression, __LINE__)

void ResetStorage()
{
    TestFilesystem::RemoveFile(TestPath);
    TestFilesystem::RemoveDirectory(TestDirectory);
    TestFilesystem::RemoveFile(TestDirectory);
}

bool WriteBytes(std::string_view data)
{
    if (!TestFilesystem::MakeDirectory(TestDirectory))
    {
        return false;
    }

    FILE *file = fopen(TestPath, "wb");
    if (!file)
    {
        return false;
    }
    bool success = fwrite(data.data(), 1, data.size(), file) == data.size();
    success &= fclose(file) == 0;
    return success;
}

bool RequiresLoadAndProvidesDefaults()
{
    ResetStorage();
    LocalUserStats::Store stats{ TestPath };
    int32_t integerValue = 42;
    float floatValue = 42.0f;
    bool achieved = true;
    uint32_t unlockTime = 42;

    return CHECK(!stats.GetStat("STAT", integerValue))
        && CHECK(!stats.GetAchievement("ACHIEVEMENT", achieved, unlockTime))
        && CHECK(stats.Load() == LocalUserStats::LoadResult::NotFound)
        && CHECK(stats.IsLoaded())
        && CHECK(stats.GetStat("STAT", integerValue))
        && CHECK(integerValue == 0)
        && CHECK(stats.GetStat("FLOAT_STAT", floatValue))
        && CHECK(floatValue == 0.0f)
        && CHECK(stats.GetAchievement("ACHIEVEMENT", achieved, unlockTime))
        && CHECK(!achieved)
        && CHECK(unlockTime == 0);
}

bool RoundTripsStatsAndAchievements()
{
    ResetStorage();
    LocalUserStats::Store output{ TestPath };
    if (!CHECK(output.Load() == LocalUserStats::LoadResult::NotFound)
        || !CHECK(output.SetStat("TOTAL_KILLS", 1729))
        || !CHECK(output.SetStat("AVERAGE", 1.25f))
        || !CHECK(output.SetAchievement("WIN_BOMB_PLANT", 1234567890)))
    {
        return false;
    }

    std::vector<std::string> storedAchievements;
    if (!CHECK(output.Save(storedAchievements))
        || !CHECK(storedAchievements.size() == 1)
        || !CHECK(storedAchievements[0] == "WIN_BOMB_PLANT"))
    {
        return false;
    }

    storedAchievements.emplace_back("sentinel");
    if (!CHECK(output.Save(storedAchievements)) || !CHECK(storedAchievements.empty()))
    {
        return false;
    }

    LocalUserStats::Store input{ TestPath };
    int32_t integerValue{};
    float floatValue{};
    bool achieved{};
    uint32_t unlockTime{};
    return CHECK(input.Load() == LocalUserStats::LoadResult::Success)
        && CHECK(input.GetStat("TOTAL_KILLS", integerValue))
        && CHECK(integerValue == 1729)
        && CHECK(input.GetStat("AVERAGE", floatValue))
        && CHECK(floatValue == 1.25f)
        && CHECK(input.GetAchievement("WIN_BOMB_PLANT", achieved, unlockTime))
        && CHECK(achieved)
        && CHECK(unlockTime == 1234567890);
}

bool EnforcesStatTypesAndValidNames()
{
    ResetStorage();
    LocalUserStats::Store stats{ TestPath };
    if (!CHECK(stats.Load() == LocalUserStats::LoadResult::NotFound)
        || !CHECK(stats.SetStat("INTEGER", 7))
        || !CHECK(stats.SetStat("FLOAT", 2.5f)))
    {
        return false;
    }

    int32_t integerValue{};
    float floatValue{};
    std::string tooLong(128, 'x');
    return CHECK(!stats.GetStat("INTEGER", floatValue))
        && CHECK(!stats.GetStat("FLOAT", integerValue))
        && CHECK(!stats.SetStat("", 1))
        && CHECK(!stats.SetStat("INVALID\nNAME", 1))
        && CHECK(!stats.SetStat("INVALID\"NAME", 1))
        && CHECK(!stats.SetAchievement(tooLong, 1));
}

bool ClearAndResetFollowSteamSemantics()
{
    ResetStorage();
    LocalUserStats::Store stats{ TestPath };
    if (!CHECK(stats.Load() == LocalUserStats::LoadResult::NotFound)
        || !CHECK(stats.SetStat("STAT", 9))
        || !CHECK(stats.SetAchievement("FIRST", 100))
        || !CHECK(stats.SetAchievement("SECOND", 200))
        || !CHECK(stats.ClearAchievement("FIRST"))
        || !CHECK(stats.ResetAllStats(false)))
    {
        return false;
    }

    int32_t value = 1;
    bool achieved{};
    uint32_t unlockTime{};
    if (!CHECK(stats.GetStat("STAT", value))
        || !CHECK(value == 0)
        || !CHECK(stats.GetAchievement("SECOND", achieved, unlockTime))
        || !CHECK(achieved)
        || !CHECK(stats.ResetAllStats(true))
        || !CHECK(stats.GetAchievement("SECOND", achieved, unlockTime))
        || !CHECK(!achieved))
    {
        return false;
    }

    std::vector<std::string> storedAchievements;
    return CHECK(stats.Save(storedAchievements))
        && CHECK(storedAchievements.empty());
}

bool ResetAchievementsAlsoClearsStatsPermanently()
{
    ResetStorage();
    LocalUserStats::Store stats{ TestPath };
    if (!CHECK(stats.Load() == LocalUserStats::LoadResult::NotFound)
        || !CHECK(stats.SetStat("TOTAL_KILLS", 1729))
        || !CHECK(stats.SetStat("AVERAGE", 1.25f))
        || !CHECK(stats.SetAchievement("ALREADY_SAVED", 100)))
    {
        return false;
    }

    std::vector<std::string> stored;
    if (!CHECK(stats.Save(stored)) || !CHECK(stored.size() == 1)
        || !CHECK(stats.SetAchievement("NOT_YET_SAVED", 200)))
    {
        return false;
    }

    size_t removedCount = 99;
    if (!CHECK(stats.ResetAchievements(removedCount))
        || !CHECK(removedCount == 2)
        || !CHECK(stats.Save(stored))
        || !CHECK(stored.empty()))
    {
        return false;
    }

    LocalUserStats::Store reloaded{ TestPath };
    int32_t kills{999};
    float average{999.0f};
    bool achieved = true;
    uint32_t unlockTime = 999;
    return CHECK(reloaded.Load() == LocalUserStats::LoadResult::Success)
        && CHECK(reloaded.GetStat("TOTAL_KILLS", kills))
        && CHECK(kills == 0)
        && CHECK(reloaded.GetStat("AVERAGE", average))
        && CHECK(average == 0.0f)
        && CHECK(reloaded.GetAchievement("ALREADY_SAVED", achieved, unlockTime))
        && CHECK(!achieved && unlockTime == 0)
        && CHECK(reloaded.GetAchievement("NOT_YET_SAVED", achieved, unlockTime))
        && CHECK(!achieved && unlockTime == 0)
        && CHECK(reloaded.ResetAchievements(removedCount))
        && CHECK(removedCount == 0);
}

bool FailedAchievementResetRestoresMemoryAndPendingUnlocks()
{
    ResetStorage();
    LocalUserStats::Store stats{ TestPath };
    if (!CHECK(stats.Load() == LocalUserStats::LoadResult::NotFound)
        || !CHECK(stats.SetStat("KEEP_ME", 7))
        || !CHECK(stats.SetAchievement("PENDING", 321))
        || !CHECK(TestFilesystem::MakeDirectory(TestDirectory))
        || !CHECK(TestFilesystem::MakeDirectory(TestPath)))
    {
        return false;
    }

    size_t removedCount = 55;
    bool achieved{};
    uint32_t unlockTime{};
    int32_t statValue{};
    const bool failed = !stats.ResetAchievements(removedCount);
    const bool retained = stats.GetAchievement("PENDING", achieved, unlockTime)
        && achieved && unlockTime == 321
        && stats.GetStat("KEEP_ME", statValue) && statValue == 7
        && removedCount == 55;
    TestFilesystem::RemoveDirectory(TestPath);
    std::vector<std::string> stored;
    return CHECK(failed)
        && CHECK(retained)
        && CHECK(stats.Save(stored))
        && CHECK(stored.size() == 1)
        && CHECK(stored[0] == "PENDING");
}

bool FailedSaveRetainsPendingAchievementCallbacks()
{
    ResetStorage();
    LocalUserStats::Store stats{ TestPath };
    if (!CHECK(stats.Load() == LocalUserStats::LoadResult::NotFound)
        || !CHECK(stats.SetAchievement("RETRY_ME", 300)))
    {
        return false;
    }

    FILE *blockingFile = fopen(TestDirectory, "wb");
    if (!CHECK(blockingFile != nullptr))
    {
        return false;
    }
    bool blocked = fclose(blockingFile) == 0;

    std::vector<std::string> storedAchievements;
    bool failedAsExpected = blocked && !stats.Save(storedAchievements);
    bool failedSaveDidNotReportAchievements = storedAchievements.empty();
    bool removedBlockingFile = remove(TestDirectory) == 0;
    bool retrySucceeded = removedBlockingFile && stats.Save(storedAchievements);

    return CHECK(blocked)
        && CHECK(failedAsExpected)
        && CHECK(failedSaveDidNotReportAchievements)
        && CHECK(removedBlockingFile)
        && CHECK(retrySucceeded)
        && CHECK(storedAchievements.size() == 1)
        && CHECK(storedAchievements[0] == "RETRY_ME");
}

bool FreshUnlockFlagPreservesStoreStatsSemantics()
{
    ResetStorage();
    LocalUserStats::Store store{ TestPath };
    bool wasNew = true;
    if (!CHECK(store.Load() == LocalUserStats::LoadResult::NotFound)
        || !CHECK(store.SetAchievement("A", 111, &wasNew))
        || !CHECK(wasNew))
        return false;

    wasNew = true;
    if (!CHECK(store.SetAchievement("A", 222, &wasNew))
        || !CHECK(!wasNew)) // repeat SetAchievement still succeeds, not a new event
        return false;

    std::vector<std::string> stored;
    if (!CHECK(store.Save(stored))
        || !CHECK(stored.size() == 1 && stored[0] == "A"))
        return false;

    bool achieved = false;
    uint32_t timestamp = 0;
    if (!CHECK(store.GetAchievement("A", achieved, timestamp))
        || !CHECK(achieved && timestamp == 111))
        return false;

    wasNew = true;
    if (!CHECK(store.SetAchievement("A", 333, &wasNew))
        || !CHECK(!wasNew)
        || !CHECK(store.Save(stored))
        || !CHECK(stored.empty()))
        return false;

    wasNew = true;
    return CHECK(store.ClearAchievement("A"))
        && CHECK(store.SetAchievement("A", 444, &wasNew))
        && CHECK(wasNew)
        && CHECK(store.Save(stored))
        && CHECK(stored.size() == 1 && stored[0] == "A");
}

bool MalformedFilesAreRejectedWithoutPartialState()
{
    ResetStorage();
    if (!CHECK(WriteBytes(
        "\"version\" \"1\"\n"
        "\"integer_stats\"\n{\n"
        "\t\"STAT\" \"not-a-number\"\n}\n")))
    {
        return false;
    }

    LocalUserStats::Store stats{ TestPath };
    int32_t value{};
    return CHECK(stats.Load() == LocalUserStats::LoadResult::InvalidFormat)
        && CHECK(!stats.IsLoaded())
        && CHECK(!stats.GetStat("STAT", value));
}

bool ProductionPathMatchesOtherLocalData()
{
    return CHECK(std::string_view{ LocalUserStats::LocalPath }
        == "csgo_gc/achievements.txt");
}

} // namespace

int main()
{
    bool success = RequiresLoadAndProvidesDefaults()
        && RoundTripsStatsAndAchievements()
        && EnforcesStatTypesAndValidNames()
        && ClearAndResetFollowSteamSemantics()
        && ResetAchievementsAlsoClearsStatsPermanently()
        && FailedAchievementResetRestoresMemoryAndPendingUnlocks()
        && FailedSaveRetainsPendingAchievementCallbacks()
        && FreshUnlockFlagPreservesStoreStatsSemantics()
        && MalformedFilesAreRejectedWithoutPartialState()
        && ProductionPathMatchesOtherLocalData();
    ResetStorage();
    return success ? 0 : 1;
}
