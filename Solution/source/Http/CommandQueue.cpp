/*
* Menyoo PC - Grand Theft Auto V single-player trainer mod
*/
#include "CommandQueue.h"

#include "../Util/FileLogger.h"

#include <algorithm>
#include <string>
#include <utility>

namespace Http
{
	namespace
	{
		constexpr size_t kMaxQueueDepth = 64;
		constexpr std::chrono::milliseconds kFiberTimeout{ 2000 };
	}

	CommandQueue::CommandQueue(size_t maxDepth, std::chrono::milliseconds defaultTimeout)
		: m_maxDepth(maxDepth), m_defaultTimeout(defaultTimeout), m_running(true), m_draining(false)
	{
	}

	Response CommandQueue::Submit(std::function<Response()> work)
	{
		return Submit(std::move(work), m_defaultTimeout);
	}

	Response CommandQueue::Submit(std::function<Response()> work, std::chrono::milliseconds timeout)
	{
		auto command = std::make_shared<Command>();
		command->run = std::move(work);
		auto pending = command->result.get_future();

		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (!m_running)
				return Response{ 503, R"({"error":"plugin is shutting down"})" };
			if (m_pending.size() >= m_maxDepth)
				return Response{ 429, R"({"error":"command queue is full, the game thread is behind"})" };
			m_pending.push_back(command);
		}

		if (pending.wait_for(timeout) != std::future_status::ready)
		{
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				if (command->state == Command::State::Pending)
				{
					auto queued = std::find(m_pending.begin(), m_pending.end(), command);
					if (queued != m_pending.end())
						m_pending.erase(queued);
					command->state = Command::State::Cancelled;
					addlog(ige::LogType::LOG_WARNING, "HTTP command timed out before the game thread started it");
					return Response{ 503, R"({"error":"game thread did not respond before the pending command timeout","commandState":"cancelled"})" };
				}
			}

			addlog(ige::LogType::LOG_WARNING, "HTTP command timed out waiting for the game thread");
			return Response{ 503, R"({"error":"game thread command is still running after the timeout","commandState":"running"})" };
		}

		return pending.get();
	}

	void CommandQueue::Drain()
	{
		std::deque<std::shared_ptr<Command>> batch;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (m_draining)
				return; // re-entered from a command that yielded the fiber
			m_draining = true;
			batch.swap(m_pending);
			for (auto& command : batch)
			{
				command->state = Command::State::Running;
				++m_runningCount;
			}
		}

		// Commands are written not to throw: an exception on this fiber kills
		// the script regardless of who catches it (see ApiError.h). This does
		// not change that - it rethrows - it only writes down what was thrown
		// first, so the log names the cause instead of a bare clr.dll address.
		// An access violation inside a native is not a C++ exception and will
		// not pass through here; no line in the log then means exactly that.
		for (auto& command : batch)
		{
			try
			{
				command->result.set_value(command->run());
				command->state = Command::State::Completed;
				--m_runningCount;
			}
			catch (const std::exception& error)
			{
				addlog(ige::LogType::LOG_ERROR, std::string("a command threw on the game thread: ") + error.what());
				throw;
			}
			catch (...)
			{
				addlog(ige::LogType::LOG_ERROR, "a command threw a non-std exception on the game thread");
				throw;
			}
		}

		std::lock_guard<std::mutex> lock(m_mutex);
		m_draining = false;
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
			command->state = Command::State::Cancelled;
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

	size_t CommandQueue::RunningCount() const
	{
		return m_runningCount.load();
	}

	CommandQueue& Queue()
	{
		static CommandQueue instance(kMaxQueueDepth, kFiberTimeout);
		return instance;
	}
}
