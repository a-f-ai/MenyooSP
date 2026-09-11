/*
* Menyoo PC - Grand Theft Auto V single-player trainer mod
*
* Request parsing shared by the route handlers. Everything here runs on the
* HTTP thread and signals failure by throwing ApiError, which is safe only
* there (see ApiError.h).
*/
#pragma once

#include "ApiError.h"

#include <json/single_include/nlohmann/json.hpp>

#include <cmath>
#include <string>

namespace Http::Json
{
	using json = nlohmann::json;

	inline const json& Field(const json& body, const char* name)
	{
		const auto found = body.find(name);
		if (found == body.end())
			throw ApiError(400, std::string("missing required field \"") + name + "\"");
		return *found;
	}

	inline float Number(const json& body, const char* name)
	{
		const json& value = Field(body, name);
		if (!value.is_number())
			throw ApiError(400, std::string("field \"") + name + "\" must be a number");

		const double raw = value.get<double>();
		if (!std::isfinite(raw))
			throw ApiError(400, std::string("field \"") + name + "\" must be finite");
		return static_cast<float>(raw);
	}

	inline float OptionalNumber(const json& body, const char* name, float whenAbsent)
	{
		return body.contains(name) ? Number(body, name) : whenAbsent;
	}

	inline bool OptionalBool(const json& body, const char* name, bool whenAbsent)
	{
		if (!body.contains(name))
			return whenAbsent;
		const json& value = body.at(name);
		if (!value.is_boolean())
			throw ApiError(400, std::string("field \"") + name + "\" must be a boolean");
		return value.get<bool>();
	}

	inline std::string OptionalString(const json& body, const char* name, const std::string& whenAbsent)
	{
		if (!body.contains(name))
			return whenAbsent;
		const json& value = body.at(name);
		if (!value.is_string())
			throw ApiError(400, std::string("field \"") + name + "\" must be a string");
		return value.get<std::string>();
	}

	inline int OptionalInt(const json& body, const char* name, int whenAbsent)
	{
		if (!body.contains(name))
			return whenAbsent;
		const json& value = body.at(name);
		if (!value.is_number_integer())
			throw ApiError(400, std::string("field \"") + name + "\" must be an integer");
		return value.get<int>();
	}

	// Serialising with the replacing error handler, because names come from the
	// game and from map XML that is declared ISO-8859-1 and can hold any bytes.
	inline std::string Dump(const json& payload)
	{
		return payload.dump(2, ' ', false, json::error_handler_t::replace);
	}
}
