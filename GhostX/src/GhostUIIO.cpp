#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../include/GhostUI.h"
#include "../include/GhostImage.h"
#include "../include/json.hpp"
#include <filesystem>
#include <fstream>
#include <string.h>
#include <math.h>

using json = nlohmann::json;
namespace fs = std::filesystem;

static const char* kTypeNames[GHOST_W_COUNT] = { "Canvas", "Image", "Text", "Button", "ProgressBar" };

struct Preset { const char* name; float mn[2], mx[2]; };
static const Preset kPresets[] =
{
    { "TopLeft", { 0, 0 }, { 0, 0 } },          { "TopCenter", { 0.5f, 0 }, { 0.5f, 0 } },       { "TopRight", { 1, 0 }, { 1, 0 } },
    { "CenterLeft", { 0, 0.5f }, { 0, 0.5f } }, { "Center", { 0.5f, 0.5f }, { 0.5f, 0.5f } },    { "CenterRight", { 1, 0.5f }, { 1, 0.5f } },
    { "BottomLeft", { 0, 1 }, { 0, 1 } },       { "BottomCenter", { 0.5f, 1 }, { 0.5f, 1 } },    { "BottomRight", { 1, 1 }, { 1, 1 } },
    { "Fill", { 0, 0 }, { 1, 1 } },
    { "StretchTop", { 0, 0 }, { 1, 0 } },       { "StretchBottom", { 0, 1 }, { 1, 1 } },
    { "StretchLeft", { 0, 0 }, { 0, 1 } },      { "StretchRight", { 1, 0 }, { 1, 1 } },
};

// Fills as many of the n floats as the array has. Returns true if the key exists.
static bool ReadF(const json& j, const char* key, float* out, int n)
{
    auto it = j.find(key);
    if (it == j.end() || !it->is_array()) return false;
    for (int i = 0; i < n && i < (int)it->size(); i++)
        if ((*it)[(size_t)i].is_number()) out[i] = (*it)[(size_t)i].get<float>();
    return true;
}

static json A2(const float* a) { return json::array({ a[0], a[1] }); }
static json A4(const float* a) { return json::array({ a[0], a[1], a[2], a[3] }); }

static std::string ResolveStored(const std::string& stored, const std::string& root)
{
    fs::path p(GhostUtf8ToWide(stored));
    if (p.is_relative() && !root.empty()) p = fs::path(GhostUtf8ToWide(root)) / p;
    return GhostPath_Normalize(GhostWideToUtf8(p.wstring()));
}

// ============================================================
//  Load
// ============================================================
static void LoadWidget(const json& j, int parent, int depth, GhostUIAsset& a, const std::string& root,
    const GhostLoadTexFn& loadTex, std::string& warn)
{
    if (!j.is_object()) return;
    if (depth > 12 || a.widgets.size() >= GHOST_UI_MAX_WIDGETS)
    {
        warn += "Too many widgets or nested too deep: the rest were skipped.\n"; return;
    }

    const std::string tname = j.value("type", std::string("Canvas"));
    int type = -1;
    for (int i = 0; i < GHOST_W_COUNT; i++) if (tname == kTypeNames[i]) type = i;
    if (type < 0) { warn += "Skipped a widget with the unknown type \"" + tname + "\".\n"; return; }

    GhostWidget w;
    w.type = type;
    w.parent = parent;
    w.name = j.value("name", tname);

    // per-type defaults
    if (type == GHOST_W_CANVAS) w.color[3] = 0.0f;                                   // an invisible container unless given a color
    if (type == GHOST_W_BUTTON) { w.color[0] = 0.25f; w.color[1] = 0.25f; w.color[2] = 0.30f; }
    if (type == GHOST_W_PROGRESS) { w.color[0] = 0.30f; w.color[1] = 0.85f; w.color[2] = 0.40f; }

    // anchors: a preset name, then optional explicit arrays
    const std::string preset = j.value("anchor", std::string());
    if (!preset.empty())
    {
        bool found = false;
        for (const Preset& p : kPresets)
            if (preset == p.name)
            {
                w.anchorMin[0] = p.mn[0]; w.anchorMin[1] = p.mn[1];
                w.anchorMax[0] = p.mx[0]; w.anchorMax[1] = p.mx[1];
                found = true;
            }
        if (!found) warn += "Unknown anchor \"" + preset + "\" on " + w.name + ".\n";
    }
    ReadF(j, "anchorMin", w.anchorMin, 2);
    ReadF(j, "anchorMax", w.anchorMax, 2);

    for (int i = 0; i < 2; i++)                                                      // defaults that depend on the anchors
    {
        const bool stretch = fabsf(w.anchorMax[i] - w.anchorMin[i]) > 1e-5f;
        w.size[i] = stretch ? 0.0f : (i == 0 ? 100.0f : 40.0f);
        w.pivot[i] = stretch ? 0.0f : w.anchorMin[i];                                // pivot sits on the anchor
    }
    ReadF(j, "offset", w.offset, 2);
    ReadF(j, "size", w.size, 2);
    ReadF(j, "pivot", w.pivot, 2);

    ReadF(j, "color", w.color, 4);
    for (int k = 0; k < 3; k++)                                                      // button states follow the face color
    {
        w.hoverColor[k] = w.color[k] + (1.0f - w.color[k]) * 0.2f;
        w.pressColor[k] = w.color[k] * 0.7f;
    }
    w.hoverColor[3] = w.pressColor[3] = w.color[3];
    ReadF(j, "textColor", w.textColor, 4);
    ReadF(j, "hoverColor", w.hoverColor, 4);
    ReadF(j, "pressColor", w.pressColor, 4);
    ReadF(j, "bgColor", w.bgColor, 4);

    w.visible = j.value("visible", true);
    w.text = j.value("text", std::string());
    if (w.text.size() > 255) w.text.resize(255);
    w.fontSize = j.value("fontSize", 32.0f);
    if (w.fontSize < 4.0f) w.fontSize = 4.0f;
    if (w.fontSize > 400.0f) w.fontSize = 400.0f;
    w.align = j.value("align", 0);
    if (w.align < 0) w.align = 0;
    if (w.align > 2) w.align = 2;
    w.shadow = j.value("shadow", true);
    w.value = j.value("value", 1.0f);
    if (w.value < 0.0f) w.value = 0.0f;
    if (w.value > 1.0f) w.value = 1.0f;

    w.texture = j.value("texture", std::string());
    if (type == GHOST_W_IMAGE && !w.texture.empty() && loadTex)
    {
        const std::string abs = ResolveStored(w.texture, root);
        const std::string file = GhostWideToUtf8(fs::path(GhostUtf8ToWide(abs)).filename().wstring());
        const int id = loadTex(abs, file);
        if (id > 0) w.textureId = id; else warn += "Missing texture: " + w.texture + "\n";
    }

    const int me = (int)a.widgets.size();
    a.widgets.push_back(w);

    auto kids = j.find("children");
    if (kids != j.end() && kids->is_array())
        for (const json& c : *kids) LoadWidget(c, me, depth + 1, a, root, loadTex, warn);
}

bool GhostUI_Load(GhostUIAsset& a, const std::string& root, const std::string& file,
    const GhostLoadTexFn& loadTex, std::string& err, std::string& warn)
{
    json j;
    try
    {
        std::ifstream in(fs::path(GhostUtf8ToWide(file)), std::ios::binary);
        if (!in) { err = "Could not open " + file; return false; }
        if (in.peek() == 0xEF) { char bom[3]; in.read(bom, 3); }                      // skip a UTF-8 byte order mark
        j = json::parse(in);
    }
    catch (const std::exception& e)
    {
        err = std::string("The UI file is not valid JSON:\n") + e.what();
        return false;
    }

    try
    {
        if (!j.is_object() || j.value("format", std::string()) != "ghostui")
        {
            err = "This is not a GhostX UI file (\"format\" must be \"ghostui\")."; return false;
        }
        if (j.value("version", 0) > 1)
        {
            err = "This UI file was saved by a newer version of GhostX."; return false;
        }

        a.widgets.clear();
        a.shown = true;
        a.name = j.value("name", GhostWideToUtf8(fs::path(GhostUtf8ToWide(file)).stem().wstring()));

        auto list = j.find("widgets");
        if (list != j.end() && list->is_array())
            for (const json& w : *list) LoadWidget(w, -1, 0, a, root, loadTex, warn);
        return true;
    }
    catch (const std::exception& e)
    {
        err = std::string("The UI file has invalid data:\n") + e.what();
        return false;
    }
}

// ============================================================
//  Save (used by the UI Designer in 5C)
// ============================================================
static json SaveWidget(const GhostUIAsset& a, int idx)
{
    const GhostWidget& w = a.widgets[(size_t)idx];
    json j;
    j["type"] = kTypeNames[w.type];
    j["name"] = w.name;
    j["anchorMin"] = A2(w.anchorMin);
    j["anchorMax"] = A2(w.anchorMax);
    j["offset"] = A2(w.offset);
    j["size"] = A2(w.size);
    j["pivot"] = A2(w.pivot);
    j["color"] = A4(w.color);
    if (!w.visible) j["visible"] = false;

    switch (w.type)
    {
    case GHOST_W_IMAGE:
        j["texture"] = w.texture;
        break;
    case GHOST_W_TEXT:
        j["text"] = w.text; j["fontSize"] = w.fontSize; j["align"] = w.align; j["shadow"] = w.shadow;
        break;
    case GHOST_W_BUTTON:
        j["text"] = w.text; j["fontSize"] = w.fontSize; j["shadow"] = w.shadow;
        j["textColor"] = A4(w.textColor); j["hoverColor"] = A4(w.hoverColor); j["pressColor"] = A4(w.pressColor);
        break;
    case GHOST_W_PROGRESS:
        j["value"] = w.value; j["bgColor"] = A4(w.bgColor);
        break;
    }

    json kids = json::array();
    for (int i = 0; i < (int)a.widgets.size(); i++)
        if (a.widgets[(size_t)i].parent == idx) kids.push_back(SaveWidget(a, i));
    if (!kids.empty()) j["children"] = kids;
    return j;
}

bool GhostUI_Save(const GhostUIAsset& a, const std::string& file, std::string& err)
{
    try
    {
        json j;
        j["format"] = "ghostui";
        j["version"] = 1;
        j["name"] = a.name;
        json list = json::array();
        for (int i = 0; i < (int)a.widgets.size(); i++)
            if (a.widgets[(size_t)i].parent < 0) list.push_back(SaveWidget(a, i));
        j["widgets"] = list;

        fs::path out(GhostUtf8ToWide(file));
        if (out.has_parent_path()) fs::create_directories(out.parent_path());
        fs::path tmp = out;
        tmp += L".tmp";
        {
            std::ofstream f(tmp, std::ios::binary);
            if (!f) { err = "Could not write to the destination folder."; return false; }
            f << j.dump(2);
            if (!f) { err = "Writing the UI file failed."; return false; }
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