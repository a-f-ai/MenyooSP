#include "SpidermanBike.h"
#include "../Natives/natives2.h"
#include "../Scripting/GTAped.h"
#include "../Scripting/GTAvehicle.h"
#include "../Scripting/Model.h"
#include "../Scripting/enums.h"
#include "../Submenus/PedModelChanger.h"
#include "../Util/StringManip.h"
#include <cmath>

namespace Http
{
    namespace
    {
        using json=nlohmann::json;
        struct NativeBike final : SpidermanBikeRuntime
        {
            GTAmodel::Model hero{std::string("SpidermanRed")},bikeModel{std::string("bati2")};
            int oldPed=0,oldVehicle=0,bike=0;
            Vector3 position;
            float heading=0;
            ~NativeBike() override { hero.Unload(); bikeModel.Unload(); }
            BikeStep Prepare() override
            {
                if(NETWORK_IS_IN_SESSION()) return {409,"single-player only"};
                oldPed=PLAYER_PED_ID();GTAped player(oldPed);
                if(!player.Exists() || IS_ENTITY_DEAD(oldPed,false)) return {409,"player is unavailable or dead"};
                if(!hero.IsInCdImage() || !hero.IsPed()) return {422,"SpidermanRed is not an installed ped model"};
                if(!bikeModel.IsInCdImage() || !bikeModel.IsBike()) return {422,"bati2 is not an installed motorcycle model"};
                if(!hero.Load(4000) || !bikeModel.Load(4000)) return {503,"SpidermanRed or bati2 did not stream within the preparation deadline"};
                if(PLAYER_PED_ID()!=oldPed || !player.Exists()) return {409,"player changed during model streaming"};
                oldVehicle=GET_VEHICLE_PED_IS_IN(oldPed,false);
                heading=player.GetHeading();
                position=player.GetPosition();
                const float radians=heading*0.017453292519943295f;
                position.x+=2.0f*std::cos(radians);
                position.y+=2.0f*std::sin(radians);
                return {200,""};
            }
            BikeStep LeaveVehicle() override
            {
                if(oldVehicle==0) return {200,""};
                TASK_LEAVE_VEHICLE(oldPed,oldVehicle,16);
                const int start=GET_GAME_TIMER();
                while(IS_PED_IN_ANY_VEHICLE(oldPed,false) && GET_GAME_TIMER()-start<2000) WAIT(0);
                if(PLAYER_PED_ID()!=oldPed || !DOES_ENTITY_EXIST(oldPed)) return {409,"player changed during vehicle exit"};
                if(IS_PED_IN_ANY_VEHICLE(oldPed,false)) return {409,"player did not leave the previous vehicle within 2000 ms"};
                return {200,""};
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
