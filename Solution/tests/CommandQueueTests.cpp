/*
* Behaviour tests for the HTTP bridge's thread hand-off.
*
* CommandQueue is portable C++, so these run on any host compiler without a
* game, a Windows SDK or ScriptHookV. Run them with Solution/tests/run_tests.sh.
*/
#include "CommandQueue.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

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

	Http::Response Ok(const std::string& body)
	{
		return Http::Response{ 200, body };
	}

	// A command submitted from another thread is executed only by Drain, and
	// its result comes back to the submitter.
	void SubmittedWorkRunsOnTheDrainingThread()
	{
		std::printf("submitted work runs on the draining thread\n");
		Http::CommandQueue queue(64, 2000ms);

		std::atomic<std::thread::id> ranOn{};
		std::atomic<bool> done{ false };
		Http::Response result{};

		std::thread submitter([&] {
			result = queue.Submit([&]() -> Http::Response {
				ranOn = std::this_thread::get_id();
				return Ok(R"({"value":42})");
			});
			done = true;
		});

		// Give the submitter time to enqueue and block.
		std::this_thread::sleep_for(50ms);
		Check(!done, "submitter blocks until the queue is drained");
		Check(queue.Depth() == 1, "the command is queued while waiting");

		const std::thread::id drainThread = std::this_thread::get_id();
		queue.Drain();
		submitter.join();

		Check(done, "submitter is released by Drain");
		Check(result.status == 200, "status travels back to the submitter");
		Check(result.body == R"({"value":42})", "body travels back to the submitter");
		Check(ranOn == drainThread, "the command body ran on the draining thread, not the submitter");
		Check(queue.Depth() == 0, "the queue is empty after draining");
	}

	// Commands must report failure by returning a status, because an
	// exception raised on the ScriptHookV fiber kills the script no matter
	// who catches it. Drain deliberately has no handler, so this test pins
	// the contract: a returned failure travels back intact.
	void FailureStatusesTravelBackAsValues()
	{
		std::printf("a command reports failure by returning a status\n");
		Http::CommandQueue queue(64, 2000ms);

		Http::Response result{};
		std::thread submitter([&] {
			result = queue.Submit([]() -> Http::Response {
				return Http::Response{ 422, R"({"error":"model not installed"})" };
			});
		});

		std::this_thread::sleep_for(50ms);
		queue.Drain();
		submitter.join();

		Check(result.status == 422, "the failure status is preserved");
		Check(result.body.find("model not installed") != std::string::npos,
			"the failure message reaches the caller");
	}

	// A game thread that never ticks must not hold connections open.
	void ABlockedGameThreadTimesOut()
	{
		std::printf("a blocked game thread times out instead of hanging\n");
		Http::CommandQueue queue(64, 150ms);

		const auto started = std::chrono::steady_clock::now();
		const Http::Response result = queue.Submit([]() -> Http::Response {
			return Ok("never reached");
		});
		const auto elapsed = std::chrono::steady_clock::now() - started;

		Check(result.status == 503, "a stalled fiber answers 503");
		Check(result.body.find("did not respond") != std::string::npos,
			"the 503 explains that the game thread is unresponsive");
		Check(elapsed >= 150ms, "the caller waited for the timeout");
		Check(elapsed < 2s, "the caller was not left hanging");
	}

	void AFullQueueIsRejected()
	{
		std::printf("a full queue is rejected rather than growing\n");
		Http::CommandQueue queue(2, 100ms);

		// Two submitters fill the queue and stay blocked, since nothing drains.
		std::vector<std::thread> blocked;
		for (int i = 0; i < 2; ++i)
		{
			blocked.emplace_back([&] {
				queue.Submit([]() -> Http::Response { return Ok("{}"); });
			});
		}
		std::this_thread::sleep_for(50ms);
		Check(queue.Depth() == 2, "the queue holds its maximum depth");

		const Http::Response rejected = queue.Submit([]() -> Http::Response { return Ok("{}"); });
		Check(rejected.status == 429, "the overflowing command is rejected with 429");
		Check(rejected.body.find("queue is full") != std::string::npos,
			"the 429 says the queue is full");

		for (auto& thread : blocked)
			thread.join();
	}

	// Shutting down must release waiters instead of making them wait out the
	// full timeout.
	void StopReleasesWaitingCallers()
	{
		std::printf("Stop releases callers that are already waiting\n");
		Http::CommandQueue queue(64, 5000ms);

		Http::Response result{};
		std::thread submitter([&] {
			result = queue.Submit([]() -> Http::Response { return Ok("{}"); });
		});

		std::this_thread::sleep_for(50ms);
		const auto started = std::chrono::steady_clock::now();
		queue.Stop();
		submitter.join();
		const auto elapsed = std::chrono::steady_clock::now() - started;

		Check(result.status == 503, "a waiter is answered 503 on shutdown");
		Check(elapsed < 1s, "the waiter is released immediately, not after the timeout");
		Check(!queue.IsRunning(), "the queue reports that it stopped");

		const Http::Response afterStop = queue.Submit([]() -> Http::Response { return Ok("{}"); });
		Check(afterStop.status == 503, "submitting after Stop is refused");
	}

	// Many concurrent submitters must all get their own result back.
	void ConcurrentSubmittersEachGetTheirOwnResult()
	{
		std::printf("concurrent submitters each get their own result\n");
		Http::CommandQueue queue(256, 5000ms);

		constexpr int kCallers = 32;
		std::vector<std::thread> callers;
		std::vector<Http::Response> results(kCallers);

		for (int i = 0; i < kCallers; ++i)
		{
			callers.emplace_back([&, i] {
				results[i] = queue.Submit([i]() -> Http::Response {
					return Ok(std::to_string(i));
				});
			});
		}

		// Drain repeatedly until every caller has been served.
		for (int spin = 0; spin < 200; ++spin)
		{
			queue.Drain();
			std::this_thread::sleep_for(5ms);
		}
		for (auto& thread : callers)
			thread.join();

		bool allMatched = true;
		for (int i = 0; i < kCallers; ++i)
		{
			if (results[i].status != 200 || results[i].body != std::to_string(i))
				allMatched = false;
		}
		Check(allMatched, "all 32 callers received exactly their own payload");
	}
}

int main()
{
	SubmittedWorkRunsOnTheDrainingThread();
	FailureStatusesTravelBackAsValues();
	ABlockedGameThreadTimesOut();
	AFullQueueIsRejected();
	StopReleasesWaitingCallers();
	ConcurrentSubmittersEachGetTheirOwnResult();

	if (g_failures == 0)
	{
		std::printf("\nall checks passed\n");
		return 0;
	}
	std::printf("\n%d check(s) failed\n", g_failures);
	return 1;
}
