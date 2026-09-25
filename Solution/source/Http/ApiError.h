/*
* Menyoo PC - Grand Theft Auto V single-player trainer mod
*/
#pragma once

#include <exception>
#include <string>
#include <utility>

namespace Http
{
	// Carries a status code out of request parsing.
	//
	// This may only be thrown on the HTTP thread. Raising a C++ exception
	// inside the ScriptHookV fiber kills the script: ScriptHookV reports
	// "An exception occurred while executing 'Menyoo.asi'" and stops ticking
	// it, and with ScriptHookVDotNet's clr.dll loaded the fault surfaces
	// inside the CLR's vectored exception handler. Fiber-side code therefore
	// returns a Response instead of throwing.
	class ApiError : public std::exception
	{
	public:
		ApiError(int status, std::string message)
			: m_status(status), m_message(std::move(message))
		{
		}

		int Status() const noexcept { return m_status; }
		const char* what() const noexcept override { return m_message.c_str(); }

	private:
		int m_status;
		std::string m_message;
	};
}
