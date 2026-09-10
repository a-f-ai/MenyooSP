/*
* Menyoo PC - Grand Theft Auto V single-player trainer mod
*/
#include "CommandQueue.h"

#include "../Util/FileLogger.h"

#include <utility>

namespace Http
{
	namespace
	{
		constexpr size_t kMaxQueueDepth = 64;
		constexpr std::chrono::milliseconds kFiberTimeout{ 2000 };
	}

	ApiError::ApiError(int status, std::string message)
		: m_status(status), m_message(std::move(message))
	{
	}

	int ApiError::Status() const noexcept
	{
		return m_status;
	}

	const char* ApiError::what() const noexcept
	{
		return m_message.c_str();
	}

	CommandQueue::CommandQueue(size_t maxDepth, std::chrono::milliseconds timeout)
		: m_maxDepth(maxDepth), m_timeout(timeout), m_running(true)
	{
	}

	Response CommandQueue::Submit(std::function<Response()> work)
	{
		auto command = std::make_shared<Command>();
		command->run = std::move(work);
		auto pending = command->result.get_future();

		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (!m_running)
				return Response{ 503, R"({"error":"plugin is shutting down"})" };
			if (m_pending.size() >= m_maxDepth)
			{
				return Response{ 429, R"({"error":"command queue is full, the game thread is behind"})" };
			}
			m_pending.push_back(command);
		}

		if (pending.wait_for(m_timeout) != std::future_status::ready)
		{
			// The fiber is blocked (map load, RequestAnimDict, a pause menu).
			// Say so instead of holding the connection open.
			addlog(ige::LogType::LOG_WARNING, "HTTP command timed out waiting for the game thread");
			return Response{ 503, R"({"error":"game thread did not respond within the timeout"})" };
		}

		return pending.get();
	}

	void CommandQueue::Drain()
	{
		std::deque<std::shared_ptr<Command>> batch;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			batch.swap(m_pending);
		}

		for (auto& command : batch)
		{
			try
			{
				command->result.set_value(command->run());
			}
			catch (const ApiError& error)
			{
				command->result.set_value(Response{
					error.Status(),
					std::string(R"({"error":")") + error.what() + R"("})" });
			}
			catch (const std::exception& error)
			{
				addlog(ige::LogType::LOG_ERROR,
					std::string("HTTP command threw: ") + error.what());
				command->result.set_value(Response{
					500,
					std::string(R"({"error":"unhandled exception: )") + error.what() + R"("})" });
			}
		}
	}

	void CommandQueue::Stop()
	{
		std::deque<std::shared_ptr<Command>> batch;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_running = false;
			batch.swap(m_pending);
		}
		// Release anyone still waiting rather than letting them hit the timeout.
		for (auto& command : batch)
		{
			command->result.set_value(Response{ 503, R"({"error":"plugin is shutting down"})" });
		}
	}

	bool CommandQueue::IsRunning() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_running;
	}

	size_t CommandQueue::Depth() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_pending.size();
	}

	CommandQueue& Queue()
	{
		static CommandQueue instance(kMaxQueueDepth, kFiberTimeout);
		return instance;
	}
}
