#include "../include/GhostText.h"

static int GlyphIndex(const GhostFont& f, unsigned char c)
{
    if (c >= f.firstChar && c < f.firstChar + f.count) return c - f.firstChar;
    return '?' - f.firstChar;
}

void GhostText_Draw(std::vector<GhostUIQuad>& out, const GhostFont& f, float x, float y,
    float size, const char* text, const float* rgba)
{
    if (!f.ready || !text || size <= 0.0f || f.lineHeight <= 0.0f) return;
    const float s = size / f.lineHeight;
    float pen = x, line = y;

    for (const unsigned char* p = (const unsigned char*)text; *p; p++)
    {
        const unsigned char c = *p;
        if ((c & 0xC0) == 0x80) continue;                       // UTF-8 continuation byte: the lead byte already drew a '?'
        if (c == '\r') continue;
        if (c == '\n') { pen = x; line += size; continue; }
        if (c == '\t') { pen += f.advance[0] * s * 4.0f; continue; }
        if (c == ' ') { pen += f.advance[0] * s; continue; }

        const int idx = GlyphIndex(f, c);
        if (out.size() >= GHOST_UI_MAX_QUADS) return;

        const int col = idx % f.cols, row = idx / f.cols;
        GhostUIQuad q;
        q.x = pen - f.pad * s;
        q.y = line - f.pad * s;
        q.w = f.cellW * s;
        q.h = f.cellH * s;
        q.u0 = (col * f.cellW) / f.atlasW;
        q.v0 = (row * f.cellH) / f.atlasH;
        q.u1 = ((col + 1) * f.cellW) / f.atlasW;
        q.v1 = ((row + 1) * f.cellH) / f.atlasH;
        q.color[0] = rgba[0]; q.color[1] = rgba[1]; q.color[2] = rgba[2]; q.color[3] = rgba[3];
        q.textureId = f.textureId;
        out.push_back(q);

        pen += f.advance[idx] * s;
    }
}

float GhostText_Measure(const GhostFont& f, float size, const char* text)
{
    if (!f.ready || !text || f.lineHeight <= 0.0f) return 0.0f;
    const float s = size / f.lineHeight;
    float pen = 0.0f, widest = 0.0f;

    for (const unsigned char* p = (const unsigned char*)text; *p; p++)
    {
        const unsigned char c = *p;
        if ((c & 0xC0) == 0x80 || c == '\r') continue;
        if (c == '\n') { if (pen > widest) widest = pen; pen = 0.0f; continue; }
        if (c == '\t') { pen += f.advance[0] * s * 4.0f; continue; }
        int idx = (c == ' ') ? 0 : GlyphIndex(f, c);
        pen += f.advance[idx] * s;
    }
    return pen > widest ? pen : widest;
}