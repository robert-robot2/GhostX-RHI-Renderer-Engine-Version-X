#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../include/GhostSceneIO.h"
#include "../include/GhostImage.h"
#include "../include/json.hpp"
#include "../include/GhostMods.h"
#include <filesystem>
#include <fstream>
#include <map>
#include <set>

// GhostSceneIO.cpp
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

// ---- per-role parameters (generated from GhostRoleParams.h) ----
#define RP_SAVE(n, d, lo, hi) j[#n] = p.n;
#define RP_LOAD(n, d, lo, hi) p.n = j.value(#n, p.n);
static json PlayerParamsToJson(const GhostPlayerParams& p) { json j = json::object(); GHOST_PLAYER_PARAMS(RP_SAVE, RP_SAVE) return j; }
static json EnemyParamsToJson(const GhostEnemyParams& p) { json j = json::object(); GHOST_ENEMY_PARAMS(RP_SAVE, RP_SAVE) return j; }
static void PlayerParamsFromJson(const json& j, GhostPlayerParams& p) { GHOST_PLAYER_PARAMS(RP_LOAD, RP_LOAD) GhostPlayerParams_Clamp(&p); }
static void EnemyParamsFromJson(const json& j, GhostEnemyParams& p) { GHOST_ENEMY_PARAMS(RP_LOAD, RP_LOAD) GhostEnemyParams_Clamp(&p); }
static json BossParamsToJson(const GhostBossParams& p) { json j = json::object(); GHOST_BOSS_PARAMS(RP_SAVE, RP_SAVE) return j; }
static json PathParamsToJson(const GhostPathParams& p) { json j = json::object(); GHOST_PATH_PARAMS(RP_SAVE, RP_SAVE) return j; }
static void BossParamsFromJson(const json& j, GhostBossParams& p) { GHOST_BOSS_PARAMS(RP_LOAD, RP_LOAD) GhostBossParams_Clamp(&p); }
static void PathParamsFromJson(const json& j, GhostPathParams& p) { GHOST_PATH_PARAMS(RP_LOAD, RP_LOAD) GhostPathParams_Clamp(&p); }

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
    j["metallic"] = m.metallic;
    j["roughness"] = m.roughness;
    j["specular"] = m.specular;
    j["emissive"] = F3(m.emissive);
    j["emissiveStrength"] = m.emissiveStrength;
    j["alphaMode"] = m.alphaMode;
    j["alphaSource"] = m.alphaSource;
    j["alphaCutoff"] = m.alphaCutoff;
    json mp = json::object();
    for (int k = 0; k < GHOST_MAP_COUNT; k++)
    {
        const GhostMapParams& p = m.maps[k];
        const int t = (p.textureId < GHOST_FIRST_USER_TEXTURE || savedTex.count(p.textureId)) ? p.textureId : 0;
        mp[GhostMapKey(k)] = json{ { "texture", t },{ "amount", p.amount },{ "channel", p.channel },{ "invert", p.invert } };
    }
    j["maps"] = mp;
    j["mapTiling"] = json::array({ m.mapTiling[0], m.mapTiling[1] });
    j["mapOffset"] = json::array({ m.mapOffset[0], m.mapOffset[1] });
    j["opacity"] = m.opacity;
    j["twoSided"] = m.twoSided;
    j["refractionIOR"] = m.refractionIOR;
    j["reflectionRot"] = m.reflectionRotDeg;
    j["anisotropy"] = m.anisotropy;
    j["sss"] = F3(m.sssColor);
    j["sssAmount"] = m.sssAmount;
    j["wpo"] = json{ { "sway", m.wpoSway },{ "speed", m.wpoSpeed },{ "mid", m.wpoMid } };
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
    auto c01 = [](float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); };
    m.metallic = c01(j.value("metallic", 0.0f));
    m.roughness = j.value("roughness", 0.6f);
    m.roughness = m.roughness < 0.02f ? 0.02f : (m.roughness > 1.0f ? 1.0f : m.roughness);
    m.specular = c01(j.value("specular", 0.5f));
    ReadF(j, "emissive", m.emissive, 3);
    for (int i = 0; i < 3; i++) m.emissive[i] = c01(m.emissive[i]);
    m.emissiveStrength = j.value("emissiveStrength", 1.0f);
    m.emissiveStrength = m.emissiveStrength < 0.0f ? 0.0f : (m.emissiveStrength > 20.0f ? 20.0f : m.emissiveStrength);
    for (int i = 0; i < 2; i++) if (fabsf(m.tiling[i]) < 0.001f) m.tiling[i] = 1.0f;
    m.alphaMode = Clampi(j.value("alphaMode", 0), 0, 2);
    m.alphaSource = Clampi(j.value("alphaSource", 0), 0, 2);
    m.alphaCutoff = c01(j.value("alphaCutoff", 0.5f));  
    if (j.contains("maps") && j["maps"].is_object())
        for (int k = 0; k < GHOST_MAP_COUNT; k++)
        {
            auto it = j["maps"].find(GhostMapKey(k));
            if (it == j["maps"].end() || !it->is_object()) continue;
            GhostMapParams& p = m.maps[k];
            p.textureId = mapTex(it->value("texture", 0));
            p.amount = it->value("amount", p.amount);
            p.channel = Clampi(it->value("channel", 0), 0, 4);
            p.invert = Clampi(it->value("invert", 0), 0, 1);
        }
    ReadF(j, "mapTiling", m.mapTiling, 2);
    ReadF(j, "mapOffset", m.mapOffset, 2);
    for (int i = 0; i < 2; i++) if (fabsf(m.mapTiling[i]) < 0.001f) m.mapTiling[i] = 1.0f;
    m.opacity = c01(j.value("opacity", 1.0f));
    m.twoSided = Clampi(j.value("twoSided", 1), 0, 1);
    m.refractionIOR = j.value("refractionIOR", 1.5f);
    m.reflectionRotDeg = j.value("reflectionRot", 0.0f);
    m.anisotropy = j.value("anisotropy", 0.0f);
    ReadF(j, "sss", m.sssColor, 3);
    m.sssAmount = c01(j.value("sssAmount", 0.0f));
    if (j.contains("wpo") && j["wpo"].is_object())
    {
        m.wpoSway = j["wpo"].value("sway", 0.0f);
        m.wpoSpeed = j["wpo"].value("speed", 1.0f);
        m.wpoMid = j["wpo"].value("mid", 0.5f);
    }
    return m;
}

static json FlickerToJson(const GhostFlickerLayer& f, const std::set<int>& savedTex)
{
    const int tex = (f.textureId < GHOST_FIRST_USER_TEXTURE || savedTex.count(f.textureId)) ? f.textureId : 0;
    return json{ { "enabled", f.enabled },{ "pattern", f.pattern },{ "custom", f.custom },{ "speed", f.speed },
                 { "minLevel", f.minLevel },{ "smooth", f.smoothSteps },{ "baseInfluence", f.baseInfluence },
                 { "brightness", f.brightness },{ "color", F3(f.color) },{ "texture", tex },
                 { "texSpeed", f.texSpeed },{ "texTiling", f.texTiling },{ "texAmount", f.texAmount },
                 { "diffuse", f.diffuse },{ "specular", f.specular },{ "roughness", f.roughness },
                 { "metallic", f.metallic },{ "seed", f.seed } };
}

static void FlickerFromJson(const json& j, GhostFlickerLayer& f, const std::function<int(int)>& mapTex)
{
    f.enabled = j.value("enabled", f.enabled);
    f.pattern = j.value("pattern", f.pattern);
    f.custom = j.value("custom", f.custom);
    f.speed = j.value("speed", f.speed);
    f.minLevel = j.value("minLevel", f.minLevel);
    f.smoothSteps = j.value("smooth", f.smoothSteps);
    f.baseInfluence = j.value("baseInfluence", f.baseInfluence);
    f.brightness = j.value("brightness", f.brightness);
    ReadF(j, "color", f.color, 3);
    f.textureId = mapTex(j.value("texture", 0));
    f.texSpeed = j.value("texSpeed", f.texSpeed);
    f.texTiling = j.value("texTiling", f.texTiling);
    f.texAmount = j.value("texAmount", f.texAmount);
    f.diffuse = j.value("diffuse", f.diffuse);
    f.specular = j.value("specular", f.specular);
    f.roughness = j.value("roughness", f.roughness);
    f.metallic = j.value("metallic", f.metallic);
    f.seed = j.value("seed", f.seed);
    GhostFlicker_Clamp(f);
}


// ---------------- Tile Map data: run-length + base64 ----------------
static std::string B64Enc(const unsigned char* d, size_t n)
{
    static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o;
    o.reserve((n + 2) / 3 * 4);
    for (size_t i = 0; i < n; i += 3)
    {
        unsigned v = (unsigned)d[i] << 16;
        if (i + 1 < n) v |= (unsigned)d[i + 1] << 8;
        if (i + 2 < n) v |= (unsigned)d[i + 2];
        o.push_back(T[(v >> 18) & 63]);
        o.push_back(T[(v >> 12) & 63]);
        o.push_back(i + 1 < n ? T[(v >> 6) & 63] : '=');
        o.push_back(i + 2 < n ? T[v & 63] : '=');
    }
    return o;
}

static bool B64Dec(const std::string& s, std::vector<unsigned char>& out)
{
    out.clear();
    unsigned acc = 0;
    int bits = 0;
    for (char c : s)
    {
        int v;
        if (c >= 'A' && c <= 'Z') v = c - 'A';
        else if (c >= 'a' && c <= 'z') v = 26 + (c - 'a');
        else if (c >= '0' && c <= '9') v = 52 + (c - '0');
        else if (c == '+') v = 62;
        else if (c == '/') v = 63;
        else if (c == '=') break;
        else return false;
        acc = (acc << 6) | (unsigned)v;
        bits += 6;
        if (bits >= 8) { bits -= 8; out.push_back((unsigned char)((acc >> bits) & 0xFF)); }
    }
    return true;
}

static void RleEnc(const unsigned char* d, size_t n, std::vector<unsigned char>& out)    // pairs: (count 1..255, value)
{
    out.clear();
    for (size_t i = 0; i < n; )
    {
        size_t j = i + 1;
        while (j < n && d[j] == d[i] && j - i < 255) j++;
        out.push_back((unsigned char)(j - i));
        out.push_back(d[i]);
        i = j;
    }
}

static bool RleDec(const std::vector<unsigned char>& in, size_t expect, std::vector<unsigned char>& out)
{
    out.clear();
    out.reserve(expect);
    for (size_t i = 0; i + 1 < in.size(); i += 2)
    {
        if (in[i] == 0 || out.size() + in[i] > expect) return false;
        out.insert(out.end(), in[i], in[i + 1]);
    }
    return out.size() == expect;
}

// Texture ids the scene really uses, so a save never drags along registry leftovers
static void CollectUsedTextures(const GhostScene& s, std::set<int>& used)
{
    auto add = [&](int id) { if (id >= GHOST_FIRST_USER_TEXTURE) used.insert(id); };
    for (const GhostObject& o : s.objects)
    {
        const int n = GhostObject_FaceCount(o);
        for (int f = 0; f < n; f++)
        {
            add(o.faces[f].textureId);
            for (int k = 0; k < GHOST_MAP_COUNT; k++) add(o.faces[f].maps[k].textureId);
        }
        if (o.meshId == GHOST_MESH_QUAD) add(o.sprite.textureId);
        if (!o.missingFaces.empty())
        {
            json mf = json::parse(o.missingFaces, nullptr, false);
            if (mf.is_array()) for (const json& fj : mf) if (fj.is_object()) add(fj.value("texture", 0));
        }
    }
    for (const GhostLight& l : s.lights)
        for (int k = 0; k < GHOST_FLICKER_LAYERS; k++) add(l.flicker[k].textureId);
    for (const GhostVolume& v : s.volumes) for (int t : v.tex) add(t);
    for (const GhostParticleDef& d : s.particles) add(d.p.textureId);
    for (const GhostModEntity& m : s.mods)
    {
        const GhostModType* t = GhostMods_Type(m.typeId);
        if (!t) continue;
        for (int p = 0; p < t->paramCount; p++)
        {
            if (t->params[p].kind != GHOST_PARAM_TEXTURE) continue;
            const int off = GhostMods_ParamOffset(m.typeId, p);
            if (off >= 0 && (size_t)off < m.values.size()) add((int)(m.values[(size_t)off] + 0.5f));
        }
    }
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
        j["gameStartCamera"] = s.gameStartCameraId;
        json amb;
        amb["color"] = F3(s.ambientColor);
        amb["intensity"] = s.ambientIntensity;
        j["ambient"] = amb;
        j["shadows"] = json{ { "enabled", s.shadowsEnabled },{ "strength", s.shadowStrength },{ "resolution", s.shadowRes },
            { "filter", s.shadowFilter },{ "softness", s.shadowSoftness },{ "bias", s.shadowBias },
            { "normalBias", s.shadowNormalBias },{ "distance", s.shadowDistance } };
        j["audio"] = json{ { "music", s.music },{ "ambience", s.ambience } };
        j["post"] = json{ { "toneMode", s.post.toneMode },{ "exposure", s.post.exposureEV },{ "whitePoint", s.post.whitePoint },
         { "bloom", json{ { "enabled", s.post.bloomEnabled },{ "threshold", s.post.bloomThreshold },{ "knee", s.post.bloomKnee },
                          { "intensity", s.post.bloomIntensity },{ "scatter", s.post.bloomScatter },{ "mips", s.post.bloomMips },
                          { "tint", F3(s.post.bloomTint) } } } };
        j["post"]["ssao"] = json{ { "enabled", s.post.ssaoEnabled },{ "radius", s.post.ssaoRadius },{ "intensity", s.post.ssaoIntensity },
    { "bias", s.post.ssaoBias },{ "strength", s.post.ssaoStrength },{ "samples", s.post.ssaoSamples },
    { "directFade", s.post.ssaoDirectFade },{ "halfRes", s.post.ssaoHalfRes },{ "fadeDist", s.post.ssaoFadeDist } };
        j["post"]["ssr"] = json{ { "enabled", s.post.ssrEnabled },{ "intensity", s.post.ssrIntensity },{ "maxDistance", s.post.ssrMaxDistance },
    { "thickness", s.post.ssrThickness },{ "steps", s.post.ssrSteps },{ "maxRoughness", s.post.ssrMaxRoughness },
    { "edgeFade", s.post.ssrEdgeFade } };
        j["post"]["ssgi"] = json{ { "enabled", s.post.ssgiEnabled },{ "intensity", s.post.ssgiIntensity },{ "radius", s.post.ssgiRadius },
    { "samples", s.post.ssgiSamples },{ "temporal", s.post.ssgiTemporal },{ "fadeDist", s.post.ssgiFadeDist } };

        std::set<int> saved, used;
        CollectUsedTextures(s, used);
        json texs = json::array();
        for (const GhostTextureInfo& t : s.textures)
            if (t.id >= GHOST_FIRST_USER_TEXTURE && !t.path.empty() && used.count(t.id))
            {
                saved.insert(t.id);
                json jt;
                jt["id"] = t.id;
                jt["name"] = t.name;
                jt["path"] = MakeStoredPath(t.path, root);
                texs.push_back(jt);
            }
        j["textures"] = texs;



        {
            const GhostFogSettings& f = s.fog;
            auto tex = [&](int t) { return (t < GHOST_FIRST_USER_TEXTURE || saved.count(t)) ? t : 0; };
            j["fog"] = json{ { "enabled", f.enabled },{ "density", f.density },{ "color", F3(f.color) },{ "brightness", f.brightness },
                { "followAmbient", f.followAmbient },{ "heightBase", f.heightBase },{ "heightFalloff", f.heightFalloff },
                { "start", f.startDistance },{ "max", f.maxDistance },{ "sky", f.skyFog },
                { "envMode", f.envMode },{ "envAmount", f.envAmount },{ "envTexture", tex(f.envTexture) },{ "envYaw", f.envYaw },
                { "noiseTexture", tex(f.noiseTexture) },{ "noiseAmount", f.noiseAmount },{ "noiseScale", f.noiseScale },
                { "scrollX", f.scrollX },{ "scrollZ", f.scrollZ },
                { "volLight", f.volLight },{ "volIntensity", f.volIntensity },{ "anisotropy", f.anisotropy },{ "steps", f.steps } };
        }

        json objs = json::array();
        for (const GhostObject& o : s.objects)
        {
            json jo;
            jo["id"] = o.id;
            jo["mesh"] = o.meshId;
            /*
            if (const GhostMeshInfo* mi = GhostMesh_Info(o.meshId))
                if (!mi->builtin) { jo["meshName"] = mi->name; jo["meshSource"] = mi->source; }
            */
            if (!o.missingMesh.empty() || !o.missingMeshFile.empty())     // placeholder: write the original reference back
            {
                jo["meshName"] = o.missingMesh;
                if (!o.missingMeshFile.empty()) jo["meshFile"] = o.missingMeshFile;
            }
            else if (const GhostMeshInfo* mi = GhostMesh_Info(o.meshId))
                if (!mi->builtin)
                {
                    jo["meshName"] = mi->name;
                    jo["meshSource"] = mi->source;
                    if (!mi->file.empty()) jo["meshFile"] = MakeStoredPath(mi->file, root);
                }

            jo["name"] = o.name;
            jo["position"] = V3(o.position);
            jo["rotation"] = V3(o.rotationDeg);
            jo["scale"] = V3(o.scale);
            jo["vertexColor"] = o.useVertexColor;
            jo["role"] = o.role;
            jo["customTag"] = o.customTag;
            jo["collider"] = o.collider;
            jo["colliderSize"] = V3(o.colliderSize);
            jo["colliderOffset"] = V3(o.colliderOffset);
            jo["colliderRotation"] = V3(o.colliderRotation);
            jo["visible"] = o.visible;
            jo["locked"] = o.locked;
            jo["link"] = o.link;
            jo["move"] = json{ { "offset", V3(o.pathOffset) },{ "speed", o.moveSpeed },{ "pause", o.movePause },{ "mode", o.pathMode },{ "enemyMode", o.enemyPathMode },{ "timeOffset", o.timeOffset } };
            jo["flying"] = o.flying;
            jo["enemyDeathEffect"] = o.enemyDeathEffect;
            jo["breakEffect"] = o.breakEffect;
            jo["contents"] = o.contents;
            jo["portalTrigger"] = o.portalTrigger;
            jo["item"] = json{ { "powerType", o.powerType },{ "moves", o.itemMoves },{ "coins", o.coinCount } };
            {
                const GhostPlayerParams pd = GhostPlayerParams_Make();
                const GhostEnemyParams  ed = GhostEnemyParams_Make();
                const GhostBossParams   bd = GhostBossParams_Make();
                const GhostPathParams   td = GhostPathParams_Make();
                if (memcmp(&o.playerP, &pd, sizeof pd) != 0) jo["playerParams"] = PlayerParamsToJson(o.playerP);
                if (memcmp(&o.enemyP, &ed, sizeof ed) != 0) jo["enemyParams"] = EnemyParamsToJson(o.enemyP);
                if (memcmp(&o.bossP, &bd, sizeof bd) != 0) jo["bossParams"] = BossParamsToJson(o.bossP);
                if (memcmp(&o.pathP, &td, sizeof td) != 0) jo["pathParams"] = PathParamsToJson(o.pathP);
            }
            jo["shadow"] = json{ { "mode", o.shadowMode },{ "light", o.shadowLightId },{ "receive", o.receiveShadows } };

            if (o.tileMap.enabled)
            {
                const GhostTileMap& t = o.tileMap;
                json jt = json{ { "tileSize", t.tileSize },{ "gridX", t.gridX },{ "gridY", t.gridY },{ "gridZ", t.gridZ },
                                { "direction", t.direction },{ "shadowLight", t.shadowLightId },{ "layers", t.layerCount },
                                { "subdiv", t.subdiv },{ "autoCollider", t.autoCollider } };
                std::vector<unsigned char> rle;
                bool flat = true;
                for (float h : t.height) if (h != 0.0f) { flat = false; break; }
                if (!flat)                                                // sculpted heights
                {
                    RleEnc((const unsigned char*)t.height.data(), t.height.size() * sizeof(float), rle);
                    jt["heights"] = B64Enc(rle.data(), rle.size());
                }
                const size_t texels = (size_t)t.splatW * (size_t)t.splatH;
                if (t.layerCount >= 2 && texels > 0 && t.splat.size() == texels * 8)      // painted weights, one plane per layer
                {
                    std::vector<unsigned char> planar(texels * (size_t)t.layerCount);
                    for (int l = 0; l < t.layerCount; l++)
                        for (size_t i = 0; i < texels; i++) planar[(size_t)l * texels + i] = t.splat[i * 8 + (size_t)l];
                    RleEnc(planar.data(), planar.size(), rle);
                    jt["splat"] = B64Enc(rle.data(), rle.size());
                    jt["splatW"] = t.splatW;
                    jt["splatH"] = t.splatH;
                }
                jo["tileMap"] = jt;
            }


            if (o.meshId == GHOST_MESH_QUAD)
            {
                const GhostSprite& sp = o.sprite;
                int tex = (sp.textureId < GHOST_FIRST_USER_TEXTURE || saved.count(sp.textureId)) ? sp.textureId : 0;
                json js;
                js["texture"] = tex;
                js["source"] = sp.source;
                js["columns"] = sp.cols;
                js["rows"] = sp.rows;
                js["frame"] = sp.frame;
                js["flipX"] = sp.flipX;
                js["billboard"] = sp.billboard;
                js["unlit"] = sp.unlit;
                js["alphaClip"] = sp.alphaClip;
                js["pixelArt"] = sp.pixelArt;
                js["playing"] = sp.playing;
                js["tint"] = F3(sp.tint);
                js["speed"] = sp.animSpeed;
                js["anim"] = sp.anim;
                js["clip"] = sp.clip;
                jo["sprite"] = js;
            }
            if (!o.animator.anim.empty())
                jo["animator"] = json{ { "anim", o.animator.anim },{ "clip", o.animator.clip },
                                       { "playing", o.animator.playing },{ "speed", o.animator.speed } };
            json faces = json::array();
            const int n = GhostObject_FaceCount(o);
            for (int f = 0; f < n; f++) faces.push_back(MatToJson(o.faces[f], saved));
            jo["faces"] = faces;
            if (!o.missingFaces.empty())                                   // a placeholder keeps the original slots
            {
                json mf = json::parse(o.missingFaces, nullptr, false);
                if (!mf.is_discarded()) jo["faces"] = mf;
            }
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
            jl["castShadows"] = l.castShadows;
            jl["innerCone"] = l.innerDeg;
            jl["outerCone"] = l.outerDeg;
            jl["shadowObjects"] = l.shadowObjects;
            jl["shadowLandscape"] = l.shadowLandscape;
            jl["spinYaw"] = l.spinYaw;
            jl["spinPitch"] = l.spinPitch;
            jl["volumetric"] = l.volumetric;
            json fl = json::array();
            for (int k = 0; k < GHOST_FLICKER_LAYERS; k++) fl.push_back(FlickerToJson(l.flicker[k], saved));
            jl["flicker"] = fl;
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
            jc["mode"] = c.mode;
            jc["targetId"] = c.targetId;
            jc["targetTag"] = c.targetTag;
            jc["distance"] = c.distance;
            jc["heightOffset"] = c.heightOffset;
            jc["minDist"] = c.minDist;
            jc["maxDist"] = c.maxDist;
            jc["minPitch"] = c.minPitchDeg;
            jc["maxPitch"] = c.maxPitchDeg;
            jc["smoothing"] = c.smoothing;
            jc["moveSpeed"] = c.moveSpeed;
            cams.push_back(jc);
        }
        j["cameras"] = cams;
        json mods = json::array();
        for (const GhostModEntity& m : s.mods)
        {
            const GhostModType* t = GhostMods_Type(m.typeId);
            if (!t) continue;
            json jm;
            jm["id"] = m.id;
            jm["type"] = t->saveName;
            jm["name"] = m.name;
            jm["position"] = V3(m.position);
            json pr = json::object();
            for (int p = 0; p < t->paramCount; p++)
            {
                const GhostParamDesc& d = t->params[p];
                const int off = GhostMods_ParamOffset(m.typeId, p), n = GhostParam_Count(d);
                if (off < 0 || (size_t)(off + n) > m.values.size()) continue;
                if (d.kind == GHOST_PARAM_COLOR)
                    pr[d.name] = json::array({ m.values[(size_t)off], m.values[(size_t)off + 1], m.values[(size_t)off + 2] });
                else if (d.kind == GHOST_PARAM_TEXTURE)
                {
                    int tex = (int)(m.values[(size_t)off] + 0.5f);
                    if (tex >= GHOST_FIRST_USER_TEXTURE && !saved.count(tex)) tex = 0;
                    pr[d.name] = tex;
                }
                else pr[d.name] = m.values[(size_t)off];
            }
            jm["params"] = pr;
            mods.push_back(jm);
        }
        j["mods"] = mods;

        json vols = json::array();
        for (const GhostVolume& v : s.volumes)
        {
            json jv;
            jv["id"] = v.id; jv["type"] = v.type; jv["name"] = v.name;
            jv["position"] = V3(v.position); jv["rotation"] = V3(v.rotationDeg); jv["size"] = V3(v.size);
            jv["shape"] = v.shape; jv["radius"] = v.radius; jv["blend"] = v.blend;
            jv["priority"] = v.priority; jv["enabled"] = v.enabled;
            json pj = json::array(); for (float x : v.p) pj.push_back(x);
            jv["params"] = pj;
            json tj = json::array();
            for (int t : v.tex) tj.push_back((t < GHOST_FIRST_USER_TEXTURE || saved.count(t)) ? t : 0);
            jv["textures"] = tj;
            vols.push_back(jv);
        }
        j["volumes"] = vols;

        json parts = json::array();
        for (const GhostParticleDef& d : s.particles)
        {
            const GhostParticleParams& p = d.p;
            json jp;
            jp["id"] = p.id;
            jp["type"] = p.type;
            jp["name"] = d.name;
#define PP_SAVE(n) jp[#n] = p.n;
            GHOST_PARTICLE_FIELDS(PP_SAVE)
#undef PP_SAVE
                jp["texture"] = (p.textureId < GHOST_FIRST_USER_TEXTURE || saved.count(p.textureId)) ? p.textureId : 0;
            jp["mesh"] = -1;
            if (const GhostMeshInfo* mi = GhostMesh_Info(p.meshId))
            {
                if (mi->builtin) jp["mesh"] = p.meshId;
                else
                {
                    jp["meshName"] = mi->name;
                    if (!mi->file.empty()) jp["meshFile"] = MakeStoredPath(mi->file, root);
                }
            }
            jp["anim"] = d.anim;
            jp["clip"] = d.clip;
            parts.push_back(jp);
        }
        j["particles"] = parts;


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
    const GhostLoadTexFn& loadTex, size_t maxObjects, std::string& err, std::string& warn,
    const GhostLoadMeshFn& loadMesh)
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

      
            // ---- textures: old id -> engine id. LAZY: a texture is only loaded when something in the scene uses it ----
        struct TexRef { std::string stored, name; int newId = 0; bool tried = false; };
        std::map<int, TexRef> texRefs;
        if (j.contains("textures") && j["textures"].is_array())
            for (const json& t : j["textures"])
            {
                TexRef r;
                r.name = t.value("name", std::string("Texture"));
                r.stored = t.value("path", std::string());
                texRefs[t.value("id", -1)] = r;
            }
        std::function<int(int)> mapTex = [&](int old) -> int
            {
                if (old < GHOST_FIRST_USER_TEXTURE) return old < 0 ? 0 : old;
                auto it = texRefs.find(old);
                if (it == texRefs.end()) return 0;
                TexRef& r = it->second;
                if (!r.tried)
                {
                    r.tried = true;
                    int id = 0;
                    if (!r.stored.empty() && loadTex) id = loadTex(ResolveStoredPath(r.stored, root), r.name);
                    if (id <= 0) { warn += "Missing texture: " + r.stored + "\n"; id = 0; }
                    r.newId = id;
                }
                return r.newId;
            };

        // ---- ids: keep the saved ones, hand out new ones for missing or duplicate ids ----
        int maxId = 0;
        for (const char* key : { "objects", "lights", "cameras", "mods", "volumes", "particles" })
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
        s.objects.clear(); s.lights.clear(); s.cameras.clear(); s.mods.clear(); s.volumes.clear(); s.particles.clear();
        s.selectedId = 0; s.selectedFace = -1; s.activeCameraId = 0;
        s.ambientColor[0] = s.ambientColor[1] = s.ambientColor[2] = 1.0f;
        s.ambientIntensity = 0.15f;

        if (j.contains("ambient") && j["ambient"].is_object())
        {
            ReadF(j["ambient"], "color", s.ambientColor, 3);
            s.ambientIntensity = j["ambient"].value("intensity", 0.15f);
        }
        s.shadowsEnabled = true;
        s.shadowStrength = 1.0f;
        s.shadowRes = 1024; s.shadowFilter = 2; s.shadowSoftness = 1.0f; s.shadowBias = 1.0f; s.shadowNormalBias = 1.0f; s.shadowDistance = 30.0f;
        if (j.contains("shadows") && j["shadows"].is_object())
        {
            s.shadowsEnabled = j["shadows"].value("enabled", true);
            float st = j["shadows"].value("strength", 1.0f);
            s.shadowStrength = st < 0.0f ? 0.0f : (st > 1.0f ? 1.0f : st);
            s.shadowRes = GhostShadowRes_Clamp(j["shadows"].value("resolution", 1024));
            s.shadowFilter = Clampi(j["shadows"].value("filter", 2), 0, 3);
            auto cf = [](float v, float lo, float hi) { return v != v ? lo : (v < lo ? lo : (v > hi ? hi : v)); };
            s.shadowSoftness = cf(j["shadows"].value("softness", 1.0f), 0.25f, 4.0f);
            s.shadowBias = cf(j["shadows"].value("bias", 1.0f), 0.0f, 8.0f);
            s.shadowNormalBias = cf(j["shadows"].value("normalBias", 1.0f), 0.0f, 8.0f);
            s.shadowDistance = cf(j["shadows"].value("distance", 30.0f), 0.0f, 1000.0f);
        }

        s.music.clear(); s.ambience.clear();
        if (j.contains("audio") && j["audio"].is_object())
        {
            s.music = j["audio"].value("music", std::string());
            s.ambience = j["audio"].value("ambience", std::string());
            if (s.music.size() > 47) s.music.resize(47);
            if (s.ambience.size() > 47) s.ambience.resize(47);
        }

        s.post = GhostPostSettings();
        if (j.contains("post") && j["post"].is_object())
        {
            const json& jp = j["post"];
            s.post.toneMode = jp.value("toneMode", 0);
            s.post.exposureEV = jp.value("exposure", 0.0f);
            s.post.whitePoint = jp.value("whitePoint", 4.0f);
            if (jp.contains("bloom") && jp["bloom"].is_object())
            {
                const json& jb = jp["bloom"];
                s.post.bloomEnabled = jb.value("enabled", 0);
                s.post.bloomThreshold = jb.value("threshold", 1.0f);
                s.post.bloomKnee = jb.value("knee", 0.5f);
                s.post.bloomIntensity = jb.value("intensity", 0.5f);
                s.post.bloomScatter = jb.value("scatter", 0.7f);
                s.post.bloomMips = jb.value("mips", 5);
                ReadF(jb, "tint", s.post.bloomTint, 3);
            }
            if (jp.contains("ssao") && jp["ssao"].is_object())
            {
                const json& ja = jp["ssao"];
                s.post.ssaoEnabled = ja.value("enabled", 0);
                s.post.ssaoRadius = ja.value("radius", 0.5f);
                s.post.ssaoIntensity = ja.value("intensity", 1.0f);
                s.post.ssaoBias = ja.value("bias", 0.03f);
                s.post.ssaoStrength = ja.value("strength", 1.0f);
                s.post.ssaoSamples = ja.value("samples", 16);
                s.post.ssaoDirectFade = ja.value("directFade", 0.5f);
                s.post.ssaoHalfRes = ja.value("halfRes", 0);
                s.post.ssaoFadeDist = ja.value("fadeDist", 50.0f);
            }
            if (jp.contains("ssr") && jp["ssr"].is_object())
            {
                const json& jr = jp["ssr"];
                s.post.ssrEnabled = jr.value("enabled", 0);
                s.post.ssrIntensity = jr.value("intensity", 1.0f);
                s.post.ssrMaxDistance = jr.value("maxDistance", 30.0f);
                s.post.ssrThickness = jr.value("thickness", 0.4f);
                s.post.ssrSteps = jr.value("steps", 32);
                s.post.ssrMaxRoughness = jr.value("maxRoughness", 0.6f);
                s.post.ssrEdgeFade = jr.value("edgeFade", 0.1f);
            }
            if (jp.contains("ssgi") && jp["ssgi"].is_object())
            {
                const json& jg = jp["ssgi"];
                s.post.ssgiEnabled = jg.value("enabled", 0);
                s.post.ssgiIntensity = jg.value("intensity", 1.0f);
                s.post.ssgiRadius = jg.value("radius", 2.5f);
                s.post.ssgiSamples = jg.value("samples", 12);
                s.post.ssgiTemporal = jg.value("temporal", 0.85f);
                s.post.ssgiFadeDist = jg.value("fadeDist", 60.0f);
            }
            GhostPostSettings_Clamp(s.post);
        }

        s.fog = GhostFogSettings();
        if (j.contains("fog") && j["fog"].is_object())
        {
            const json& jf = j["fog"];
            GhostFogSettings& f = s.fog;
            f.enabled = jf.value("enabled", 0);
            f.density = jf.value("density", 0.02f);
            ReadF(jf, "color", f.color, 3);
            f.brightness = jf.value("brightness", 1.0f);
            f.followAmbient = jf.value("followAmbient", 0.0f);
            f.heightBase = jf.value("heightBase", 0.0f);
            f.heightFalloff = jf.value("heightFalloff", 0.0f);
            f.startDistance = jf.value("start", 0.0f);
            f.maxDistance = jf.value("max", 80.0f);
            f.skyFog = jf.value("sky", 0.5f);
            f.envMode = jf.value("envMode", 0);
            f.envAmount = jf.value("envAmount", 1.0f);
            f.envTexture = mapTex(jf.value("envTexture", 0));
            f.envYaw = jf.value("envYaw", 0.0f);
            f.noiseTexture = mapTex(jf.value("noiseTexture", 0));
            f.noiseAmount = jf.value("noiseAmount", 0.7f);
            f.noiseScale = jf.value("noiseScale", 0.15f);
            f.scrollX = jf.value("scrollX", 0.3f);
            f.scrollZ = jf.value("scrollZ", 0.1f);
            f.volLight = jf.value("volLight", 0);
            f.volIntensity = jf.value("volIntensity", 1.0f);
            f.anisotropy = jf.value("anisotropy", 0.5f);
            f.steps = jf.value("steps", 32);
            GhostFogSettings_Clamp(f);
        }


        if (j.contains("objects") && j["objects"].is_array())
            for (const json& jo : j["objects"])
            {
                int mesh = jo.value("mesh", -1);
                const std::string meshName = jo.value("meshName", std::string());
                const std::string meshFile = jo.value("meshFile", std::string());
                std::string whyMissing;
                if (!meshFile.empty())                                     // 11.B scenes: the converted .ghostmesh
                {
                    mesh = loadMesh ? loadMesh(ResolveStoredPath(meshFile, root), whyMissing) : -1;
                    if (whyMissing.empty() && !GhostMesh_Valid(mesh)) whyMissing = "the file could not be loaded";
                }
                else if (!meshName.empty())                                // 11.A scenes: only if imported this session
                    mesh = GhostMesh_FindByName(meshName);

                bool placeholder = false;
                if (!GhostMesh_Valid(mesh))
                {
                    if (meshName.empty() && meshFile.empty()) { warn += "Skipped an object with an unknown mesh.\n"; continue; }
                    placeholder = true;
                    mesh = GHOST_MESH_CUBE;
                    warn += "\"" + jo.value("name", std::string("object")) + "\": the model \"" +
                        (meshName.empty() ? meshFile : meshName) + "\" is missing" +
                        (whyMissing.empty() ? std::string() : " (" + whyMissing + ")") +
                        ". A red placeholder cube stands in for it, and the scene keeps the reference.\n";
                }

                if (s.objects.size() >= maxObjects) { warn += "Too many objects: the rest were skipped.\n"; break; }

                GhostObject o;
                o.id = takeId(jo.value("id", 0));
                o.meshId = mesh;
                o.name = jo.value("name", std::string("Object"));
                ReadV3(jo, "position", o.position);
                ReadV3(jo, "rotation", o.rotationDeg);
                ReadV3(jo, "scale", o.scale);
                o.useVertexColor = jo.value("vertexColor", false);
                o.role = Clampi(jo.value("role", 0), 0, GHOST_ROLE_COUNT - 1);
                o.customTag = jo.value("customTag", std::string());
                if (o.customTag.size() > 31) o.customTag.resize(31);
                o.collider = jo.value("collider", false);
                ReadV3(jo, "colliderSize", o.colliderSize);
                ReadV3(jo, "colliderOffset", o.colliderOffset);
                ReadV3(jo, "colliderRotation", o.colliderRotation);
                o.visible = jo.value("visible", true);
                o.locked = jo.value("locked", false);
                o.link = jo.value("link", std::string());
                if (o.link.size() > 63) o.link.resize(63);
                if (jo.contains("move") && jo["move"].is_object())
                {
                    const json& mv = jo["move"];
                    ReadV3(mv, "offset", o.pathOffset);
                    o.moveSpeed = mv.value("speed", 2.0f);
                    o.movePause = mv.value("pause", 0.0f);
                    o.pathMode = Clampi(mv.value("mode", 0), 0, 3);
                    o.timeOffset = mv.value("timeOffset", 0.0f);
                    o.timeOffset = o.timeOffset < 0.0f ? 0.0f : (o.timeOffset > 3600.0f ? 3600.0f : o.timeOffset);
                    o.enemyPathMode = Clampi(mv.value("enemyMode", 0), 0, 3);
                    o.flying = Clampi(jo.value("flying", 0), 0, 2);
                    o.enemyDeathEffect = Clampi(jo.value("enemyDeathEffect", 0), 0, 3);
                }
                o.breakEffect = Clampi(jo.value("breakEffect", 0), 0, 2);
                o.contents = Clampi(jo.value("contents", 0), 0, 3);
                o.portalTrigger = Clampi(jo.value("portalTrigger", 1), 0, 2);
                if (jo.contains("item") && jo["item"].is_object())
                {
                    const json& it = jo["item"];
                    o.powerType = Clampi(it.value("powerType", 0), 0, 5);
                    o.itemMoves = Clampi(it.value("moves", 0), 0, 2);
                    o.coinCount = Clampi(it.value("coins", 1), 1, 999);
                }
                if (jo.contains("playerParams") && jo["playerParams"].is_object()) PlayerParamsFromJson(jo["playerParams"], o.playerP);
                if (jo.contains("enemyParams") && jo["enemyParams"].is_object())    EnemyParamsFromJson(jo["enemyParams"], o.enemyP);
                if (jo.contains("bossParams") && jo["bossParams"].is_object()) BossParamsFromJson(jo["bossParams"], o.bossP);
                if (jo.contains("pathParams") && jo["pathParams"].is_object()) PathParamsFromJson(jo["pathParams"], o.pathP);
                if (jo.contains("shadow") && jo["shadow"].is_object())
                {
                    const json& sh = jo["shadow"];
                    o.shadowMode = Clampi(sh.value("mode", 1), 0, 2);
                    o.shadowLightId = sh.value("light", 0);
                    o.receiveShadows = sh.value("receive", true);
                }
                if (jo.contains("tileMap") && jo["tileMap"].is_object())
                {
                    const json& jt = jo["tileMap"];
                    GhostTileMap& t = o.tileMap;
                    t.enabled = true;
                    t.tileSize = jt.value("tileSize", 1.0f);
                    t.gridX = jt.value("gridX", 10);
                    t.gridY = jt.value("gridY", 10);
                    t.gridZ = jt.value("gridZ", 1);
                    t.direction = jt.value("direction", 1);
                    t.shadowLightId = jt.value("shadowLight", 0);
                    t.layerCount = jt.value("layers", 0);
                    t.subdiv = jt.value("subdiv", 4);
                    t.autoCollider = jt.value("autoCollider", true);
                    GhostTileMap_Clamp(t);
                    GhostTileMap_EnsureTiles(t);

                    std::vector<unsigned char> raw, dec;
                    if (jt.contains("heights") && jt["heights"].is_string() &&
                        B64Dec(jt["heights"].get<std::string>(), raw) && RleDec(raw, t.height.size() * sizeof(float), dec))
                        memcpy(t.height.data(), dec.data(), dec.size());

                    const size_t texels = (size_t)t.splatW * (size_t)t.splatH;
                    if (jt.contains("splat") && jt["splat"].is_string() && t.layerCount >= 2 &&
                        jt.value("splatW", 0) == t.splatW && jt.value("splatH", 0) == t.splatH &&
                        B64Dec(jt["splat"].get<std::string>(), raw) && RleDec(raw, texels * (size_t)t.layerCount, dec))
                    {
                        for (size_t i = 0; i < texels; i++)
                            for (int l = 0; l < 8; l++) t.splat[i * 8 + (size_t)l] = (l < t.layerCount) ? dec[(size_t)l * texels + i] : 0;
                    }
                    else if (jt.contains("tiles") && jt["tiles"].is_string() && t.layerCount >= 2)   // a Phase 2 scene: one layer per tile
                    {
                        const std::string ts = jt["tiles"].get<std::string>();
                        if (ts.size() == (size_t)t.gridX * (size_t)t.gridY)
                            for (int r = 0; r < t.splatH; r++)
                                for (int c = 0; c < t.splatW; c++)
                                {
                                    const int tc = c * t.gridX / t.splatW, tr = r * t.gridY / t.splatH;
                                    int L = ts[(size_t)tr * t.gridX + tc] - '0';
                                    if (L < 0 || L >= t.layerCount) L = 0;
                                    unsigned char* b = &t.splat[((size_t)r * t.splatW + c) * 8];
                                    memset(b, 0, 8);
                                    b[L] = 255;
                                }
                    }
                    GhostTileMap_UpdateRange(t);
                    t.hStamp = GhostTile_NextStamp();
                    t.sStamp = GhostTile_NextStamp();
                }

                if (mesh == GHOST_MESH_QUAD && jo.contains("sprite") && jo["sprite"].is_object())
                {
                    const json& js = jo["sprite"];
                    GhostSprite& sp = o.sprite;
                    sp.textureId = mapTex(js.value("texture", 0));
                    sp.source = Clampi(js.value("source", 0), 0, 1);
                    sp.cols = Clampi(js.value("columns", 1), 1, 64);
                    sp.rows = Clampi(js.value("rows", 1), 1, 64);
                    sp.frame = Clampi(js.value("frame", 0), 0, 4095);
                    sp.flipX = js.value("flipX", false);
                    sp.billboard = js.value("billboard", true);
                    sp.unlit = js.value("unlit", false);
                    sp.alphaClip = js.value("alphaClip", true);
                    sp.pixelArt = js.value("pixelArt", true);
                    sp.playing = js.value("playing", true);
                    ReadF(js, "tint", sp.tint, 3);
                    sp.animSpeed = js.value("speed", 1.0f);
                    sp.anim = js.value("anim", std::string());
                    sp.clip = js.value("clip", std::string());
                }
                if (jo.contains("animator") && jo["animator"].is_object())
                {
                    const json& ja = jo["animator"];
                    o.animator.anim = ja.value("anim", std::string());
                    o.animator.clip = ja.value("clip", std::string());
                    o.animator.playing = ja.value("playing", true);
                    o.animator.speed = ja.value("speed", 1.0f);
                }
                if (jo.contains("faces") && jo["faces"].is_array())
                {
                    const int n = GhostObject_FaceCount(o);
                    for (int f = 0; f < n && f < GHOST_MAX_FACES && f < (int)jo["faces"].size(); f++)
                        o.faces[f] = MatFromJson(jo["faces"][f], mapTex);
                }
                if (placeholder)
                {
                    o.missingMesh = meshName;
                    o.missingMeshFile = meshFile;
                    if (jo.contains("faces") && jo["faces"].is_array())
                    {
                        json mf = jo["faces"];                             // texture ids are remapped to this session's
                        for (json& fj : mf) if (fj.is_object()) fj["texture"] = mapTex(fj.value("texture", 0));
                        o.missingFaces = mf.dump();
                    }
                    for (int f = 0; f < GHOST_MAX_FACES; f++)
                    {
                        o.faces[f].tint[0] = 0.9f; o.faces[f].tint[1] = 0.15f; o.faces[f].tint[2] = 0.35f;
                    }
                }

                s.objects.push_back(o);
            }

        if (j.contains("lights") && j["lights"].is_array())
            for (const json& jl : j["lights"])
            {
                if (s.lights.size() >= GHOST_MAX_LIGHTS) { warn += "Too many lights: the rest were skipped.\n"; break; }
                GhostLight l;
                l.id = takeId(jl.value("id", 0));
                l.type = (GhostLightType)Clampi(jl.value("type", 1), 0, 3);
                l.name = jl.value("name", std::string("Light"));
                ReadV3(jl, "position", l.position);
                l.yawDeg = jl.value("yaw", 0.0f);
                l.pitchDeg = jl.value("pitch", 0.0f);
                ReadF(jl, "color", l.color, 3);
                l.intensity = jl.value("intensity", 1.0f);
                l.range = jl.value("range", 10.0f);
                l.width = jl.value("width", 1.0f);
                l.height = jl.value("height", 1.0f);
                l.castShadows = jl.value("castShadows", true);
                l.outerDeg = jl.value("outerCone", 35.0f);
                l.outerDeg = l.outerDeg < 1.0f ? 1.0f : (l.outerDeg > 89.0f ? 89.0f : l.outerDeg);
                l.innerDeg = jl.value("innerCone", 20.0f);
                l.innerDeg = l.innerDeg < 0.0f ? 0.0f : (l.innerDeg > l.outerDeg ? l.outerDeg : l.innerDeg);
                l.shadowObjects = jl.value("shadowObjects", true);
                l.shadowLandscape = jl.value("shadowLandscape", true);
                l.spinYaw = jl.value("spinYaw", 0.0f);
                l.spinPitch = jl.value("spinPitch", 0.0f);
                l.volumetric = jl.value("volumetric", 1.0f);
                l.volumetric = l.volumetric != l.volumetric ? 1.0f : (l.volumetric < 0.0f ? 0.0f : (l.volumetric > 5.0f ? 5.0f : l.volumetric));
                if (jl.contains("flicker") && jl["flicker"].is_array())
                    for (int k = 0; k < GHOST_FLICKER_LAYERS && k < (int)jl["flicker"].size(); k++)
                        if (jl["flicker"][k].is_object()) FlickerFromJson(jl["flicker"][k], l.flicker[k], mapTex);
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
                c.mode = Clampi(jc.value("mode", 0), 0, 1);
                c.targetId = jc.value("targetId", 0);
                c.targetTag = jc.value("targetTag", std::string());
                if (c.targetTag.size() > 31) c.targetTag.resize(31);
                c.distance = jc.value("distance", 7.0f);
                c.heightOffset = jc.value("heightOffset", 0.6f);
                c.minDist = jc.value("minDist", 2.0f);
                c.maxDist = jc.value("maxDist", 20.0f);
                c.minPitchDeg = jc.value("minPitch", -70.0f);
                c.maxPitchDeg = jc.value("maxPitch", 40.0f);
                c.smoothing = jc.value("smoothing", 10.0f);
                c.moveSpeed = jc.value("moveSpeed", 3.5f);
                c.moveSpeed = c.moveSpeed < 0.1f ? 0.1f : (c.moveSpeed > 200.0f ? 200.0f : c.moveSpeed);
                s.cameras.push_back(c);
            }

        if (j.contains("mods") && j["mods"].is_array())
            for (const json& jm : j["mods"])
            {
                const int typeId = GhostMods_FindByName(jm.value("type", std::string()).c_str());
                const GhostModType* ty = GhostMods_Type(typeId);
                if (!ty) { warn += "Skipped an unknown mod type.\n"; continue; }

                int have = 0;
                for (const GhostModEntity& x : s.mods) if (x.typeId == typeId) have++;
                if (s.mods.size() >= GHOST_MAX_MODS || (ty->maxPerScene > 0 && have >= ty->maxPerScene))
                {
                    warn += std::string("Too many ") + ty->name + " entities: the rest were skipped.\n"; continue;
                }

                GhostModEntity m;
                m.id = takeId(jm.value("id", 0));
                m.typeId = typeId;
                m.name = jm.value("name", std::string(ty->name));
                ReadV3(jm, "position", m.position);
                GhostMods_Defaults(typeId, m.values);

                auto pj = jm.find("params");
                if (pj != jm.end() && pj->is_object())
                    for (int p = 0; p < ty->paramCount; p++)
                    {
                        const GhostParamDesc& d = ty->params[p];
                        auto it = pj->find(d.name);
                        if (it == pj->end()) continue;
                        const size_t off = (size_t)GhostMods_ParamOffset(typeId, p);

                        if (d.kind == GHOST_PARAM_COLOR)
                        {
                            if (it->is_array())
                                for (size_t k = 0; k < 3 && k < it->size(); k++)
                                    if ((*it)[k].is_number()) m.values[off + k] = (*it)[k].get<float>();
                        }
                        else if (it->is_boolean()) m.values[off] = it->get<bool>() ? 1.0f : 0.0f;
                        else if (it->is_number())
                        {
                            float x = it->get<float>();
                            if (d.kind == GHOST_PARAM_TEXTURE) x = (float)mapTex((int)(x + 0.5f));
                            m.values[off] = x;
                        }
                        GhostMods_Clamp(typeId, p, &m.values[off]);
                    }
                s.mods.push_back(m);
            }
        s.nextId = (std::max)(s.nextId, maxId + 1);

        if (j.contains("volumes") && j["volumes"].is_array())
            for (const json& jv : j["volumes"])
            {
                if (s.volumes.size() >= GHOST_MAX_VOLUMES) { warn += "Too many volumes: the rest were skipped.\n"; break; }
                GhostVolume v;
                v.id = takeId(jv.value("id", 0));
                v.type = Clampi(jv.value("type", 0), 0, GHOST_VOL_COUNT - 1);
                v.name = jv.value("name", std::string("Volume"));
                ReadV3(jv, "position", v.position);
                ReadV3(jv, "rotation", v.rotationDeg);
                ReadV3(jv, "size", v.size);
                v.shape = Clampi(jv.value("shape", 0), 0, 1);
                v.radius = jv.value("radius", 5.0f);
                v.blend = jv.value("blend", 1.0f);
                v.priority = jv.value("priority", 0);
                v.enabled = jv.value("enabled", true);
                ReadF(jv, "params", v.p, GHOST_VOL_PARAMS);
                if (jv.contains("textures") && jv["textures"].is_array())
                    for (int k = 0; k < GHOST_VOL_TEXTURES && k < (int)jv["textures"].size(); k++)
                        if (jv["textures"][k].is_number_integer()) v.tex[k] = mapTex(jv["textures"][k].get<int>());
                GhostVolume_Clamp(v);
                s.volumes.push_back(v);
            }

        if (j.contains("particles") && j["particles"].is_array())
            for (const json& jp : j["particles"])
            {
                if ((int)s.particles.size() >= GHOST_PFX_MAX_EMITTERS) { warn += "Too many particle systems: the rest were skipped.\n"; break; }
                GhostParticleDef d;
                GhostParticles_FillPreset(Clampi(jp.value("type", 0), 0, GHOST_PFX_TYPE_COUNT - 1), d.p);   // fields missing in the file keep the preset's value
                GhostParticleParams& p = d.p;
#define PP_LOAD(n) p.n = jp.value(#n, p.n);
                GHOST_PARTICLE_FIELDS(PP_LOAD)
#undef PP_LOAD
                    p.id = takeId(jp.value("id", 0));
                p.textureId = mapTex(jp.value("texture", 0));

                int mesh = jp.value("mesh", -1);
                const std::string meshName = jp.value("meshName", std::string());
                const std::string meshFile = jp.value("meshFile", std::string());
                if (!meshFile.empty() && loadMesh) { std::string why; mesh = loadMesh(ResolveStoredPath(meshFile, root), why); }
                else if (!meshName.empty()) mesh = GhostMesh_FindByName(meshName);
                p.meshId = (GhostMesh_Valid(mesh) && mesh != GHOST_MESH_QUAD) ? mesh : -1;

                d.name = jp.value("name", std::string());
                if (d.name.empty()) d.name = GhostParticles_DefaultName(p.type, p.id);
                if (d.name.size() > 63) d.name.resize(63);
                d.anim = jp.value("anim", std::string());
                d.clip = jp.value("clip", std::string());
                if (d.anim.size() > 259) d.anim.resize(259);
                if (d.clip.size() > 47) d.clip.resize(47);
                GhostParticles_Clamp(p);
                s.particles.push_back(d);
            }


        if (s.cameras.empty()) GhostScene_AddCamera(s);          // there is always at least one camera
        int active = j.value("activeCamera", 0);
        s.activeCameraId = s.cameras.front().id;
        for (const GhostCamera& c : s.cameras) if (c.id == active) s.activeCameraId = active;
        s.gameStartCameraId = 0;
        int gs = j.value("gameStartCamera", 0);
        for (const GhostCamera& c : s.cameras) if (c.id == gs) s.gameStartCameraId = gs;
        for (GhostObject& o : s.objects)         // a designated light that no longer exists -> All Lights
            if (o.shadowMode == 2)
            {
                bool ok = false;
                for (const GhostLight& l : s.lights) if (l.id == o.shadowLightId) ok = true;
                if (!ok) { o.shadowMode = 1; o.shadowLightId = 0; }
            }
        for (GhostObject& o : s.objects)         // a Tile Map whose assigned light is gone -> All Lights
            if (o.tileMap.enabled && o.tileMap.shadowLightId != 0)
            {
                bool ok = false;
                for (const GhostLight& l : s.lights) if (l.id == o.tileMap.shadowLightId) ok = true;
                if (!ok) o.tileMap.shadowLightId = 0;
            }
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
    I(s.gameStartCameraId);
    I(s.shadowsEnabled ? 1 : 0); F(s.shadowStrength); I(s.shadowRes); I(s.shadowFilter); F(s.shadowSoftness); F(s.shadowBias); F(s.shadowNormalBias); F(s.shadowDistance);
    S(s.music); S(s.ambience);
    F(s.ambientColor[0]); F(s.ambientColor[1]); F(s.ambientColor[2]); F(s.ambientIntensity);
    I(s.post.toneMode); F(s.post.exposureEV); F(s.post.whitePoint);
    I(s.post.bloomEnabled); F(s.post.bloomThreshold); F(s.post.bloomKnee); F(s.post.bloomIntensity);
    F(s.post.bloomScatter); I(s.post.bloomMips); F(s.post.bloomTint[0]); F(s.post.bloomTint[1]); F(s.post.bloomTint[2]);
    I(s.post.ssaoEnabled); F(s.post.ssaoRadius); F(s.post.ssaoIntensity); F(s.post.ssaoBias); F(s.post.ssaoStrength);
    I(s.post.ssaoSamples); F(s.post.ssaoDirectFade); I(s.post.ssaoHalfRes); F(s.post.ssaoFadeDist);
    I(s.post.ssrEnabled); F(s.post.ssrIntensity); F(s.post.ssrMaxDistance); F(s.post.ssrThickness);
    I(s.post.ssrSteps); F(s.post.ssrMaxRoughness); F(s.post.ssrEdgeFade);
    I(s.post.ssgiEnabled); F(s.post.ssgiIntensity); F(s.post.ssgiRadius); I(s.post.ssgiSamples);
    F(s.post.ssgiTemporal); F(s.post.ssgiFadeDist);

    {
        const GhostFogSettings& f = s.fog;
        I(f.enabled); F(f.density); F(f.color[0]); F(f.color[1]); F(f.color[2]); F(f.brightness); F(f.followAmbient);
        F(f.heightBase); F(f.heightFalloff); F(f.startDistance); F(f.maxDistance); F(f.skyFog);
        I(f.envMode); F(f.envAmount); I(f.envTexture); F(f.envYaw);
        I(f.noiseTexture); F(f.noiseAmount); F(f.noiseScale); F(f.scrollX); F(f.scrollZ);
        I(f.volLight); F(f.volIntensity); F(f.anisotropy); I(f.steps);
    }
    for (const GhostObject& o : s.objects)
    {
        I(o.id); I(o.meshId); S(o.name);
        V(o.position); V(o.rotationDeg); V(o.scale);
        I(o.useVertexColor ? 1 : 0);
        I(o.role); S(o.customTag); I(o.collider ? 1 : 0); V(o.colliderSize); V(o.colliderOffset); V(o.colliderRotation); I(o.visible ? 1 : 0);
        I(o.shadowMode); I(o.shadowLightId); I(o.receiveShadows ? 1 : 0);
        I(o.tileMap.enabled ? 1 : 0); F(o.tileMap.tileSize); I(o.tileMap.gridX); I(o.tileMap.gridY);
        I(o.tileMap.gridZ); I(o.tileMap.direction); I(o.tileMap.shadowLightId);

        I(o.tileMap.layerCount); I(o.tileMap.subdiv); I((int)o.tileMap.hStamp); I((int)o.tileMap.sStamp);

        I(o.locked ? 1 : 0); S(o.link); V(o.pathOffset); F(o.moveSpeed); F(o.movePause);
        F(o.timeOffset);
        I(o.pathMode); I(o.enemyPathMode); I(o.flying ? 1 : 0); I(o.enemyDeathEffect); I(o.breakEffect); I(o.contents); I(o.portalTrigger);
        I(o.powerType); I(o.itemMoves); I(o.coinCount);
        mix(&o.playerP, sizeof o.playerP); mix(&o.enemyP, sizeof o.enemyP); mix(&o.bossP, sizeof o.bossP); mix(&o.pathP, sizeof o.pathP);
        S(o.animator.anim); S(o.animator.clip); I(o.animator.playing ? 1 : 0); F(o.animator.speed);
        const int n = GhostObject_FaceCount(o);
        for (int f = 0; f < n; f++) mix(&o.faces[f], sizeof(GhostMaterial));
        if (o.meshId == GHOST_MESH_QUAD)
        {
            const GhostSprite& sp = o.sprite;
            I(sp.textureId); I(sp.source); I(sp.cols); I(sp.rows); I(sp.frame);
            I(sp.flipX); I(sp.billboard); I(sp.unlit); I(sp.alphaClip); I(sp.pixelArt); I(sp.playing);
            F(sp.tint[0]); F(sp.tint[1]); F(sp.tint[2]); F(sp.animSpeed);
            S(sp.anim); S(sp.clip);
        }
    }
    for (const GhostLight& l : s.lights)
    {
        I(l.id); I((int)l.type); S(l.name);
        V(l.position); F(l.yawDeg); F(l.pitchDeg);
        F(l.color[0]); F(l.color[1]); F(l.color[2]);
        F(l.intensity); F(l.range); F(l.width); F(l.height);
        I(l.castShadows ? 1 : 0);
        F(l.innerDeg); F(l.outerDeg); I(l.shadowObjects ? 1 : 0); I(l.shadowLandscape ? 1 : 0); F(l.spinYaw); F(l.spinPitch); F(l.volumetric);
        for (int k = 0; k < GHOST_FLICKER_LAYERS; k++)
        {
            const GhostFlickerLayer& f = l.flicker[k];
            I(f.enabled ? 1 : 0); I(f.pattern); S(f.custom); F(f.speed); F(f.minLevel); I(f.smoothSteps ? 1 : 0);
            F(f.baseInfluence); F(f.brightness); F(f.color[0]); F(f.color[1]); F(f.color[2]);
            I(f.textureId); F(f.texSpeed); F(f.texTiling); F(f.texAmount);
            F(f.diffuse); F(f.specular); F(f.roughness); F(f.metallic); F(f.seed);
        }
    }
    for (const GhostCamera& c : s.cameras)
    {
        I(c.id); S(c.name); F(c.fovDeg); I(c.resW); I(c.resH);
        I(c.mode); I(c.targetId); S(c.targetTag);
        F(c.distance); F(c.heightOffset); F(c.minDist); F(c.maxDist);
        F(c.minPitchDeg); F(c.maxPitchDeg); F(c.smoothing);
        F(c.moveSpeed);
        if (c.id != s.activeCameraId)               // flying the main camera isn't an edit
        {
            if (c.mode != 1) V(c.position);         // an orbit camera's position is computed, so it isn't an edit either
            F(c.yawDeg); F(c.pitchDeg);
        }
    }
    for (const GhostModEntity& m : s.mods)
    {
        I(m.id); I(m.typeId); S(m.name); V(m.position);
        for (float x : m.values) F(x);
    }
    for (const GhostVolume& v : s.volumes)
    {
        I(v.id); I(v.type); S(v.name); V(v.position); V(v.rotationDeg); V(v.size);
        I(v.shape); F(v.radius); F(v.blend); I(v.priority); I(v.enabled ? 1 : 0);
        for (float x : v.p) F(x);
        for (int t : v.tex) I(t);
    }
    for (const GhostParticleDef& d : s.particles) { mix(&d.p, sizeof d.p); S(d.name); S(d.anim); S(d.clip); }
    return h;
}