#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../include/GhostSceneIO.h"
#include "../include/GhostImage.h"
#include "../include/json.hpp"
#include <filesystem>
#include <fstream>
#include <map>
#include <set>

using json = nlohmann::json;
namespace fs = std::filesystem;

// ---------------- paths ----------------
std::string GhostPath_Normalize(const std::string& p)
{
    if (p.empty()) return p;
    fs::path n = fs::path(GhostUtf8ToWide(p)).lexically_normal();
    n.make_preferred();
    return GhostWideToUtf8(n.wstring());
}

// Inside the project root -> relative with forward slashes; otherwise absolute
static std::string MakeStoredPath(const std::string& absUtf8, const std::string& rootUtf8)
{
    if (absUtf8.empty()) return "";
    fs::path p = fs::path(GhostUtf8ToWide(absUtf8)).lexically_normal();
    if (!rootUtf8.empty())
    {
        fs::path r = fs::path(GhostUtf8ToWide(rootUtf8)).lexically_normal();
        fs::path rel = p.lexically_relative(r);
        if (!rel.empty() && *rel.begin() != fs::path(L".."))
            return GhostWideToUtf8(rel.generic_wstring());
    }
    return GhostWideToUtf8(p.generic_wstring());
}

static std::string ResolveStoredPath(const std::string& stored, const std::string& rootUtf8)
{
    fs::path p(GhostUtf8ToWide(stored));
    if (p.is_relative() && !rootUtf8.empty()) p = fs::path(GhostUtf8ToWide(rootUtf8)) / p;
    return GhostPath_Normalize(GhostWideToUtf8(p.wstring()));
}

// ---------------- json helpers ----------------
static json F3(const float* a) { return json::array({ a[0], a[1], a[2] }); }
static json V3(const GhostVec3& v) { return json::array({ v.x, v.y, v.z }); }

static void ReadF(const json& j, const char* key, float* out, int n)
{
    auto it = j.find(key);
    if (it == j.end() || !it->is_array()) return;
    for (int i = 0; i < n && i < (int)it->size(); i++)
        if ((*it)[i].is_number()) out[i] = (*it)[i].get<float>();
}

static void ReadV3(const json& j, const char* key, GhostVec3& v)
{
    float a[3] = { v.x, v.y, v.z };
    ReadF(j, key, a, 3);
    v = { a[0], a[1], a[2] };
}

static int Clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// ---------------- materials ----------------
static json MatToJson(const GhostMaterial& m, const std::set<int>& savedTex)
{
    int tex = (m.textureId < GHOST_FIRST_USER_TEXTURE || savedTex.count(m.textureId)) ? m.textureId : 0;
    json j;
    j["texture"] = tex;
    j["mapMode"] = m.mapMode;
    j["wrap"] = m.wrap;
    j["filter"] = m.filter;
    j["tiling"] = json::array({ m.tiling[0], m.tiling[1] });
    j["offset"] = json::array({ m.offset[0], m.offset[1] });
    j["rotation"] = m.rotationDeg;
    j["tint"] = F3(m.tint);
    return j;
}

static GhostMaterial MatFromJson(const json& j, const std::function<int(int)>& mapTex)
{
    GhostMaterial m;
    m.textureId = mapTex(j.value("texture", 0));
    m.mapMode = Clampi(j.value("mapMode", 0), 0, 7);
    m.wrap = Clampi(j.value("wrap", 0), 0, 1);
    m.filter = Clampi(j.value("filter", 0), 0, 1);
    ReadF(j, "tiling", m.tiling, 2);
    ReadF(j, "offset", m.offset, 2);
    m.rotationDeg = j.value("rotation", 0.0f);
    ReadF(j, "tint", m.tint, 3);
    for (int i = 0; i < 2; i++) if (fabsf(m.tiling[i]) < 0.001f) m.tiling[i] = 1.0f;
    return m;
}

// ============================================================
//  Save
// ============================================================
bool GhostScene_Save(const GhostScene& s, const std::string& root, const std::string& file, std::string& err)
{
    try
    {
        json j;
        j["format"] = "ghostscene";
        j["version"] = GHOSTSCENE_VERSION;
        j["nextId"] = s.nextId;
        j["activeCamera"] = s.activeCameraId;

        json amb;
        amb["color"] = F3(s.ambientColor);
        amb["intensity"] = s.ambientIntensity;
        j["ambient"] = amb;

        std::set<int> saved;
        json texs = json::array();
        for (const GhostTextureInfo& t : s.textures)
            if (t.id >= GHOST_FIRST_USER_TEXTURE && !t.path.empty())
            {
                saved.insert(t.id);
                json jt;
                jt["id"] = t.id;
                jt["name"] = t.name;
                jt["path"] = MakeStoredPath(t.path, root);
                texs.push_back(jt);
            }
        j["textures"] = texs;

        json objs = json::array();
        for (const GhostObject& o : s.objects)
        {
            json jo;
            jo["id"] = o.id;
            jo["mesh"] = o.meshId;
            jo["name"] = o.name;
            jo["position"] = V3(o.position);
            jo["rotation"] = V3(o.rotationDeg);
            jo["scale"] = V3(o.scale);
            jo["vertexColor"] = o.useVertexColor;
            json faces = json::array();
            const int n = GhostMesh_FaceCount(o.meshId);
            for (int f = 0; f < n; f++) faces.push_back(MatToJson(o.faces[f], saved));
            jo["faces"] = faces;
            objs.push_back(jo);
        }
        j["objects"] = objs;

        json lights = json::array();
        for (const GhostLight& l : s.lights)
        {
            json jl;
            jl["id"] = l.id;
            jl["type"] = (int)l.type;
            jl["name"] = l.name;
            jl["position"] = V3(l.position);
            jl["yaw"] = l.yawDeg;
            jl["pitch"] = l.pitchDeg;
            jl["color"] = F3(l.color);
            jl["intensity"] = l.intensity;
            jl["range"] = l.range;
            jl["width"] = l.width;
            jl["height"] = l.height;
            lights.push_back(jl);
        }
        j["lights"] = lights;

        json cams = json::array();
        for (const GhostCamera& c : s.cameras)
        {
            json jc;
            jc["id"] = c.id;
            jc["name"] = c.name;
            jc["position"] = V3(c.position);
            jc["yaw"] = c.yawDeg;
            jc["pitch"] = c.pitchDeg;
            jc["fov"] = c.fovDeg;
            jc["resW"] = c.resW;
            jc["resH"] = c.resH;
            cams.push_back(jc);
        }
        j["cameras"] = cams;

        // Write to a temp file first, so a failed save never destroys the old one
        fs::path out(GhostUtf8ToWide(file));
        if (out.has_parent_path()) fs::create_directories(out.parent_path());
        fs::path tmp = out;
        tmp += L".tmp";
        {
            std::ofstream f(tmp, std::ios::binary);
            if (!f) { err = "Could not write to the destination folder."; return false; }
            f << j.dump(2);
            if (!f) { err = "Writing the scene file failed."; return false; }
        }
        fs::rename(tmp, out);
        return true;
    }
    catch (const std::exception& e)
    {
        err = std::string("Save failed: ") + e.what();
        return false;
    }
}

// ============================================================
//  Load
// ============================================================
bool GhostScene_Load(GhostScene& s, const std::string& root, const std::string& file,
    const GhostLoadTexFn& loadTex, size_t maxObjects, std::string& err, std::string& warn)
{
    json j;
    try
    {
        std::ifstream in(fs::path(GhostUtf8ToWide(file)), std::ios::binary);
        if (!in) { err = "Could not open the scene file."; return false; }
        j = json::parse(in);
    }
    catch (const std::exception& e)
    {
        err = std::string("The scene file is damaged or not valid JSON:\n") + e.what();
        return false;
    }

    try
    {
        if (!j.is_object() || j.value("format", std::string()) != "ghostscene")
        {
            err = "This is not a GhostX scene file."; return false;
        }
        if (j.value("version", 0) > GHOSTSCENE_VERSION)
        {
            err = "This scene was saved by a newer version of GhostX."; return false;
        }

        // ---- textures: old id -> engine id ----
        std::map<int, int> texMap;
        if (j.contains("textures") && j["textures"].is_array())
            for (const json& t : j["textures"])
            {
                int oldId = t.value("id", -1);
                std::string name = t.value("name", std::string("Texture"));
                std::string stored = t.value("path", std::string());
                int newId = 0;
                if (!stored.empty() && loadTex) newId = loadTex(ResolveStoredPath(stored, root), name);
                if (newId <= 0) { warn += "Missing texture: " + stored + "\n"; newId = 0; }
                texMap[oldId] = newId;
            }
        std::function<int(int)> mapTex = [&](int old) -> int
            {
                if (old < GHOST_FIRST_USER_TEXTURE) return old < 0 ? 0 : old;
                auto it = texMap.find(old);
                return it == texMap.end() ? 0 : it->second;
            };

        // ---- ids: keep the saved ones, hand out new ones for missing or duplicate ids ----
        int maxId = 0;
        for (const char* key : { "objects", "lights", "cameras" })
            if (j.contains(key) && j[key].is_array())
                for (const json& it : j[key]) maxId = (std::max)(maxId, it.value("id", 0));
        std::set<int> used;
        auto takeId = [&](int id) -> int
            {
                if (id <= 0 || used.count(id)) id = ++maxId;
                used.insert(id);
                return id;
            };

        // ---- reset and fill ----
        s.objects.clear(); s.lights.clear(); s.cameras.clear();
        s.selectedId = 0; s.selectedFace = -1; s.activeCameraId = 0;
        s.ambientColor[0] = s.ambientColor[1] = s.ambientColor[2] = 1.0f;
        s.ambientIntensity = 0.15f;

        if (j.contains("ambient") && j["ambient"].is_object())
        {
            ReadF(j["ambient"], "color", s.ambientColor, 3);
            s.ambientIntensity = j["ambient"].value("intensity", 0.15f);
        }

        if (j.contains("objects") && j["objects"].is_array())
            for (const json& jo : j["objects"])
            {
                int mesh = jo.value("mesh", -1);
                if (mesh < 0 || mesh >= GHOST_MESH_COUNT) { warn += "Skipped an object with an unknown mesh.\n"; continue; }
                if (s.objects.size() >= maxObjects) { warn += "Too many objects: the rest were skipped.\n"; break; }

                GhostObject o;
                o.id = takeId(jo.value("id", 0));
                o.meshId = mesh;
                o.name = jo.value("name", std::string("Object"));
                ReadV3(jo, "position", o.position);
                ReadV3(jo, "rotation", o.rotationDeg);
                ReadV3(jo, "scale", o.scale);
                o.useVertexColor = jo.value("vertexColor", false);
                if (jo.contains("faces") && jo["faces"].is_array())
                {
                    const int n = GhostMesh_FaceCount(mesh);
                    for (int f = 0; f < n && f < GHOST_MAX_FACES && f < (int)jo["faces"].size(); f++)
                        o.faces[f] = MatFromJson(jo["faces"][f], mapTex);
                }
                s.objects.push_back(o);
            }

        if (j.contains("lights") && j["lights"].is_array())
            for (const json& jl : j["lights"])
            {
                if (s.lights.size() >= GHOST_MAX_LIGHTS) { warn += "Too many lights: the rest were skipped.\n"; break; }
                GhostLight l;
                l.id = takeId(jl.value("id", 0));
                l.type = (GhostLightType)Clampi(jl.value("type", 1), 0, 2);
                l.name = jl.value("name", std::string("Light"));
                ReadV3(jl, "position", l.position);
                l.yawDeg = jl.value("yaw", 0.0f);
                l.pitchDeg = jl.value("pitch", 0.0f);
                ReadF(jl, "color", l.color, 3);
                l.intensity = jl.value("intensity", 1.0f);
                l.range = jl.value("range", 10.0f);
                l.width = jl.value("width", 1.0f);
                l.height = jl.value("height", 1.0f);
                s.lights.push_back(l);
            }

        s.nextId = (std::max)(j.value("nextId", 1), maxId + 1);

        if (j.contains("cameras") && j["cameras"].is_array())
            for (const json& jc : j["cameras"])
            {
                if (s.cameras.size() >= GHOST_MAX_CAMERAS) { warn += "Too many cameras: the rest were skipped.\n"; break; }
                GhostCamera c;
                c.id = takeId(jc.value("id", 0));
                c.name = jc.value("name", std::string("Camera"));
                ReadV3(jc, "position", c.position);
                c.yawDeg = jc.value("yaw", 0.0f);
                c.pitchDeg = jc.value("pitch", 0.0f);
                c.fovDeg = jc.value("fov", 45.0f);
                c.resW = jc.value("resW", 0);
                c.resH = jc.value("resH", 0);
                s.cameras.push_back(c);
            }
        s.nextId = (std::max)(s.nextId, maxId + 1);

        if (s.cameras.empty()) GhostScene_AddCamera(s);          // there is always at least one camera
        int active = j.value("activeCamera", 0);
        s.activeCameraId = s.cameras.front().id;
        for (const GhostCamera& c : s.cameras) if (c.id == active) s.activeCameraId = active;
        return true;
    }
    catch (const std::exception& e)
    {
        err = std::string("The scene file has invalid data:\n") + e.what();
        return false;
    }
}

// ============================================================
//  Hash (unsaved-changes detection)
// ============================================================
unsigned int GhostScene_Hash(const GhostScene& s)
{
    unsigned int h = 2166136261u;
    auto mix = [&](const void* p, size_t n)
        {
            const unsigned char* b = (const unsigned char*)p;
            for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 16777619u; }
        };
    auto I = [&](int v) { mix(&v, sizeof v); };
    auto F = [&](float v) { mix(&v, sizeof v); };
    auto V = [&](const GhostVec3& v) { F(v.x); F(v.y); F(v.z); };
    auto S = [&](const std::string& t) { mix(t.data(), t.size()); I((int)t.size()); };

    I(s.activeCameraId);
    F(s.ambientColor[0]); F(s.ambientColor[1]); F(s.ambientColor[2]); F(s.ambientIntensity);

    for (const GhostObject& o : s.objects)
    {
        I(o.id); I(o.meshId); S(o.name);
        V(o.position); V(o.rotationDeg); V(o.scale);
        I(o.useVertexColor ? 1 : 0);
        const int n = GhostMesh_FaceCount(o.meshId);
        for (int f = 0; f < n; f++) mix(&o.faces[f], sizeof(GhostMaterial));
    }
    for (const GhostLight& l : s.lights)
    {
        I(l.id); I((int)l.type); S(l.name);
        V(l.position); F(l.yawDeg); F(l.pitchDeg);
        F(l.color[0]); F(l.color[1]); F(l.color[2]);
        F(l.intensity); F(l.range); F(l.width); F(l.height);
    }
    for (const GhostCamera& c : s.cameras)
    {
        I(c.id); S(c.name); F(c.fovDeg); I(c.resW); I(c.resH);
        if (c.id != s.activeCameraId) { V(c.position); F(c.yawDeg); F(c.pitchDeg); }   // flying the main camera isn't an edit
    }
    return h;
}