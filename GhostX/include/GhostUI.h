#pragma once
#include "GhostText.h"
#include "GhostSceneIO.h"        // GhostLoadTexFn
#include "GhostUIScale.h"
#include <string>
#include <vector>

// GhostUI.h

constexpr size_t GHOST_UI_MAX_WIDGETS = 512;

enum GhostWidgetType { GHOST_W_CANVAS = 0, GHOST_W_IMAGE, GHOST_W_TEXT, GHOST_W_BUTTON, GHOST_W_PROGRESS, GHOST_W_COUNT };

// Layout (per axis): if anchorMin == anchorMax the widget has a fixed size, placed at
//   parent.pos + parent.size * anchor + offset - pivot * size.
// If they differ the widget stretches: offset = left/top margin, size = right/bottom margin.
struct GhostWidget
{
    std::string name = "Widget";
    int   type = GHOST_W_CANVAS;
    int   parent = -1;                            // index in the asset's list (always lower than this widget's), -1 = root
    float anchorMin[2] = { 0, 0 }, anchorMax[2] = { 0, 0 };
    float offset[2] = { 0, 0 };
    float size[2] = { 100, 40 };
    float pivot[2] = { 0, 0 };
    float color[4] = { 1, 1, 1, 1 };              // Text: the text color. Button: the face. Image: tint. ProgressBar: the fill
    float textColor[4] = { 1, 1, 1, 1 };          // Button label
    float hoverColor[4] = { 1, 1, 1, 1 };         // Button
    float pressColor[4] = { 1, 1, 1, 1 };         // Button
    float bgColor[4] = { 0, 0, 0, 0.5f };         // ProgressBar background
    bool  visible = true;
    std::string text;
    float fontSize = 32.0f;
    int   align = 0;                              // 0 left, 1 center, 2 right
    bool  shadow = true;
    std::string texture;                          // Image: project-relative path
    int   textureId = 0;
    float value = 1.0f;                           // ProgressBar 0..1

    // runtime
    float rect[4] = { 0, 0, 0, 0 };               // x, y, w, h in reference units
    float lrect[4] = { 0, 0, 0, 0 };              // x, y, w, h in LAYOUT space (what children resolve against)
    float drawScale = 1.0f;                       // layout -> screen scale, used for text size
    bool  eff = true;                             // visible, and so are all its ancestors
    bool  hovered = false, pressed = false, clicked = false;
};

struct GhostUIAsset
{
    std::string name, path;
    std::vector<GhostWidget> widgets;
    bool shown = true;
    float refW = 1920.0f;                         // the width the UI was designed for
    int   scaleMode = GHOST_UI_SCALE_FIT_HEIGHT;  // GhostUIScaleMode. Files without the key keep the old behavior
    float safeArea = 0.0f;                        // 0..0.2: margin on every side, as a fraction of the layout size
    GhostUIView view = { 1920.0f, 1080.0f, 1.0f, 1.0f, 0.0f, 0.0f };   // set by GhostUI_Layout
};

bool GhostUI_Load(GhostUIAsset& a, const std::string& projectRoot, const std::string& absFile,
    const GhostLoadTexFn& loadTex, std::string& err, std::string& warn);
bool GhostUI_Save(const GhostUIAsset& a, const std::string& absFile, std::string& err);

// W x H = the real screen in reference units (H is always 1080). Applies the asset's scale mode and safe area.
void GhostUI_Layout(GhostUIAsset& a, float W, float H);
void GhostUI_Build(const GhostUIAsset& a, const GhostFont& font, std::vector<GhostUIQuad>& out);
// ev: 0 move, 1 left down, 2 left up, 3 leave. mx, my in reference units.
void GhostUI_Mouse(GhostUIAsset& a, int ev, float mx, float my, bool inside);