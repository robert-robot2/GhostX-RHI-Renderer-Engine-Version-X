// GhostWorldPart.h - landscape world partition: settings, stats, view frustum, config file. Header only.
#pragma once
#include "GhostMath.h"
#include <Windows.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

struct GhostWorldCfg
{
    int   streaming = 1;        // cells stream in / out by distance
    int   lod = 1;              // terrain LOD by distance
    int   cull = 1;             // frustum culling (objects + terrain bands)
    int   showCells = 0;        // debug: draw every cell's box, coloured by state
    int   maxLoads = 2;         // cells built per frame (the first load of a frame is always allowed)
    float loadDist = 0.0f;      // 0 = auto (view distance + 10)
    float unloadPad = 40.0f;    // a cell unloads this far beyond the load distance (hysteresis)
    float msBudget = 4.0f;      // stop starting new cell loads after this many ms in one frame
    float lodDist1 = 70.0f;     // terrain drops to half resolution beyond this
    float lodDist2 = 160.0f;    // ... and to quarter resolution beyond this
    float farPlane = 100.0f;    // view distance
    float nearPlane = 0.1f;
};

struct GhostWorldStats
{
    int cells = 0, resident = 0, starved = 0, loadsLast = 0, cellsDrawn = 0;
    int bandsDrawn = 0, bandsCulled = 0, trisDrawn = 0, objectsCulled = 0;
    float lastLoadMs = 0.0f;
};

inline float GhostW_Cf(float v, float lo, float hi) { return v != v ? lo : (v < lo ? lo : (v > hi ? hi : v)); }

inline void GhostWorld_Sanitize(GhostWorldCfg& c)
{
    c.streaming = c.streaming ? 1 : 0;  c.lod = c.lod ? 1 : 0;
    c.cull = c.cull ? 1 : 0;            c.showCells = c.showCells ? 1 : 0;
    c.maxLoads = c.maxLoads < 1 ? 1 : (c.maxLoads > 16 ? 16 : c.maxLoads);
    c.loadDist = GhostW_Cf(c.loadDist, 0.0f, 5000.0f);
    c.unloadPad = GhostW_Cf(c.unloadPad, 5.0f, 1000.0f);
    c.msBudget = GhostW_Cf(c.msBudget, 0.5f, 50.0f);
    c.lodDist1 = GhostW_Cf(c.lodDist1, 10.0f, 5000.0f);
    c.lodDist2 = GhostW_Cf(c.lodDist2, c.lodDist1 * 1.2f, 10000.0f);
    c.farPlane = GhostW_Cf(c.farPlane, 20.0f, 3000.0f);
    c.nearPlane = GhostW_Cf(c.nearPlane, 0.02f, 5.0f);
    if (c.nearPlane > c.farPlane * 0.1f) c.nearPlane = c.farPlane * 0.1f;
}

// Per-machine settings file (until the settings move into the .ghostscene)
inline bool GhostWorld_CfgPath(char* out, size_t n)
{
    char base[MAX_PATH];
    const DWORD len = GetEnvironmentVariableA("LOCALAPPDATA", base, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return false;
    snprintf(out, n, "%s\\GhostX", base);
    CreateDirectoryA(out, nullptr);
    snprintf(out, n, "%s\\GhostX\\world.cfg", base);
    return true;
}

inline void GhostWorld_Save(const GhostWorldCfg& c)
{
    char path[MAX_PATH + 32];
    if (!GhostWorld_CfgPath(path, sizeof path)) return;
    FILE* f = nullptr;
    if (fopen_s(&f, path, "w") != 0 || !f) return;
    fprintf(f, "streaming %d\nlod %d\ncull %d\nshowCells %d\nmaxLoads %d\n", c.streaming, c.lod, c.cull, c.showCells, c.maxLoads);
    fprintf(f, "loadDist %g\nunloadPad %g\nmsBudget %g\nlodDist1 %g\nlodDist2 %g\nfarPlane %g\nnearPlane %g\n",
        c.loadDist, c.unloadPad, c.msBudget, c.lodDist1, c.lodDist2, c.farPlane, c.nearPlane);
    fclose(f);
}

inline void GhostWorld_Load(GhostWorldCfg& c)
{
    char path[MAX_PATH + 32];
    if (!GhostWorld_CfgPath(path, sizeof path)) { GhostWorld_Sanitize(c); return; }
    FILE* f = nullptr;
    if (fopen_s(&f, path, "r") == 0 && f)
    {
        char key[64]; float v;
        while (fscanf_s(f, "%63s %f", key, (unsigned)sizeof key, &v) == 2)
        {
            if (!strcmp(key, "streaming")) c.streaming = (int)v;
            else if (!strcmp(key, "lod")) c.lod = (int)v;
            else if (!strcmp(key, "cull")) c.cull = (int)v;
            else if (!strcmp(key, "showCells")) c.showCells = (int)v;
            else if (!strcmp(key, "maxLoads")) c.maxLoads = (int)v;
            else if (!strcmp(key, "loadDist")) c.loadDist = v;
            else if (!strcmp(key, "unloadPad")) c.unloadPad = v;
            else if (!strcmp(key, "msBudget")) c.msBudget = v;
            else if (!strcmp(key, "lodDist1")) c.lodDist1 = v;
            else if (!strcmp(key, "lodDist2")) c.lodDist2 = v;
            else if (!strcmp(key, "farPlane")) c.farPlane = v;
            else if (!strcmp(key, "nearPlane")) c.nearPlane = v;
        }
        fclose(f);
    }
    GhostWorld_Sanitize(c);
}

// ---- View frustum (6 planes, inside = a*x + b*y + c*z + d >= 0). Matrices here are column-vector: clip = VP * v ----
struct GhostFrustum { float p[6][4] = {}; };

inline void GhostFrustum_FromVP(GhostFrustum& f, const GhostMat4& vp)
{
    const float* m = &vp.m[0][0];
    const float* r0 = m, * r1 = m + 4, * r2 = m + 8, * r3 = m + 12;
    for (int k = 0; k < 4; k++)
    {
        f.p[0][k] = r3[k] + r0[k];     // left
        f.p[1][k] = r3[k] - r0[k];     // right
        f.p[2][k] = r3[k] + r1[k];     // bottom
        f.p[3][k] = r3[k] - r1[k];     // top
        f.p[4][k] = r2[k];             // near  (D3D depth 0..w)
        f.p[5][k] = r3[k] - r2[k];     // far
    }
    for (int i = 0; i < 6; i++)
    {
        const float l = sqrtf(f.p[i][0] * f.p[i][0] + f.p[i][1] * f.p[i][1] + f.p[i][2] * f.p[i][2]);
        if (l > 1e-8f) for (int k = 0; k < 4; k++) f.p[i][k] /= l;
    }
}

inline bool GhostFrustum_Sphere(const GhostFrustum& f, const GhostVec3& c, float r)
{
    for (int i = 0; i < 6; i++)
        if (f.p[i][0] * c.x + f.p[i][1] * c.y + f.p[i][2] * c.z + f.p[i][3] < -r) return false;
    return true;
}

// 8 corners of a (possibly rotated) box. Visible unless every corner is outside the same plane.
inline bool GhostFrustum_Box8(const GhostFrustum& f, const GhostVec3* c, float slack)
{
    for (int i = 0; i < 6; i++)
    {
        bool anyIn = false;
        for (int k = 0; k < 8 && !anyIn; k++)
            if (f.p[i][0] * c[k].x + f.p[i][1] * c[k].y + f.p[i][2] * c[k].z + f.p[i][3] >= -slack) anyIn = true;
        if (!anyIn) return false;
    }
    return true;
}
