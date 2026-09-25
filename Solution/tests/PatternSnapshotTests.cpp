#include "PatternSnapshot.h"
#include "ApiError.h"
#include <cmath>
#include <iostream>

using namespace Http::Pattern;
int failures = 0;
void Check(bool value, const char* name) { std::cout << (value ? "ok " : "FAIL ") << name << '\n'; if (!value) ++failures; }
bool Rejected(const json& body) { try { Parse(body); return false; } catch (const Http::ApiError& e) { return e.Status() == 400; } }
int main()
{
    auto body = json::parse(R"({"origin":{"kind":"player"},"radius":45,"types":["ped","vehicle","prop"],"maxEntities":400,"frame":{"kind":"world"},"supportProbe":{"enabled":true,"maxDrop":3,"sample":"center-and-four-corners"},"include":{"geometry":true,"physics":true,"attachments":true,"pedState":true,"vehicleCustomization":true,"propCustomization":true,"sourceProvenance":true}})");
    Check(Parse(body).radius == 45, "explicit request accepted");
    Check(Parse(body).scope==Scope::World,"existing request retains world selection");
    auto scoped=body;scoped["scope"]="spooner";
    Check(Parse(scoped).scope==Scope::Spooner,"explicit spooner scope accepted");
    scoped["scope"]="nearest";Check(Rejected(scoped),"unknown scope rejected");
    Check(SelectForScope(Scope::World,7,7,8,false).included,"world scope still includes untracked player");
    auto playerScope=SelectForScope(Scope::Spooner,7,7,8,true);
    Check(!playerScope.included && playerScope.reason=="player","spooner scope explicitly excludes even a tracked player");
    auto vehicleScope=SelectForScope(Scope::Spooner,8,7,8,true);
    Check(!vehicleScope.included && vehicleScope.reason=="player_current_vehicle","spooner scope explicitly excludes current vehicle");
    Check(!SelectForScope(Scope::Spooner,9,7,8,false).included,"unowned world entity is out of scope");
    Check(SelectForScope(Scope::Spooner,9,7,8,true).included,"spooner owned entity is selected");
    for (auto it = body.begin(); it != body.end(); ++it) { auto missing = body; missing.erase(it.key()); Check(Rejected(missing), "missing top-level field rejected"); }
    for (auto it = body["include"].begin(); it != body["include"].end(); ++it) { auto missing = body; missing["include"].erase(it.key()); Check(Rejected(missing), "missing include flag rejected"); }
    auto bad = body; bad["extra"] = true; Check(Rejected(bad), "unknown field rejected");
    bad = body; bad["origin"]["position"] = {{"x",0},{"y",0},{"z",0}}; Check(Rejected(bad), "mixed origin union rejected");
    bad = body; bad["frame"] = {{"kind","entity"}}; Check(Rejected(bad), "entity frame requires id");
    bad = body; bad["radius"] = 0; Check(Rejected(bad), "zero radius rejected");
    bad = body; bad["maxEntities"] = 401; Check(Rejected(bad), "overflow limit rejected");
    bad = body; bad["types"] = {"ped","ped"}; Check(Rejected(bad), "duplicate type rejected");
    bad = body; bad["include"]["physics"] = 1; Check(Rejected(bad), "nonboolean flag rejected");

    Transform frame{{100,200,300},{23,-31,89}}, entity{{107,211,317},{-42,17,136}};
    auto relative = Relative(entity, frame); auto restored = Compose(frame, relative);
    Check(Distance(restored.position, entity.position) < 1e-8, "rigid position round trip");
    auto matrixA = RotationMatrix(restored.rotation), matrixB = RotationMatrix(entity.rotation);
    double error = 0; for (int i=0;i<9;++i) error += std::abs(matrixA[i]-matrixB[i]);
    Check(error < 1e-8, "nonzero pitch roll yaw round trip");
    Check(Distance(Relative(entity, Transform{}).position,entity.position)<1e-8, "world frame preserves position");
    Bounds base{{0,0,0},{2,2,1}}, full{{0,0,1},{2,2,2}}, partial{{1,0,1},{3,2,2}};
    Check(Overlap(base,full)==4, "full support area");
    Check(Overlap(base,partial)==2, "partial support area");
    Check(Overlap(base,Bounds{{3,3,1},{4,4,2}})==0, "no support area");
    std::vector<Probe> samples{{"center",true,9,{1,1,1}},{"min-min",false,0,{}},{"min-max",false,0,{}},{"max-min",false,0,{}},{"max-max",false,0,{}}};
    auto edges=json::parse(SupportEdges(7,full,samples,{{9,base}}).body);
    Check(edges[0]["xyOverlapArea"]==4 && edges[0]["supportedFootprintRatio"]==1 && edges[0]["verticalGap"]==0, "full support edge uses actual hit and bounds");
    edges=json::parse(SupportEdges(7,partial,samples,{{9,base}}).body);
    Check(edges[0]["xyOverlapArea"]==2 && edges[0]["supportedFootprintRatio"]==0.5, "partial support edge retains overlap ratio");
    Check(SupportEdges(7,full,samples,{}).status==409,"unknown support geometry is not fabricated");
    samples[0].hit=false;
    edges=json::parse(SupportEdges(7,full,samples,{}).body);
    Check(edges[0]["state"]=="none" && edges[0]["probeSamples"].size()==5,"absent support keeps five samples");

    SourceRegistry registry;
    Check(registry.Lookup(7,42).body.find("untracked")!=std::string::npos, "untracked explicit");
    auto map = registry.Begin("2026-4", "menyooStuff/Spooner/2026-4.xml");
    registry.Register(7,42,map,428);
    Check(registry.Lookup(7,42).status==409, "unfinished map is stale");
    registry.Complete(map);
    auto source = json::parse(registry.Lookup(7,42).body);
    Check(source["placementIndex"]==428 && source["mapName"]=="2026-4", "loader-owned source preserved");
    Check(registry.Lookup(7,43).status==409, "recycled handle model mismatch rejected");
    registry.Remove(7);
    Check(registry.Lookup(7,42).body.find("untracked")!=std::string::npos, "deletion removes provenance");
    Check(registry.Maps().empty(), "last removed entity removes loaded map");
    return failures ? 1 : 0;
}
