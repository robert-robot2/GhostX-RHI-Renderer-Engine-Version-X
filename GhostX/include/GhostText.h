#pragma once
#include <vector>
#include <stddef.h>

constexpr size_t GHOST_UI_MAX_QUADS = 6000;

// Backend-neutral: coordinates are in reference units (1080 tall, origin top-left)
struct GhostUIQuad
{
    float x, y, w, h;
    float u0, v0, u1, v1;
    float color[4];
    int   textureId;
};

struct GhostFont
{
    bool  ready = false;
    int   textureId = 0;
    int   atlasW = 0, atlasH = 0;
    int   cols = 16, firstChar = 32, count = 95;
    float cellW = 0, cellH = 0;      // atlas pixels per glyph cell
    float pad = 0;                   // transparent border inside each cell
    float lineHeight = 0;            // baked pixel height of one line (without the padding)
    float advance[96] = {};
};

// size = line height in reference units. '\n' starts a new line.
void  GhostText_Draw(std::vector<GhostUIQuad>& out, const GhostFont& f, float x, float y,
    float size, const char* utf8, const float* rgba4);
float GhostText_Measure(const GhostFont& f, float size, const char* utf8);   // widest line