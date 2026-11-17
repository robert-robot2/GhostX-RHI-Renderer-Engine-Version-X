#include "../include/GhostAnim.h"
#include "../include/GhostImage.h"
#include "../include/json.hpp"
#include <filesystem>
#include <fstream>
#include <string.h>
#include <math.h>
#include <algorithm>
// GhostAnim.cpp

using json = nlohmann::json;
namespace fs = std::filesystem;

static int Clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

bool GhostAnim_Load(GhostAnimAsset& a, const std::string& root, const std::string& file,
    const GhostLoadTexFn& loadTex, std::string& err, std::string& warn)
{
    json j;
    try
    {
        std::ifstream in(fs::path(GhostUtf8ToWide(file)), std::ios::binary);
        if (!in) { err = "Could not open " + file; return false; }
        if (in.peek() == 0xEF) { char bom[3]; in.read(bom, 3); }   // skip UTF-8 BOM
        j = json::parse(in);
    }
    catch (const std::exception& e)
    {
        err = std::string("The animation file is not valid JSON:\n") + e.what();
        return false;
    }

    try
    {
        if (!j.is_object() || j.value("format", std::string()) != "ghostanim")
        {
            err = "This is not a GhostX animation file (\"format\" must be \"ghostanim\").";
            return false;
        }
        if (j.value("version", 0) > 1)
        {
            err = "This animation file was saved by a newer version of GhostX.";
            return false;
        }

        GhostAnimAsset n;
        n.name = j.value("name", GhostWideToUtf8(fs::path(GhostUtf8ToWide(file)).stem().wstring()));

        if (j.contains("clips") && j["clips"].is_array())
        {
            for (const json& jc : j["clips"])
            {
                if (!jc.is_object()) continue;
                if (n.clips.size() >= 64) { warn += "Too many clips: the rest were skipped.\n"; break; }

                GhostClip c;
                c.name = jc.value("name", std::string());
                if (c.name.empty()) { warn += "Skipped a clip with no name.\n"; continue; }

                c.fps = jc.value("fps", 8.0f);
                if (c.fps < 0.1f)   c.fps = 0.1f;
                if (c.fps > 120.0f) c.fps = 120.0f;
                c.loop = jc.value("loop", true);
                c.speed = jc.value("speed", 1.0f);
                if (c.speed < 0.05f) c.speed = 0.05f;
                if (c.speed > 8.0f)  c.speed = 8.0f;

                // 3D clips: an FBX instead of a sprite sheet
                c.skeletal = (jc.value("kind", std::string("sprite")) == "skeletal");
                c.model = jc.value("model", std::string());
                if (c.skeletal)
                {
                    if (c.model.empty()) { warn += "Skipped 3D clip \"" + c.name + "\": it has no FBX file.\n"; continue; }
                    n.clips.push_back(std::move(c));
                    continue;
                }
                // Per-clip texture / grid
                c.texture = jc.value("texture", std::string());
                c.source = (jc.value("source", std::string("sheet")) == "single") ? 0 : 1;
                c.cols = Clampi(jc.value("columns", 1), 1, 64);
                c.rows = Clampi(jc.value("rows", 1), 1, 64);
                if (c.source == 0) { c.cols = 1; c.rows = 1; }

                // Load this clip's texture
                if (!c.texture.empty() && loadTex)
                {
                    fs::path p(GhostUtf8ToWide(c.texture));
                    if (p.is_relative() && !root.empty())
                        p = fs::path(GhostUtf8ToWide(root)) / p;
                    const std::string abs = GhostPath_Normalize(GhostWideToUtf8(p.wstring()));
                    const int id = loadTex(abs, GhostWideToUtf8(p.filename().wstring()));
                    if (id > 0) c.textureId = id;
                    else warn += "Missing texture for clip \"" + c.name + "\": " + c.texture + "\n";
                }

                const int maxFrame = c.cols * c.rows - 1;

                if (jc.contains("frames") && jc["frames"].is_array())
                {
                    for (const json& f : jc["frames"])
                        if (f.is_number_integer() && c.frames.size() < 256)
                            c.frames.push_back(Clampi(f.get<int>(), 0, maxFrame));
                }
                else if (jc.contains("range") && jc["range"].is_array() && jc["range"].size() >= 2)
                {
                    const int from = Clampi(jc["range"][0].get<int>(), 0, maxFrame);
                    const int to = Clampi(jc["range"][1].get<int>(), 0, maxFrame);
                    const int step = from <= to ? 1 : -1;
                    for (int f = from; c.frames.size() < 256; f += step)
                    {
                        c.frames.push_back(f);
                        if (f == to) break;
                    }
                }

                if (c.frames.empty())
                {
                    warn += "Skipped clip \"" + c.name + "\": it has no frames.\n";
                    continue;
                }
                n.clips.push_back(std::move(c));
            }
        }

        if (j.contains("states") && j["states"].is_object())
            for (auto it = j["states"].begin(); it != j["states"].end(); ++it)
                if (it.value().is_string() && n.states.size() < 64)
                    n.states.push_back({ it.key(), it.value().get<std::string>() });
        if (j.contains("graph") && j["graph"].is_object())
        {
            const json& jg = j["graph"];
            if (jg.contains("nodes") && jg["nodes"].is_array())
                for (const json& jn : jg["nodes"])
                {
                    if (!jn.is_object() || n.graph.nodes.size() >= 128) continue;
                    GhostGraphNode g;
                    g.id = jn.value("id", 0);
                    if (g.id <= 0) continue;
                    g.type = Clampi(jn.value("type", 1), 0, GNODE_COUNT - 1);
                    g.x = jn.value("x", 0.0f);  g.y = jn.value("y", 0.0f);
                    g.name = jn.value("name", std::string());
                    g.clip = jn.value("clip", std::string());
                    g.curve = Clampi(jn.value("curve", 3), 0, 3);
                    if (jn.contains("p") && jn["p"].is_array())
                        for (size_t k = 0; k < 4 && k < jn["p"].size(); k++)
                            if (jn["p"][k].is_number()) g.p[k] = jn["p"][k].get<float>();
                    n.graph.nodes.push_back(g);
                }
            if (jg.contains("links") && jg["links"].is_array())
                for (const json& jl : jg["links"])
                    if (jl.is_object() && n.graph.links.size() < 256)
                        n.graph.links.push_back({ jl.value("from", 0), jl.value("to", 0) });
        }

        a = std::move(n);
        return true;
    }
    catch (const std::exception& e)
    {
        err = std::string("The animation file has invalid data:\n") + e.what();
        return false;
    }
}

int GhostAnim_FindClip(const GhostAnimAsset& a, const char* name)
{
    if (!name) return -1;
    for (size_t i = 0; i < a.clips.size(); i++)
        if (_stricmp(a.clips[i].name.c_str(), name) == 0) return (int)i;
    return -1;
}

int GhostAnim_FrameAt(const GhostClip& c, float t)
{
    const int n = (int)c.frames.size();
    if (n == 0) return 0;
    int i = (int)floorf(t * c.fps);
    if (i < 0) i = 0;
    if (c.loop) i %= n; else if (i >= n) i = n - 1;
    return c.frames[(size_t)i];
}

static int NodeIndex(const GhostAnimGraph& g, int id)
{
    for (size_t i = 0; i < g.nodes.size(); i++) if (g.nodes[i].id == id) return (int)i;
    return -1;
}
static int FindStateNode(const GhostAnimGraph& g, const std::string& name)
{
    for (size_t i = 0; i < g.nodes.size(); i++)
        if (g.nodes[i].type == GNODE_STATE && _stricmp(g.nodes[i].name.c_str(), name.c_str()) == 0) return (int)i;
    return -1;
}

bool GhostAnimGraph_Resolve(const GhostAnimGraph& g, const std::string& from, const std::string& to, GhostGraphPath& out)
{
    out = GhostGraphPath();
    const int dst = FindStateNode(g, to);
    if (dst < 0) return false;
    out.target = dst;

    std::vector<int> starts;                                  // the current state first, then every "Any State"
    const int src = from.empty() ? -1 : FindStateNode(g, from);
    if (src >= 0) starts.push_back(src);
    for (size_t i = 0; i < g.nodes.size(); i++) if (g.nodes[i].type == GNODE_ANY) starts.push_back((int)i);

    for (int s : starts)
    {
        if (s == dst) continue;
        std::vector<int> prev(g.nodes.size(), -2), queue;
        prev[(size_t)s] = -1; queue.push_back(s);
        for (size_t h = 0; h < queue.size(); h++)
        {
            const int cur = queue[h];
            for (const GhostGraphLink& l : g.links)
            {
                if (l.from != g.nodes[(size_t)cur].id) continue;
                const int ni = NodeIndex(g, l.to);
                if (ni < 0 || prev[(size_t)ni] != -2) continue;
                prev[(size_t)ni] = cur;
                if (ni == dst)
                {
                    for (int k = cur; k != s; k = prev[(size_t)k]) out.fx.push_back(k);
                    std::reverse(out.fx.begin(), out.fx.end());
                    return true;
                }
                const int t = g.nodes[(size_t)ni].type;      // only effect nodes are passed through
                if (t != GNODE_STATE && t != GNODE_ENTRY && t != GNODE_ANY) queue.push_back(ni);
            }
        }
    }
    return true;                                              // no wire found: direct switch with the default blend
}

int GhostAnimGraph_EntryState(const GhostAnimGraph& g)
{
    for (const GhostGraphNode& n : g.nodes)
        if (n.type == GNODE_ENTRY)
            for (const GhostGraphLink& l : g.links)
                if (l.from == n.id) { const int ni = NodeIndex(g, l.to); if (ni >= 0 && g.nodes[(size_t)ni].type == GNODE_STATE) return ni; }
    return -1;
}