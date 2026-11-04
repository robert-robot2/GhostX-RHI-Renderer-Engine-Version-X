// GhostUIScale.h — UI scale modes and the layout-space math.
// GhostUIDoc.cs mirrors this exactly (UIView.Compute), so the designer preview matches the engine.
#pragma once

constexpr float GHOST_UI_REF_H = 1080.0f;          // the reference height never changes

enum GhostUIScaleMode
{
    GHOST_UI_SCALE_FIT_HEIGHT = 0,   // scale 1, width expands (the original behavior)
    GHOST_UI_SCALE_EXPAND = 1,   // uniform min(fitW, fitH), layout fills the whole screen
    GHOST_UI_SCALE_FIT_WIDTH = 2,   // uniform to the reference width, height flexes
    GHOST_UI_SCALE_LETTERBOX = 3,   // refW x 1080 scaled to fit and centered
    GHOST_UI_SCALE_STRETCH = 4,   // refW x 1080 stretched to the screen
    GHOST_UI_SCALE_COUNT
};

// Anchors resolve inside layout space (lw x lh). A layout point (x, y) lands on the screen at
// (ox + x * sx, oy + y * sy). The screen is screenW x screenH reference units (screenH is always 1080).
struct GhostUIView
{
    float lw, lh;
    float sx, sy;
    float ox, oy;
};

inline GhostUIView GhostUI_ComputeView(int mode, float refW, float screenW, float screenH)
{
    if (!(refW >= 1.0f))    refW = 1920.0f;
    if (!(screenW >= 1.0f)) screenW = 1.0f;
    if (!(screenH >= 1.0f)) screenH = 1.0f;

    const float H = GHOST_UI_REF_H;
    const float fw = screenW / refW, fh = screenH / H;
    const float fit = fw < fh ? fw : fh;

    GhostUIView v = {};
    switch (mode)
    {
    case GHOST_UI_SCALE_EXPAND:
        v = { screenW / fit, screenH / fit, fit, fit, 0.0f, 0.0f };
        break;
    case GHOST_UI_SCALE_FIT_WIDTH:
        v = { refW, screenH / fw, fw, fw, 0.0f, 0.0f };
        break;
    case GHOST_UI_SCALE_LETTERBOX:
        v = { refW, H, fit, fit, (screenW - refW * fit) * 0.5f, (screenH - H * fit) * 0.5f };
        break;
    case GHOST_UI_SCALE_STRETCH:
        v = { refW, H, fw, fh, 0.0f, 0.0f };
        break;
    default:                                              // FIT_HEIGHT
        v = { screenW / fh, H, fh, fh, 0.0f, 0.0f };
        break;
    }
    return v;
}