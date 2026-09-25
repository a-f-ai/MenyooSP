#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>

namespace Http
{
	struct GameFiberSnapshot
	{
		bool started;
		bool stalled;
		std::string stage;
		uint64_t frame;
		uint64_t ageMilliseconds;
	};

	class GameFiberHeartbeat
	{
	public:
		void Mark(const std::string& stage, uint64_t frame);
		GameFiberSnapshot Snapshot(std::chrono::milliseconds stallAfter) const;

	private:
		mutable std::mutex m_mutex;
		bool m_started = false;
		std::string m_stage = "not-started";
		uint64_t m_frame = 0;
		std::chrono::steady_clock::time_point m_markedAt{};
	};

	GameFiberHeartbeat& Heartbeat();
}
