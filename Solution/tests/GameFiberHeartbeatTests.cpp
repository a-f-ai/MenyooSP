#include "GameFiberHeartbeat.h"

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

using namespace std::chrono_literals;

namespace
{
	int g_failures = 0;

	void Check(bool condition, const std::string& what)
	{
		if (condition)
		{
			std::printf("  ok   %s\n", what.c_str());
			return;
		}
		std::printf("  FAIL %s\n", what.c_str());
		++g_failures;
	}
}

int main()
{
	Http::GameFiberHeartbeat heartbeat;

	auto beforeStart = heartbeat.Snapshot(20ms);
	Check(!beforeStart.started, "health distinguishes a fiber that has not started");
	Check(!beforeStart.stalled, "a not-yet-started fiber is not mislabeled as stalled");

	heartbeat.Mark("camera-paths", 41);
	auto live = heartbeat.Snapshot(20ms);
	Check(live.started, "marking a stage starts the heartbeat");
	Check(!live.stalled, "a fresh heartbeat is healthy");
	Check(live.stage == "camera-paths", "health reports the semantic tick stage");
	Check(live.frame == 41, "health reports the current frame counter");

	std::this_thread::sleep_for(30ms);
	auto stalled = heartbeat.Snapshot(20ms);
	Check(stalled.stalled, "an expired heartbeat is reported as stalled");
	Check(stalled.stage == "camera-paths", "a stall preserves the last entered stage");

	return g_failures == 0 ? 0 : 1;
}
