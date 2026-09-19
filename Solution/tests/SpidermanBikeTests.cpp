#include "SpidermanBike.h"
#include <iostream>
using namespace Http;
using json=nlohmann::json;
struct Game : SpidermanBikeRuntime
{
    std::string fail;
    bool inOldVehicle,collision=false;
    std::string model="original";
    int bike=0,livery=-1,vehicle=0;
    explicit Game(bool seated):inOldVehicle(seated) { if(seated) vehicle=8; }
    BikeStep Preflight() override { return fail=="preflight"?BikeStep{422,"missing model"}:BikeStep{200,""}; }
    BikeStep LeaveVehicle() override { if(fail=="leave-vehicle") return {409,"exit blocked"}; inOldVehicle=false;vehicle=0;return {200,""}; }
    BikeStep ChangeModel() override { if(inOldVehicle) return {500,"still in vehicle"}; if(fail=="change-model") return {500,"model failed"}; model="SpidermanRed";return {200,""}; }
    BikeStep EnableCollision() override { if(fail=="collision") return {500,"collision failed"};collision=true;return {200,""}; }
    BikeStep SpawnBike() override { if(fail=="spawn-bike") return {500,"spawn failed"};bike=9;livery=1;return {200,""}; }
    BikeStep SeatPlayer() override { if(fail=="seat") return {500,"seat failed"};vehicle=bike;return {200,""}; }
    BikeStep Verify() override { if(fail=="verify" || model!="SpidermanRed" || !collision || vehicle!=9 || livery!=1) return {500,"readback mismatch"};return {200,""}; }
    json Observe() override { return {{"model",model},{"collision",collision},{"vehicleId",vehicle},{"createdVehicleId",bike},{"livery",livery}}; }
};
int main()
{
    int failures=0;
    auto check=[&](bool ok,const char* name) { std::cout<<(ok?"ok ":"FAIL ")<<name<<'\n';if(!ok)++failures; };
    for(bool seated:{false,true})
    {
        Game game(seated);
        auto result=RunSpidermanBike(game);auto body=json::parse(result.body);
        check(result.status==201 && body["state"]["model"]=="SpidermanRed" && body["state"]["vehicleId"]==9 && body["state"]["collision"]==true,"full action succeeds on foot and from existing vehicle");
    }
    for(const auto* stage:{"preflight","leave-vehicle","change-model","collision","spawn-bike","seat","verify"})
    {
        Game game(true);game.fail=stage;
        auto result=RunSpidermanBike(game);auto body=json::parse(result.body);
        check(result.status>=400 && body["stage"]==stage && body["state"]==game.Observe() && !body.contains("status"),"failure returns actual state and failing stage, never success");
        if(game.fail=="preflight") check(game.model=="original" && game.vehicle==8 && game.bike==0,"preflight failure leaves world unchanged");
        if(game.fail=="collision") check(game.bike==0,"collision failure stops before spawning");
        if(game.fail=="seat") check(game.bike==9 && game.vehicle==0,"seat failure reports retained bike without invented rollback");
    }
    return failures?1:0;
}
