#pragma once
#include "CommandQueue.h"
#include <json/single_include/nlohmann/json.hpp>
#include <string>
namespace Http
{
    struct BikeStep { int status; std::string error; };
    struct SpidermanBikeRuntime
    {
        virtual ~SpidermanBikeRuntime()=default;
        virtual BikeStep Preflight()=0;
        virtual BikeStep LeaveVehicle()=0;
        virtual BikeStep ChangeModel()=0;
        virtual BikeStep EnableCollision()=0;
        virtual BikeStep SpawnBike()=0;
        virtual BikeStep SeatPlayer()=0;
        virtual BikeStep Verify()=0;
        virtual nlohmann::json Observe()=0;
    };
    Response RunSpidermanBike(SpidermanBikeRuntime& game);
    Response MakeSpidermanOnBike();
    bool IsSpidermanBikeChord(bool control,bool shift,bool alt);
}
