#include "SpidermanBike.h"
namespace Http
{
    Response RunSpidermanBike(SpidermanBikeRuntime& game)
    {
        using json=nlohmann::json;
        const std::pair<const char*,BikeStep(SpidermanBikeRuntime::*)()> steps[]{
            {"preflight",&SpidermanBikeRuntime::Preflight},{"leave-vehicle",&SpidermanBikeRuntime::LeaveVehicle},
            {"change-model",&SpidermanBikeRuntime::ChangeModel},{"collision",&SpidermanBikeRuntime::EnableCollision},
            {"spawn-bike",&SpidermanBikeRuntime::SpawnBike},{"seat",&SpidermanBikeRuntime::SeatPlayer},{"verify",&SpidermanBikeRuntime::Verify}};
        for(const auto& step:steps)
        {
            const auto result=(game.*step.second)();
            if(result.status!=200) return {result.status,json{{"error",result.error},{"stage",step.first},{"state",game.Observe()}}.dump(2,' ',false,json::error_handler_t::replace)};
        }
        return {201,json{{"status","seated"},{"state",game.Observe()}}.dump(2,' ',false,json::error_handler_t::replace)};
    }
}
