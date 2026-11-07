// GhostVideoSettings.h: the video options shared by the engine, the editor and the game.
// Plain C layout (no classes, no constexpr) so GhostEngine.h and the C# mirror stay in sync.
// C# mirror: GhostVideoSettings in GhostEngine.cs. Field order MUST match.
#pragma once

#define GHOST_VIDEO_MAX_DIM   4096      // largest render width / height (matches the camera limit)
#define GHOST_VIDEO_MIN_DIM   16

// displayMode
#define GHOST_DISPLAY_WINDOWED     0    // normal window with a border
#define GHOST_DISPLAY_BORDERLESS   1    // no border, fills the monitor's work area (taskbar stays visible)
#define GHOST_DISPLAY_FULLSCREEN   2    // no border, covers the whole monitor

// scaleFilter: used when the render size differs from the window
#define GHOST_FILTER_LINEAR   0
#define GHOST_FILTER_POINT    1

// fit: how a fixed-size image is placed in the window
#define GHOST_FIT_LETTERBOX   0         // keep aspect, black bars
#define GHOST_FIT_INTEGER     1         // keep aspect, whole-number scale when it is >= 1x (pixel perfect)
#define GHOST_FIT_STRETCH     2         // fill the window, aspect ignored

typedef struct GhostVideoSettings
{
    int resW, resH;       // 0, 0 = Match Window. Otherwise the main view renders at this size
    int displayMode;      // GHOST_DISPLAY_*
    int scaleFilter;      // GHOST_FILTER_*
    int fit;              // GHOST_FIT_*
} GhostVideoSettings;

// Fixed list offered by the dropdowns. Monitor modes are added on top by GhostEngine_GetResolutionList.
#define GHOST_RES_PRESET_COUNT 12
static const int kGhostResPresets[GHOST_RES_PRESET_COUNT][2] =
{
    { 640,  360 },{ 800,  600 },{ 1024,  768 },{ 1280,  720 },
    { 1280,  800 },{ 1366,  768 },{ 1600,  900 },{ 1920, 1080 },
    { 2560, 1080 },{ 2560, 1440 },{ 3440, 1440 },{ 3840, 2160 },
};

static inline void GhostVideo_Sanitize(GhostVideoSettings* v)
{
    if (v->resW < GHOST_VIDEO_MIN_DIM || v->resH < GHOST_VIDEO_MIN_DIM) { v->resW = 0; v->resH = 0; }
    if (v->resW > GHOST_VIDEO_MAX_DIM) v->resW = GHOST_VIDEO_MAX_DIM;
    if (v->resH > GHOST_VIDEO_MAX_DIM) v->resH = GHOST_VIDEO_MAX_DIM;
    if (v->displayMode < 0 || v->displayMode > 2) v->displayMode = GHOST_DISPLAY_WINDOWED;
    if (v->scaleFilter < 0 || v->scaleFilter > 1) v->scaleFilter = GHOST_FILTER_LINEAR;
    if (v->fit < 0 || v->fit > 2) v->fit = GHOST_FIT_LETTERBOX;
}