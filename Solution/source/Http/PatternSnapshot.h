#pragma once
#include "CommandQueue.h"
#include <json/single_include/nlohmann/json.hpp>
#include <array>
#include <map>
#include <string>
#include <vector>

namespace Http::Pattern
{
    using json = nlohmann::json;
    struct Vec { double x=0, y=0, z=0; };
    struct Transform { Vec position, rotation; };
    struct Bounds { Vec min, max; };
    enum class Scope { World, Spooner };
    struct ScopeDecision { bool included; std::string reason; };
    ScopeDecision SelectForScope(Scope scope,int entity,int player,int currentVehicle,bool spoonerOwned);
    struct Probe { std::string sample; bool hit; int entity; Vec point; };
    Response SupportEdges(int entity, Bounds bounds, const std::vector<Probe>& samples, const std::map<int,Bounds>& supports);
    struct Request
    {
        Scope scope;
        std::string originKind, frameKind;
        Vec origin;
        Transform frame;
        int frameEntity, maxEntities;
        double radius, maxDrop;
        std::vector<int> types;
        bool probe, geometry, physics, attachments, pedState, vehicles, props, provenance;
    };
    Request Parse(const json& body);
    json Point(Vec value);
    json Pose(Transform value);
    std::array<double,9> RotationMatrix(Vec rotation);
    Transform Relative(Transform entity, Transform frame);
    Transform Compose(Transform frame, Transform relative);
    double Distance(Vec a, Vec b);
    double Overlap(Bounds a, Bounds b);
    Response Error(int status, const std::string& message);

    class SourceRegistry
    {
        struct Map { std::string name, path; bool complete; };
        struct Source { unsigned long model; uint64_t map; size_t index; };
        std::map<uint64_t,Map> m_maps;
        std::map<int,Source> m_entities;
        uint64_t m_sequence=0;
    public:
        uint64_t Begin(const std::string& name, const std::string& path);
        void Register(int handle, unsigned long model, uint64_t map, size_t index);
        void Complete(uint64_t map);
        void Remove(int handle);
        void Clear();
        Response Lookup(int handle, unsigned long model) const;
        bool Tracks(int handle) const;
        json Maps() const;
    };
    SourceRegistry& Sources();
    Response Capture(const Request& request);
}
