/*
* Menyoo PC - Grand Theft Auto V single-player trainer mod
*
* Thread hand-off between the HTTP server thread and the ScriptHookV fiber.
* Every GTA native must run on the fiber, so the server thread may only
* enqueue work and wait for the fiber to report back.
*/
#pragma once

#include <chrono>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>

namespace Http
{
	// What a handler produces: an HTTP status plus a JSON body.
	struct Response
	{
		int status;
		std::string body;
	};

	struct Command
	{
		// Runs on the fiber. Must not throw: see ApiError.h.
		std::function<Response()> run;
		std::promise<Response> result;
	};

	class CommandQueue
	{
	public:
		CommandQueue(size_t maxDepth, std::chrono::milliseconds defaultTimeout);

		// Called from the HTTP thread. Blocks until the fiber has run `work`,
		// then returns whatever the fiber produced. A queue that is full, or a
		// fiber that stopped ticking, answers immediately with 429 / 503
		// instead of leaving the caller hanging.
		Response Submit(std::function<Response()> work);

		// Batch work legitimately spans many frames, so its caller sets its own
		// deadline rather than sharing the single-command one.
		Response Submit(std::function<Response()> work, std::chrono::milliseconds timeout);

		// Called from the ScriptHookV fiber once per frame.
		void Drain();

		void Stop();
		bool IsRunning() const;
		size_t Depth() const;

	private:
		mutable std::mutex m_mutex;
		std::deque<std::shared_ptr<Command>> m_pending;
		const size_t m_maxDepth;
		const std::chrono::milliseconds m_defaultTimeout;
		bool m_running;
		// A command may yield the fiber with WAIT(0), which re-enters the tick
		// loop and calls Drain again. Without this the nested call would steal
		// the queue out from under the command that is still running.
		bool m_draining;
	};

	CommandQueue& Queue();
}
