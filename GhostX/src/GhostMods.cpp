#include "../include/GhostMods.h"
#include <math.h>
#include <string.h>

#define P_FLOAT(n, c, d, lo, hi)  { n, c, GHOST_PARAM_FLOAT,   { d, 0.0f, 0.0f }, lo, hi, nullptr }
#define P_BOOL(n, c, d)           { n, c, GHOST_PARAM_BOOL,    { d, 0.0f, 0.0f }, 0.0f, 1.0f, nullptr }
#define P_ENUM(n, c, d, names)    { n, c, GHOST_PARAM_ENUM,    { d, 0.0f, 0.0f }, 0.0f, 0.0f, names }
#define P_COLOR(n, c, r, g, b)    { n, c, GHOST_PARAM_COLOR,   { r, g, b },       0.0f, 1.0f, nullptr }
#define P_TEX(n, c)               { n, c, GHOST_PARAM_TEXTURE, { 0.0f, 0.0f, 0.0f }, 0.0f, 0.0f, nullptr }

static const GhostParamDesc kSkyParams[] =
{
    P_BOOL("Enabled",                    "General", 1.0f),
    P_FLOAT("Time Of Day (hours)",        "Time", 12.0f, 0.0f, 24.0f),
    P_FLOAT("Cycle Speed (hours/sec)",    "Time", 0.0f, -2.0f, 2.0f),
    P_FLOAT("Twilight Width",             "Time", 0.25f, 0.02f, 0.8f),
    P_FLOAT("Sun Path Direction (deg)",   "Time", 90.0f, -360.0f, 360.0f),

        P_BOOL("Hide Sun At Times",        "Time", 0.0f),
    P_FLOAT("Hide Sun From (hour)",    "Time", 12.0f, 0.0f, 24.0f),
    P_FLOAT("Hide Sun Until (hour)",   "Time", 14.0f, 0.0f, 24.0f),
    P_BOOL("Hide Moon At Times",       "Time", 0.0f),
    P_FLOAT("Hide Moon From (hour)",   "Time", 0.0f, 0.0f, 24.0f),
    P_FLOAT("Hide Moon Until (hour)",  "Time", 2.0f, 0.0f, 24.0f),
    P_FLOAT("Hide Fade (hours)",       "Time", 0.25f, 0.0f, 6.0f),

    P_COLOR("Day Zenith",                 "Sky Gradient", 0.18f, 0.40f, 0.80f),
    P_COLOR("Day Horizon",                "Sky Gradient", 0.62f, 0.78f, 0.95f),
    P_COLOR("Night Zenith",               "Sky Gradient", 0.01f, 0.02f, 0.08f),
    P_COLOR("Night Horizon",              "Sky Gradient", 0.05f, 0.07f, 0.16f),

    P_FLOAT("Horizon Height",             "Horizon", 0.0f, -0.5f, 0.5f),
    P_FLOAT("Horizon Width",              "Horizon", 0.9f, 0.05f, 1.5f),
    P_FLOAT("Gradient Curve",             "Horizon", 0.6f, 0.1f, 4.0f),
    P_COLOR("Ground Color",               "Horizon", 0.10f, 0.11f, 0.12f),
    P_FLOAT("Ground Blend",               "Horizon", 0.05f, 0.001f, 0.5f),
    P_COLOR("Dusk/Dawn Color",            "Horizon", 1.0f, 0.45f, 0.20f),
    P_FLOAT("Dusk/Dawn Strength",         "Horizon", 0.8f, 0.0f, 1.0f),

    P_COLOR("Sun Color",                  "Sun", 1.0f, 0.95f, 0.80f),
    P_FLOAT("Sun Size (deg)",             "Sun", 2.5f, 0.0f, 20.0f),
    P_FLOAT("Sun Intensity",              "Sun", 1.5f, 0.0f, 10.0f),
    P_FLOAT("Sun Glow",                   "Sun", 0.35f, 0.0f, 3.0f),
    P_FLOAT("Sun Glow Size",              "Sun", 0.08f, 0.005f, 1.0f),

    P_COLOR("Moon Color",                 "Moon", 0.85f, 0.90f, 1.0f),
    P_FLOAT("Moon Size (deg)",            "Moon", 2.2f, 0.0f, 20.0f),
    P_FLOAT("Moon Intensity",             "Moon", 1.0f, 0.0f, 10.0f),

    P_FLOAT("Brightness",                 "General", 1.0f, 0.0f, 4.0f),

    P_TEX("Day Texture",                "Day Layer"),
    P_ENUM("Day Layout",                 "Day Layer", 0.0f, "Cube Cross|Equirectangular"),
    P_FLOAT("Day Opacity",                "Day Layer", 1.0f, 0.0f, 1.0f),
    P_FLOAT("Day Brightness",             "Day Layer", 1.0f, 0.0f, 4.0f),
    P_COLOR("Day Tint",                   "Day Layer", 1.0f, 1.0f, 1.0f),
    P_FLOAT("Day Rotation (deg)",         "Day Layer", 0.0f, -360.0f, 360.0f),
    P_FLOAT("Day Scroll (deg/sec)",       "Day Layer", 0.5f, -90.0f, 90.0f),
    P_BOOL("Day Black Is Transparent",   "Day Layer", 1.0f),
    P_FLOAT("Day Black Cutoff",           "Day Layer", 0.05f, 0.0f, 1.0f),
    P_FLOAT("Day Black Softness",         "Day Layer", 0.30f, 0.0f, 1.0f),
    P_BOOL("Day Flip Horizontal",        "Day Layer", 0.0f),

    P_TEX("Night Texture",              "Night Layer"),
    P_ENUM("Night Layout",               "Night Layer", 0.0f, "Cube Cross|Equirectangular"),
    P_FLOAT("Night Opacity",              "Night Layer", 1.0f, 0.0f, 1.0f),
    P_FLOAT("Night Brightness",           "Night Layer", 1.2f, 0.0f, 4.0f),
    P_COLOR("Night Tint",                 "Night Layer", 1.0f, 1.0f, 1.0f),
    P_FLOAT("Night Rotation (deg)",       "Night Layer", 0.0f, -360.0f, 360.0f),
    P_FLOAT("Night Scroll (deg/sec)",     "Night Layer", 0.2f, -90.0f, 90.0f),
    P_BOOL("Night Black Is Transparent", "Night Layer", 1.0f),
    P_FLOAT("Night Black Cutoff",         "Night Layer", 0.03f, 0.0f, 1.0f),
    P_FLOAT("Night Black Softness",       "Night Layer", 0.20f, 0.0f, 1.0f),
    P_BOOL("Night Flip Horizontal",      "Night Layer", 0.0f),
        P_BOOL("Show Ground",                "Horizon", 0.0f),
            P_BOOL("Ambient Follows Time",        "Ambient", 0.0f),
    P_FLOAT("Ambient Night Start (hour)", "Ambient", 18.0f, 0.0f, 24.0f),
    P_FLOAT("Ambient Night End (hour)",   "Ambient", 6.0f, 0.0f, 24.0f),
    P_FLOAT("Ambient Fade Length (hours)","Ambient", 2.0f, 0.1f, 12.0f),
    P_FLOAT("Night Ambient Intensity",    "Ambient", 0.05f, 0.0f, 1.0f),
        P_BOOL("Auto Ambience By Hour",   "Ambient Hours", 0.0f),
    P_FLOAT("Ambient Hour 00", "Ambient Hours", 0.0500f, 0.0f, 2.0f),
    P_FLOAT("Ambient Hour 01", "Ambient Hours", 0.0625f, 0.0f, 2.0f),
    P_FLOAT("Ambient Hour 02", "Ambient Hours", 0.0750f, 0.0f, 2.0f),
    P_FLOAT("Ambient Hour 03", "Ambient Hours", 0.0875f, 0.0f, 2.0f),
    P_FLOAT("Ambient Hour 04", "Ambient Hours", 0.1000f, 0.0f, 2.0f),
    P_FLOAT("Ambient Hour 05", "Ambient Hours", 0.1125f, 0.0f, 2.0f),
    P_FLOAT("Ambient Hour 06", "Ambient Hours", 0.1250f, 0.0f, 2.0f),
    P_FLOAT("Ambient Hour 07", "Ambient Hours", 0.1375f, 0.0f, 2.0f),
    P_FLOAT("Ambient Hour 08", "Ambient Hours", 0.1500f, 0.0f, 2.0f),
    P_FLOAT("Ambient Hour 09", "Ambient Hours", 0.1625f, 0.0f, 2.0f),
    P_FLOAT("Ambient Hour 10", "Ambient Hours", 0.1750f, 0.0f, 2.0f),
    P_FLOAT("Ambient Hour 11", "Ambient Hours", 0.1875f, 0.0f, 2.0f),
    P_FLOAT("Ambient Hour 12", "Ambient Hours", 0.2000f, 0.0f, 2.0f),
    P_FLOAT("Ambient Hour 13", "Ambient Hours", 0.1875f, 0.0f, 2.0f),
    P_FLOAT("Ambient Hour 14", "Ambient Hours", 0.1750f, 0.0f, 2.0f),
    P_FLOAT("Ambient Hour 15", "Ambient Hours", 0.1625f, 0.0f, 2.0f),
    P_FLOAT("Ambient Hour 16", "Ambient Hours", 0.1500f, 0.0f, 2.0f),
    P_FLOAT("Ambient Hour 17", "Ambient Hours", 0.1375f, 0.0f, 2.0f),
    P_FLOAT("Ambient Hour 18", "Ambient Hours", 0.1250f, 0.0f, 2.0f),
    P_FLOAT("Ambient Hour 19", "Ambient Hours", 0.1125f, 0.0f, 2.0f),
    P_FLOAT("Ambient Hour 20", "Ambient Hours", 0.1000f, 0.0f, 2.0f),
    P_FLOAT("Ambient Hour 21", "Ambient Hours", 0.0875f, 0.0f, 2.0f),
    P_FLOAT("Ambient Hour 22", "Ambient Hours", 0.0750f, 0.0f, 2.0f),
    P_FLOAT("Ambient Hour 23", "Ambient Hours", 0.0625f, 0.0f, 2.0f),

    P_FLOAT("Moon Glow",      "Moon", 0.2f, 0.0f, 3.0f),
    P_FLOAT("Moon Glow Size", "Moon", 0.08f, 0.005f, 1.0f),

    P_TEX("Sun Texture",                     "Sun Texture"),
    P_FLOAT("Sun Texture Size (deg)",        "Sun Texture", 8.0f, 0.1f, 90.0f),
    P_FLOAT("Sun Texture Brightness",        "Sun Texture", 1.0f, 0.0f, 8.0f),
    P_FLOAT("Sun Texture Rotation (deg)",    "Sun Texture", 0.0f, -360.0f, 360.0f),
    P_BOOL("Sun Texture Black Is Transparent", "Sun Texture", 1.0f),
    P_BOOL("Sun Texture Replaces Disc",      "Sun Texture", 1.0f),

    P_TEX("Moon Texture",                    "Moon Texture"),
    P_FLOAT("Moon Texture Size (deg)",       "Moon Texture", 6.0f, 0.1f, 90.0f),
    P_FLOAT("Moon Texture Brightness",       "Moon Texture", 1.0f, 0.0f, 8.0f),
    P_FLOAT("Moon Texture Rotation (deg)",   "Moon Texture", 0.0f, -360.0f, 360.0f),
    P_BOOL("Moon Texture Black Is Transparent", "Moon Texture", 1.0f),
    P_BOOL("Moon Texture Replaces Disc",     "Moon Texture", 1.0f),

    P_TEX("Cloud Texture",                   "Clouds"),
    P_FLOAT("Cloud Translucency (1 = solid)", "Clouds", 0.8f, 0.0f, 1.0f),
    P_FLOAT("Cloud Coverage",                "Clouds", 0.5f, 0.0f, 1.0f),
    P_ENUM("Cloud Density From",             "Clouds", 1.0f, "Alpha|Brightness"),
    P_FLOAT("Cloud Brightness",              "Clouds", 1.0f, 0.0f, 4.0f),
    P_COLOR("Cloud Tint",                    "Clouds", 1.0f, 1.0f, 1.0f),
    P_FLOAT("Cloud Tiling",                  "Clouds", 1.0f, 0.05f, 20.0f),
    P_FLOAT("Cloud Horizon Fade",            "Clouds", 0.25f, 0.01f, 1.0f),
    P_FLOAT("Cloud Move U (per sec)",        "Cloud Movement", 0.01f, -1.0f, 1.0f),
    P_FLOAT("Cloud Move V (per sec)",        "Cloud Movement", 0.0f, -1.0f, 1.0f),
    P_FLOAT("Cloud Translucency Variation",  "Cloud Movement", 0.0f, 0.0f, 1.0f),
    P_FLOAT("Cloud Variation Frequency",     "Cloud Movement", 2.0f, 0.1f, 20.0f),
    P_BOOL("Cloud Blend With Night Stars",   "Clouds", 1.0f),
    P_FLOAT("Cloud Night Brightness",        "Clouds", 0.15f, 0.0f, 1.0f),

        P_FLOAT("Sun Texture Opacity", "Sun Texture", 1.0f, 0.0f, 1.0f),
        P_FLOAT("Sun Halo Strength", "Sun Texture", 0.6f, 0.0f, 2.0f),
        P_FLOAT("Sun Halo Thickness", "Sun Texture", 0.06f, 0.005f, 0.5f),
        P_FLOAT("Sun Halo Overlap", "Sun Texture", 0.03f, 0.0f, 0.5f),
        P_COLOR("Sun Halo Color", "Sun Texture", 1.0f, 0.85f, 0.5f),

        P_FLOAT("Moon Texture Opacity", "Moon Texture", 1.0f, 0.0f, 1.0f),
        P_FLOAT("Moon Halo Strength", "Moon Texture", 0.6f, 0.0f, 2.0f),
        P_FLOAT("Moon Halo Thickness", "Moon Texture", 0.06f, 0.005f, 0.5f),
        P_FLOAT("Moon Halo Overlap", "Moon Texture", 0.03f, 0.0f, 0.5f),
        P_COLOR("Moon Halo Color", "Moon Texture", 0.8f, 0.9f, 1.0f),

        P_FLOAT("Sun Glare Strength", "Sun Glare", 0.8f, 0.0f, 4.0f),
        P_FLOAT("Sun Glare Size (deg)", "Sun Glare", 10.0f, 1.0f, 90.0f),
        P_FLOAT("Sun Glare Overexposure", "Sun Glare", 2.5f, 1.0f, 30.0f),
        P_FLOAT("Sun Glare Look Boost", "Sun Glare", 0.7f, 0.0f, 1.0f),
        P_COLOR("Sun Glare Color", "Sun Glare", 1.0f, 0.95f, 0.8f),

        P_FLOAT("Moon Glare Strength", "Moon Glare", 0.4f, 0.0f, 4.0f),
        P_FLOAT("Moon Glare Size (deg)", "Moon Glare", 6.0f, 1.0f, 90.0f),
        P_FLOAT("Moon Glare Overexposure", "Moon Glare", 2.0f, 1.0f, 30.0f),
        P_FLOAT("Moon Glare Look Boost", "Moon Glare", 0.5f, 0.0f, 1.0f),
        P_COLOR("Moon Glare Color", "Moon Glare", 0.85f, 0.92f, 1.0f),

        P_TEX("Moon Cutoff Texture", "Moon Eclipse"),
        P_ENUM("Moon Phase Source", "Moon Eclipse", 0.0f, "Manual|Day Of Month (placeholder)"),
        P_FLOAT("Moon Cutoff Amount", "Moon Eclipse", 0.0f, 0.0f, 1.0f),
        P_FLOAT("Moon Cutoff Angle (deg)", "Moon Eclipse", 0.0f, -180.0f, 180.0f),
        P_FLOAT("Moon Cutoff Softness", "Moon Eclipse", 0.03f, 0.0f, 0.5f),
        P_FLOAT("Moon Day Of Month (placeholder)", "Moon Eclipse", 15.0f, 1.0f, 30.0f),

        P_BOOL("Sun Body Enabled", "Sun Body", 0.0f),
        P_FLOAT("Sun Body Size (deg)", "Sun Body", 6.0f, 0.5f, 60.0f),
        P_FLOAT("Sun Body Brightness", "Sun Body", 1.2f, 0.0f, 8.0f),
        P_FLOAT("Sun Body Edge Softness", "Sun Body", 0.06f, 0.005f, 0.5f),
        P_FLOAT("Sun Limb Darkening", "Sun Body", 0.35f, 0.0f, 1.0f),
        P_TEX("Sun Heat Texture", "Sun Body"),
        P_FLOAT("Sun Heat Texture Strength", "Sun Body", 0.6f, 0.0f, 1.0f),
        P_FLOAT("Sun Heat Texture Tiling", "Sun Body", 1.5f, 0.1f, 8.0f),

        P_FLOAT("Plasma Turbulence", "Sun Plasma", 0.8f, 0.0f, 2.0f),
        P_FLOAT("Plasma Speed", "Sun Plasma", 0.15f, 0.0f, 2.0f),
        P_FLOAT("Plasma Scale", "Sun Plasma", 2.5f, 0.5f, 12.0f),

        P_FLOAT("Volumetric Scattering", "Sun Volume", 0.7f, 0.0f, 2.0f),
        P_FLOAT("Volume Density", "Sun Volume", 1.5f, 0.1f, 6.0f),
        P_FLOAT("Refractive Distortion", "Sun Refraction", 0.35f, 0.0f, 1.0f),

        P_FLOAT("Caustics Strength", "Sun Caustics", 0.5f, 0.0f, 2.0f),
        P_FLOAT("Caustics Scale", "Sun Caustics", 2.0f, 0.5f, 10.0f),
        P_FLOAT("Caustics Speed", "Sun Caustics", 0.4f, 0.0f, 3.0f),

        P_FLOAT("Subsurface Scattering", "Sun Subsurface", 0.6f, 0.0f, 2.0f),
        P_COLOR("Subsurface Color", "Sun Subsurface", 1.0f, 0.25f, 0.05f),

};
static_assert(sizeof(kSkyParams) / sizeof(kSkyParams[0]) == SKY_PARAM_COUNT, "SkyParam enum and kSkyParams are out of sync");

static const GhostModType kTypes[GHOST_MOD_COUNT] =
{
    { "Sky Sphere", "SkySphere", 1, kSkyParams, SKY_PARAM_COUNT },
};

// ============================================================
//  Registry
// ============================================================
static std::vector<int> g_offsets[GHOST_MOD_COUNT];
static int              g_valueCount[GHOST_MOD_COUNT] = {};

static void EnsureOffsets(int t)
{
    if (!g_offsets[t].empty()) return;
    int off = 0;
    for (int p = 0; p < kTypes[t].paramCount; p++)
    {
        g_offsets[t].push_back(off);
        off += GhostParam_Count(kTypes[t].params[p]);
    }
    g_valueCount[t] = off;
}

const GhostModType* GhostMods_Type(int t) { return (t >= 0 && t < GHOST_MOD_COUNT) ? &kTypes[t] : nullptr; }

int GhostMods_FindByName(const char* n)
{
    if (!n) return -1;
    for (int t = 0; t < GHOST_MOD_COUNT; t++) if (strcmp(kTypes[t].saveName, n) == 0) return t;
    return -1;
}

int GhostMods_ParamOffset(int t, int p)
{
    if (t < 0 || t >= GHOST_MOD_COUNT || p < 0 || p >= kTypes[t].paramCount) return -1;
    EnsureOffsets(t);
    return g_offsets[t][(size_t)p];
}

int GhostMods_ValueCount(int t)
{
    if (t < 0 || t >= GHOST_MOD_COUNT) return 0;
    EnsureOffsets(t);
    return g_valueCount[t];
}

void GhostMods_Defaults(int t, std::vector<float>& out)
{
    out.assign((size_t)GhostMods_ValueCount(t), 0.0f);
    if (t < 0 || t >= GHOST_MOD_COUNT) return;
    for (int p = 0; p < kTypes[t].paramCount; p++)
    {
        const GhostParamDesc& d = kTypes[t].params[p];
        const int off = g_offsets[t][(size_t)p], n = GhostParam_Count(d);
        for (int i = 0; i < n; i++) out[(size_t)(off + i)] = d.def[i];
    }
}

static int EnumCount(const char* e)
{
    if (!e || !*e) return 1;
    int n = 1;
    for (; *e; e++) if (*e == '|') n++;
    return n;
}

void GhostMods_Clamp(int t, int p, float* v)
{
    if (t < 0 || t >= GHOST_MOD_COUNT || p < 0 || p >= kTypes[t].paramCount || !v) return;
    const GhostParamDesc& d = kTypes[t].params[p];
    switch (d.kind)
    {
    case GHOST_PARAM_FLOAT:
        if (d.mx > d.mn) v[0] = v[0] < d.mn ? d.mn : (v[0] > d.mx ? d.mx : v[0]);
        break;
    case GHOST_PARAM_BOOL:
        v[0] = v[0] > 0.5f ? 1.0f : 0.0f;
        break;
    case GHOST_PARAM_ENUM:
    {
        int i = (int)floorf(v[0] + 0.5f), n = EnumCount(d.enums);
        v[0] = (float)(i < 0 ? 0 : (i >= n ? n - 1 : i));
        break;
    }
    case GHOST_PARAM_COLOR:
        for (int k = 0; k < 3; k++) v[k] = v[k] < 0.0f ? 0.0f : (v[k] > 1.0f ? 1.0f : v[k]);
        break;
    case GHOST_PARAM_TEXTURE:
    {
        int i = (int)floorf(v[0] + 0.5f);
        v[0] = (float)(i < 0 ? 0 : i);
        break;
    }
    }
}

GhostModEntity* GhostScene_AddMod(GhostScene& s, int typeId)
{
    const GhostModType* t = GhostMods_Type(typeId);
    if (!t || s.mods.size() >= GHOST_MAX_MODS) return nullptr;
    if (t->maxPerScene > 0)
    {
        int have = 0;
        for (const GhostModEntity& m : s.mods) if (m.typeId == typeId) have++;
        if (have >= t->maxPerScene) return nullptr;
    }
    GhostModEntity m;
    m.id = s.nextId++;
    m.typeId = typeId;
    m.name = std::string(t->name) + " " + std::to_string(m.id);
    GhostMods_Defaults(typeId, m.values);
    s.mods.push_back(m);
    s.version++;
    return &s.mods.back();
}

// ============================================================
//  Sky
// ============================================================
static float Sat(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }
static float Smooth(float a, float b, float x) { float t = Sat((x - a) / (b - a)); return t * t * (3.0f - 2.0f * t); }
static float Mix(float a, float b, float t) { return a + (b - a) * t; }
// 1 = visible, 0 = hidden. A window [start, end) in hours that may wrap past midnight, with a fade at both edges.
static float HideWindowVis(float t, float start, float end, float fade)
{
    const float len = fmodf(end - start + 24.0f, 24.0f);
    if (len < 0.01f) return 1.0f;                           // start == end: nothing is hidden
    const float pos = fmodf(t - start + 24.0f, 24.0f);
    if (pos >= len) return 1.0f;
    fade = fade < 0.01f ? 0.01f : (fade > len * 0.5f ? len * 0.5f : fade);
    return 1.0f - fminf(Smooth(0.0f, fade, pos), Smooth(0.0f, fade, len - pos));
}


float GhostSky_GetTime(const GhostModEntity& e)
{
    const int o = GhostMods_ParamOffset(GHOST_MOD_SKY, SKY_TIME);
    return ((size_t)o < e.values.size()) ? e.values[(size_t)o] : 12.0f;
}

void GhostSky_SetTime(GhostModEntity& e, float h)
{
    const int o = GhostMods_ParamOffset(GHOST_MOD_SKY, SKY_TIME);
    if ((size_t)o >= e.values.size()) return;
    h = fmodf(h, 24.0f);
    if (h < 0.0f) h += 24.0f;
    e.values[(size_t)o] = h;
}

void GhostSky_AdvanceTime(GhostModEntity& e, float dt)
{
    const int o = GhostMods_ParamOffset(GHOST_MOD_SKY, SKY_SPEED);
    if ((size_t)o >= e.values.size()) return;
    const float speed = e.values[(size_t)o];
    if (speed != 0.0f) GhostSky_SetTime(e, GhostSky_GetTime(e) + speed * dt);
}

void GhostSky_Speeds(const GhostModEntity& e, float& day, float& night)
{
    day = night = 0.0f;
    const int a = GhostMods_ParamOffset(GHOST_MOD_SKY, SKY_DAY_SCROLL);
    const int b = GhostMods_ParamOffset(GHOST_MOD_SKY, SKY_NIGHT_SCROLL);
    if ((size_t)a < e.values.size()) day = e.values[(size_t)a];
    if ((size_t)b < e.values.size()) night = e.values[(size_t)b];
}

void GhostSky_CloudSpeeds(const GhostModEntity& e, float& u, float& v)
{
    u = v = 0.0f;
    const int a = GhostMods_ParamOffset(GHOST_MOD_SKY, SKY_CLOUD_SCROLL_U);
    const int b = GhostMods_ParamOffset(GHOST_MOD_SKY, SKY_CLOUD_SCROLL_V);
    if ((size_t)a < e.values.size()) u = e.values[(size_t)a];
    if ((size_t)b < e.values.size()) v = e.values[(size_t)b];
}

float GhostSky_AmbientIntensity(const GhostModEntity& e, float base)
{
    if (e.values.size() < (size_t)GhostMods_ValueCount(GHOST_MOD_SKY)) return base;
    auto V = [&](int p) { return e.values[(size_t)GhostMods_ParamOffset(GHOST_MOD_SKY, p)]; };
    if (V(SKY_AMB_AUTO) > 0.5f)                          // per-hour table, linear between hours, 24 wraps to 0
    {
        float th = fmodf(GhostSky_GetTime(e), 24.0f);
        if (th < 0.0f) th += 24.0f;
        const int h0 = (int)floorf(th) % 24, h1 = (h0 + 1) % 24;
        return Mix(V(SKY_AMB_HOUR0 + h0), V(SKY_AMB_HOUR0 + h1), th - floorf(th));
    }
    if (V(SKY_AMB_ENABLE) < 0.5f) return base;

    float t = fmodf(GhostSky_GetTime(e), 24.0f);
    if (t < 0.0f) t += 24.0f;

    const float start = V(SKY_AMB_START), end = V(SKY_AMB_END);
    float len = fmodf(end - start + 24.0f, 24.0f);          // length of the night window, wraps past midnight
    if (len < 0.01f) len = 24.0f;
    const float pos = fmodf(t - start + 24.0f, 24.0f);      // hours since the night began
    if (pos >= len) return base;                            // daytime

    float fade = V(SKY_AMB_FADE);
    if (fade > len * 0.5f) fade = len * 0.5f;
    if (fade < 0.01f) fade = 0.01f;

    const float k = fminf(Smooth(0.0f, fade, pos), Smooth(0.0f, fade, len - pos));   // fade in at dusk, out at dawn
    return Mix(base, fminf(base, V(SKY_AMB_LEVEL)), k);
}

int GhostSky_Build(const GhostModEntity& e, const std::vector<GhostTextureInfo>& textures,
    float dayScroll, float nightScroll, float cloudU, float cloudV, float timeSec, GhostSkyDraw out[9])
{
    if (e.values.size() < (size_t)GhostMods_ValueCount(GHOST_MOD_SKY)) return 0;
    auto V = [&](int p) { return e.values[(size_t)GhostMods_ParamOffset(GHOST_MOD_SKY, p)]; };
    auto C = [&](int p, float* c) { memcpy(c, &e.values[(size_t)GhostMods_ParamOffset(GHOST_MOD_SKY, p)], 3 * sizeof(float)); };
    if (V(SKY_ENABLED) < 0.5f) return 0;

    const float d2r = GHOST_PI / 180.0f;

    // ---- sun / moon from the time of day ----
    float t = fmodf(GhostSky_GetTime(e), 24.0f);
    if (t < 0.0f) t += 24.0f;
    const float ang = (t - 6.0f) / 24.0f * 2.0f * GHOST_PI;       // 6h rises, 12h overhead, 18h sets
    const float yaw = V(SKY_SUN_YAW) * d2r;
    const GhostVec3 H = { sinf(yaw), 0.0f, cosf(yaw) };            // the direction the sun rises in
    const GhostVec3 sunDir = (H * cosf(ang) + GhostVec3(0, 1, 0) * sinf(ang)).Normalize();
    const GhostVec3 moonDir = sunDir * -1.0f;
    const float elev = sinf(ang);

    const float tw = V(SKY_TWILIGHT) < 0.02f ? 0.02f : V(SKY_TWILIGHT);
    const float dayAmt = Smooth(-tw, tw, elev);
    const float duskK = Sat(1.0f - fabsf(elev) / tw) * V(SKY_DUSK_STRENGTH);
    const float bright = V(SKY_BRIGHTNESS);

    float dz[3], dh[3], nz[3], nh[3], gr[3], dusk[3], sunC[3], moonC[3];
    C(SKY_DAY_ZENITH, dz); C(SKY_DAY_HORIZON, dh); C(SKY_NIGHT_ZENITH, nz); C(SKY_NIGHT_HORIZON, nh);
    C(SKY_GROUND, gr); C(SKY_DUSK, dusk); C(SKY_SUN_COLOR, sunC); C(SKY_MOON_COLOR, moonC);

    float common[64] = {};
    auto F4 = [&](int i, float x, float y, float z, float w)
        { common[i * 4] = x; common[i * 4 + 1] = y; common[i * 4 + 2] = z; common[i * 4 + 3] = w; };

    const float groundDim = Mix(0.25f, 1.0f, dayAmt);
    F4(0, sunDir.x, sunDir.y, sunDir.z, dayAmt);
    F4(1, Mix(nz[0], dz[0], dayAmt), Mix(nz[1], dz[1], dayAmt), Mix(nz[2], dz[2], dayAmt), V(SKY_HORIZON_HEIGHT));
    F4(2, Mix(nh[0], dh[0], dayAmt), Mix(nh[1], dh[1], dayAmt), Mix(nh[2], dh[2], dayAmt), V(SKY_HORIZON_WIDTH));
    F4(3, gr[0] * groundDim, gr[1] * groundDim, gr[2] * groundDim, V(SKY_GROUND_BLEND));
    F4(4, dusk[0], dusk[1], dusk[2], duskK);
    F4(5, sunC[0], sunC[1], sunC[2], cosf(V(SKY_SUN_SIZE) * d2r));
    F4(6, moonDir.x, moonDir.y, moonDir.z, cosf(V(SKY_MOON_SIZE) * d2r));
    const float mi = V(SKY_MOON_INTENSITY);
    F4(7, moonC[0] * mi, moonC[1] * mi, moonC[2] * mi, V(SKY_GRAD_EXP));
    const float glowSize = V(SKY_SUN_GLOW_SIZE) < 0.005f ? 0.005f : V(SKY_SUN_GLOW_SIZE);
    F4(8, V(SKY_SUN_GLOW), 2.0f / glowSize, V(SKY_SUN_INTENSITY), bright);
    const float sunVis  = V(SKY_HIDE_SUN)  > 0.5f ? HideWindowVis(t, V(SKY_HIDE_SUN_START),  V(SKY_HIDE_SUN_END),  V(SKY_HIDE_FADE)) : 1.0f;
    const float moonVis = V(SKY_HIDE_MOON) > 0.5f ? HideWindowVis(t, V(SKY_HIDE_MOON_START), V(SKY_HIDE_MOON_END), V(SKY_HIDE_FADE)) : 1.0f;
    F4(12, 0.0f, V(SKY_SHOW_GROUND) > 0.5f ? 1.0f : 0.0f, sunVis, moonVis);

    auto texId = [&](int p) { const int x = (int)floorf(V(p) + 0.5f); return (x > 0 && x < (int)textures.size()) ? x : 0; };
    const bool  bodyOn = V(SKY_BODY_ON) > 0.5f;
    const float sunDiscOn  = (bodyOn || (texId(SKY_SUN_TEX) > 0 && V(SKY_SUN_TEX_REPLACE) > 0.5f)) ? 0.0f : 1.0f;
    const float moonDiscOn = (texId(SKY_MOON_TEX) > 0 && V(SKY_MOON_TEX_REPLACE) > 0.5f) ? 0.0f : 1.0f;
    const float moonGlowSize = V(SKY_MOON_GLOW_SIZE) < 0.005f ? 0.005f : V(SKY_MOON_GLOW_SIZE);
    F4(14, V(SKY_MOON_GLOW), 2.0f / moonGlowSize, sunDiscOn, moonDiscOn);

    // moon cutoff: manual, or the Day Of Month placeholder (a simple phase until the calendar exists)
    float cutAmount = V(SKY_MOON_CUT_AMOUNT), cutAngle = V(SKY_MOON_CUT_ANGLE) * d2r;
    if (V(SKY_MOON_PHASE_SRC) > 0.5f)
    {
        const float phase = fmodf(V(SKY_MOON_DAY) - 1.0f, 29.53f) / 29.53f;     // 0 new, 0.5 full
        cutAmount = 0.5f + 0.5f * cosf(2.0f * GHOST_PI * phase);                 // 1 new, 0 full
        cutAngle = (phase < 0.5f) ? GHOST_PI : 0.0f;                             // waxing: dark side on the left
    }
    const float cover = Sat(cutAmount);

    int n = 0;
    memcpy(out[n].sky, common, sizeof common);                     // pass 0 (common[12].x is already 0)
    out[n].textureId = 0;
    n++;

    auto layer = [&](int texP, int layP, int opP, int brP, int tintP, int yawP, float scroll,
        int blkP, int cutP, int softP, int flipP, float weight, const float* extra)
        {
            const int tex = (int)floorf(V(texP) + 0.5f);
            const float op = V(opP) * weight;
            if (tex <= 0 || tex >= (int)textures.size() || op <= 0.002f) return;

            float tint[3];
            C(tintP, tint);
            const float br = V(brP) * bright;

            GhostSkyDraw& d = out[n];
            memcpy(d.sky, common, sizeof common);
            float* L = d.sky;
            L[9 * 4 + 0] = tint[0] * br * extra[0];
            L[9 * 4 + 1] = tint[1] * br * extra[1];
            L[9 * 4 + 2] = tint[2] * br * extra[2];
            L[9 * 4 + 3] = op > 1.0f ? 1.0f : op;
            L[10 * 4 + 0] = floorf(V(layP) + 0.5f);
            L[10 * 4 + 1] = (V(yawP) + scroll) * d2r;
            L[10 * 4 + 2] = V(flipP) > 0.5f ? 1.0f : 0.0f;
            L[10 * 4 + 3] = V(blkP) > 0.5f ? 1.0f : 0.0f;
            L[11 * 4 + 0] = V(cutP);
            L[11 * 4 + 1] = V(softP);
            L[11 * 4 + 2] = (float)textures[(size_t)tex].width;
            L[11 * 4 + 3] = (float)textures[(size_t)tex].height;
            L[12 * 4 + 0] = 1.0f;                                       // layer pass
            d.textureId = tex;
            n++;
        };
    auto sprite = [&](int kind, int texP, int sizeP, int brP, int rotP, int blkP,
        int opP, int haloP, int hsP, int hoP, int hcP, const GhostVec3& dir, float weight) -> GhostSkyDraw*
        {
            const int tex = texId(texP);
            if (tex <= 0 || weight <= 0.002f) return nullptr;
            GhostSkyDraw& d = out[n];
            memcpy(d.sky, common, sizeof common);
            float* L = d.sky;
            const float br = V(brP) * bright;
            const float w = weight > 1.0f ? 1.0f : weight;
            L[9 * 4 + 0] = br; L[9 * 4 + 1] = br; L[9 * 4 + 2] = br;
            L[9 * 4 + 3] = Sat(w * V(opP));
            L[10 * 4 + 1] = V(rotP) * d2r;
            L[10 * 4 + 3] = V(blkP) > 0.5f ? 1.0f : 0.0f;
            L[11 * 4 + 0] = V(haloP);
            L[11 * 4 + 1] = V(hsP);
            L[11 * 4 + 2] = V(hoP);
            L[11 * 4 + 3] = w;
            L[12 * 4 + 0] = (float)kind;
            L[13 * 4 + 0] = dir.x; L[13 * 4 + 1] = dir.y; L[13 * 4 + 2] = dir.z;
            L[13 * 4 + 3] = V(sizeP) * 0.5f * d2r;
            L[14 * 4 + 0] = 99.0f; L[14 * 4 + 1] = 0.0f; L[14 * 4 + 2] = 0.03f; L[14 * 4 + 3] = 0.0f;   // no eclipse unless the caller sets one
            C(hcP, L + 15 * 4);
            d.textureId = tex;
            n++;
            return &d;
        };

    auto glare = [&](int kind, int strP, int sizeP, int overP, int lookP, int colP, const GhostVec3& dir, float weight)
        {
            const float str = V(strP) * weight;
            if (str <= 0.002f || n >= 9) return;
            GhostSkyDraw& d = out[n];
            memcpy(d.sky, common, sizeof common);
            float* L = d.sky;
            C(colP, L + 9 * 4);
            L[9 * 4 + 3] = str;
            L[10 * 4 + 0] = V(sizeP) * d2r;               // glare radius
            L[10 * 4 + 1] = V(overP);                     // overexposure
            L[10 * 4 + 2] = V(lookP);                     // look boost
            L[12 * 4 + 0] = (float)kind;                  // 6 sun glare, 7 moon glare
            L[13 * 4 + 0] = dir.x; L[13 * 4 + 1] = dir.y; L[13 * 4 + 2] = dir.z; L[13 * 4 + 3] = 0.0f;
            d.textureId = 0;
            n++;
        };

    // stars: fade out as the sun comes up. clouds: fade in with the day, warmed by dusk light
    const float nightW = 1.0f - Smooth(-tw, tw * 0.5f, elev);
    const float one[3] = { 1.0f, 1.0f, 1.0f };
    const float warm = duskK * 0.6f;
    const float cloudTint[3] = { Mix(1.0f, dusk[0], warm), Mix(1.0f, dusk[1], warm), Mix(1.0f, dusk[2], warm) };

    layer(SKY_NIGHT_TEX, SKY_NIGHT_LAYOUT, SKY_NIGHT_OPACITY, SKY_NIGHT_BRIGHT, SKY_NIGHT_TINT, SKY_NIGHT_YAW, nightScroll,
        SKY_NIGHT_BLACK, SKY_NIGHT_CUTOFF, SKY_NIGHT_SOFT, SKY_NIGHT_FLIP, nightW, one);

    const float sunW = Smooth(-0.2f, 0.05f, elev) * sunVis;
    const float moonW = Smooth(0.2f, -0.05f, elev) * moonVis;

    sprite(2, SKY_SUN_TEX, SKY_SUN_TEX_SIZE, SKY_SUN_TEX_BRIGHT, SKY_SUN_TEX_ROT, SKY_SUN_TEX_BLACK,
        SKY_SUN_TEX_OPACITY, SKY_SUN_HALO, SKY_SUN_HALO_SIZE, SKY_SUN_HALO_OVERLAP, SKY_SUN_HALO_COLOR, sunDir, sunW);

    if (bodyOn && sunW > 0.002f)                                  // sun body: plasma, volume, refraction, caustics, subsurface
    {
        GhostSkyDraw& d = out[n];
        memcpy(d.sky, common, sizeof common);
        float* L = d.sky;
        const float br = V(SKY_BODY_BRIGHT) * bright;
        L[9 * 4 + 0] = sunC[0] * br; L[9 * 4 + 1] = sunC[1] * br; L[9 * 4 + 2] = sunC[2] * br;
        L[9 * 4 + 3] = sunW > 1.0f ? 1.0f : sunW;
        L[10 * 4 + 0] = timeSec;
        L[10 * 4 + 1] = V(SKY_PLASMA_SPEED);
        L[10 * 4 + 2] = V(SKY_PLASMA);
        L[10 * 4 + 3] = (float)texId(SKY_HEAT_TEX);
        L[11 * 4 + 0] = V(SKY_SCATTER);
        L[11 * 4 + 1] = V(SKY_SCATTER_DENSITY);
        L[11 * 4 + 2] = V(SKY_REFRACT);
        L[11 * 4 + 3] = V(SKY_CAUSTIC);
        L[12 * 4 + 0] = 5.0f;
        L[13 * 4 + 0] = sunDir.x; L[13 * 4 + 1] = sunDir.y; L[13 * 4 + 2] = sunDir.z;
        L[13 * 4 + 3] = V(SKY_BODY_SIZE) * 0.5f * d2r;
        L[14 * 4 + 0] = V(SKY_PLASMA_SCALE);
        L[14 * 4 + 1] = V(SKY_CAUSTIC_SCALE);
        L[14 * 4 + 2] = V(SKY_CAUSTIC_SPEED);
        L[14 * 4 + 3] = V(SKY_HEAT_STRENGTH);
        L[15 * 4 + 0] = V(SKY_HEAT_TILING);
        L[15 * 4 + 1] = V(SKY_SSS);
        L[15 * 4 + 2] = V(SKY_BODY_EDGE);
        L[15 * 4 + 3] = V(SKY_BODY_LIMB);
        C(SKY_SSS_COLOR, L + 8 * 4);
        d.textureId = 0;
        n++;
    }

    GhostSkyDraw* moonD = sprite(3, SKY_MOON_TEX, SKY_MOON_TEX_SIZE, SKY_MOON_TEX_BRIGHT, SKY_MOON_TEX_ROT, SKY_MOON_TEX_BLACK,
        SKY_MOON_TEX_OPACITY, SKY_MOON_HALO, SKY_MOON_HALO_SIZE, SKY_MOON_HALO_OVERLAP, SKY_MOON_HALO_COLOR, moonDir, moonW);
    if (moonD)                                                    // eclipse / phase cutoff
    {
        float* L = moonD->sky;
        const float soft = V(SKY_MOON_CUT_SOFT);
        L[14 * 4 + 0] = cutAmount <= 0.001f ? 99.0f : (1.0f - cover) * (2.0f + 2.0f * soft);   // occluder distance from the moon's centre
        L[14 * 4 + 1] = cutAngle;
        L[14 * 4 + 2] = soft;
        L[14 * 4 + 3] = (float)texId(SKY_MOON_CUT_TEX);
    }

    layer(SKY_DAY_TEX, SKY_DAY_LAYOUT, SKY_DAY_OPACITY, SKY_DAY_BRIGHT, SKY_DAY_TINT, SKY_DAY_YAW, dayScroll,
        SKY_DAY_BLACK, SKY_DAY_CUTOFF, SKY_DAY_SOFT, SKY_DAY_FLIP, dayAmt, cloudTint);

    // ---- clouds (unchanged) ----
    const int ctex = (int)floorf(V(SKY_CLOUD_TEX) + 0.5f);
    if (ctex > 0 && ctex < (int)textures.size())
    {
        const bool  nightBlend = V(SKY_CLOUD_NIGHT_BLEND) > 0.5f;
        const float cw = nightBlend ? 1.0f : dayAmt;
        const float dim = nightBlend ? Mix(V(SKY_CLOUD_NIGHT_BRIGHT), 1.0f, dayAmt) : 1.0f;
        const float op = V(SKY_CLOUD_OPACITY) * cw;
        if (op > 0.002f)
        {
            float tint[3];
            C(SKY_CLOUD_TINT, tint);
            const float br = V(SKY_CLOUD_BRIGHT) * bright * dim;
            GhostSkyDraw& d = out[n];
            memcpy(d.sky, common, sizeof common);
            float* L = d.sky;
            L[9 * 4 + 0] = tint[0] * cloudTint[0] * br;
            L[9 * 4 + 1] = tint[1] * cloudTint[1] * br;
            L[9 * 4 + 2] = tint[2] * cloudTint[2] * br;
            L[9 * 4 + 3] = op > 1.0f ? 1.0f : op;
            L[10 * 4 + 0] = floorf(V(SKY_CLOUD_DENSITY_SRC) + 0.5f);
            L[10 * 4 + 1] = cloudU;
            L[10 * 4 + 2] = cloudV;
            L[10 * 4 + 3] = V(SKY_CLOUD_COVERAGE);
            L[11 * 4 + 0] = V(SKY_CLOUD_TILING);
            L[11 * 4 + 1] = V(SKY_CLOUD_HORIZON);
            L[11 * 4 + 2] = V(SKY_CLOUD_VARIATION);
            L[11 * 4 + 3] = V(SKY_CLOUD_VAR_FREQ);
            L[12 * 4 + 0] = 4.0f;
            d.textureId = ctex;
            n++;
        }
    }

    // ---- glares last, over everything (like bloom). An eclipsed moon glares less ----
    glare(6, SKY_SUN_GLARE, SKY_SUN_GLARE_SIZE, SKY_SUN_GLARE_OVEREXP, SKY_SUN_GLARE_LOOK, SKY_SUN_GLARE_COLOR, sunDir, sunW);
    glare(7, SKY_MOON_GLARE, SKY_MOON_GLARE_SIZE, SKY_MOON_GLARE_OVEREXP, SKY_MOON_GLARE_LOOK, SKY_MOON_GLARE_COLOR, moonDir, moonW * (1.0f - 0.8f * cover));
    return n;
}