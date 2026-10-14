#pragma once
#include "GhostScene.h"

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
struct GhostSkyDraw { float sky[64]; int textureId; };          // 16 float4, copied into the draw's constant buffer

// Fills up to 3 draws: [0] gradient + sun + moon, then the night layer, then the day layer.
// Returns how many to draw (0 when the sky is disabled).
int   GhostSky_Build(const GhostModEntity& e, const std::vector<GhostTextureInfo>& textures,
    float dayScrollDeg, float nightScrollDeg, GhostSkyDraw out[3]);
float GhostSky_GetTime(const GhostModEntity& e);
void  GhostSky_SetTime(GhostModEntity& e, float hours);
void  GhostSky_AdvanceTime(GhostModEntity& e, float dtSeconds);
void  GhostSky_Speeds(const GhostModEntity& e, float& dayDegPerSec, float& nightDegPerSec);