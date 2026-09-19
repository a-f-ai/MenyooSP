#include "PatternSnapshot.h"
#include "ModelApi.h"
#include "../Memory/GTAmemory.h"
#include "../Natives/natives2.h"
#include "../Scripting/GTAped.h"
#include "../Scripting/GTAvehicle.h"
#include "../Scripting/Model.h"
#include "../Scripting/ModelNames.h"
#include "../Scripting/Raycast.h"
#include "../Submenus/Spooner/Databases.h"
#include "../Submenus/Spooner/SpoonerEntity.h"
#include "../Util/StringManip.h"
#include <algorithm>
#include <chrono>
#include <limits>
#include <tuple>
#undef min
#undef max

namespace Http::Pattern
{
    namespace
    {
        using sub::Spooner::SpoonerEntity;
        Vec V(Vector3 v) { return {v.x,v.y,v.z}; }
        Vector3 Native(Vec v) { return Vector3(static_cast<float>(v.x),static_cast<float>(v.y),static_cast<float>(v.z)); }
        Transform PoseOf(GTAentity entity) { return {V(entity.GetPosition()),V(entity.GetRotation())}; }
        json BoxJson(Bounds b) { return {{"min",Point(b.min)},{"max",Point(b.max)}}; }
        json Rgb(RgbS c) { return {{"r",c.R},{"g",c.G},{"b",c.B}}; }
        bool Geometry(int id,unsigned long model,Bounds& local,Bounds& world)
        {
            const auto box=ModelApi::GetBox(model);
            if(!box.valid) return false;
            local={V(box.min),V(box.max)};
            const auto inf=std::numeric_limits<double>::infinity();
            world={{inf,inf,inf},{-inf,-inf,-inf}};
            for(int i=0;i<8;++i)
            {
                auto p=GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS(id,(i&1)?box.max.x:box.min.x,(i&2)?box.max.y:box.min.y,(i&4)?box.max.z:box.min.z);
                world.min.x=std::min(world.min.x,double(p.x)); world.min.y=std::min(world.min.y,double(p.y)); world.min.z=std::min(world.min.z,double(p.z));
                world.max.x=std::max(world.max.x,double(p.x)); world.max.y=std::max(world.max.y,double(p.y)); world.max.z=std::max(world.max.z,double(p.z));
            }
            return true;
        }
        json VehicleState(int id)
        {
            GTAvehicle v(id);
            json mods=json::array();
            for(int slot=0;slot<50;++slot)
            {
                if(slot>=17 && slot<=22) { mods.push_back({{"slot",slot},{"kind","toggle"},{"enabled",v.IsToggleModOn(slot)}}); continue; }
                mods.push_back({{"slot",slot},{"kind","indexed"},{"index",v.GetMod(slot)},{"variation",v.GetModVariation(slot)}});
            }
            int p1,c1,pearl,p2,c2;
            GET_VEHICLE_MOD_COLOR_1(id,&p1,&c1,&pearl); GET_VEHICLE_MOD_COLOR_2(id,&p2,&c2);
            return {
                {"colours",{{"primary",v.GetPrimaryColour()},{"secondary",v.GetSecondaryColour()},{"pearl",v.GetPearlescentColour()},{"rim",v.GetRimColour()},
                    {"interior",v.GetInteriorColour()},{"dashboard",v.GetDashboardColour()},{"xenon",v.GetHeadlightColour()},
                    {"primaryCustom",v.IsPrimaryColorCustom()},{"secondaryCustom",v.IsSecondaryColorCustom()},
                    {"primaryRgb",Rgb(v.GetCustomPrimaryColour())},{"secondaryRgb",Rgb(v.GetCustomSecondaryColour())},
                    {"modPrimary",{{"paintType",p1},{"colour",c1},{"pearl",pearl}}},{"modSecondary",{{"paintType",p2},{"colour",c2}}}}},
                {"livery",v.GetLivery()},{"modLivery",v.GetMod(48)},{"wheelType",v.GetWheelType()},{"mods",mods},
                {"neon",{{"left",v.IsNeonLightOn(static_cast<VehicleNeonLight>(0))},{"right",v.IsNeonLightOn(static_cast<VehicleNeonLight>(1))},
                    {"front",v.IsNeonLightOn(static_cast<VehicleNeonLight>(2))},{"back",v.IsNeonLightOn(static_cast<VehicleNeonLight>(3))},{"rgb",Rgb(v.GetNeonLightsColour())}}},
                {"tyreSmokeRgb",Rgb(v.GetTyreSmokeColour())}
            };
        }
        Response PedState(int id,const SpoonerEntity& placed)
        {
            json scenario{{"active",false}},animation{{"active",false}};
            if(IS_PED_USING_ANY_SCENARIO(id))
            {
                if(placed.currentScenario.empty() || !IS_PED_USING_SCENARIO(id,placed.currentScenario.c_str())) return Error(409,"active scenario is not tracked for ped "+std::to_string(id));
                scenario={{"active",true},{"name",placed.currentScenario}};
            }
            int active=0;
            for(const auto& a:placed.lastAnimations)
            {
                if(!IS_ENTITY_PLAYING_ANIM(id,a.dict.c_str(),a.name.c_str(),3)) continue;
                ++active;
                animation={{"active",true},{"dict",a.dict},{"name",a.name},{"flags",a.flag},{"speed",a.speed},
                    {"phase",GET_ENTITY_ANIM_CURRENT_TIME(id,a.dict.c_str(),a.name.c_str())},
                    {"speedMultiplier",a.speedMultiplier},{"playbackRate",a.playbackRate},{"duration",a.duration},{"lockPosition",a.lockPos}};
            }
            if(active>1) return Error(409,"multiple active animations cannot be represented by the singular animation contract for ped "+std::to_string(id));
            return {200,json{{"still",placed.isStill},{"canRagdoll",GTAped(id).GetCanRagdoll()},{"scenario",scenario},{"animation",animation}}.dump()};
        }
        struct Captured { int id,type; unsigned long model; Transform pose; Bounds bounds; bool tracked; size_t placement; json data; };
        int Rank(int type) { if(type==3) return 0; if(type==1) return 1; return 2; }
    }
    Response Capture(const Request& request)
    {
        const auto started=std::chrono::steady_clock::now();
        const int frameNumber=GET_FRAME_COUNT();
        auto expired=[&] { return std::chrono::steady_clock::now()-started>std::chrono::milliseconds(3500); };
        Vec origin=request.origin;
        if(request.originKind=="player")
        {
            GTAentity player(PLAYER_PED_ID());
            if(!player.Exists()) return Error(503,"player does not exist");
            origin=V(player.GetPosition());
        }
        Transform frame=request.frame;
        if(request.frameKind=="entity")
        {
            GTAentity entity(request.frameEntity);
            if(!entity.Exists()) return Error(404,"frame entity does not exist");
            frame=PoseOf(entity);
        }
        std::vector<Entity> handles;
        GTAmemory::GetEntityHandles(handles,Native(origin),static_cast<float>(request.radius));
        std::sort(handles.begin(),handles.end()); handles.erase(std::unique(handles.begin(),handles.end()),handles.end());
        std::vector<int> selected;
        for(int id:handles)
        {
            if(!DOES_ENTITY_EXIST(id)) return Error(409,"entity pool changed during capture");
            if(std::find(request.types.begin(),request.types.end(),GET_ENTITY_TYPE(id))!=request.types.end() && Distance(V(GTAentity(id).GetPosition()),origin)<=request.radius) selected.push_back(id);
        }
        if(selected.size()>static_cast<size_t>(request.maxEntities)) return {409,json{{"error","entity limit exceeded"},{"observedCount",selected.size()},{"maxEntities",request.maxEntities}}.dump()};
        std::map<int,const SpoonerEntity*> db;
        for(const auto& e:sub::Spooner::Databases::EntityDb) db.emplace(e.handle.GetHandle(),&e);
        std::vector<Captured> captured;
        for(int id:selected)
        {
            if(expired()) return Error(503,"snapshot exceeded its 3500 ms capture budget; no partial capture returned");
            GTAentity entity(id);
            const auto model=entity.Model(); const int type=GET_ENTITY_TYPE(id);
            const auto sourceResult=Sources().Lookup(id,model.hash);
            if(sourceResult.status!=200) return sourceResult;
            const auto source=json::parse(sourceResult.body,nullptr,false);
            if(source.is_discarded()) return Error(500,"invalid source registry record");
            auto record=db.find(id);
            const SpoonerEntity* placed=record==db.end()?nullptr:record->second;
            std::string internal,display;
            if(type==1) { internal=GetPedModelLabel(model,false); display=GetPedModelLabel(model,true); }
            if(type==2) { internal=model.VehicleModelName(); display=model.VehicleDisplayName(true); }
            if(type==3) { internal=get_prop_model_label(model); display=internal; }
            if(internal.empty() || GET_HASH_KEY(internal.c_str())!=model.hash || display.empty()) return Error(409,"exact model name unavailable for entity "+std::to_string(id));
            const auto pose=PoseOf(entity);
            json data{{"id",id},{"type",type==1?"ped":type==2?"vehicle":"prop"},{"spoonerName",placed?placed->hashName:std::string()},
                {"model",{{"hash",IntToHexString(model.hash,true)},{"internalName",internal},{"displayName",display}}},
                {"transform",Pose(pose)},{"relativeTransform",Pose(Relative(pose,frame))}};
            Bounds local{},world{};
            if(request.geometry || request.probe)
            {
                if(!Geometry(id,model.hash,local,world)) return Error(409,"model geometry unavailable for entity "+std::to_string(id));
                if(request.geometry) data["geometry"]={{"localBounds",BoxJson(local)},{"worldBounds",BoxJson(world)},
                    {"size",Point({local.max.x-local.min.x,local.max.y-local.min.y,local.max.z-local.min.z})},{"restZOffset",-local.min.z}};
            }
            if(request.physics)
            {
                if(!placed || entity.MemoryAddress()==0) return Error(409,"complete physics state unavailable for entity "+std::to_string(id));
                data["physics"]={{"dynamic",placed->dynamic},{"frozen",entity.IsPositionFrozen()},{"gravity",entity.GetHasGravity()},{"collision",entity.GetIsCollisionEnabled()}};
            }
            if(request.attachments)
            {
                data["attachment"]={{"state","unattached"}};
                if(entity.IsAttached())
                {
                    if(!placed || !placed->attachmentArgs.isAttached) return Error(409,"attachment bone and offsets unavailable for entity "+std::to_string(id));
                    data["attachment"]={{"state","attached"},{"parentId",GET_ENTITY_ATTACHED_TO(id)},{"bone",placed->attachmentArgs.boneIndex},
                        {"offset",Point(V(placed->attachmentArgs.offset))},{"rotation",Pose({{},V(placed->attachmentArgs.rotation)})["rotation"]}};
                }
            }
            if(request.provenance) data["source"]=source;
            if(request.pedState && type==1)
            {
                if(!placed) return Error(409,"ped animation and still metadata unavailable for entity "+std::to_string(id));
                auto ped=PedState(id,*placed); if(ped.status!=200) return ped;
                data["ped"]=json::parse(ped.body,nullptr,false);
                if(data["ped"].is_discarded()) return Error(500,"invalid ped state");
            }
            if(request.vehicles && type==2) data["vehicle"]=VehicleState(id);
            if(request.props && type==3) data["prop"]={{"textureVariation",GET_OBJECT_TINT_INDEX(id)}};
            const bool tracked=source["state"]=="tracked";
            captured.push_back({id,type,model.hash,pose,world,tracked,tracked?source["placementIndex"].get<size_t>():0,data});
        }
        std::sort(captured.begin(),captured.end(),[](const Captured& a,const Captured& b) {
            return std::make_tuple(Rank(a.type),!a.tracked,a.placement,a.id)<std::make_tuple(Rank(b.type),!b.tracked,b.placement,b.id);
        });
        json entities=json::array(),edges=json::array();
        for(const auto& e:captured)
        {
            if(expired()) return Error(503,"snapshot exceeded its 3500 ms capture budget; no partial capture returned");
            entities.push_back(e.data);
            if(!request.probe) continue;
            const auto& b=e.bounds;
            const std::array<std::pair<const char*,Vec>,5> points{{
                {"center",{(b.min.x+b.max.x)/2,(b.min.y+b.max.y)/2,e.pose.position.z}},
                {"min-min",{b.min.x,b.min.y,e.pose.position.z}},{"min-max",{b.min.x,b.max.y,e.pose.position.z}},
                {"max-min",{b.max.x,b.min.y,e.pose.position.z}},{"max-max",{b.max.x,b.max.y,e.pose.position.z}}}};
            std::vector<Probe> probes; std::map<int,Bounds> supports;
            for(const auto& point:points)
            {
                const auto hit=RaycastResult::Raycast(Native(point.second),Native({point.second.x,point.second.y,b.min.z-request.maxDrop}),IntersectOptions::Everything,GTAentity(e.id));
                if(hit.Result()!=2) return Error(409,"support probe did not finish in the captured frame");
                const bool didHit=hit.DidHitAnything();
                const int support=didHit?hit.HitEntity().GetHandle():0;
                probes.push_back({point.first,didHit,support,V(hit.HitCoords())});
                if(didHit)
                {
                    Bounds local,world;
                    if(!DOES_ENTITY_EXIST(support) || !Geometry(support,GET_ENTITY_MODEL(support),local,world)) return Error(409,"support hit has no entity geometry; overlap cannot be reported");
                    supports.emplace(support,world);
                }
            }
            auto result=SupportEdges(e.id,b,probes,supports); if(result.status!=200) return result;
            auto records=json::parse(result.body,nullptr,false); if(records.is_discarded()) return Error(500,"invalid support records");
            for(const auto& edge:records) edges.push_back(edge);
        }
        for(const auto& e:captured)
        {
            if(!DOES_ENTITY_EXIST(e.id) || GET_ENTITY_MODEL(e.id)!=e.model) return Error(409,"entity changed during capture");
        }
        if(GET_FRAME_COUNT()!=frameNumber) return Error(409,"snapshot crossed a game frame");
        if(expired()) return Error(503,"snapshot exceeded its 3500 ms capture budget; no partial capture returned");
        return {200,json{{"origin",Point(origin)},{"frame",Pose(frame)},{"loadedMaps",Sources().Maps()},{"count",entities.size()},
            {"truncated",false},{"entities",entities},{"supportEdges",edges},{"captureMs",std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count()}}.dump(2,' ',false,json::error_handler_t::replace)};
    }
}
