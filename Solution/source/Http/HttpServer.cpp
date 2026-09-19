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
#include "GameFiberHeartbeat.h"
#include "Json.h"
#include "MapApi.h"
#include "ModelApi.h"
#include "PlayerApi.h"
#include "PlayerCommand.h"
#include "PlayerInput.h"
#include "PatternSnapshot.h"
#include "SpidermanBike.h"
#include "WorldApi.h"

#include "../Natives/natives2.h"
#include "../Scripting/Raycast.h"
#include "../Util/ExePath.h"
#include "../Util/FileLogger.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <fstream>
#include <memory>
#include <sstream>
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

		unsigned long ResolveModel(const std::string& raw, const char* what)
		{
			if (raw.empty())
				throw ApiError(400, std::string(what) + " must not be empty");

			if (raw.size() > 2 && raw[0] == '0' && (raw[1] == 'x' || raw[1] == 'X'))
			{
				try
				{
					return std::stoul(raw, nullptr, 16);
				}
				catch (const std::exception&)
				{
					throw ApiError(400, std::string(what) + " looks like hex but will not parse: " + raw);
				}
			}
			// Menyoo's own joaat, not a game native, so calling it here is safe.
			return GET_HASH_KEY(raw.c_str());
		}

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

			label = value.get<std::string>();
			return ResolveModel(label, "field \"model\"");
		}

		json ReadJsonFile(const std::string& name)
		{
			const std::string path = GetPathffA(Pathff::Main, true) + name;
			std::ifstream file(path, std::ios::binary);
			if (!file)
				throw ApiError(500, "cannot open " + path);

			std::stringstream contents;
			contents << file.rdbuf();
			const json document = json::parse(contents.str(), nullptr, false);
			if (document.is_discarded())
				throw ApiError(500, path + " is not valid JSON");
			return document;
		}

		struct ResolvedPlayerModel
		{
			unsigned long hash;
			std::string label;
			std::string alias;
			std::string variant;
		};

		ResolvedPlayerModel ResolvePlayerModel(const json& body, bool character)
		{
			const PlayerCommand::ModelInput input = PlayerCommand::ParseModelInput(body);
			if (input.hasNumericModel)
				return ResolvedPlayerModel{ input.numericModel, std::to_string(input.numericModel), "", "" };
			if (!input.model.empty())
				return ResolvedPlayerModel{ ResolveModel(input.model, "field \"model\""), input.model, "", "" };

			const PlayerCommand::ResolvedAlias resolved = character
				? PlayerCommand::ResolveCharacterAlias(ReadJsonFile("Characters.json"), input)
				: PlayerCommand::ResolveVehicleAlias(ReadJsonFile("VehicleShortlist.json"), input);
			return ResolvedPlayerModel{ resolved.hash, resolved.model, resolved.alias, resolved.variant };
		}

		std::vector<ModelApi::ModelRef> ParseModelList(const httplib::Request& request)
		{
			const std::string raw = Param(request, "models");
			if (raw.empty())
				throw ApiError(400, "missing required query parameter \"models\": a comma-separated "
					"list of model names or 0x hashes");

			std::vector<ModelApi::ModelRef> models;
			size_t start = 0;
			while (start <= raw.size())
			{
				const size_t comma = raw.find(',', start);
				const size_t end = comma == std::string::npos ? raw.size() : comma;

				std::string name = raw.substr(start, end - start);
				const size_t first = name.find_first_not_of(" \t");
				const size_t last = name.find_last_not_of(" \t");
				if (first != std::string::npos)
				{
					name = name.substr(first, last - first + 1);
					models.push_back(ModelApi::ModelRef{ ResolveModel(name, "\"models\""), name });
				}

				if (comma == std::string::npos)
					break;
				start = comma + 1;
			}

			if (models.empty())
				throw ApiError(400, "query parameter \"models\" held no model names");
			if (models.size() > ModelApi::kMaxModelsPerQuery)
				throw ApiError(400, "at most " + std::to_string(ModelApi::kMaxModelsPerQuery) +
					" models per request, got " + std::to_string(models.size()) +
					"; every one of them has to be streamed in to be measured");
			return models;
		}

		int RequiredInteger(const json& body, const char* name)
		{
			const json& value = Field(body, name);
			if (!value.is_number_integer())
				throw ApiError(400, std::string("field \"") + name + "\" must be an integer");
			return value.get<int>();
		}

		std::vector<PlayerApi::ControlRequest> ParseControls(const json& body)
		{
			const json& source = Field(body, "controls");
			if (!source.is_array() || source.empty() || source.size() > 16)
				throw ApiError(400, "field \"controls\" must contain between 1 and 16 controls");

			std::vector<PlayerApi::ControlRequest> controls;
			controls.reserve(source.size());
			for (const json& item : source)
			{
				if (!item.is_object())
					throw ApiError(400, "every item in \"controls\" must be an object");
				const int control = RequiredInteger(item, "control");
				const float value = Number(item, "value");
				if (!PlayerInput::IsControlIdValid(control))
					throw ApiError(400, "field \"control\" must be between 0 and 337");
				if (!PlayerInput::IsValueValid(value))
					throw ApiError(400, "field \"value\" must be between -1 and 1");
				controls.push_back(PlayerApi::ControlRequest{ control, value });
			}
			return controls;
		}

		// Named rather than a raw bitmask: a caller reading "map" knows what it
		// asked for, and a caller reading "1" does not.
		int ParseIntersectFlags(const json& body)
		{
			if (!body.contains("include"))
				return static_cast<int>(IntersectOptions::Map);

			const json& value = body.at("include");
			if (!value.is_array())
				throw ApiError(400, "\"include\" must be an array of surface kinds");

			int flags = 0;
			for (const auto& item : value)
			{
				if (!item.is_string())
					throw ApiError(400, "\"include\" entries must be strings");

				const std::string kind = item.get<std::string>();
				if (kind == "everything") return static_cast<int>(IntersectOptions::Everything);
				else if (kind == "map")      flags |= static_cast<int>(IntersectOptions::Map);
				else if (kind == "vehicles") flags |= static_cast<int>(IntersectOptions::Mission_Entities);
				else if (kind == "peds")     flags |= static_cast<int>(IntersectOptions::Peds1);
				else if (kind == "objects")  flags |= static_cast<int>(IntersectOptions::Objects);
				else if (kind == "foliage")  flags |= static_cast<int>(IntersectOptions::Vegetation);
				else throw ApiError(400, "unknown \"include\" entry \"" + kind +
					"\"; use map, vehicles, peds, objects, foliage or everything");
			}

			if (flags == 0)
				throw ApiError(400, "\"include\" held no surface kinds");
			return flags;
		}

		WorldApi::RayRequest ParseRay(const json& body)
		{
			const json& from = Field(body, "from");
			const json& to = Field(body, "to");
			if (!from.is_object() || !to.is_object())
				throw ApiError(400, "\"from\" and \"to\" must both be objects with x, y and z");

			WorldApi::RayRequest ray{};
			ray.fromX = Number(from, "x");
			ray.fromY = Number(from, "y");
			ray.fromZ = Number(from, "z");
			ray.toX = Number(to, "x");
			ray.toY = Number(to, "y");
			ray.toZ = Number(to, "z");
			ray.flags = ParseIntersectFlags(body);
			ray.ignoreEntity = OptionalInt(body, "ignoreEntity", 0);

			const float dx = ray.toX - ray.fromX;
			const float dy = ray.toY - ray.fromY;
			const float dz = ray.toZ - ray.fromZ;
			const float length = std::sqrt(dx * dx + dy * dy + dz * dz);
			if (length < 0.001f)
				throw ApiError(400, "\"from\" and \"to\" are the same point");
			if (length > 2000.0f)
				throw ApiError(400, "the ray is " + std::to_string(length) +
					" metres long; keep it under 2000");
			return ray;
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
			// The author's own Menyoo settings: peds and vehicles spawn dynamic, props
			// frozen. A frozen ped stays wherever it was put - in the air or in the
			// floor - and hides a wrong height, so it must be asked for explicitly.
			create.dynamic = OptionalBool(body, "dynamic", create.type != 3);
			create.snapToGround = OptionalBool(body, "snapToGround", !body.at("position").contains("z"));
			// Placement is what this API is for, so a ped holds its spot unless
			// the caller asks for ambient behaviour.
			create.still = OptionalBool(body, "still", true);
			create.scenario = OptionalString(body, "scenario", "");
			create.animDict = OptionalString(body, "animDict", "");
			create.animName = OptionalString(body, "animName", "");

			if (create.animDict.empty() != create.animName.empty())
				throw ApiError(400, "\"animDict\" and \"animName\" must be given together");

			if (body.contains("expectedSupportZ"))
				create.expectedSupportZ = Number(body, "expectedSupportZ");
			create.tolerance = OptionalNumber(body, "tolerance", 0.25f);
			if (create.tolerance <= 0.0f)
				throw ApiError(400, "\"tolerance\" must be positive");
			create.textureVariation = OptionalInt(body, "textureVariation", -1);
			if (create.textureVariation < -1 || create.textureVariation > 63)
				throw ApiError(400, "\"textureVariation\" must be between 0 and 63, or omitted");
			if (create.textureVariation >= 0 && create.type != 3)
				throw ApiError(400, "\"textureVariation\" applies to props only");
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

		void direct(httplib::Response& response, const std::function<Response()>& work)
		{
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
		}

		json Describe()
		{
			return json{
				{ "service", "menyoo-entity-bridge" },
				{ "version", 3 },
				{ "coordinates", "GTA V world space in metres; rotation in degrees, pitch=X roll=Y yaw=Z" },
				{ "identity", "an entity id is its live script handle, valid only while the entity exists. "
							  "Give entities a name and address them by name prefix to survive a restart" },
				{ "placement", "omit position.z, or pass snapToGround, and the plugin casts down from "
							   "position.z (default 1000) and puts the entity on the first surface below. "
							   "Never guess a height" },
				{ "geometry", "entity listings carry \"size\", the model's box in metres, and \"bounds\", "
							  "the world box it occupies once rotated. Ask /models/dimensions before "
							  "placing something whose footprint you do not already know" },
				{ "entities", json::array({
					"GET    /entities?name=&type=&limit=&offset=",
					"POST   /entities            {type, model, position:{x,y,z?}, rotation?, name?, dynamic?, snapToGround?, still?, scenario?, animDict?, animName?, expectedSupportZ?, tolerance?}",
					"       peds and vehicles spawn dynamic unless dynamic:false; a ped snapped to ground gets its origin 1.0 m above it",
					"       textureVariation (props): the tint index that colours stunt blocks and tubes - the author's maps use 0..16 on bkr_prop_biker_bblock_*; entity listings report it",
					"       expectedSupportZ: the surface z you believe is under the entity; the spawn refuses (with the z it found) if the real surface is further than tolerance (0.25)",
					"POST   /entities/settle     {name?|type?, frames?=90, epsilon?=0.1} waits, then lists what moved - a measurement, nothing is corrected",
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
					"GET /world/aim?maxDistance=  what the camera is looking at; \"hit\":false when it is pointed at nothing",
					"POST /world/raycast  {from:{x,y,z}, to:{x,y,z}, include?:[map|vehicles|peds|objects|foliage|everything], ignoreEntity?}",
					"     the only way to see static map geometry - a building is not an entity and /world/nearby cannot see it",
					"GET /world/nearby?x=&y=&z=&radius=&type=&limit=  world entities near a point",
					"POST /world/pattern-snapshot {scope:spooner|world,origin,radius,types,maxEntities,frame,supportProbe,include}; pattern capture uses explicit spooner scope and receives excluded IDs/reasons; legacy omitted scope is world; see PATTERN_CAPTURE_API.md; read-only, no partial success",
				}) },
				{ "player", json::array({
					"POST /player/model  {model} or {alias, variant?}; exact aliases come from Characters.json; multi-variant aliases require an explicit defaultVariant or variant",
					"POST /player/vehicle  {model|alias, position?:{x,y,z}, heading?} spawns a vehicle and immediately seats the player as driver",
					"POST /player/spiderman-bike {} exact SpidermanRed + bati2 livery 1; safe adjacent placement, collision on, driver readback; Ctrl+Shift+O (latched v2); failures carry stage and actual state",
					"POST /player/enter-vehicle  {vehicleId} starts the normal walk-and-enter animation for the driver seat",
					"POST /player/input  {controls:[{control,value}, ...], holdMilliseconds} holds native GTA input on every game tick; control is 0..337 and value is -1..1",
					"POST /player/input/release  releases every virtual control immediately",
					"GET /player/input/diagnostic?controlGroup=&control=  reports the last held control's native acceptance and live PAD state for group 0..2",
					"POST /player/drive-to  {x,y,z,speed,drivingStyle,pushEntities} uses Menyoo's vehicle auto drive for the current player vehicle",
					"POST /player/drive-to/stop  stops the active Menyoo auto drive",
					"POST /debug/player/teleport  {x,y,z} moves the player exactly to an explicit debug position",
				}) },
				{ "catalog", json::array({
					"GET /catalog/peds?q=&limit=&offset=&verify=",
					"GET /catalog/vehicles?q=&filter=<class>&verify=",
					"GET /catalog/props?q=",
					"GET /catalog/scenarios?q=",
					"GET /catalog/animations?q=&filter=<dict>",
				}) },
				{ "models", json::array({
					"GET /models/dimensions?models=a,b,c   up to 64 names or 0x hashes at a time",
					"    min/max/size in model space, and restZOffset, the z to add to a ground "
					"height so the model rests on it instead of sinking into it",
					"    the query streams every model in to measure it, so ask once and cache",
				}) },
				{ "camera", json::array({
					"GET    /camera/path        the current flythrough and where it is playing",
					"PUT    /camera/path        {name?, loop?, constantSpeed?, keys:[{time, position:{x,y,z}, rotation?, fov?, easing?}]}",
					"POST   /camera/keys        a key body, or empty to take one from the live camera",
					"DELETE /camera/keys/{index}",
					"POST   /camera/play | /camera/pause | /camera/stop | /camera/seek {time}",
					"POST   /camera/look        {position:{x,y,z}, at:{x,y,z} | rotation:{pitch,roll,yaw}, fov?} hold the camera there; stop gives the view back",
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
			server.Post("/player/spiderman-bike", [](const httplib::Request& request, httplib::Response& response) {
				Handle(request, response, [](const httplib::Request& req) {
					const auto body = ParseObjectBody(req);
					if (!body.empty()) throw ApiError(400, "spiderman-bike requires an empty JSON object");
					return std::function<Response()>([] { return MakeSpidermanOnBike(); });
				}, 12000ms);
			});
			server.Post("/world/pattern-snapshot", [](const httplib::Request& request, httplib::Response& response) {
				Handle(request, response, [](const httplib::Request& req) {
					const auto capture = Pattern::Parse(ParseObjectBody(req));
					return std::function<Response()>([capture] { return Pattern::Capture(capture); });
				}, 4000ms);
			});
			server.Get("/", [](const httplib::Request&, httplib::Response& response) {
				Write(response, 200, Describe());
			});

			server.Get("/health", [](const httplib::Request&, httplib::Response& response) {
				const auto snapshot = Heartbeat().Snapshot(2000ms);
				Write(response, snapshot.stalled ? 503 : 200, json{
					{ "ok", Queue().IsRunning() && snapshot.started && !snapshot.stalled },
					{ "fiberStarted", snapshot.started },
					{ "fiberStalled", snapshot.stalled },
					{ "stage", snapshot.stage },
					{ "frame", snapshot.frame },
					{ "heartbeatAgeMs", snapshot.ageMilliseconds },
					{ "queueDepth", Queue().Depth() },
					{ "runningCommands", Queue().RunningCount() },
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

			server.Post("/entities/settle", [](const httplib::Request& request, httplib::Response& response) {
				std::chrono::milliseconds timeout = 4000ms;
				EntityApi::SettleQuery query{};
				try
				{
					const json body = ParseObjectBody(request);
					query.namePrefix = OptionalString(body, "name", "");
					query.type = OptionalString(body, "type", "");
					if (query.namePrefix.empty() && query.type.empty())
						throw ApiError(400, "give \"name\" (a prefix) or \"type\" so the check has a subject");
					query.frames = OptionalInt(body, "frames", 90);
					if (query.frames < 1 || query.frames > 600)
						throw ApiError(400, "\"frames\" must be between 1 and 600");
					query.epsilon = OptionalNumber(body, "epsilon", 0.1f);
					// Frames pass at the game's rate; allow for 30 fps plus a margin.
					timeout = std::chrono::milliseconds(3000 + query.frames * 40);
				}
				catch (const ApiError& error)
				{
					Write(response, error.Status(), json{ { "error", error.what() } });
					return;
				}
				Handle(request, response, [query](const httplib::Request&) {
					return std::function<Response()>([query] { return EntityApi::Settle(query); });
				}, timeout);
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
					return std::function<Response()>([maxDistance] { return WorldApi::Aim(maxDistance); });
				});
			});

			server.Post("/world/raycast", [](const httplib::Request& request, httplib::Response& response) {
				Handle(request, response, [](const httplib::Request& req) {
					const WorldApi::RayRequest ray = ParseRay(ParseObjectBody(req));
					return std::function<Response()>([ray] { return WorldApi::Raycast(ray); });
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

			// ---- player ----

			server.Post("/player/model", [](const httplib::Request& request, httplib::Response& response) {
				Handle(request, response, [](const httplib::Request& req) {
					const json body = ParseObjectBody(req);
					const ResolvedPlayerModel model = ResolvePlayerModel(body, true);
					const PlayerApi::SetModelRequest command{
						model.hash, model.label, model.alias, model.variant,
					};
					return std::function<Response()>([command] { return PlayerApi::SetModel(command); });
				}, 10000ms);
			});

			server.Post("/player/vehicle", [](const httplib::Request& request, httplib::Response& response) {
				Handle(request, response, [](const httplib::Request& req) {
					const json body = ParseObjectBody(req);
					const ResolvedPlayerModel model = ResolvePlayerModel(body, false);
					const PlayerCommand::VehiclePlacement placement = PlayerCommand::ParseVehiclePlacement(body);
					const PlayerApi::SpawnVehicleRequest command{
						model.hash, model.label, model.alias,
						placement.hasPosition, placement.x, placement.y, placement.z,
						placement.hasHeading, placement.heading,
					};
					return std::function<Response()>([command] { return PlayerApi::SpawnVehicleAndSeat(command); });
				}, 10000ms);
			});

			server.Post("/player/enter-vehicle", [](const httplib::Request& request, httplib::Response& response) {
				Handle(request, response, [](const httplib::Request& req) {
					const json body = ParseObjectBody(req);
					const int vehicleId = RequiredInteger(body, "vehicleId");
					if (vehicleId <= 0)
						throw ApiError(400, "field \"vehicleId\" must be a live positive entity id");
					return std::function<Response()>([vehicleId] {
						return PlayerApi::EnterVehicle(PlayerApi::EnterVehicleRequest{ vehicleId });
					});
				});
			});

			server.Post("/player/input", [](const httplib::Request& request, httplib::Response& response) {
				Handle(request, response, [](const httplib::Request& req) {
					const json body = ParseObjectBody(req);
					const std::vector<PlayerApi::ControlRequest> controls = ParseControls(body);
					const int holdMilliseconds = RequiredInteger(body, "holdMilliseconds");
					if (!PlayerInput::IsHoldMillisecondsValid(holdMilliseconds))
						throw ApiError(400, "field \"holdMilliseconds\" must be between 1 and 1000");
					return std::function<Response()>([controls, holdMilliseconds] { return PlayerApi::ApplyControls(controls, holdMilliseconds); });
				});
			});

			server.Post("/player/input/release", [](const httplib::Request& request, httplib::Response& response) {
				Handle(request, response, [](const httplib::Request&) {
					return std::function<Response()>([] { return PlayerApi::ReleaseControls(); });
				});
			});

			server.Get("/player/input/diagnostic", [](const httplib::Request& request, httplib::Response& response) {
				Handle(request, response, [](const httplib::Request& req) {
					if (!req.has_param("controlGroup"))
						throw ApiError(400, "missing required query parameter \"controlGroup\"");
					if (!req.has_param("control"))
						throw ApiError(400, "missing required query parameter \"control\"");
					const PlayerApi::ControlDiagnosticRequest diagnostic{
						IntParam(req, "controlGroup", 0),
						IntParam(req, "control", 0),
					};
					if (!PlayerInput::IsControlGroupValid(diagnostic.controlGroup))
						throw ApiError(400, "query parameter \"controlGroup\" must be between 0 and 2");
					if (!PlayerInput::IsControlIdValid(diagnostic.control))
						throw ApiError(400, "query parameter \"control\" must be between 0 and 337");
					return std::function<Response()>([diagnostic] { return PlayerApi::GetControlDiagnostic(diagnostic); });
				});
			});

			server.Post("/player/drive-to", [](const httplib::Request& request, httplib::Response& response) {
				Handle(request, response, [](const httplib::Request& req) {
					const json body = ParseObjectBody(req);
					const json& pushEntities = Field(body, "pushEntities");
					if (!pushEntities.is_boolean())
						throw ApiError(400, "field \"pushEntities\" must be a boolean");
					const PlayerApi::DriveToRequest drive{
						Number(body, "x"), Number(body, "y"), Number(body, "z"),
						Number(body, "speed"), RequiredInteger(body, "drivingStyle"), pushEntities.get<bool>(),
					};
					if (!std::isfinite(drive.speed) || drive.speed <= 0.0f || drive.speed > 100.0f)
						throw ApiError(400, "field \"speed\" must be between 0 and 100 metres per second");
					return std::function<Response()>([drive] { return PlayerApi::DriveTo(drive); });
				});
			});

			server.Post("/player/drive-to/stop", [](const httplib::Request& request, httplib::Response& response) {
				Handle(request, response, [](const httplib::Request&) {
					return std::function<Response()>([] { return PlayerApi::StopDriving(); });
				});
			});

			server.Post("/debug/player/teleport", [](const httplib::Request& request, httplib::Response& response) {
				Handle(request, response, [](const httplib::Request& req) {
					const json body = ParseObjectBody(req);
					const PlayerApi::TeleportRequest destination{
						Number(body, "x"), Number(body, "y"), Number(body, "z"),
					};
					return std::function<Response()>([destination] { return PlayerApi::Teleport(destination); });
				});
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

			// ---- models ----

			server.Get("/models/dimensions", [](const httplib::Request& request, httplib::Response& response) {
				Handle(request, response, [](const httplib::Request& req) {
					const std::vector<ModelApi::ModelRef> models = ParseModelList(req);
					return std::function<Response()>([models] { return ModelApi::GetDimensions(models); });
				}, 10000ms);
			});

			// ---- maps ----

			server.Get("/maps", [](const httplib::Request&, httplib::Response& response) {
				direct(response, [] { return MapApi::ListMaps(); });
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

			server.Get("/camera/path", [](const httplib::Request&, httplib::Response& response) {
				direct(response, [] { return CameraApi::GetPath(); });
			});

			server.Put("/camera/path", [](const httplib::Request& request, httplib::Response& response) {
				direct(response, [&request] { return CameraApi::ReplacePath(ParseObjectBody(request)); });
			});

			server.Post("/camera/keys", [](const httplib::Request& request, httplib::Response& response) {
				direct(response, [&request] {
					const json body = request.body.empty() ? json::object() : ParseObjectBody(request);
					return CameraApi::AppendKey(body);
				});
			});

			server.Delete(R"(/camera/keys/(\d+))", [](const httplib::Request& request, httplib::Response& response) {
				direct(response, [&request] { return CameraApi::DeleteKey(ParseId(request.matches[1])); });
			});

			server.Post(R"(/camera/(play|pause|stop|seek))", [](const httplib::Request& request, httplib::Response& response) {
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

			server.Post("/camera/look", [](const httplib::Request& request, httplib::Response& response) {
				direct(response, [&request] { return CameraApi::Look(ParseObjectBody(request)); });
			});

			server.Get("/camera/paths", [](const httplib::Request&, httplib::Response& response) {
				direct(response, [] { return CameraApi::ListSaved(); });
			});

			server.Post("/camera/paths/save", [](const httplib::Request& request, httplib::Response& response) {
				direct(response, [&request] {
					return CameraApi::SavePath(Field(ParseObjectBody(request), "name").get<std::string>());
				});
			});

			server.Post("/camera/paths/load", [](const httplib::Request& request, httplib::Response& response) {
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
