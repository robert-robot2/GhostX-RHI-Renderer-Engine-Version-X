#pragma once
// GhostOverlay.h — CPU-side builder for viewport overlays (grid, gizmos, dimensions, text).
// Everything becomes screen-space triangles (thick lines) in clip space, drawn by GhostDrawOverlay.
#include "GhostMath.h"
#include "GhostText.h"
#include <vector>
#include <math.h>
#include <stdio.h>
#include <string.h>

struct GhostOverlayVertex { float x, y, z, u, v, r, g, b, a; };      // 36 bytes: clip-space position, uv, color

class GhostOverlay
{
public:
    struct Clip { float x, y, z, w; };
    struct SP { float x, y, z; bool ok; };               // pixels (y down) + NDC depth

    std::vector<GhostOverlayVertex> depth;                  // depth tested (the grid)
    std::vector<GhostOverlayVertex> top;                    // always on top (gizmos, dimensions)
    std::vector<GhostOverlayVertex> text;                   // font texture, always on top
    unsigned int w = 1, h = 1;

    void Begin(const GhostMat4& view, const GhostMat4& proj, unsigned int rw, unsigned int rh, const GhostFont* f)
    {
        depth.clear(); top.clear(); text.clear();
        vp = proj * view;
        w = rw ? rw : 1;
        h = rh ? rh : 1;
        font = f;
    }

    Clip ToClip(const GhostVec3& p) const
    {
        Clip c;
        c.x = vp.m[0][0] * p.x + vp.m[0][1] * p.y + vp.m[0][2] * p.z + vp.m[0][3];
        c.y = vp.m[1][0] * p.x + vp.m[1][1] * p.y + vp.m[1][2] * p.z + vp.m[1][3];
        c.z = vp.m[2][0] * p.x + vp.m[2][1] * p.y + vp.m[2][2] * p.z + vp.m[2][3];
        c.w = vp.m[3][0] * p.x + vp.m[3][1] * p.y + vp.m[3][2] * p.z + vp.m[3][3];
        return c;
    }

    SP Proj(const GhostVec3& p) const
    {
        const Clip c = ToClip(p);
        if (c.z < 0.0f || c.w <= 1e-5f) return { 0.0f, 0.0f, 0.0f, false };
        return { (c.x / c.w * 0.5f + 0.5f) * (float)w, (0.5f - c.y / c.w * 0.5f) * (float)h, c.z / c.w, true };
    }

    // A world-space line, clipped to the near plane, drawn as a quad of widthPx pixels
    void Line(const GhostVec3& a, const GhostVec3& b, const float* c, float widthPx, bool depthTest)
    {
        Clip ca = ToClip(a), cb = ToClip(b);
        if (!ClipNear(ca, cb)) return;
        Seg(depthTest ? depth : top, PxX(ca), PxY(ca), ca.z / ca.w, PxX(cb), PxY(cb), cb.z / cb.w, c, widthPx);
    }

    // a -> b with an arrowhead (a filled triangle, headPx long) at b
    void Arrow(const GhostVec3& a, const GhostVec3& b, const float* c, float widthPx, float headPx, bool depthTest)
    {
        const SP pa = Proj(a), pb = Proj(b);
        if (!pa.ok || !pb.ok) { Line(a, b, c, widthPx, depthTest); return; }
        float dx = pb.x - pa.x, dy = pb.y - pa.y;
        const float len = sqrtf(dx * dx + dy * dy);
        if (len < 1.0f) return;
        dx /= len; dy /= len;
        std::vector<GhostOverlayVertex>& v = depthTest ? depth : top;
        const float shaft = len > headPx ? len - headPx * 0.8f : len * 0.2f;
        Seg(v, pa.x, pa.y, pa.z, pa.x + dx * shaft, pa.y + dy * shaft, pb.z, c, widthPx);
        HeadAt(v, pb.x, pb.y, pb.z, dx, dy, headPx, c);
    }

    // A CAD dimension line: arrowheads at both ends pointing at the extension lines
    void Dimension(const GhostVec3& a, const GhostVec3& b, const float* c, float widthPx, float headPx)
    {
        const SP pa = Proj(a), pb = Proj(b);
        if (!pa.ok || !pb.ok) { Line(a, b, c, widthPx, false); return; }
        float dx = pb.x - pa.x, dy = pb.y - pa.y;
        const float len = sqrtf(dx * dx + dy * dy);
        if (len < 1.0f) return;
        dx /= len; dy /= len;
        Seg(top, pa.x, pa.y, pa.z, pb.x, pb.y, pb.z, c, widthPx);
        if (len > headPx * 2.2f)
        {
            HeadAt(top, pa.x, pa.y, pa.z, -dx, -dy, headPx, c);
            HeadAt(top, pb.x, pb.y, pb.z, dx, dy, headPx, c);
        }
        else                                                // too short: the heads sit outside, pointing in
        {
            HeadAt(top, pa.x, pa.y, pa.z, dx, dy, headPx, c);
            HeadAt(top, pb.x, pb.y, pb.z, -dx, -dy, headPx, c);
        }
    }

    // A square of sizePx pixels centered on a world point
    void Point(const GhostVec3& p, float sizePx, const float* c)
    {
        const SP s = Proj(p);
        if (!s.ok) return;
        const float hs = sizePx * 0.5f;
        Tri(top, s.x - hs, s.y - hs, s.z, s.x + hs, s.y - hs, s.z, s.x + hs, s.y + hs, s.z, c);
        Tri(top, s.x - hs, s.y - hs, s.z, s.x + hs, s.y + hs, s.z, s.x - hs, s.y + hs, s.z, c);
    }

    // Text with its top-left at a pixel position
    void TextPx(float px, float py, float sizePx, const char* s, const float* c)
    {
        if (!font || !font->ready || !s) return;
        std::vector<GhostUIQuad> q;
        const float sc[4] = { 0.0f, 0.0f, 0.0f, c[3] * 0.85f };
        GhostText_Draw(q, *font, px + 1.0f, py + 1.0f, sizePx, s, sc);
        GhostText_Draw(q, *font, px, py, sizePx, s, c);
        for (const GhostUIQuad& g : q)
        {
            const float col[4] = { g.color[0], g.color[1], g.color[2], g.color[3] };
            const float x0 = g.x, y0 = g.y, x1 = g.x + g.w, y1 = g.y + g.h;
            V(text, x0, y0, 0.0f, col, g.u0, g.v0); V(text, x1, y0, 0.0f, col, g.u1, g.v0); V(text, x1, y1, 0.0f, col, g.u1, g.v1);
            V(text, x0, y0, 0.0f, col, g.u0, g.v0); V(text, x1, y1, 0.0f, col, g.u1, g.v1); V(text, x0, y1, 0.0f, col, g.u0, g.v1);
        }
    }

    
    // Text centered on a world point, with a pixel offset. rect4 (optional) receives x, y, w, h in pixels.
    bool Label(const GhostVec3& p, const char* s, const float* c, float sizePx, float dx, float dy, float* rect4 = nullptr)
    {
        const SP sp = Proj(p);
        if (!sp.ok || !font || !font->ready) return false;
        const float tw = GhostText_Measure(*font, sizePx, s);
        const float x = sp.x - tw * 0.5f + dx, y = sp.y - sizePx * 0.5f + dy;
        if (rect4) { rect4[0] = x - 4.0f; rect4[1] = y - 2.0f; rect4[2] = tw + 8.0f; rect4[3] = sizePx + 4.0f; }
        TextPx(x, y, sizePx, s, c);
        return true;
    }

private:
    GhostMat4 vp;
    const GhostFont* font = nullptr;

    float PxX(const Clip& c) const { return (c.x / c.w * 0.5f + 0.5f) * (float)w; }
    float PxY(const Clip& c) const { return (0.5f - c.y / c.w * 0.5f) * (float)h; }

    // Clips a segment to the near plane (clip z >= 0). false = nothing left.
    static bool ClipNear(Clip& a, Clip& b)
    {
        const bool ia = a.z >= 0.0f, ib = b.z >= 0.0f;
        if (!ia && !ib) return false;
        if (ia && ib) return true;
        const float t = a.z / (a.z - b.z);
        const Clip m = { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, 0.0f, a.w + (b.w - a.w) * t };
        if (ia) b = m; else a = m;
        return true;
    }

    // px, py are pixels (y down); the vertex stores clip space
    void V(std::vector<GhostOverlayVertex>& v, float px, float py, float z, const float* c, float u = 0.0f, float tv = 0.0f) const
    {
        v.push_back({ px * 2.0f / (float)w - 1.0f, 1.0f - py * 2.0f / (float)h, z, u, tv, c[0], c[1], c[2], c[3] });
    }

    void Tri(std::vector<GhostOverlayVertex>& v, float x0, float y0, float z0, float x1, float y1, float z1,
        float x2, float y2, float z2, const float* c) const
    {
        V(v, x0, y0, z0, c); V(v, x1, y1, z1, c); V(v, x2, y2, z2, c);
    }

    void Seg(std::vector<GhostOverlayVertex>& v, float x0, float y0, float z0, float x1, float y1, float z1,
        const float* c, float width) const
    {
        float dx = x1 - x0, dy = y1 - y0;
        const float len = sqrtf(dx * dx + dy * dy);
        if (len < 1e-3f) return;
        dx /= len; dy /= len;
        const float hw = width * 0.5f, nx = -dy * hw, ny = dx * hw;
        Tri(v, x0 + nx, y0 + ny, z0, x0 - nx, y0 - ny, z0, x1 + nx, y1 + ny, z1, c);
        Tri(v, x1 + nx, y1 + ny, z1, x0 - nx, y0 - ny, z0, x1 - nx, y1 - ny, z1, c);
    }

    // tip at (tx, ty), pointing along (dx, dy)
    void HeadAt(std::vector<GhostOverlayVertex>& v, float tx, float ty, float tz, float dx, float dy,
        float head, const float* c) const
    {
        const float bx = tx - dx * head, by = ty - dy * head;
        const float px = -dy * head * 0.38f, py = dx * head * 0.38f;
        Tri(v, tx, ty, tz, bx + px, by + py, tz, bx - px, by - py, tz, c);
    }
};