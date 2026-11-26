// GhostSMAA.hlsl — entry points around the official SMAA.hlsl (put both files in shaders/)
cbuffer SmaaCB : register(b0) { float4 gMetrics; };      // 1/w, 1/h, w, h
#define SMAA_RT_METRICS gMetrics
#define SMAA_HLSL_4_1
// SMAA_PRESET_LOW / MEDIUM / HIGH / ULTRA is passed in by GhostSMAA.cpp
#include "../GhostX/shaders/SMAA.hlsl"

Texture2D gT0 : register(t0);
Texture2D gT1 : register(t1);
Texture2D gT2 : register(t2);

float2 FullscreenUV(uint vid, out float4 pos)
{
    float2 c = float2((vid << 1) & 2, vid & 2);
    pos = float4(c * 2.0f - 1.0f, 0.0f, 1.0f);
    return float2(c.x, 1.0f - c.y);
}

// ---- pass 1: luma edge detection (gT0 = tone mapped image) ----
struct EdgeIO { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; float4 offset[3] : TEXCOORD1; };

EdgeIO EdgeVS(uint vid : SV_VertexID)
{
    EdgeIO o;
    o.uv = FullscreenUV(vid, o.pos);
    SMAAEdgeDetectionVS(o.uv, o.offset);
    return o;
}
float2 EdgePS(EdgeIO i) : SV_TARGET
{
    return SMAALumaEdgeDetectionPS(i.uv, i.offset, gT0);
}

// ---- pass 2: blending weights (gT0 = edges, gT1 = area, gT2 = search) ----
struct WeightIO { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; float2 pix : TEXCOORD1; float4 offset[3] : TEXCOORD2; };

WeightIO WeightVS(uint vid : SV_VertexID)
{
    WeightIO o;
    o.uv = FullscreenUV(vid, o.pos);
    SMAABlendingWeightCalculationVS(o.uv, o.pix, o.offset);
    return o;
}
float4 WeightPS(WeightIO i) : SV_TARGET
{
    return SMAABlendingWeightCalculationPS(i.uv, i.pix, i.offset, gT0, gT1, gT2, float4(0, 0, 0, 0));
}

// ---- pass 3: neighborhood blending (gT0 = tone mapped image, gT1 = weights) ----
struct BlendIO { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; float4 offset : TEXCOORD1; };

BlendIO BlendVS(uint vid : SV_VertexID)
{
    BlendIO o;
    o.uv = FullscreenUV(vid, o.pos);
    SMAANeighborhoodBlendingVS(o.uv, o.offset);
    return o;
}
float4 BlendPS(BlendIO i) : SV_TARGET
{
    return SMAANeighborhoodBlendingPS(i.uv, i.offset, gT0, gT1);
}