#include "MapEnvironment.h"
#include "../Menu/Routine.h"
#include "../Natives/natives2.h"
#include "../Scripting/Game.h"
#include "../Util/FileLogger.h"
#include <json/single_include/nlohmann/json.hpp>

namespace MapEnvironment
{
    namespace
    {
        bool active = false;
        std::string ownerMap;
    }

    bool IsActive() { return active; }

    bool Acquire(const std::string& mapPath)
    {
        active = true;
        ownerMap = mapPath;
        CLEAR_OVERRIDE_WEATHER();
        SET_WEATHER_TYPE_NOW_PERSIST("EXTRASUNNY");
        SET_OVERRIDE_WEATHER("EXTRASUNNY");
        NETWORK_CLEAR_CLOCK_TIME_OVERRIDE();
        SET_CLOCK_TIME(12, 0, 0);
        PAUSE_CLOCK(true);
        const bool matched = GET_PREV_WEATHER_TYPE_HASH_NAME() == GET_HASH_KEY("EXTRASUNNY") &&
            GET_CLOCK_HOURS() == 12 && GET_CLOCK_MINUTES() == 0 && GET_CLOCK_SECONDS() == 0;
        addlog(matched ? ige::LogType::LOG_INFO : ige::LogType::LOG_ERROR,
            "Map environment " + std::string(matched ? "locked: " : "readback failed: ") +
            Policy + " owner=" + ownerMap);
        if (!matched) Game::Print::PrintBottomLeft("Map environment readback failed; see /world/environment and menyooLog.txt");
        return matched;
    }

    void Release(const char* reason)
    {
        if (!active) return;
        CLEAR_OVERRIDE_WEATHER();
        CLEAR_WEATHER_TYPE_PERSIST();
        PAUSE_CLOCK(false);
        NETWORK_CLEAR_CLOCK_TIME_OVERRIDE();
        active = false;
        addlog(ige::LogType::LOG_INFO, "Map environment released: owner=" + ownerMap + " reason=" + reason);
        ownerMap.clear();
    }

    bool RejectManualChange()
    {
        if (!active) return false;
        addlog(ige::LogType::LOG_WARNING, "Weather/time change rejected: map environment owns " + ownerMap);
        Game::Print::PrintBottomLeft("Weather/time locked by active map. Unload the map first.");
        return true;
    }

    Http::Response Describe()
    {
        using json = nlohmann::json;
        Hash from, to;
        float blend;
        GET_CURR_WEATHER_STATE(&from, &to, &blend);
        const auto previous = GET_PREV_WEATHER_TYPE_HASH_NAME();
        const auto next = GET_NEXT_WEATHER_TYPE_HASH_NAME();
        const auto sunny = GET_HASH_KEY("EXTRASUNNY");
        const int hour = GET_CLOCK_HOURS(), minute = GET_CLOCK_MINUTES(), second = GET_CLOCK_SECONDS();
        json policy{{"active", active}};
        if (active)
        {
            policy["id"] = Policy;
            policy["ownerMap"] = ownerMap;
            policy["weather"] = "EXTRASUNNY";
            policy["clock"] = {{"hour", 12}, {"minute", 0}, {"second", 0}};
            policy["clockPauseRequested"] = true;
            policy["actualMatchesPolicy"] = previous == sunny && hour == 12 && minute == 0 && second == 0;
        }
        json result{
            {"policy", policy},
            {"actual", {
                {"clock", {{"hour", hour}, {"minute", minute}, {"second", second}}},
                {"weather", {{"previousHash", previous}, {"nextHash", next}, {"transitionFromHash", from},
                    {"transitionToHash", to}, {"transitionBlend", blend}, {"extraSunnyHash", sunny}}}}},
            {"menyooClockSettings", {{"pause", pauseClock}, {"hour", pauseClockH}, {"minute", pauseClockM},
                {"syncSystemTime", syncClock}, {"writersSuppressedByMap", active}}},
            {"unloadBehavior", "release weather override/persistence and native clock pause; resume normal Menyoo clock writers"}
        };
        return {200, result.dump(2, ' ', false, json::error_handler_t::replace)};
    }
}
