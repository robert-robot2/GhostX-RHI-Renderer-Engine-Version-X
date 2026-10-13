#define MAX_LIGHTS 16

struct Light
{
    float4 posRange;        // xyz position, w range
    float4 dirType;         // xyz direction, w type (0 dir, 1 point, 2 rect)
    float4 colorIntensity;  // rgb color, w intensity
    float4 rectRight;       // xyz right axis, w half-width
    float4 rectUp;          // xyz up axis, w half-height
};

cbuffer SceneBuffer : register(b1)
{
    float4 ambientColor;    // rgb = color * intensity
    uint   lightCount;
    uint3  padScene;
    Light  lights[MAX_LIGHTS];
};

Texture2D    gTexture : register(t0);
SamplerState sLinWrap : register(s0);
SamplerState sLinClamp : register(s1);
SamplerState sPtWrap : register(s2);
SamplerState sPtClamp : register(s3);

cbuffer ObjectBuffer : register(b0)       // same layout as MVPBuffer in GhostVS.hlsl
{
    row_major float4x4 model;
    row_major float4x4 view;
    row_major float4x4 proj;
    float4 baseColor;
    float  useVertexColor;
    float3 objPad;
    float4 uvParams;                      // tilingU, tilingV, offsetU, offsetV
    float4 mapParams;                     // mode, rotation (radians), wrap, filter
};

struct PSInput
{
    float4 pos : SV_POSITION;
    float4 color : COLOR;
    float2 uv : TEXCOORD0;
    float3 worldPos : TEXCOORD1;
    float3 worldNormal : TEXCOORD2;
    float3 localPos : TEXCOORD3;
    float3 localNormal : TEXCOORD4;
};

float RangeFalloff(float dist, float range)
{
    float r = saturate(1.0f - pow(saturate(dist / max(range, 0.001f)), 4.0f));
    return (r * r) / (dist * dist + 1.0f);
}

static const float PI = 3.14159265f;
static const float TWO_PI = 6.28318531f;

// Per-face planar layout, upright and unmirrored when viewed from outside.
// On the unit cube this is identical to the mesh UVs.
float2 BoxUV(float3 p, float3 n)
{
    float3 a = abs(n);
    if (a.x >= a.y && a.x >= a.z)                                  // +-X
        return float2((n.x > 0.0f ? p.z : -p.z) + 0.5f, 0.5f - p.y);
    if (a.y >= a.z)                                                // +-Y
        return float2(p.x + 0.5f, n.y > 0.0f ? 0.5f - p.z : 0.5f + p.z);
    return float2((n.z < 0.0f ? p.x : -p.x) + 0.5f, 0.5f - p.y);   // +-Z
}

// 4x3 horizontal cross:   .  Top .  .
//                         Left Front Right Back
//                         .  Bottom .  .
float2 CrossCell(float3 n)
{
    float3 a = abs(n);
    if (a.x >= a.y && a.x >= a.z) return n.x > 0.0f ? float2(2, 1) : float2(0, 1);
    if (a.y >= a.z)               return n.y > 0.0f ? float2(1, 0) : float2(1, 2);
    return n.z < 0.0f ? float2(1, 1) : float2(3, 1);
}

float2 Rot2(float2 v, float cs, float sn)
{
    return float2(cs * v.x - sn * v.y, sn * v.x + cs * v.y);
}

float4 SampleTex(float2 uv, float2 dx, float2 dy, int wrap, int filt)
{
    if (filt == 0)
    {
        if (wrap == 0) return gTexture.SampleGrad(sLinWrap, uv, dx, dy);
        return                gTexture.SampleGrad(sLinClamp, uv, dx, dy);
    }
    if (wrap == 0) return gTexture.SampleGrad(sPtWrap, uv, dx, dy);
    return                gTexture.SampleGrad(sPtClamp, uv, dx, dy);
}

float4 MappedTexel(PSInput input)
{
    int    mode = (int)(mapParams.x + 0.5f);
    int    wrap = (int)(mapParams.z + 0.5f);
    int    filt = (int)(mapParams.w + 0.5f);
    float3 lp = input.localPos;
    float3 ln = normalize(input.localNormal);

    // ---- 1. projection -> raw uv ----
    float2 raw = input.uv;
    bool   seam = false;
    if (mode == 2) raw = float2(lp.x + 0.5f, 0.5f - lp.y);       // planar XY (projects along Z)
    else if (mode == 3) raw = float2(lp.x + 0.5f, 0.5f - lp.z);       // planar XZ (along Y)
    else if (mode == 4) raw = float2(lp.z + 0.5f, 0.5f - lp.y);       // planar YZ (along X)
    else if (mode == 5)                                               // cylindrical around Y
    {
        raw = float2(atan2(lp.x, lp.z) / TWO_PI + 0.5f, 0.5f - lp.y);
        seam = true;
    }
    else if (mode == 6)                                               // spherical
    {
        float r = max(length(lp), 1e-5f);
        raw = float2(atan2(lp.x, lp.z) / TWO_PI + 0.5f, acos(clamp(lp.y / r, -1.0f, 1.0f)) / PI);
        seam = true;
    }
    else if (mode == 1 || mode == 7) raw = BoxUV(lp, ln);             // box / cube cross

    // ---- 2. gradients (fixed across the wrap seam of cylindrical / spherical) ----
    float2 rdx = ddx(raw), rdy = ddy(raw);
    if (seam)
    {
        float2 alt = float2(frac(raw.x + 0.5f), raw.y);
        float2 adx = ddx(alt), ady = ddy(alt);
        if (abs(adx.x) + abs(ady.x) < abs(rdx.x) + abs(rdy.x)) { rdx.x = adx.x; rdy.x = ady.x; }
    }

    // ---- 3. tiling (about the center), rotation, offset ----
    float cs = cos(mapParams.y), sn = sin(mapParams.y);
    float2 uv = Rot2((raw - 0.5f) * uvParams.xy, cs, sn) + 0.5f + uvParams.zw;
    float2 dx = Rot2(rdx * uvParams.xy, cs, sn);
    float2 dy = Rot2(rdy * uvParams.xy, cs, sn);

    // ---- 4. sample ----
    if (mode == 7)                                                    // each face reads its own cell
    {
        float2 f = (wrap == 0) ? frac(uv) : saturate(uv);
        float2 atlas = (CrossCell(ln) + f) / float2(4.0f, 3.0f);
        return SampleTex(atlas, dx / float2(4.0f, 3.0f), dy / float2(4.0f, 3.0f), 1, filt);
    }
    return SampleTex(uv, dx, dy, wrap, filt);
}

float4 PSMain(PSInput input, bool isFront : SV_IsFrontFace) : SV_TARGET
{
    // Culling is off, so flip the normal on back faces
    float3 N = normalize(input.worldNormal);
    if (!isFront) N = -N;

    // Light in linear space, encode to gamma at the end
        // The texture view is sRGB, so the sample arrives already linear
    float4 texel = MappedTexel(input);
    float3 albedo = pow(input.color.rgb, 2.2f) * texel.rgb;
    float3 lit = ambientColor.rgb;

    for (uint i = 0; i < lightCount; i++)
    {
        Light  L = lights[i];
        int    type = (int)(L.dirType.w + 0.5f);
        float3 radiance = L.colorIntensity.rgb * L.colorIntensity.w;
        float3 toLight = float3(0, 1, 0);
        float  atten = 1.0f;

        if (type == 0)                      // directional
        {
            toLight = -normalize(L.dirType.xyz);
        }
        else if (type == 1)                 // point
        {
            float3 d = L.posRange.xyz - input.worldPos;
            float  dist = length(d);
            toLight = d / max(dist, 1e-4f);
            atten = RangeFalloff(dist, L.posRange.w);
        }
        else                                // rect area (representative point, one-sided)
        {
            float3 center = L.posRange.xyz;
            float3 faceN = normalize(L.dirType.xyz);
            float3 fromC = input.worldPos - center;

            if (dot(fromC, faceN) <= 0.0f) continue;   // behind the emitter

            float u = clamp(dot(fromC, L.rectRight.xyz), -L.rectRight.w, L.rectRight.w);
            float v = clamp(dot(fromC, L.rectUp.xyz),    -L.rectUp.w,    L.rectUp.w);
            float3 rep = center + L.rectRight.xyz * u + L.rectUp.xyz * v;

            float3 d = rep - input.worldPos;
            float  dist = length(d);
            toLight = d / max(dist, 1e-4f);
            atten = RangeFalloff(dist, L.posRange.w) * saturate(dot(-toLight, faceN));
        }

        lit += radiance * saturate(dot(N, toLight)) * atten;
    }

    float3 color = albedo * lit;
    if (objPad.x > 1.5f) color += float3(0.25f, 0.12f, 0.0f);   // selected-face glow, visible even when unlit
    return float4(pow(saturate(color), 1.0f / 2.2f), input.color.a);
}

// ---------------- Icons ----------------
struct IconPSInput
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR;
    nointerpolation float2 info : TEXCOORD1;
};

float4 IconPS(IconPSInput input) : SV_TARGET
{
    float2 p = input.uv;
    int    type = (int)(input.info.x + 0.5f);
    int    state = (int)(input.info.y + 0.5f);
    float  r = length(p);
    bool   inside = false;

    if (type == 0)                    // sun: disc + 8 rays
    {
        float a = atan2(p.y, p.x);
        inside = (r < 0.38f) || (r > 0.52f && r < 0.95f && cos(a * 8.0f) > 0.7f);
    }
    else if (type == 1)               // bulb: glass + base
    {
        bool glass = length(p - float2(0.0f, 0.2f)) < 0.55f;
        bool bulbBase = abs(p.x) < 0.25f && p.y > -0.85f && p.y < -0.2f;
        inside = glass || bulbBase;
    }
    else if (type == 2)               // rect light: panel
    {
        inside = abs(p.x) < 0.85f && abs(p.y) < 0.6f;
    }
    else if (type == 4)               // camera: body, lens, two film reels
    {
        bool body = p.x > -0.85f && p.x < 0.25f && abs(p.y + 0.1f) < 0.45f;
        bool lens = p.x >= 0.25f && p.x < 0.9f && abs(p.y + 0.1f) < 0.12f + (p.x - 0.25f) * 0.65f;
        bool reels = length(p - float2(-0.55f, 0.55f)) < 0.27f || length(p - float2(-0.05f, 0.55f)) < 0.27f;
        inside = body || lens || reels;
    }
    else                              // arrow: u = p.x along the arrow, v = p.y across
    {
        bool shaft = p.x < 0.3f && abs(p.y) < 0.15f;
        bool head = p.x >= 0.3f && abs(p.y) < (1.0f - p.x) * 0.78f;
        inside = shaft || head;
    }

    bool ring = (state == 2) && (type != 3) && (r > 0.88f) && (r < 1.0f);
    if (!inside && !ring) discard;

    float3 col = input.color.rgb;
    if (state == 1) col = lerp(col, float3(1, 1, 1), 0.45f);   // hover
    if (ring)       col = float3(1.0f, 0.62f, 0.1f);           // selected
    return float4(col, 1.0f);
}

// ---------------- Blit ----------------
struct BlitPSInput
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float4 BlitPS(BlitPSInput input) : SV_TARGET
{
    // The offscreen image already holds gamma-encoded colors, so it is copied as is
    return float4(gTexture.Sample(sLinClamp, input.uv).rgb, 1.0f);
}

// ---------------- 2D UI ----------------
struct UIPSInput
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR;
};

float4 UIPS(UIPSInput input) : SV_TARGET
{
    // Texture views are sRGB (linear on sample); the target holds display colors, so encode again.
    // Vertex colors are already display colors. Text atlases are white, so only their alpha matters.
    float4 t = gTexture.Sample(sLinClamp, input.uv);
    return float4(pow(saturate(t.rgb), 1.0f / 2.2f) * input.color.rgb, t.a * input.color.a);
}