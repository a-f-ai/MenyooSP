/*
* Menyoo PC - Grand Theft Auto V single-player trainer mod
*/
// httplib.h must come before any Menyoo header. Menyoo's headers reach
// <Windows.h> without WIN32_LEAN_AND_MEAN, which drags in winsock.h (v1);
// httplib needs winsock2.h, and the two cannot coexist. Including httplib
// first lets winsock2.h define _WINSOCKAPI_ so Windows.h skips the old one.
#include <cpp-httplib/httplib.h>

#include "HttpServer.h"

#include "ApiError.h"
#include "CameraApi.h"
#include "CatalogApi.h"
#include "CommandQueue.h"
#include "EntityApi.h"
#include "Json.h"
#include "MapApi.h"
#include "WorldApi.h"

#include "../Natives/natives2.h"
#include "../Util/FileLogger.h"

#include <algorithm>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace Http::Json;
using namespace std::chrono_literals;

namespace Http::Server
{
	namespace
	{
		constexpr const char* kBindAddress = "127.0.0.1";
		constexpr int kPort = 21170;
		constexpr size_t kMaxBodyBytes = 4 * 1024 * 1024;
		constexpr size_t kMaxBatchItems = 400;

		std::unique_ptr<httplib::Server> g_server;
		std::thread g_thread;

		// ---------------------------------------------------------------
		// Query strings
		// ---------------------------------------------------------------

		std::string Param(const httplib::Request& request, const char* name, const std::string& whenAbsent = "")
		{
			return request.has_param(name) ? request.get_param_value(name) : whenAbsent;
		}

		int IntParam(const httplib::Request& request, const char* name, int whenAbsent)
		{
			if (!request.has_param(name))
				return whenAbsent;
			const std::string raw = request.get_param_value(name);
			try
			{
				return std::stoi(raw);
			}
			catch (const std::exception&)
			{
				throw ApiError(400, std::string("query parameter \"") + name +
					"\" must be an integer, got \"" + raw + "\"");
			}
		}

		float FloatParam(const httplib::Request& request, const char* name, float whenAbsent)
		{
			if (!request.has_param(name))
				return whenAbsent;
			const std::string raw = request.get_param_value(name);
			try
			{
				return std::stof(raw);
			}
			catch (const std::exception&)
			{
				throw ApiError(400, std::string("query parameter \"") + name +
					"\" must be a number, got \"" + raw + "\"");
			}
		}

		float RequiredFloatParam(const httplib::Request& request, const char* name)
		{
			if (!request.has_param(name))
				throw ApiError(400, std::string("missing required query parameter \"") + name + "\"");
			return FloatParam(request, name, 0.0f);
		}

		bool BoolParam(const httplib::Request& request, const char* name, bool whenAbsent)
		{
			if (!request.has_param(name))
				return whenAbsent;
			const std::string raw = request.get_param_value(name);
			return raw == "1" || raw == "true" || raw == "yes";
		}

		// ---------------------------------------------------------------
		// Bodies
		// ---------------------------------------------------------------

		unsigned long ParseModel(const json& body, std::string& label)
		{
			const json& value = Field(body, "model");

			if (value.is_number_unsigned())
			{
				label = std::to_string(value.get<unsigned long>());
				return value.get<unsigned long>();
			}
			if (!value.is_string())
				throw ApiError(400, "field \"model\" must be a model name or a hash");

			const std::string raw = value.get<std::string>();
			if (raw.empty())
				throw ApiError(400, "field \"model\" must not be empty");
			label = raw;

			if (raw.size() > 2 && raw[0] == '0' && (raw[1] == 'x' || raw[1] == 'X'))
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
			// Menyoo's own joaat, not a game native, so calling it here is safe.
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

		EntityApi::Vec3 ParsePosition(const json& position)
		{
			if (!position.is_object())
				throw ApiError(400, "\"position\" must be an object with x, y and z");
			// z may be omitted when the caller asks to snap to the ground, but a
			// probe height is still needed, so default it high above the map.
			return EntityApi::Vec3{
				Number(position, "x"),
				Number(position, "y"),
				OptionalNumber(position, "z", 1000.0f),
			};
		}

		EntityApi::Vec3 ParseRotation(const json& rotation)
		{
			if (!rotation.is_object())
				throw ApiError(400, "\"rotation\" must be an object with pitch, roll and yaw");
			return EntityApi::Vec3{
				OptionalNumber(rotation, "pitch", 0.0f),
				OptionalNumber(rotation, "roll", 0.0f),
				OptionalNumber(rotation, "yaw", 0.0f),
			};
		}

		EntityApi::CreateRequest ParseCreate(const json& body)
		{
			EntityApi::CreateRequest create{};
			create.type = ParseType(body);
			create.model = ParseModel(body, create.modelLabel);
			create.name = OptionalString(body, "name", "");
			create.position = ParsePosition(Field(body, "position"));
			create.rotation = body.contains("rotation")
				? ParseRotation(body.at("rotation"))
				: EntityApi::Vec3{ 0.0f, 0.0f, 0.0f };
			create.dynamic = OptionalBool(body, "dynamic", false);
			create.snapToGround = OptionalBool(body, "snapToGround", !body.at("position").contains("z"));
			// Placement is what this API is for, so a ped holds its spot unless
			// the caller asks for ambient behaviour.
			create.still = OptionalBool(body, "still", true);
			create.scenario = OptionalString(body, "scenario", "");
			create.animDict = OptionalString(body, "animDict", "");
			create.animName = OptionalString(body, "animName", "");

			if (create.animDict.empty() != create.animName.empty())
				throw ApiError(400, "\"animDict\" and \"animName\" must be given together");
			return create;
		}

		json ParseBody(const httplib::Request& request)
		{
			if (request.body.size() > kMaxBodyBytes)
				throw ApiError(413, "request body exceeds " + std::to_string(kMaxBodyBytes) + " bytes");

			json body = json::parse(request.body, nullptr, false);
			if (body.is_discarded())
				throw ApiError(400, "request body is not valid JSON");
			return body;
		}

		json ParseObjectBody(const httplib::Request& request)
		{
			json body = ParseBody(request);
			if (!body.is_object())
				throw ApiError(400, "request body must be a JSON object");
			return body;
		}

		CatalogApi::Query ParseCatalogQuery(const httplib::Request& request)
		{
			CatalogApi::Query query{};
			query.text = Param(request, "q");
			query.filter = Param(request, "filter");
			query.limit = IntParam(request, "limit", 0);
			query.offset = IntParam(request, "offset", 0);
			query.verifyInstalled = BoolParam(request, "verify", false);
			return query;
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

		// ---------------------------------------------------------------
		// Routing
		// ---------------------------------------------------------------

		void Write(httplib::Response& response, int status, const json& payload)
		{
			response.status = status;
			response.set_content(Dump(payload), "application/json");
		}

		using Plan = std::function<std::function<Response()>(const httplib::Request&)>;

		// Validates on this thread, then runs already-typed work on the fiber.
		void Handle(const httplib::Request& request, httplib::Response& response,
			const Plan& plan, std::chrono::milliseconds timeout = 2000ms)
		{
			try
			{
				const Response result = Queue().Submit(plan(request), timeout);
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
				{ "version", 2 },
				{ "coordinates", "GTA V world space in metres; rotation in degrees, pitch=X roll=Y yaw=Z" },
				{ "identity", "an entity id is its live script handle, valid only while the entity exists. "
							  "Give entities a name and address them by name prefix to survive a restart" },
				{ "placement", "omit position.z, or pass snapToGround, and the plugin casts down from "
							   "position.z (default 1000) and puts the entity on the first surface below. "
							   "Never guess a height" },
				{ "entities", json::array({
					"GET    /entities?name=&type=&limit=&offset=",
					"POST   /entities            {type, model, position:{x,y,z?}, rotation?, name?, dynamic?, snapToGround?, still?, scenario?, animDict?, animName?}",
					"POST   /entities/batch      {items:[ <the same object>, ... ]} up to 400, one shared model load, 207 when some fail",
					"GET    /entities/{id}",
					"PATCH  /entities/{id}       {position?, rotation?, snapToGround?, scenario?, animDict?, animName?}",
					"DELETE /entities/{id}",
					"DELETE /entities?name=&type=  removes everything matching",
				}) },
				{ "world", json::array({
					"GET /world/player     where the player is, plus the ground height under them",
					"GET /world/camera     gameplay camera, and the spooner camera when it is on",
					"GET /world/ground?x=&y=&z=   the surface below a point",
					"GET /world/aim?maxDistance=  where the spooner camera points (409 if spooner mode is off)",
					"GET /world/nearby?x=&y=&z=&radius=&type=&limit=  world entities near a point",
				}) },
				{ "catalog", json::array({
					"GET /catalog/peds?q=&limit=&offset=&verify=",
					"GET /catalog/vehicles?q=&filter=<class>&verify=",
					"GET /catalog/props?q=",
					"GET /catalog/scenarios?q=",
					"GET /catalog/animations?q=&filter=<dict>",
				}) },
				{ "camera", json::array({
					"GET    /camera/path        the current flythrough and where it is playing",
					"PUT    /camera/path        {name?, loop?, constantSpeed?, keys:[{time, position:{x,y,z}, rotation?, fov?, easing?}]}",
					"POST   /camera/keys        a key body, or empty to take one from the live camera",
					"DELETE /camera/keys/{index}",
					"POST   /camera/play | /camera/pause | /camera/stop | /camera/seek {time}",
					"GET    /camera/paths       saved flythroughs in menyooStuff/CameraPaths",
					"POST   /camera/paths/save | /camera/paths/load  {name}",
				}) },
				{ "maps", json::array({
					"GET    /maps           what is in menyooStuff/Spooner",
					"POST   /maps/save      {name} writes the spooner database as a Menyoo map XML",
					"POST   /maps/load      {name} loads one (slow, fades the screen)",
					"DELETE /maps/spawned   deletes everything the spooner owns",
					"GET    /maps/names     reports entity names inflated by the old encoding bug",
					"POST   /maps/names/repair  fixes them in place, keeping a .bak per map",
				}) },
				{ "statuses", json::array({
					"207 a batch where some items failed; read \"failures\"",
					"404 no such entity or route, 410 the entity existed but is gone",
					"422 the model is not installed, or no ground was found below the point",
					"429 too many commands queued, 503 the game thread is blocked - retry",
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

			// ---- entities ----

			server.Get("/entities", [](const httplib::Request& request, httplib::Response& response) {
				Handle(request, response, [](const httplib::Request& req) {
					EntityApi::ListQuery query{};
					query.namePrefix = Param(req, "name");
					query.type = Param(req, "type");
					query.limit = IntParam(req, "limit", 0);
					query.offset = IntParam(req, "offset", 0);
					return std::function<Response()>([query] { return EntityApi::ListEntities(query); });
				});
			});

			server.Post("/entities", [](const httplib::Request& request, httplib::Response& response) {
				Handle(request, response, [](const httplib::Request& req) {
					const EntityApi::CreateRequest create = ParseCreate(ParseObjectBody(req));
					return std::function<Response()>([create] { return EntityApi::CreateEntity(create); });
				}, 8000ms);
			});

			server.Post("/entities/batch", [](const httplib::Request& request, httplib::Response& response) {
				std::chrono::milliseconds timeout = 8000ms;
				try
				{
					const json body = ParseBody(request);
					const json& items = body.is_array() ? body : Field(body, "items");
					if (!items.is_array())
						throw ApiError(400, "\"items\" must be an array of create requests");
					if (items.empty())
						throw ApiError(400, "\"items\" must not be empty");
					if (items.size() > kMaxBatchItems)
						throw ApiError(413, "a batch is limited to " + std::to_string(kMaxBatchItems) + " items");

					std::vector<EntityApi::CreateRequest> creates;
					creates.reserve(items.size());
					for (const json& item : items)
					{
						if (!item.is_object())
							throw ApiError(400, "every item in \"items\" must be an object");
						creates.push_back(ParseCreate(item));
					}

					// A batch spans frames by design, so its deadline grows with
					// the work rather than sharing the single-command one.
					timeout = std::chrono::milliseconds(
						std::min<long long>(60000, 5000 + 120LL * static_cast<long long>(creates.size())));

					const Response result = Queue().Submit(
						[creates] { return EntityApi::CreateBatch(creates); }, timeout);
					response.status = result.status;
					response.set_content(result.body, "application/json");
				}
				catch (const ApiError& error)
				{
					Write(response, error.Status(), json{ { "error", error.what() } });
				}
				catch (const std::exception& error)
				{
					addlog(ige::LogType::LOG_ERROR, std::string("batch failed: ") + error.what());
					Write(response, 500, json{ { "error", std::string("unhandled exception: ") + error.what() } });
				}
			});

			server.Get(R"(/entities/(-?\d+))", [](const httplib::Request& request, httplib::Response& response) {
				Handle(request, response, [](const httplib::Request& req) {
					const int id = ParseId(req.matches[1]);
					return std::function<Response()>([id] { return EntityApi::GetEntity(id); });
				});
			});

			server.Patch(R"(/entities/(-?\d+))", [](const httplib::Request& request, httplib::Response& response) {
				Handle(request, response, [](const httplib::Request& req) {
					const int id = ParseId(req.matches[1]);
					const json body = ParseObjectBody(req);

					EntityApi::PatchRequest patch{};
					if (body.contains("position"))
						patch.position = ParsePosition(body.at("position"));
					if (body.contains("rotation"))
						patch.rotation = ParseRotation(body.at("rotation"));
					if (body.contains("snapToGround"))
						patch.snapToGround = OptionalBool(body, "snapToGround", false);
					if (body.contains("scenario"))
						patch.scenario = OptionalString(body, "scenario", "");
					if (body.contains("animDict"))
						patch.animDict = OptionalString(body, "animDict", "");
					if (body.contains("animName"))
						patch.animName = OptionalString(body, "animName", "");

					if (!patch.position && !patch.rotation && !patch.snapToGround &&
						!patch.scenario && !patch.animDict)
					{
						throw ApiError(400, "give at least one of position, rotation, snapToGround, "
							"scenario or animDict/animName");
					}
					return std::function<Response()>([id, patch] { return EntityApi::PatchEntity(id, patch); });
				}, 4000ms);
			});

			server.Delete(R"(/entities/(-?\d+))", [](const httplib::Request& request, httplib::Response& response) {
				Handle(request, response, [](const httplib::Request& req) {
					const int id = ParseId(req.matches[1]);
					return std::function<Response()>([id] { return EntityApi::DeleteEntity(id); });
				});
			});

			server.Delete("/entities", [](const httplib::Request& request, httplib::Response& response) {
				Handle(request, response, [](const httplib::Request& req) {
					const std::string name = Param(req, "name");
					const std::string type = Param(req, "type");
					if (name.empty() && type.empty())
					{
						throw ApiError(400, "refusing to delete everything by accident: pass name= or "
							"type=, or use DELETE /maps/spawned to clear the whole database");
					}
					return std::function<Response()>([name, type] { return EntityApi::DeleteMatching(name, type); });
				}, 30000ms);
			});

			// ---- world ----

			server.Get("/world/player", [](const httplib::Request& request, httplib::Response& response) {
				Handle(request, response, [](const httplib::Request&) {
					return std::function<Response()>([] { return WorldApi::GetPlayer(); });
				});
			});

			server.Get("/world/camera", [](const httplib::Request& request, httplib::Response& response) {
				Handle(request, response, [](const httplib::Request&) {
					return std::function<Response()>([] { return WorldApi::GetCamera(); });
				});
			});

			server.Get("/world/ground", [](const httplib::Request& request, httplib::Response& response) {
				Handle(request, response, [](const httplib::Request& req) {
					const float x = RequiredFloatParam(req, "x");
					const float y = RequiredFloatParam(req, "y");
					const float z = FloatParam(req, "z", 1000.0f);
					return std::function<Response()>([x, y, z] { return WorldApi::GetGround(x, y, z); });
				});
			});

			server.Get("/world/aim", [](const httplib::Request& request, httplib::Response& response) {
				Handle(request, response, [](const httplib::Request& req) {
					const float maxDistance = FloatParam(req, "maxDistance", 160.0f);
					return std::function<Response()>([maxDistance] { return WorldApi::Raycast(maxDistance); });
				});
			});

			server.Get("/world/nearby", [](const httplib::Request& request, httplib::Response& response) {
				Handle(request, response, [](const httplib::Request& req) {
					const float x = RequiredFloatParam(req, "x");
					const float y = RequiredFloatParam(req, "y");
					const float z = RequiredFloatParam(req, "z");
					const float radius = FloatParam(req, "radius", 30.0f);
					const std::string type = Param(req, "type");
					const int limit = IntParam(req, "limit", 0);
					return std::function<Response()>([x, y, z, radius, type, limit] {
						return WorldApi::GetNearby(x, y, z, radius, type, limit);
					});
				}, 4000ms);
			});

			// ---- catalog ----

			const auto catalogRoute = [](Response (*fn)(const CatalogApi::Query&)) {
				return [fn](const httplib::Request& request, httplib::Response& response) {
					Handle(request, response, [fn](const httplib::Request& req) {
						const CatalogApi::Query query = ParseCatalogQuery(req);
						return std::function<Response()>([fn, query] { return fn(query); });
					}, 8000ms);
				};
			};

			server.Get("/catalog/peds", catalogRoute(&CatalogApi::Peds));
			server.Get("/catalog/vehicles", catalogRoute(&CatalogApi::Vehicles));
			server.Get("/catalog/props", catalogRoute(&CatalogApi::Props));
			server.Get("/catalog/scenarios", catalogRoute(&CatalogApi::Scenarios));
			server.Get("/catalog/animations", catalogRoute(&CatalogApi::Animations));

			// ---- maps ----

			server.Get("/maps", [](const httplib::Request& request, httplib::Response& response) {
				Handle(request, response, [](const httplib::Request&) {
					return std::function<Response()>([] { return MapApi::ListMaps(); });
				});
			});

			server.Post("/maps/save", [](const httplib::Request& request, httplib::Response& response) {
				Handle(request, response, [](const httplib::Request& req) {
					const std::string name = Field(ParseObjectBody(req), "name").get<std::string>();
					return std::function<Response()>([name] { return MapApi::SaveMap(name); });
				}, 30000ms);
			});

			server.Post("/maps/load", [](const httplib::Request& request, httplib::Response& response) {
				Handle(request, response, [](const httplib::Request& req) {
					const std::string name = Field(ParseObjectBody(req), "name").get<std::string>();
					return std::function<Response()>([name] { return MapApi::LoadMap(name); });
				}, 120000ms);
			});

			server.Get("/maps/names", [](const httplib::Request& request, httplib::Response& response) {
				Handle(request, response, [](const httplib::Request&) {
					return std::function<Response()>([] { return MapApi::RepairNames(false); });
				}, 30000ms);
			});

			server.Post("/maps/names/repair", [](const httplib::Request& request, httplib::Response& response) {
				Handle(request, response, [](const httplib::Request&) {
					return std::function<Response()>([] { return MapApi::RepairNames(true); });
				}, 60000ms);
			});

			server.Delete("/maps/spawned", [](const httplib::Request& request, httplib::Response& response) {
				Handle(request, response, [](const httplib::Request&) {
					return std::function<Response()>([] { return MapApi::ClearSpawned(); });
				}, 30000ms);
			});

			// ---- camera paths ----
			// These touch only mutex-guarded state, never a native, so they
			// answer on this thread instead of waiting for a frame.

			const auto direct = [](httplib::Response& response, const std::function<Response()>& work) {
				try
				{
					const Response result = work();
					response.status = result.status;
					response.set_content(result.body, "application/json");
				}
				catch (const ApiError& error)
				{
					Write(response, error.Status(), json{ { "error", error.what() } });
				}
				catch (const std::exception& error)
				{
					Write(response, 500, json{ { "error", std::string("unhandled exception: ") + error.what() } });
				}
			};

			server.Get("/camera/path", [direct](const httplib::Request&, httplib::Response& response) {
				direct(response, [] { return CameraApi::GetPath(); });
			});

			server.Put("/camera/path", [direct](const httplib::Request& request, httplib::Response& response) {
				direct(response, [&request] { return CameraApi::ReplacePath(ParseObjectBody(request)); });
			});

			server.Post("/camera/keys", [direct](const httplib::Request& request, httplib::Response& response) {
				direct(response, [&request] {
					const json body = request.body.empty() ? json::object() : ParseObjectBody(request);
					return CameraApi::AppendKey(body);
				});
			});

			server.Delete(R"(/camera/keys/(\d+))", [direct](const httplib::Request& request, httplib::Response& response) {
				direct(response, [&request] { return CameraApi::DeleteKey(ParseId(request.matches[1])); });
			});

			server.Post(R"(/camera/(play|pause|stop|seek))", [direct](const httplib::Request& request, httplib::Response& response) {
				direct(response, [&request] {
					const std::string action = request.matches[1];
					float time = 0.0f;
					if (action == "seek")
					{
						const json body = request.body.empty() ? json::object() : ParseObjectBody(request);
						time = Number(body, "time");
					}
					return CameraApi::Transport(action, time);
				});
			});

			server.Get("/camera/paths", [direct](const httplib::Request&, httplib::Response& response) {
				direct(response, [] { return CameraApi::ListSaved(); });
			});

			server.Post("/camera/paths/save", [direct](const httplib::Request& request, httplib::Response& response) {
				direct(response, [&request] {
					return CameraApi::SavePath(Field(ParseObjectBody(request), "name").get<std::string>());
				});
			});

			server.Post("/camera/paths/load", [direct](const httplib::Request& request, httplib::Response& response) {
				direct(response, [&request] {
					return CameraApi::LoadPath(Field(ParseObjectBody(request), "name").get<std::string>());
				});
			});

			// httplib calls this for every response with a status of 400 or
			// above, including the ones our own routes produce, so it may only
			// fill in a body that is still empty. Overwriting would relabel a
			// genuine "no such entity" as "no such route".
			server.set_error_handler([](const httplib::Request& request, httplib::Response& response) {
				if (response.status == 404 && response.body.empty())
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
		// A batch or a map load legitimately holds a connection for a while.
		g_server->set_read_timeout(150, 0);
		g_server->set_write_timeout(150, 0);
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
