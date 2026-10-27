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

int GhostSky_Build(const GhostModEntity& e, const std::vector<GhostTextureInfo>& textures,
    float dayScroll, float nightScroll, GhostSkyDraw out[3])
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

    // stars: fade out as the sun comes up. clouds: fade in with the day, warmed by dusk light
    const float nightW = 1.0f - Smooth(-tw, tw * 0.5f, elev);
    const float one[3] = { 1.0f, 1.0f, 1.0f };
    const float warm = duskK * 0.6f;
    const float cloudTint[3] = { Mix(1.0f, dusk[0], warm), Mix(1.0f, dusk[1], warm), Mix(1.0f, dusk[2], warm) };

    layer(SKY_NIGHT_TEX, SKY_NIGHT_LAYOUT, SKY_NIGHT_OPACITY, SKY_NIGHT_BRIGHT, SKY_NIGHT_TINT, SKY_NIGHT_YAW, nightScroll,
        SKY_NIGHT_BLACK, SKY_NIGHT_CUTOFF, SKY_NIGHT_SOFT, SKY_NIGHT_FLIP, nightW, one);
    layer(SKY_DAY_TEX, SKY_DAY_LAYOUT, SKY_DAY_OPACITY, SKY_DAY_BRIGHT, SKY_DAY_TINT, SKY_DAY_YAW, dayScroll,
        SKY_DAY_BLACK, SKY_DAY_CUTOFF, SKY_DAY_SOFT, SKY_DAY_FLIP, dayAmt, cloudTint);
    return n;
}