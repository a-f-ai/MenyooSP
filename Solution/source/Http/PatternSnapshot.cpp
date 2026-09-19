#include "PatternSnapshot.h"
#include "ApiError.h"
#include <algorithm>
#include <cmath>
#include <set>

namespace Http::Pattern
{
    namespace
    {
        void Keys(const json& obj, std::initializer_list<const char*> keys)
        {
            if (!obj.is_object() || obj.size()!=keys.size()) throw ApiError(400,"object has missing or unknown fields");
            for (auto key:keys) if (!obj.contains(key)) throw ApiError(400,std::string("missing field: ")+key);
        }
        double Number(const json& obj, const char* key)
        {
            if (!obj.contains(key) || !obj[key].is_number()) throw ApiError(400,std::string("expected finite number: ")+key);
            double v=obj[key].get<double>();
            if (!std::isfinite(v)) throw ApiError(400,"nonfinite number");
            return v;
        }
        int Integer(const json& obj,const char* key,int lo,int hi)
        {
            const double v=Number(obj,key);
            if (!obj[key].is_number_integer() || v<lo || v>hi) throw ApiError(400,std::string("integer out of range: ")+key);
            return static_cast<int>(v);
        }
        bool Flag(const json& obj,const char* key)
        {
            if (!obj[key].is_boolean()) throw ApiError(400,std::string("expected boolean: ")+key);
            return obj[key].get<bool>();
        }
        std::string Kind(const json& obj)
        {
            if (!obj.is_object() || !obj.contains("kind") || !obj["kind"].is_string()) throw ApiError(400,"union requires string kind");
            return obj["kind"].get<std::string>();
        }
        Vec Vector(const json& v) { Keys(v,{"x","y","z"}); return {Number(v,"x"),Number(v,"y"),Number(v,"z")}; }
        Vec Angles(const json& v) { Keys(v,{"pitch","roll","yaw"}); return {Number(v,"pitch"),Number(v,"roll"),Number(v,"yaw")}; }
        using Matrix=std::array<double,9>;
        Matrix Transpose(Matrix m) { return {m[0],m[3],m[6],m[1],m[4],m[7],m[2],m[5],m[8]}; }
        Matrix Multiply(Matrix a,Matrix b)
        {
            Matrix result{};
            for(int row=0;row<3;++row) for(int col=0;col<3;++col) for(int k=0;k<3;++k) result[row*3+col]+=a[row*3+k]*b[k*3+col];
            return result;
        }
        Vec Apply(Matrix m,Vec v) { return {m[0]*v.x+m[1]*v.y+m[2]*v.z,m[3]*v.x+m[4]*v.y+m[5]*v.z,m[6]*v.x+m[7]*v.y+m[8]*v.z}; }
        Vec Euler(Matrix m)
        {
            constexpr double degrees=180.0/3.14159265358979323846;
            double pitch=std::asin(std::clamp(m[7],-1.0,1.0));
            if(std::abs(std::cos(pitch))<1e-9) return {pitch*degrees,0,std::atan2(m[3],m[0])*degrees};
            return {pitch*degrees,std::atan2(-m[6],m[8])*degrees,std::atan2(-m[1],m[4])*degrees};
        }
    }
    Request Parse(const json& b)
    {
        if(b.contains("scope")) Keys(b,{"scope","origin","radius","types","maxEntities","frame","supportProbe","include"});
        if(!b.contains("scope")) Keys(b,{"origin","radius","types","maxEntities","frame","supportProbe","include"});
        Request r{};
        r.scope=Scope::World;
        if(b.contains("scope"))
        {
            if(b["scope"]!="world" && b["scope"]!="spooner") throw ApiError(400,"scope must be world or spooner");
            if(b["scope"]=="spooner") r.scope=Scope::Spooner;
        }
        r.originKind=Kind(b["origin"]);
        if(r.originKind=="player") Keys(b["origin"],{"kind"});
        if(r.originKind=="position") { Keys(b["origin"],{"kind","position"}); r.origin=Vector(b["origin"]["position"]); }
        if(r.originKind!="player" && r.originKind!="position") throw ApiError(400,"unknown origin kind");
        r.frameKind=Kind(b["frame"]);
        if(r.frameKind=="world") Keys(b["frame"],{"kind"});
        if(r.frameKind=="entity") { Keys(b["frame"],{"kind","entityId"}); r.frameEntity=Integer(b["frame"],"entityId",1,2147483647); }
        if(r.frameKind=="transform") { Keys(b["frame"],{"kind","position","rotation"}); r.frame={Vector(b["frame"]["position"]),Angles(b["frame"]["rotation"])}; }
        if(r.frameKind!="world" && r.frameKind!="entity" && r.frameKind!="transform") throw ApiError(400,"unknown frame kind");
        r.radius=Number(b,"radius");
        if(r.radius<=0 || r.radius>500) throw ApiError(400,"radius must be (0,500]");
        r.maxEntities=Integer(b,"maxEntities",1,400);
        if(!b["types"].is_array() || b["types"].empty()) throw ApiError(400,"types must be a nonempty array");
        std::set<int> types;
        for(const auto& type:b["types"])
        {
            int id=0;
            if(type=="ped") id=1;
            if(type=="vehicle") id=2;
            if(type=="prop") id=3;
            if(id==0 || !types.insert(id).second) throw ApiError(400,"unknown or duplicate type");
            r.types.push_back(id);
        }
        const auto& p=b["supportProbe"];
        Keys(p,{"enabled","maxDrop","sample"}); r.probe=Flag(p,"enabled"); r.maxDrop=Number(p,"maxDrop");
        if(r.maxDrop<=0 || r.maxDrop>500 || p["sample"]!="center-and-four-corners") throw ApiError(400,"invalid supportProbe");
        const auto& i=b["include"];
        Keys(i,{"geometry","physics","attachments","pedState","vehicleCustomization","propCustomization","sourceProvenance"});
        r.geometry=Flag(i,"geometry"); r.physics=Flag(i,"physics"); r.attachments=Flag(i,"attachments"); r.pedState=Flag(i,"pedState");
        r.vehicles=Flag(i,"vehicleCustomization"); r.props=Flag(i,"propCustomization"); r.provenance=Flag(i,"sourceProvenance");
        return r;
    }
    json Point(Vec v) { return {{"x",v.x},{"y",v.y},{"z",v.z}}; }
    ScopeDecision SelectForScope(Scope scope,int entity,int player,int currentVehicle,bool spoonerOwned)
    {
        if(scope==Scope::World) return {true,"selected"};
        if(entity==player) return {false,"player"};
        if(currentVehicle!=0 && entity==currentVehicle) return {false,"player_current_vehicle"};
        if(!spoonerOwned) return {false,"not_spooner_owned"};
        return {true,"selected"};
    }
    json Pose(Transform t) { return {{"position",Point(t.position)},{"rotation",{{"pitch",t.rotation.x},{"roll",t.rotation.y},{"yaw",t.rotation.z}}}}; }
    std::array<double,9> RotationMatrix(Vec v)
    {
        constexpr double radians=3.14159265358979323846/180.0;
        const double a=v.x*radians,b=v.y*radians,c=v.z*radians;
        const double sx=std::sin(a),cx=std::cos(a),sy=std::sin(b),cy=std::cos(b),sz=std::sin(c),cz=std::cos(c);
        return {cz*cy-sz*sx*sy,-sz*cx,cz*sy+sz*sx*cy,sz*cy+cz*sx*sy,cz*cx,sz*sy-cz*sx*cy,-cx*sy,sx,cx*cy};
    }
    Transform Relative(Transform e,Transform f)
    {
        auto inverse=Transpose(RotationMatrix(f.rotation));
        return {Apply(inverse,{e.position.x-f.position.x,e.position.y-f.position.y,e.position.z-f.position.z}),Euler(Multiply(inverse,RotationMatrix(e.rotation)))};
    }
    Transform Compose(Transform f,Transform r)
    {
        auto m=RotationMatrix(f.rotation); auto p=Apply(m,r.position);
        return {{p.x+f.position.x,p.y+f.position.y,p.z+f.position.z},Euler(Multiply(m,RotationMatrix(r.rotation)))};
    }
    double Distance(Vec a,Vec b) { return std::hypot(a.x-b.x,a.y-b.y,a.z-b.z); }
    double Overlap(Bounds a,Bounds b) { return std::max(0.0,std::min(a.max.x,b.max.x)-std::max(a.min.x,b.min.x))*std::max(0.0,std::min(a.max.y,b.max.y)-std::max(a.min.y,b.min.y)); }
    Response Error(int status,const std::string& message) { return {status,json{{"error",message}}.dump()}; }
    Response SupportEdges(int entity,Bounds bounds,const std::vector<Probe>& samples,const std::map<int,Bounds>& supports)
    {
        json probes=json::array(),edges=json::array();
        std::map<int,Vec> hits;
        for(const auto& sample:samples)
        {
            json probe{{"sample",sample.sample},{"hit",sample.hit}};
            if(sample.hit) { probe["entityId"]=sample.entity; probe["point"]=Point(sample.point); hits.emplace(sample.entity,sample.point); }
            probes.push_back(probe);
        }
        if(hits.empty()) edges.push_back({{"supportedEntityId",entity},{"state","none"},{"probeSamples",probes}});
        const double footprint=(bounds.max.x-bounds.min.x)*(bounds.max.y-bounds.min.y);
        for(const auto& hit:hits)
        {
            auto support=supports.find(hit.first);
            if(support==supports.end() || footprint<=0) return Error(409,"support geometry unavailable for entity "+std::to_string(hit.first));
            const double overlap=Overlap(bounds,support->second);
            edges.push_back({{"supportedEntityId",entity},{"state","hit"},{"supportEntityId",hit.first},{"contactPoint",Point(hit.second)},
                {"verticalGap",bounds.min.z-hit.second.z},{"xyOverlapArea",overlap},{"supportedFootprintRatio",overlap/footprint},{"probeSamples",probes}});
        }
        return {200,edges.dump()};
    }
    uint64_t SourceRegistry::Begin(const std::string& name,const std::string& path) { const auto id=++m_sequence; m_maps.emplace(id,Map{name,path,false}); return id; }
    void SourceRegistry::Register(int handle,unsigned long model,uint64_t map,size_t index)
    {
        const auto old=m_entities.find(handle);
        if(old!=m_entities.end() && old->second.map!=map) Remove(handle);
        m_entities.insert_or_assign(handle,Source{model,map,index});
    }
    void SourceRegistry::Complete(uint64_t map) { auto it=m_maps.find(map); if(it!=m_maps.end()) it->second.complete=true; }
    void SourceRegistry::Remove(int handle)
    {
        auto it=m_entities.find(handle); if(it==m_entities.end()) return;
        auto map=it->second.map; m_entities.erase(it);
        for(const auto& entry:m_entities) if(entry.second.map==map) return;
        m_maps.erase(map);
    }
    void SourceRegistry::Clear() { m_entities.clear(); m_maps.clear(); }
    bool SourceRegistry::Tracks(int handle) const { return m_entities.find(handle)!=m_entities.end(); }
    Response SourceRegistry::Lookup(int handle,unsigned long model) const
    {
        auto source=m_entities.find(handle);
        if(source==m_entities.end()) return {200,json{{"state","untracked"},{"reason","not_created_by_spooner_loader"}}.dump()};
        auto map=m_maps.find(source->second.map);
        if(source->second.model!=model || map==m_maps.end() || !map->second.complete) return Error(409,"stale or incomplete loaded-map provenance for entity "+std::to_string(handle));
        return {200,json{{"state","tracked"},{"mapName",map->second.name},{"mapPath",map->second.path},{"placementIndex",source->second.index}}.dump()};
    }
    json SourceRegistry::Maps() const
    {
        auto result=json::array(); std::set<std::pair<std::string,std::string>> maps;
        for(const auto& entry:m_maps) if(entry.second.complete) maps.emplace(entry.second.path,entry.second.name);
        for(const auto& entry:maps) result.push_back({{"name",entry.second},{"path",entry.first}});
        return result;
    }
    SourceRegistry& Sources() { static SourceRegistry registry; return registry; }
}
