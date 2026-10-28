#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../include/GhostAnim.h"
#include "../include/GhostImage.h"
#include "../include/json.hpp"
#include <filesystem>
#include <fstream>
#include <string.h>
#include <math.h>

//GhostAnim.cpp

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
        if (in.peek() == 0xEF) { char bom[3]; in.read(bom, 3); }          // skip a UTF-8 byte order mark
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
            err = "This is not a GhostX animation file (\"format\" must be \"ghostanim\")."; return false;
        }
        if (j.value("version", 0) > 1)
        {
            err = "This animation file was saved by a newer version of GhostX."; return false;
        }

        GhostAnimAsset n;
        n.name = j.value("name", GhostWideToUtf8(fs::path(GhostUtf8ToWide(file)).stem().wstring()));
        n.texture = j.value("texture", std::string());
        n.source = (j.value("source", std::string("sheet")) == "single") ? 0 : 1;
        n.cols = Clampi(j.value("columns", 1), 1, 64);
        n.rows = Clampi(j.value("rows", 1), 1, 64);
        if (n.source == 0) n.cols = n.rows = 1;

        if (!n.texture.empty() && loadTex)
        {
            fs::path p(GhostUtf8ToWide(n.texture));
            if (p.is_relative() && !root.empty()) p = fs::path(GhostUtf8ToWide(root)) / p;
            const std::string abs = GhostPath_Normalize(GhostWideToUtf8(p.wstring()));
            const int id = loadTex(abs, GhostWideToUtf8(p.filename().wstring()));
            if (id > 0) n.textureId = id; else warn += "Missing texture: " + n.texture + "\n";
        }

        const int maxFrame = n.cols * n.rows - 1;
        if (j.contains("clips") && j["clips"].is_array())
            for (const json& jc : j["clips"])
            {
                if (!jc.is_object()) continue;
                if (n.clips.size() >= 64) { warn += "Too many clips: the rest were skipped.\n"; break; }

                GhostClip c;
                c.name = jc.value("name", std::string());
                if (c.name.empty()) { warn += "Skipped a clip with no name.\n"; continue; }
                c.fps = jc.value("fps", 8.0f);
                if (c.fps < 0.1f) c.fps = 0.1f;
                if (c.fps > 120.0f) c.fps = 120.0f;
                c.loop = jc.value("loop", true);

                if (jc.contains("frames") && jc["frames"].is_array())
                {
                    for (const json& f : jc["frames"])
                        if (f.is_number_integer() && c.frames.size() < 256) c.frames.push_back(Clampi(f.get<int>(), 0, maxFrame));
                }
                else if (jc.contains("range") && jc["range"].is_array() && jc["range"].size() >= 2)
                {
                    const int from = Clampi(jc["range"][0].get<int>(), 0, maxFrame);
                    const int to = Clampi(jc["range"][1].get<int>(), 0, maxFrame);
                    const int step = from <= to ? 1 : -1;
                    for (int f = from; c.frames.size() < 256; f += step) { c.frames.push_back(f); if (f == to) break; }
                }
                if (c.frames.empty()) { warn += "Skipped clip \"" + c.name + "\": it has no frames.\n"; continue; }
                n.clips.push_back(std::move(c));
            }
        if (j.contains("states") && j["states"].is_object())
            for (auto it = j["states"].begin(); it != j["states"].end(); ++it)
                if (it.value().is_string() && n.states.size() < 64)
                    n.states.push_back({ it.key(), it.value().get<std::string>() });
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