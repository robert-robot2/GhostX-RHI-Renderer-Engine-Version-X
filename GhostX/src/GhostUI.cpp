#include "../include/GhostUI.h"
#include <math.h>

static float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

static void Axis(float pp, float ps, float amin, float amax, float off, float sz, float piv, float& pos, float& len)
{
    if (fabsf(amax - amin) < 1e-5f)
    {
        len = sz;
        pos = pp + ps * amin + off - piv * sz;
    }
    else
    {
        float l = pp + ps * amin + off;
        float r = pp + ps * amax - sz;
        pos = l;
        len = r - l;
        if (len < 0.0f) len = 0.0f;
    }
}

void GhostUI_Layout(GhostUIAsset& a, float W, float H)
{
    for (size_t i = 0; i < a.widgets.size(); i++)
    {
        GhostWidget& w = a.widgets[i];
        float px = 0, py = 0, pw = W, ph = H;
        bool pvis = true;
        if (w.parent >= 0 && w.parent < (int)i)
        {
            const GhostWidget& p = a.widgets[(size_t)w.parent];
            px = p.rect[0]; py = p.rect[1]; pw = p.rect[2]; ph = p.rect[3];
            pvis = p.eff;
        }
        w.eff = pvis && w.visible;
        if (!w.eff) { w.hovered = false; w.pressed = false; }

        Axis(px, pw, w.anchorMin[0], w.anchorMax[0], w.offset[0], w.size[0], w.pivot[0], w.rect[0], w.rect[2]);
        Axis(py, ph, w.anchorMin[1], w.anchorMax[1], w.offset[1], w.size[1], w.pivot[1], w.rect[1], w.rect[3]);
    }
}

static void PushRect(std::vector<GhostUIQuad>& out, float x, float y, float w, float h, const float* c, int tex)
{
    if (out.size() >= GHOST_UI_MAX_QUADS || w <= 0.0f || h <= 0.0f || c[3] <= 0.001f) return;
    GhostUIQuad q;
    q.x = x; q.y = y; q.w = w; q.h = h;
    q.u0 = 0.0f; q.v0 = 0.0f; q.u1 = 1.0f; q.v1 = 1.0f;
    q.color[0] = c[0]; q.color[1] = c[1]; q.color[2] = c[2]; q.color[3] = c[3];
    q.textureId = tex;
    out.push_back(q);
}

static void PushText(std::vector<GhostUIQuad>& out, const GhostFont& font, const GhostWidget& w,
    int align, bool centerV, const float* col)
{
    if (w.text.empty() || col[3] <= 0.001f) return;
    const float tw = GhostText_Measure(font, w.fontSize, w.text.c_str());
    float tx = w.rect[0];
    if (align == 1) tx += (w.rect[2] - tw) * 0.5f;
    else if (align == 2) tx += w.rect[2] - tw;
    const float ty = centerV ? w.rect[1] + (w.rect[3] - w.fontSize) * 0.5f : w.rect[1];

    if (w.shadow)
    {
        const float sc[4] = { 0.0f, 0.0f, 0.0f, col[3] * 0.65f };
        GhostText_Draw(out, font, tx + w.fontSize * 0.05f, ty + w.fontSize * 0.05f, w.fontSize, w.text.c_str(), sc);
    }
    GhostText_Draw(out, font, tx, ty, w.fontSize, w.text.c_str(), col);
}

void GhostUI_Build(const GhostUIAsset& a, const GhostFont& font, std::vector<GhostUIQuad>& out)
{
    if (!a.shown) return;
    for (const GhostWidget& w : a.widgets)
    {
        if (!w.eff) continue;
        switch (w.type)
        {
        case GHOST_W_CANVAS:
            PushRect(out, w.rect[0], w.rect[1], w.rect[2], w.rect[3], w.color, 0);
            break;
        case GHOST_W_IMAGE:
            PushRect(out, w.rect[0], w.rect[1], w.rect[2], w.rect[3], w.color, w.textureId);
            break;
        case GHOST_W_TEXT:
            PushText(out, font, w, w.align, false, w.color);
            break;
        case GHOST_W_BUTTON:
        {
            const float* face = w.pressed ? w.pressColor : (w.hovered ? w.hoverColor : w.color);
            PushRect(out, w.rect[0], w.rect[1], w.rect[2], w.rect[3], face, 0);
            PushText(out, font, w, 1, true, w.textColor);            // button labels are always centered
            break;
        }
        case GHOST_W_PROGRESS:
        {
            PushRect(out, w.rect[0], w.rect[1], w.rect[2], w.rect[3], w.bgColor, 0);
            PushRect(out, w.rect[0], w.rect[1], w.rect[2] * Clamp01(w.value), w.rect[3], w.color, 0);
            break;
        }
        }
    }
}

void GhostUI_Mouse(GhostUIAsset& a, int ev, float mx, float my, bool inside)
{
    int hit = -1;
    if (a.shown && inside && ev != 3)
        for (int i = (int)a.widgets.size() - 1; i >= 0; i--)          // topmost button first
        {
            const GhostWidget& w = a.widgets[(size_t)i];
            if (w.type != GHOST_W_BUTTON || !w.eff) continue;
            if (mx >= w.rect[0] && mx < w.rect[0] + w.rect[2] && my >= w.rect[1] && my < w.rect[1] + w.rect[3])
            {
                hit = i; break;
            }
        }

    for (size_t i = 0; i < a.widgets.size(); i++) a.widgets[i].hovered = ((int)i == hit);

    if (ev == 1)
    {
        for (size_t i = 0; i < a.widgets.size(); i++) a.widgets[i].pressed = ((int)i == hit);
    }
    else if (ev == 2)
    {
        for (size_t i = 0; i < a.widgets.size(); i++)
        {
            GhostWidget& w = a.widgets[i];
            if (!w.pressed) continue;
            if ((int)i == hit) w.clicked = true;                         // released over the same button
            w.pressed = false;
        }
    }
}