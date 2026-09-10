/*
* Menyoo PC - Grand Theft Auto V single-player trainer mod
*/
// httplib.h must come before any Menyoo header. Menyoo's headers reach
// <Windows.h> without WIN32_LEAN_AND_MEAN, which drags in winsock.h (v1);
// httplib needs winsock2.h, and the two cannot coexist. Including httplib
// first lets winsock2.h define _WINSOCKAPI_ so Windows.h skips the old one.
#include <cpp-httplib/httplib.h>

#include "HttpServer.h"

#include "CommandQueue.h"
#include "EntityApi.h"

#include "../Natives/natives2.h"
#include "../Util/FileLogger.h"

#include <json/single_include/nlohmann/json.hpp>

#include <cmath>
#include <memory>
#include <string>
#include <thread>

using json = nlohmann::json;

namespace Http::Server
{
	namespace
	{
		constexpr const char* kBindAddress = "127.0.0.1";
		constexpr int kPort = 21170;
		constexpr size_t kMaxBodyBytes = 64 * 1024;

		std::unique_ptr<httplib::Server> g_server;
		std::thread g_thread;

		// ---------------------------------------------------------------
		// Request validation. Anything missing or malformed is a 400 that
		// names the field, so a caller can correct itself without guessing.
		// ---------------------------------------------------------------

		const json& Field(const json& body, const char* name)
		{
			const auto found = body.find(name);
			if (found == body.end())
				throw ApiError(400, std::string("missing required field \"") + name + "\"");
			return *found;
		}

		float Number(const json& body, const char* name)
		{
			const json& value = Field(body, name);
			if (!value.is_number())
				throw ApiError(400, std::string("field \"") + name + "\" must be a number");

			const double raw = value.get<double>();
			if (!std::isfinite(raw))
				throw ApiError(400, std::string("field \"") + name + "\" must be finite");
			return static_cast<float>(raw);
		}

		float OptionalNumber(const json& body, const char* name, float fallbackValue)
		{
			// Absent rotation axes mean "no rotation on this axis", which is a
			// meaningful value rather than a hidden default for missing data.
			return body.contains(name) ? Number(body, name) : fallbackValue;
		}

		bool OptionalBool(const json& body, const char* name, bool fallbackValue)
		{
			if (!body.contains(name))
				return fallbackValue;
			const json& value = body.at(name);
			if (!value.is_boolean())
				throw ApiError(400, std::string("field \"") + name + "\" must be a boolean");
			return value.get<bool>();
		}

		// Accepts "a_m_y_beach_01", "0xCADD5D2D" or 3403212... — all three are
		// how the community writes models, and all three appear in map XML.
		unsigned long ParseModel(const json& body)
		{
			const json& value = Field(body, "model");

			if (value.is_number_unsigned())
				return value.get<unsigned long>();

			if (!value.is_string())
				throw ApiError(400, "field \"model\" must be a model name or a hash");

			const std::string raw = value.get<std::string>();
			if (raw.empty())
				throw ApiError(400, "field \"model\" must not be empty");

			const bool looksHex = raw.size() > 2 && raw[0] == '0' && (raw[1] == 'x' || raw[1] == 'X');
			if (looksHex)
			{
				try
				{
					return std::stoul(raw, nullptr, 16);
				}
				catch (const std::exception&)
				{
					throw ApiError(400, "field \"model\" looks like hex but will not parse: " + raw);
				}
			}

			// GET_HASH_KEY here is Menyoo's own joaat implementation, not a
			// game native, so it is safe to call off the fiber.
			return GET_HASH_KEY(raw.c_str());
		}

		int ParseType(const json& body)
		{
			const json& value = Field(body, "type");

			if (value.is_number_integer())
			{
				const int raw = value.get<int>();
				if (raw < 1 || raw > 3)
					throw ApiError(400, "field \"type\" must be 1 (ped), 2 (vehicle) or 3 (prop)");
				return raw;
			}

			if (!value.is_string())
				throw ApiError(400, "field \"type\" must be \"ped\", \"vehicle\" or \"prop\"");

			const std::string raw = value.get<std::string>();
			if (raw == "ped")     return 1;
			if (raw == "vehicle") return 2;
			if (raw == "prop")    return 3;
			throw ApiError(400, "field \"type\" must be \"ped\", \"vehicle\" or \"prop\", got \"" + raw + "\"");
		}

		EntityApi::Transform ParseTransform(const json& body, bool positionRequired)
		{
			EntityApi::Transform transform{};

			const auto position = body.find("position");
			if (position == body.end())
			{
				if (positionRequired)
					throw ApiError(400, "missing required field \"position\"");
			}
			else
			{
				if (!position->is_object())
					throw ApiError(400, "field \"position\" must be an object with x, y and z");
				transform.x = Number(*position, "x");
				transform.y = Number(*position, "y");
				transform.z = Number(*position, "z");
			}

			const auto rotation = body.find("rotation");
			if (rotation != body.end())
			{
				if (!rotation->is_object())
					throw ApiError(400, "field \"rotation\" must be an object with pitch, roll and yaw");
				transform.pitch = OptionalNumber(*rotation, "pitch", 0.0f);
				transform.roll = OptionalNumber(*rotation, "roll", 0.0f);
				transform.yaw = OptionalNumber(*rotation, "yaw", 0.0f);
			}

			return transform;
		}

		int ParseId(const std::string& raw)
		{
			try
			{
				return std::stoi(raw);
			}
			catch (const std::exception&)
			{
				throw ApiError(400, "entity id must be an integer, got \"" + raw + "\"");
			}
		}

		json ParseBody(const httplib::Request& request)
		{
			if (request.body.size() > kMaxBodyBytes)
				throw ApiError(413, "request body exceeds " + std::to_string(kMaxBodyBytes) + " bytes");

			json body = json::parse(request.body, nullptr, false);
			if (body.is_discarded())
				throw ApiError(400, "request body is not valid JSON");
			if (!body.is_object())
				throw ApiError(400, "request body must be a JSON object");
			return body;
		}

		// ---------------------------------------------------------------
		// Routing
		// ---------------------------------------------------------------

		void Write(httplib::Response& response, int status, const json& payload)
		{
			response.status = status;
			response.set_content(payload.dump(2), "application/json");
		}

		// Parses and validates on the HTTP thread, then runs `work` on the
		// fiber. Validation errors never reach the game thread.
		void Handle(const httplib::Request& request, httplib::Response& response,
			const std::function<json(const httplib::Request&)>& prepare)
		{
			try
			{
				// `prepare` closes over the parsed request and returns the
				// fiber-side work as a deferred call.
				json parsed = prepare(request);
				const Response result = Queue().Submit([parsed]() -> Response {
					// Dispatch happens inside the fiber so natives are legal.
					const std::string& action = parsed.at("__action").get_ref<const std::string&>();
					json payload;
					int status = 200;

					if (action == "list")
					{
						payload = EntityApi::ListEntities();
					}
					else if (action == "get")
					{
						payload = EntityApi::GetEntity(parsed.at("id").get<int>());
					}
					else if (action == "create")
					{
						EntityApi::CreateRequest create{};
						create.type = parsed.at("type").get<int>();
						create.model = parsed.at("model").get<unsigned long>();
						create.name = parsed.at("name").get<std::string>();
						create.dynamic = parsed.at("dynamic").get<bool>();
						create.placeOnGround = parsed.at("placeOnGround").get<bool>();
						create.transform.x = parsed.at("x").get<float>();
						create.transform.y = parsed.at("y").get<float>();
						create.transform.z = parsed.at("z").get<float>();
						create.transform.pitch = parsed.at("pitch").get<float>();
						create.transform.roll = parsed.at("roll").get<float>();
						create.transform.yaw = parsed.at("yaw").get<float>();
						payload = EntityApi::CreateEntity(create);
						status = 201;
					}
					else if (action == "transform")
					{
						EntityApi::Transform transform{};
						transform.x = parsed.at("x").get<float>();
						transform.y = parsed.at("y").get<float>();
						transform.z = parsed.at("z").get<float>();
						transform.pitch = parsed.at("pitch").get<float>();
						transform.roll = parsed.at("roll").get<float>();
						transform.yaw = parsed.at("yaw").get<float>();
						payload = EntityApi::SetTransform(parsed.at("id").get<int>(), transform);
					}
					else if (action == "delete")
					{
						payload = EntityApi::DeleteEntity(parsed.at("id").get<int>());
					}
					else
					{
						throw ApiError(500, "unrouted action \"" + action + "\"");
					}

					return Response{ status, payload.dump(2) };
				});

				response.status = result.status;
				response.set_content(result.body, "application/json");
			}
			catch (const ApiError& error)
			{
				Write(response, error.Status(), json{ { "error", error.what() } });
			}
			catch (const std::exception& error)
			{
				addlog(ige::LogType::LOG_ERROR, std::string("HTTP handler failed: ") + error.what());
				Write(response, 500, json{ { "error", std::string("unhandled exception: ") + error.what() } });
			}
		}

		json Describe()
		{
			return json{
				{ "service", "menyoo-entity-bridge" },
				{ "version", 1 },
				{ "coordinates", "GTA V world space, metres. rotation is degrees: pitch=X, roll=Y, yaw=Z" },
				{ "identity", "an entity id is its live script handle; it is valid only while the entity exists" },
				{ "endpoints", json::array({
					json{ { "method", "GET" }, { "path", "/health" },
						  { "returns", "queue depth and whether the game thread is ticking" } },
					json{ { "method", "GET" }, { "path", "/entities" },
						  { "returns", "every entity the spooner owns" } },
					json{ { "method", "POST" }, { "path", "/entities" },
						  { "body", "{type, model, position:{x,y,z}, rotation?:{pitch,roll,yaw}, name?, dynamic?, placeOnGround?}" },
						  { "returns", "201 with the created entity" } },
					json{ { "method", "GET" }, { "path", "/entities/{id}" },
						  { "returns", "one entity, 404 if unknown, 410 if it has been destroyed" } },
					json{ { "method", "PATCH" }, { "path", "/entities/{id}" },
						  { "body", "{position?:{x,y,z}, rotation?:{pitch,roll,yaw}}" },
						  { "returns", "the updated entity" } },
					json{ { "method", "DELETE" }, { "path", "/entities/{id}" },
						  { "returns", "{deleted: id}" } },
				}) },
				{ "notes", json::array({
					"type accepts \"ped\"/\"vehicle\"/\"prop\" or 1/2/3",
					"model accepts a name (\"a_m_y_beach_01\"), a hex hash (\"0xCADD5D2D\") or a decimal hash",
					"422 means the model is not installed or would not stream in",
					"503 means the game thread is blocked, for example loading a map; retry",
					"429 means too many commands are already queued",
				}) },
			};
		}

		void RegisterRoutes(httplib::Server& server)
		{
			server.Get("/", [](const httplib::Request&, httplib::Response& response) {
				Write(response, 200, Describe());
			});

			server.Get("/health", [](const httplib::Request&, httplib::Response& response) {
				Write(response, 200, json{
					{ "ok", Queue().IsRunning() },
					{ "queueDepth", Queue().Depth() },
				});
			});

			server.Get("/entities", [](const httplib::Request& request, httplib::Response& response) {
				Handle(request, response, [](const httplib::Request&) {
					return json{ { "__action", "list" } };
				});
			});

			server.Post("/entities", [](const httplib::Request& request, httplib::Response& response) {
				Handle(request, response, [](const httplib::Request& req) {
					const json body = ParseBody(req);
					const EntityApi::Transform transform = ParseTransform(body, true);
					return json{
						{ "__action", "create" },
						{ "type", ParseType(body) },
						{ "model", ParseModel(body) },
						{ "name", body.contains("name") ? body.at("name").get<std::string>() : std::string() },
						{ "dynamic", OptionalBool(body, "dynamic", false) },
						{ "placeOnGround", OptionalBool(body, "placeOnGround", false) },
						{ "x", transform.x }, { "y", transform.y }, { "z", transform.z },
						{ "pitch", transform.pitch }, { "roll", transform.roll }, { "yaw", transform.yaw },
					};
				});
			});

			server.Get(R"(/entities/(-?\d+))", [](const httplib::Request& request, httplib::Response& response) {
				Handle(request, response, [](const httplib::Request& req) {
					return json{ { "__action", "get" }, { "id", ParseId(req.matches[1]) } };
				});
			});

			server.Patch(R"(/entities/(-?\d+))", [](const httplib::Request& request, httplib::Response& response) {
				Handle(request, response, [](const httplib::Request& req) {
					const int id = ParseId(req.matches[1]);
					const json body = ParseBody(req);
					if (!body.contains("position") && !body.contains("rotation"))
						throw ApiError(400, "provide at least one of \"position\" or \"rotation\"");

					// A PATCH that omits one of them must not reset it, so read
					// the current transform on the fiber and overlay the change.
					const json current = json{ { "__action", "get" }, { "id", id } };
					const Response existing = Queue().Submit([current]() -> Response {
						return Response{ 200, EntityApi::GetEntity(current.at("id").get<int>()).dump() };
					});
					if (existing.status != 200)
						throw ApiError(existing.status, json::parse(existing.body).at("error").get<std::string>());

					const json entity = json::parse(existing.body);
					EntityApi::Transform transform{};
					transform.x = entity.at("position").at("x").get<float>();
					transform.y = entity.at("position").at("y").get<float>();
					transform.z = entity.at("position").at("z").get<float>();
					transform.pitch = entity.at("rotation").at("pitch").get<float>();
					transform.roll = entity.at("rotation").at("roll").get<float>();
					transform.yaw = entity.at("rotation").at("yaw").get<float>();

					if (body.contains("position"))
					{
						const json& position = body.at("position");
						if (!position.is_object())
							throw ApiError(400, "field \"position\" must be an object with x, y and z");
						transform.x = Number(position, "x");
						transform.y = Number(position, "y");
						transform.z = Number(position, "z");
					}
					if (body.contains("rotation"))
					{
						const json& rotation = body.at("rotation");
						if (!rotation.is_object())
							throw ApiError(400, "field \"rotation\" must be an object with pitch, roll and yaw");
						transform.pitch = OptionalNumber(rotation, "pitch", transform.pitch);
						transform.roll = OptionalNumber(rotation, "roll", transform.roll);
						transform.yaw = OptionalNumber(rotation, "yaw", transform.yaw);
					}

					return json{
						{ "__action", "transform" }, { "id", id },
						{ "x", transform.x }, { "y", transform.y }, { "z", transform.z },
						{ "pitch", transform.pitch }, { "roll", transform.roll }, { "yaw", transform.yaw },
					};
				});
			});

			server.Delete(R"(/entities/(-?\d+))", [](const httplib::Request& request, httplib::Response& response) {
				Handle(request, response, [](const httplib::Request& req) {
					return json{ { "__action", "delete" }, { "id", ParseId(req.matches[1]) } };
				});
			});

			server.set_error_handler([](const httplib::Request& request, httplib::Response& response) {
				if (response.status == 404)
				{
					Write(response, 404, json{
						{ "error", "no route for " + request.method + " " + request.path },
						{ "hint", "GET / lists the available endpoints" },
					});
				}
			});
		}
	}

	void Start()
	{
		if (g_server)
		{
			addlog(ige::LogType::LOG_WARNING, "HTTP bridge already started, ignoring");
			return;
		}

		g_server = std::make_unique<httplib::Server>();
		RegisterRoutes(*g_server);

		g_thread = std::thread([]() {
			addlog(ige::LogType::LOG_INFO, std::string("HTTP bridge listening on ")
				+ kBindAddress + ":" + std::to_string(kPort));
			if (!g_server->listen(kBindAddress, kPort))
			{
				addlog(ige::LogType::LOG_ERROR, std::string("HTTP bridge failed to bind ")
					+ kBindAddress + ":" + std::to_string(kPort) + " - is the port already in use?");
			}
		});
	}

	void DrainCommands()
	{
		Queue().Drain();
	}

	void Shutdown()
	{
		Queue().Stop();
		if (g_server)
			g_server->stop();
		if (g_thread.joinable())
			g_thread.join();
		g_server.reset();
	}
}
