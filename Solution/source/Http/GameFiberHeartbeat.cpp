#include "GameFiberHeartbeat.h"

namespace Http
{
	void GameFiberHeartbeat::Mark(const std::string& stage, uint64_t frame)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_started = true;
		m_stage = stage;
		m_frame = frame;
		m_markedAt = std::chrono::steady_clock::now();
	}

	GameFiberSnapshot GameFiberHeartbeat::Snapshot(std::chrono::milliseconds stallAfter) const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (!m_started)
			return GameFiberSnapshot{ false, false, m_stage, m_frame, 0 };

		const auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now() - m_markedAt);
		return GameFiberSnapshot{
			true,
			age > stallAfter,
			m_stage,
			m_frame,
			static_cast<uint64_t>(age.count()),
		};
	}

	GameFiberHeartbeat& Heartbeat()
	{
		static GameFiberHeartbeat heartbeat;
		return heartbeat;
	}
}
