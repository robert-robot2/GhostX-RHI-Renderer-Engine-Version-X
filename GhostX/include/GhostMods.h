#pragma once
#include "GhostScene.h"

//GhostMods.h

enum GhostModTypeId { GHOST_MOD_SKY = 0, GHOST_MOD_COUNT };

// Order must match the table in GhostMods.cpp
enum SkyParam
{
    SKY_ENABLED = 0,
    SKY_TIME, SKY_SPEED, SKY_TWILIGHT, SKY_SUN_YAW,
    SKY_DAY_ZENITH, SKY_DAY_HORIZON, SKY_NIGHT_ZENITH, SKY_NIGHT_HORIZON,
    SKY_HORIZON_HEIGHT, SKY_HORIZON_WIDTH, SKY_GRAD_EXP, SKY_GROUND, SKY_GROUND_BLEND, SKY_DUSK, SKY_DUSK_STRENGTH,
    SKY_SUN_COLOR, SKY_SUN_SIZE, SKY_SUN_INTENSITY, SKY_SUN_GLOW, SKY_SUN_GLOW_SIZE,
    SKY_MOON_COLOR, SKY_MOON_SIZE, SKY_MOON_INTENSITY,
    SKY_BRIGHTNESS,
    SKY_DAY_TEX, SKY_DAY_LAYOUT, SKY_DAY_OPACITY, SKY_DAY_BRIGHT, SKY_DAY_TINT, SKY_DAY_YAW,
    SKY_DAY_SCROLL, SKY_DAY_BLACK, SKY_DAY_CUTOFF, SKY_DAY_SOFT, SKY_DAY_FLIP,
    SKY_NIGHT_TEX, SKY_NIGHT_LAYOUT, SKY_NIGHT_OPACITY, SKY_NIGHT_BRIGHT, SKY_NIGHT_TINT, SKY_NIGHT_YAW,
    SKY_NIGHT_SCROLL, SKY_NIGHT_BLACK, SKY_NIGHT_CUTOFF, SKY_NIGHT_SOFT, SKY_NIGHT_FLIP,
    SKY_SHOW_GROUND,
    SKY_AMB_ENABLE, SKY_AMB_START, SKY_AMB_END, SKY_AMB_FADE,
    SKY_AMB_LEVEL,
    SKY_AMB_AUTO,
    SKY_AMB_HOUR0,
    SKY_AMB_HOUR23 = SKY_AMB_HOUR0 + 23,          // 24 consecutive hour values
    SKY_MOON_GLOW, SKY_MOON_GLOW_SIZE,
    SKY_SUN_TEX, SKY_SUN_TEX_SIZE, SKY_SUN_TEX_BRIGHT, SKY_SUN_TEX_ROT, SKY_SUN_TEX_BLACK, SKY_SUN_TEX_REPLACE,
    SKY_MOON_TEX, SKY_MOON_TEX_SIZE, SKY_MOON_TEX_BRIGHT, SKY_MOON_TEX_ROT, SKY_MOON_TEX_BLACK, SKY_MOON_TEX_REPLACE,
    SKY_CLOUD_TEX, SKY_CLOUD_OPACITY, SKY_CLOUD_COVERAGE, SKY_CLOUD_DENSITY_SRC, SKY_CLOUD_BRIGHT, SKY_CLOUD_TINT,
    SKY_CLOUD_TILING, SKY_CLOUD_HORIZON, SKY_CLOUD_SCROLL_U, SKY_CLOUD_SCROLL_V,
    SKY_CLOUD_VARIATION, SKY_CLOUD_VAR_FREQ, SKY_CLOUD_NIGHT_BLEND, SKY_CLOUD_NIGHT_BRIGHT,
    SKY_SUN_TEX_OPACITY, SKY_SUN_HALO, SKY_SUN_HALO_SIZE, SKY_SUN_HALO_OVERLAP, SKY_SUN_HALO_COLOR,
    SKY_MOON_TEX_OPACITY, SKY_MOON_HALO, SKY_MOON_HALO_SIZE, SKY_MOON_HALO_OVERLAP, SKY_MOON_HALO_COLOR,
    SKY_PARAM_COUNT
};

struct GhostModType
{
    const char* name;          // shown in the palette
    const char* saveName;      // used in scene files
    int                   maxPerScene;   // 0 = unlimited
    const GhostParamDesc* params;
    int                   paramCount;
};

const GhostModType* GhostMods_Type(int typeId);                 // nullptr if unknown
int   GhostMods_FindByName(const char* saveName);               // -1 if unknown
int   GhostMods_ParamOffset(int typeId, int param);             // index into GhostModEntity::values
int   GhostMods_ValueCount(int typeId);
void  GhostMods_Defaults(int typeId, std::vector<float>& out);
void  GhostMods_Clamp(int typeId, int param, float* v);         // v = 1 or 3 floats, edited in place
inline int GhostParam_Count(const GhostParamDesc& d) { return d.kind == GHOST_PARAM_COLOR ? 3 : 1; }

// nullptr at the per-type or total limit
GhostModEntity* GhostScene_AddMod(GhostScene& s, int typeId);

// ---- Sky ----
struct GhostSkyDraw { float sky[64]; int textureId; };

// Up to 6 draws: gradient, stars, sun sprite, moon sprite, day layer, clouds
int   GhostSky_Build(const GhostModEntity& e, const std::vector<GhostTextureInfo>& textures,
    float dayScrollDeg, float nightScrollDeg, float cloudU, float cloudV, GhostSkyDraw out[6]);
void  GhostSky_CloudSpeeds(const GhostModEntity& e, float& uPerSec, float& vPerSec);


float GhostSky_GetTime(const GhostModEntity& e);
void  GhostSky_SetTime(GhostModEntity& e, float hours);
void  GhostSky_AdvanceTime(GhostModEntity& e, float dtSeconds);
void  GhostSky_Speeds(const GhostModEntity& e, float& dayDegPerSec, float& nightDegPerSec);
float GhostSky_AmbientIntensity(const GhostModEntity& e, float baseIntensity);