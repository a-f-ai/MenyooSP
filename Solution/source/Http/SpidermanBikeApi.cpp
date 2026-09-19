#include "SpidermanBike.h"
#include "ModelApi.h"
#include "PatternSnapshot.h"
#include "../Menu/Routine.h"
#include "../Memory/GTAmemory.h"
#include "../Natives/natives2.h"
#include "../Scripting/GTAped.h"
#include "../Scripting/GTAvehicle.h"
#include "../Scripting/Model.h"
#include "../Scripting/Raycast.h"
#include "../Scripting/enums.h"
#include "../Submenus/PedModelChanger.h"
#include "../Submenus/Spooner/SpoonerMode.h"
#include "../Util/StringManip.h"
#include <algorithm>
#include <cmath>
#include <limits>
#undef min
#undef max

namespace Http
{
    namespace
    {
        using json=nlohmann::json;
        struct NativeBike final : SpidermanBikeRuntime
        {
            GTAmodel::Model hero{std::string("SpidermanRed")},bikeModel{std::string("bati2")};
            int oldPed=0,oldVehicle=0,bike=0;
            Vector3 anchor,position;
            float heading=0;
            ModelApi::Box bikeBox;
            ~NativeBike() override { hero.Unload(); bikeModel.Unload(); }
            BikeStep SafePlace()
            {
                Pattern::Transform pose{{position.x,position.y,position.z},{0,0,heading}};
                float minHeight=std::numeric_limits<float>::infinity(),maxHeight=-minHeight;
                for(int i=0;i<5;++i)
                {
                    const float x=i==0?0:((i&1)?bikeBox.max.x:bikeBox.min.x);
                    const float y=i==0?0:((i&2)?bikeBox.max.y:bikeBox.min.y);
                    const auto p=Pattern::Compose(pose,{{x,y,0},{}}).position;
                    auto hit=RaycastResult::Raycast(Vector3(float(p.x),float(p.y),anchor.z+1),Vector3(float(p.x),float(p.y),anchor.z-3),IntersectOptions::Everything,GTAentity(oldPed));
                    if(hit.Result()!=2) return {409,"support probe unresolved"};
                    if(!hit.DidHitAnything() || hit.SurfaceNormal().z<0.95f) return {409,"the one designated position beside the player lacks flat support"};
                    if(hit.HitEntity().GetHandle()==oldVehicle && oldVehicle!=0) return {409,"spawn support is the current vehicle, not a safe surface"};
                    minHeight=std::min(minHeight,hit.HitCoords().z);maxHeight=std::max(maxHeight,hit.HitCoords().z);
                }
                if(maxHeight-minHeight>0.12f) return {409,"bike footprint crosses an edge or uneven support"};
                position.z=maxHeight-bikeBox.min.z+0.03f;
                pose.position.z=position.z;
                Pattern::Bounds destination{{1e20,1e20,1e20},{-1e20,-1e20,-1e20}};
                for(int i=0;i<8;++i)
                {
                    const auto p=Pattern::Compose(pose,{{(i&1)?bikeBox.max.x:bikeBox.min.x,(i&2)?bikeBox.max.y:bikeBox.min.y,(i&4)?bikeBox.max.z:bikeBox.min.z},{}}).position;
                    destination.min.x=std::min(destination.min.x,p.x);destination.min.y=std::min(destination.min.y,p.y);destination.min.z=std::min(destination.min.z,p.z);
                    destination.max.x=std::max(destination.max.x,p.x);destination.max.y=std::max(destination.max.y,p.y);destination.max.z=std::max(destination.max.z,p.z);
                }
                std::vector<Entity> nearby;GTAmemory::GetEntityHandles(nearby,position,12.0f);
                for(int id:nearby)
                {
                    if(id==oldPed) continue;
                    GTAentity e(id);if(!e.Exists()) return {409,"nearby entity disappeared during clearance check"};
                    auto box=ModelApi::GetBox(e.Model().hash);if(!box.valid) return {409,"nearby entity geometry unavailable"};
                    Pattern::Bounds occupied{{1e20,1e20,1e20},{-1e20,-1e20,-1e20}};
                    for(int i=0;i<8;++i)
                    {
                        auto p=GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS(id,(i&1)?box.max.x:box.min.x,(i&2)?box.max.y:box.min.y,(i&4)?box.max.z:box.min.z);
                        occupied.min.x=std::min(occupied.min.x,double(p.x));occupied.min.y=std::min(occupied.min.y,double(p.y));occupied.min.z=std::min(occupied.min.z,double(p.z));
                        occupied.max.x=std::max(occupied.max.x,double(p.x));occupied.max.y=std::max(occupied.max.y,double(p.y));occupied.max.z=std::max(occupied.max.z,double(p.z));
                    }
                    if(Pattern::Overlap(destination,occupied)>0 && destination.min.z<occupied.max.z && destination.max.z>occupied.min.z) return {409,"the designated bike position overlaps entity "+std::to_string(id)};
                }
                for(int level=0;level<2;++level)
                {
                    const float z=level?float(destination.max.z):float(destination.min.z)+0.15f;
                    for(int axis=0;axis<2;++axis)
                    {
                        Vector3 a(float(destination.min.x),float(destination.min.y),z),b(float(destination.max.x),float(destination.max.y),z);
                        if(axis) { a.y=float(destination.max.y);b.y=float(destination.min.y); }
                        auto ray=RaycastResult::Raycast(a,b,IntersectOptions::Everything,GTAentity(oldPed));
                        if(ray.Result()!=2 || ray.DidHitAnything()) return {409,"bike clearance intersects geometry or could not be measured"};
                    }
                }
                return {200,""};
            }
            BikeStep Preflight() override
            {
                if(NETWORK_IS_IN_SESSION()) return {409,"single-player only"};
                if(noClipToggle || sub::Spooner::SpoonerMode::bEnabled) return {409,"leave noclip and Spooner camera before changing the playable character"};
                oldPed=PLAYER_PED_ID();GTAped player(oldPed);
                if(!player.Exists() || IS_ENTITY_DEAD(oldPed,false)) return {409,"player is unavailable or dead"};
                if(player.IsAttached()) return {409,"detach the player before this action"};
                if(!hero.IsInCdImage() || !hero.IsPed()) return {422,"SpidermanRed is not an installed ped model"};
                if(!bikeModel.IsInCdImage() || !bikeModel.IsBike()) return {422,"bati2 is not an installed motorcycle model"};
                if(!hero.Load(4000) || !bikeModel.Load(4000)) return {503,"SpidermanRed or bati2 did not stream within the preflight deadline"};
                if(PLAYER_PED_ID()!=oldPed || !player.Exists()) return {409,"player changed during model streaming"};
                oldVehicle=GET_VEHICLE_PED_IS_IN(oldPed,false);
                GTAentity base(oldVehicle==0?oldPed:oldVehicle);
                if(GET_ENTITY_SPEED(base.GetHandle())>0.5f) return {409,"stop moving before spawning the bike"};
                anchor=base.GetPosition();heading=base.GetHeading();
                const auto baseBox=ModelApi::GetBox(base.Model().hash);bikeBox=ModelApi::GetBox(bikeModel.hash);
                if(!baseBox.valid || !bikeBox.valid) return {409,"model dimensions unavailable for safe placement"};
                position=base.GetOffsetInWorldCoords(Vector3(baseBox.max.x-bikeBox.min.x+0.75f,0,0));
                return SafePlace();
            }
            BikeStep LeaveVehicle() override
            {
                if(oldVehicle==0) return {200,""};
                TASK_LEAVE_VEHICLE(oldPed,oldVehicle,16);
                const int start=GET_GAME_TIMER();
                while(IS_PED_IN_ANY_VEHICLE(oldPed,false) && GET_GAME_TIMER()-start<2000) WAIT(0);
                if(PLAYER_PED_ID()!=oldPed || !DOES_ENTITY_EXIST(oldPed)) return {409,"player changed during vehicle exit"};
                if(IS_PED_IN_ANY_VEHICLE(oldPed,false)) return {409,"player did not leave the previous vehicle within 2000 ms"};
                return SafePlace();
            }
            BikeStep ChangeModel() override
            {
                if(GET_ENTITY_MODEL(PLAYER_PED_ID())!=hero.hash) sub::ChangeModel(hero);
                if(!DOES_ENTITY_EXIST(PLAYER_PED_ID()) || GET_ENTITY_MODEL(PLAYER_PED_ID())!=hero.hash) return {500,"player model did not become SpidermanRed"};
                return {200,""};
            }
            BikeStep EnableCollision() override
            {
                GTAped player(PLAYER_PED_ID());player.SetIsCollisionEnabled(true);
                if(!player.GetIsCollisionEnabled()) return {500,"player collision did not become enabled"};
                return {200,""};
            }
            BikeStep SpawnBike() override
            {
                bike=CREATE_VEHICLE(bikeModel.hash,position.x,position.y,position.z,heading,false,true,false);
                if(!DOES_ENTITY_EXIST(bike)) return {500,"CREATE_VEHICLE failed for bati2"};
                SET_ENTITY_AS_MISSION_ENTITY(bike,true,true);
                SET_VEHICLE_MOD_KIT(bike,0);
                if(GET_VEHICLE_LIVERY_COUNT(bike)<=1) return {422,"installed bati2 does not expose canonical native livery 1"};
                CLEAR_VEHICLE_CUSTOM_PRIMARY_COLOUR(bike);CLEAR_VEHICLE_CUSTOM_SECONDARY_COLOUR(bike);
                SET_VEHICLE_COLOURS(bike,0,0);SET_VEHICLE_LIVERY(bike,1);
                SET_ENTITY_COLLISION(bike,true,true);SET_VEHICLE_IS_STOLEN(bike,false);
                if(GET_VEHICLE_LIVERY(bike)!=1) return {500,"bati2 livery readback did not match 1"};
                return {200,""};
            }
            BikeStep SeatPlayer() override
            {
                if(!DOES_ENTITY_EXIST(bike)) return {409,"created bike no longer exists"};
                SET_PED_INTO_VEHICLE(PLAYER_PED_ID(),bike,-1);
                return {200,""};
            }
            BikeStep Verify() override
            {
                const int ped=PLAYER_PED_ID();
                if(!DOES_ENTITY_EXIST(ped) || GET_ENTITY_MODEL(ped)!=hero.hash || !GTAentity(ped).GetIsCollisionEnabled()) return {500,"player model or collision readback failed"};
                if(!DOES_ENTITY_EXIST(bike) || GET_ENTITY_MODEL(bike)!=bikeModel.hash || GET_VEHICLE_LIVERY(bike)!=1) return {500,"bike model or livery readback failed"};
                int primary,secondary;GET_VEHICLE_COLOURS(bike,&primary,&secondary);
                if(primary!=0 || secondary!=0 || GET_IS_VEHICLE_PRIMARY_COLOUR_CUSTOM(bike) || GET_IS_VEHICLE_SECONDARY_COLOUR_CUSTOM(bike)) return {500,"bike paint readback failed"};
                if(GET_VEHICLE_PED_IS_IN(ped,false)!=bike || GET_PED_IN_VEHICLE_SEAT(bike,-1,false)!=ped) return {500,"player is not the created bike's driver"};
                return {200,""};
            }
            json Observe() override
            {
                int ped=PLAYER_PED_ID();json state{{"playerExists",DOES_ENTITY_EXIST(ped)!=0},{"previousVehicleId",oldVehicle},{"createdVehicleId",bike},{"createdVehicleExists",DOES_ENTITY_EXIST(bike)!=0}};
                if(DOES_ENTITY_EXIST(ped)) state["player"]={{"id",ped},{"modelHash",IntToHexString(GET_ENTITY_MODEL(ped),true)},
                    {"collision",GTAentity(ped).GetIsCollisionEnabled()},{"vehicleId",GET_VEHICLE_PED_IS_IN(ped,false)}};
                if(DOES_ENTITY_EXIST(bike)) state["bike"]={{"id",bike},{"modelHash",IntToHexString(GET_ENTITY_MODEL(bike),true)},
                    {"livery",GET_VEHICLE_LIVERY(bike)},{"driverId",GET_PED_IN_VEHICLE_SEAT(bike,-1,false)}};
                return state;
            }
        };
    }
    Response MakeSpidermanOnBike() { NativeBike game;return RunSpidermanBike(game); }
}
