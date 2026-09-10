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

	// Thrown by handler code to answer with a specific status. Anything else
	// that escapes a handler becomes a 500 naming the exception.
	class ApiError : public std::exception
	{
	public:
		ApiError(int status, std::string message);

		int Status() const noexcept;
		const char* what() const noexcept override;

	private:
		int m_status;
		std::string m_message;
	};

	struct Command
	{
		std::function<Response()> run;
		std::promise<Response> result;
	};

	class CommandQueue
	{
	public:
		CommandQueue(size_t maxDepth, std::chrono::milliseconds timeout);

		// Called from the HTTP thread. Blocks until the fiber has run `work`,
		// then returns whatever the fiber produced. A queue that is full, or a
		// fiber that stopped ticking, answers immediately with 429 / 503
		// instead of leaving the caller hanging.
		Response Submit(std::function<Response()> work);

		// Called from the ScriptHookV fiber once per frame.
		void Drain();

		void Stop();
		bool IsRunning() const;
		size_t Depth() const;

	private:
		mutable std::mutex m_mutex;
		std::deque<std::shared_ptr<Command>> m_pending;
		const size_t m_maxDepth;
		const std::chrono::milliseconds m_timeout;
		bool m_running;
	};

	CommandQueue& Queue();
}
