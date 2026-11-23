#define MAX_LIGHTS 16
#define MAX_PROBES 8
#define MAX_FOGVOLS 8

struct FogVol
{
    float4 row0;    // world -> volume space
    float4 row1;
    float4 row2;
    float4 shp;     // x 0 box / 1 sphere, yzw half extents (sphere: y = radius)
    float4 dens;    // x density, y blend distance, z gradient
    float4 col;     // rgb linear colour, w light scatter amount
    float4 nz;      // x noise texture id, y amount, z scale
    float4 scr;     // xy noise offset
};
struct Probe
{
    float4 row0;    // world -> volume space: xyz = rotation row, w = translation
    float4 row1;
    float4 row2;
    float4 shp;     // x 0 box / 1 sphere, yzw = half extents (sphere: y = radius)
    float4 prm;     // x blend distance, y intensity, z last mip index, w ambient diffuse amount
    float4 misc;    // x parallax correction, y cube slot
};

struct Light
{
    float4 posRange;        // xyz position, w range
    float4 dirType;         // xyz direction, w type (0 dir, 1 point, 2 rect)
    float4 colorIntensity;  // rgb color, w intensity
    float4 rectRight;       // xyz right axis, w half-width
    float4 rectUp;          // xyz up axis, w half-height
    float4 shadow;         // x first map slice (-1 = none), y far, z texel size, w god rays strength.
};

cbuffer SceneBuffer : register(b1)
{
    float4 ambientColor;    // rgb = color * intensity
    uint   lightCount;
    uint3  padScene;
    float4 shadowGlobal;    // x strength (0 = off), y map size
    float4 shadowTune;      // x filter mode, y softness (texels), z depth bias scale, w normal bias scale
    Light  lights[MAX_LIGHTS];
    row_major float4x4 shadowVP[MAX_LIGHTS];
    float4 flickGlobal;               // x = time (s)
    float4 flick[MAX_LIGHTS * 6];     // per light, 2 layers x 3 float4: colour+intensity / texture id, speed, tiling, amount / diffuse, specular, roughness, metallic
    float4 probeInfo;       // x = probe count
    Probe  probes[MAX_PROBES];
    float4 fogA;            // x global fog on, y density, z height base, w height falloff
    float4 fogB;            // rgb fog colour (linear), w brightness
    float4 fogC;            // x start, y max distance, z sky fog, w anisotropy
    float4 fogD;            // x god rays on, y follow ambient, z steps, w noise frame offset
    float4 fogE;            // x env mode, y env amount, z env texture, w env yaw (rad)
    float4 fogF;            // x noise texture, y amount, z scale
    float4 fogG;            // xy noise offset, w volume count
    float4 fogCam[6];       // eye, right, up, forward, (p00 p11 p02 p12), (p22 p23 width height)
    FogVol fogVols[MAX_FOGVOLS];
};

Texture2D    gTexture : register(t0);
SamplerState sLinWrap : register(s0);
SamplerState sLinClamp : register(s1);
SamplerState sPtWrap : register(s2);
SamplerState sPtClamp : register(s3);
Texture2DArray<float> gShadowMap : register(t1);
TextureCube<float4> gProbe[MAX_PROBES] : register(t2);      // Reflection Captures (descriptor table is contiguous with the shadow map)

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
    float4 sky[16];                       // sky pass parameters (see GhostSky_Build)
    float4 pbrA;                          // metallic, roughness, specular, emissive strength
    float4 pbrB;                          // emissive rgb, w = 1 when this draw uses PBR
    float4 alphaP;                        // x blend mode (0 opaque, 1 masked, 2 translucent), y source, z cutoff
    float4 mapUV;                         // extra maps: tiling u,v / offset u,v
    float4 maps[13];                      // x texture id (0 none), y amount, z channel, w invert
    float4 surfA;                         // x two sided, y IOR, z reflection rotation (rad), w anisotropy
    float4 surfB;                         // subsurface rgb, w amount
    float4 wpoP;
    float4 opacityP;                      // x opacity
    float4 tileP[2];                      // Tile Map: [0] x weight texture A, y weight texture B, z layer of this pass / [1] x gridX, y gridY, z 1 = checker look
};

Texture2D gTex[256] : register(t0, space1);   // linear views, indexed by texture id

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
    if (((int)(objPad.y + 0.5f) & 4) != 0) { dx = float2(0, 0); dy = float2(0, 0); }   // sprites: always mip 0
    // ---- 4. sample ----
    if (mode == 7)                                                    // each face reads its own cell
    {
        float2 f = (wrap == 0) ? frac(uv) : saturate(uv);
        float2 atlas = (CrossCell(ln) + f) / float2(4.0f, 3.0f);
        return SampleTex(atlas, dx / float2(4.0f, 3.0f), dy / float2(4.0f, 3.0f), 1, filt);
    }
    return SampleTex(uv, dx, dy, wrap, filt);
}

// Cheap analytic environment BRDF: how much of the ambient color a surface reflects
float3 EnvBRDFApprox(float3 f0, float rough, float NoV)
{
    const float4 c0 = float4(-1.0f, -0.0275f, -0.572f, 0.022f);
    const float4 c1 = float4(1.0f, 0.0425f, 1.04f, -0.04f);
    float4 r = rough * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28f * NoV)) * r.x + r.y;
    float2 ab = float2(-1.04f, 1.04f) * a004 + r.zw;
    return f0 * ab.x + ab.y;
}

// GGX distribution, Smith visibility, Schlick Fresnel
float3 SpecularGGX(float NoL, float NoV, float NoH, float VoH, float3 f0, float alpha)
{
    float a2 = alpha * alpha;
    float d = NoH * NoH * (a2 - 1.0f) + 1.0f;
    float D = a2 / (PI * d * d);
    float gv = NoL * (NoV * (1.0f - alpha) + alpha);
    float gl = NoV * (NoL * (1.0f - alpha) + alpha);
    float vis = 0.5f / max(gv + gl, 1e-4f);
    float3 F = f0 + (1.0f - f0) * pow(1.0f - VoH, 5.0f);
    return D * vis * F;
}

static const int MAP_NORMAL = 0;   static const int MAP_SPECULAR = 1;   static const int MAP_METALLIC = 2;
static const int MAP_ROUGHNESS = 3; static const int MAP_AO = 4;        static const int MAP_EMISSIVE = 5;
static const int MAP_OPACITY = 6;  static const int MAP_MASK = 7;       static const int MAP_TANGENT = 8;
static const int MAP_WPO = 9;      static const int MAP_REFRACTION = 10; static const int MAP_REFLECTION = 11;
static const int MAP_CUSTOM = 12;

struct MapUV { float2 uv, dx, dy; };

bool HasMap(int k) { return maps[k].x > 0.5f; }

float4 FetchMap(int k, MapUV m)
{
    return gTex[(int)(maps[k].x + 0.5f)].SampleGrad(sLinWrap, m.uv, m.dx, m.dy);
}

float PickChannel(float4 t, float ch)
{
    int c = (int)(ch + 0.5f);
    if (c == 0) return t.r;
    if (c == 1) return t.g;
    if (c == 2) return t.b;
    if (c == 3) return t.a;
    return dot(t.rgb, float3(0.299f, 0.587f, 0.114f));
}

float MapRaw(int k, MapUV m)
{
    float v = PickChannel(FetchMap(k, m), maps[k].z);
    return (maps[k].w > 0.5f) ? 1.0f - v : v;
}

// lerp(slider value, map value, amount); the slider value when there is no map
float MapValue(int k, MapUV m, float fallback)
{
    if (!HasMap(k)) return fallback;
    return lerp(fallback, MapRaw(k, m), saturate(maps[k].y));
}

float3x3 CotangentFrame(float3 N, float3 p, float2 uv)      // tangent frame from screen derivatives
{
    float3 dp1 = ddx(p), dp2 = ddy(p);
    float2 duv1 = ddx(uv), duv2 = ddy(uv);
    float3 dp2perp = cross(dp2, N);
    float3 dp1perp = cross(N, dp1);
    float3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    float3 B = dp2perp * duv1.y + dp1perp * duv2.y;
    float invmax = rsqrt(max(max(dot(T, T), dot(B, B)), 1e-12f));
    return float3x3(T * invmax, B * invmax, N);
}

float3 SpecularGGXAniso(float NoL, float NoV, float NoH, float VoH, float3 f0, float ax, float ay, float XoH, float YoH)
{
    float d = XoH * XoH / (ax * ax) + YoH * YoH / (ay * ay) + NoH * NoH;
    float D = 1.0f / (PI * ax * ay * d * d);
    float a = sqrt(ax * ay);
    float gv = NoL * (NoV * (1.0f - a) + a);
    float gl = NoV * (NoL * (1.0f - a) + a);
    float vis = 0.5f / max(gv + gl, 1e-4f);
    float3 F = f0 + (1.0f - f0) * pow(1.0f - VoH, 5.0f);
    return D * vis * F;
}


float ShadowTap(float2 uv, float slice, float refDepth)
{
    float stored = gShadowMap.SampleLevel(sPtClamp, float3(uv, slice), 0);
    return refDepth <= stored ? 1.0f : 0.0f;
}

// 0 = fully shadowed, 1 = lit. Normal-offset plus a small depth bias, then a 3x3 soft edge.
static const float2 kPoisson[16] =
{
    float2(-0.94201624f, -0.39906216f), float2(0.94558609f, -0.76890725f),
    float2(-0.09418410f, -0.92938870f), float2(0.34495938f,  0.29387760f),
    float2(-0.91588581f,  0.45771432f), float2(-0.81544232f, -0.87912464f),
    float2(-0.38277543f,  0.27676845f), float2(0.97484398f,  0.75648379f),
    float2(0.44323325f, -0.97511554f), float2(0.53742981f, -0.47373420f),
    float2(-0.26496911f, -0.41893023f), float2(0.79197514f,  0.19090188f),
    float2(-0.24188840f,  0.99706507f), float2(-0.81409955f,  0.91437590f),
    float2(0.19984126f,  0.78641367f), float2(0.14383161f, -0.14100790f)
};

// One tap with a bilinear blend of the four nearest texels: a clean edge instead of stair-steps
float ShadowTapBilinear(float2 uv, float slice, float refDepth)
{
    const float size = shadowGlobal.y;
    const float inv = 1.0f / size;
    float2 t = uv * size - 0.5f;
    float2 f = frac(t);
    float2 base = (floor(t) + 0.5f) * inv;
    float a = ShadowTap(base, slice, refDepth);
    float b = ShadowTap(base + float2(inv, 0.0f), slice, refDepth);
    float c = ShadowTap(base + float2(0.0f, inv), slice, refDepth);
    float d = ShadowTap(base + float2(inv, inv), slice, refDepth);
    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}

// 0 = fully shadowed, 1 = lit
float ShadowFactor(Light L, float3 worldPos, float3 N, int type, float2 pixelPos)
{
    const int    mode = (int)(shadowTune.x + 0.5f);
    const float  soft = (mode == 0) ? 1.0f : max(shadowTune.y, 0.05f);
    const float  farW = max(L.shadow.y, 1e-3f);
    const float3 rel = worldPos - L.posRange.xyz;
    const float  dist = length(rel);
    // a wider filter needs proportionally more bias, or the surface shadows itself
    const float  texel = ((type == 0) ? L.shadow.z : L.shadow.z * dist) * max(soft, 1.0f);
    const float3 p = worldPos + N * (texel * 1.5f * shadowTune.w);       // normal offset
    const float  bias = (texel * 2.0f * shadowTune.z) / farW;               // depth bias

    float  slice = L.shadow.x;
    float4 clip;
    float  refDepth;

    if (type == 0)                                           // directional: orthographic, depth = clip z
    {
        clip = mul(shadowVP[(int)slice], float4(p, 1.0f));
        refDepth = min(clip.z, 1.0f) - bias;
    }
    else if (type == 3)                                      // spot: one perspective map, depth = distance / far
    {
        clip = mul(shadowVP[(int)slice], float4(p, 1.0f));
        refDepth = length(p - L.posRange.xyz) / farW - bias;
    }
    else                                                     // point / rect: pick the cube face, depth = distance / far
    {
        float3 a = abs(rel);
        int face = (a.x >= a.y && a.x >= a.z) ? (rel.x > 0.0f ? 0 : 1)
            : ((a.y >= a.z) ? (rel.y > 0.0f ? 2 : 3) : (rel.z > 0.0f ? 4 : 5));
        slice += face;
        clip = mul(shadowVP[(int)slice], float4(p, 1.0f));
        refDepth = length(p - L.posRange.xyz) / farW - bias;
    }

    if (clip.w <= 1e-4f) return 1.0f;
    float2 uv = float2(clip.x / clip.w * 0.5f + 0.5f, 0.5f - clip.y / clip.w * 0.5f);
    if (uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f) return 1.0f;      // outside the map: lit

    const float step = soft / shadowGlobal.y;                // filter radius unit: texels -> uv

    if (mode == 0)                                           // sharp: one bilinear tap
        return ShadowTapBilinear(uv, slice, refDepth);

    float lit = 0.0f;
    if (mode == 1)                                           // classic 3x3, point taps
    {
        [unroll] for (int y = -1; y <= 1; y++)
            [unroll] for (int x = -1; x <= 1; x++)
            lit += ShadowTap(uv + float2((float)x, (float)y) * step, slice, refDepth);
        return lit / 9.0f;
    }
    if (mode == 2)                                           // smooth: 3x3 of bilinear taps
    {
        [unroll] for (int y2 = -1; y2 <= 1; y2++)
            [unroll] for (int x2 = -1; x2 <= 1; x2++)
            lit += ShadowTapBilinear(uv + float2((float)x2, (float)y2) * step, slice, refDepth);
        return lit / 9.0f;
    }
    // soft: 16 Poisson taps, rotated per pixel (noise instead of banding; TAA cleans it up)
    const float ign = frac(52.9829189f * frac(dot(pixelPos, float2(0.06711056f, 0.00583715f))));
    const float ang = ign * 6.2831853f;
    const float cs = cos(ang), sn = sin(ang);
    const float rad = step * 2.5f;
    [unroll] for (int i = 0; i < 16; i++)
    {
        float2 k = kPoisson[i];
        float2 o = float2(cs * k.x - sn * k.y, sn * k.x + cs * k.y) * rad;
        lit += ShadowTap(uv + o, slice, refDepth);
    }
    return lit / 16.0f;
}

float3 ProbeSafeDir(float3 v)
{
    return float3(abs(v.x) < 1e-5f ? (v.x < 0.0f ? -1e-5f : 1e-5f) : v.x,
        abs(v.y) < 1e-5f ? (v.y < 0.0f ? -1e-5f : 1e-5f) : v.y,
        abs(v.z) < 1e-5f ? (v.z < 0.0f ? -1e-5f : 1e-5f) : v.z);
}

// Blends the reflection probes in order (the last one in the list wins where they overlap).
// diffuseLookup = 1: the blurriest mip, weighted by each probe's Ambient Diffuse amount.
float3 ProbeLookup(float3 dirW, float3 posW, float rough, float3 fallback, float diffuseLookup)
{
    float3 result = fallback;
    const int count = min((int)(probeInfo.x + 0.5f), MAX_PROBES);
    [loop] for (int i = 0; i < count; i++)
    {
        Probe P = probes[i];
        float3 lp = float3(dot(P.row0.xyz, posW) + P.row0.w, dot(P.row1.xyz, posW) + P.row1.w, dot(P.row2.xyz, posW) + P.row2.w);
        const float blend = max(P.prm.x, 0.001f);
        float w;
        if (P.shp.x < 0.5f) { float3 dIn = P.shp.yzw - abs(lp); w = saturate(min(dIn.x, min(dIn.y, dIn.z)) / blend); }
        else                  w = saturate((P.shp.y - length(lp)) / blend);
        if (diffuseLookup > 0.5f) w *= P.prm.w;
        if (w <= 0.0f) continue;

        float3 d = dirW;
        if (diffuseLookup < 0.5f && P.misc.x > 0.5f)             // parallax correction against the volume's shape
        {
            float3 lr = ProbeSafeDir(float3(dot(P.row0.xyz, dirW), dot(P.row1.xyz, dirW), dot(P.row2.xyz, dirW)));
            float t;
            if (P.shp.x < 0.5f)
            {
                float3 t1 = (P.shp.yzw - lp) / lr, t2 = (-P.shp.yzw - lp) / lr;
                float3 tf = max(t1, t2);
                t = min(tf.x, min(tf.y, tf.z));
            }
            else
            {
                float bq = dot(lp, lr), cq = dot(lp, lp) - P.shp.y * P.shp.y;
                t = -bq + sqrt(max(bq * bq - cq, 0.0f));
            }
            float3 hitL = lp + lr * t;                            // the probe sits at the volume's centre
            d = normalize(P.row0.xyz * hitL.x + P.row1.xyz * hitL.y + P.row2.xyz * hitL.z);
        }

        const float lod = (diffuseLookup > 0.5f) ? P.prm.z : saturate((sqrt(saturate(rough)) - 0.2f) / 0.8f) * P.prm.z;
        float3 c = pow(max(gProbe[(int)(P.misc.y + 0.5f)].SampleLevel(sLinClamp, d, lod).rgb, 0.0f), 2.2f) * P.prm.y;
        result = lerp(result, c, w);
    }
    return result;
}

float4 PSMain(PSInput input, bool isFront : SV_IsFrontFace) : SV_TARGET
{
    // gradients for the extra maps, computed before any branch
    MapUV mu;
    mu.uv = input.uv * mapUV.xy + mapUV.zw;
    mu.dx = ddx(mu.uv);
    mu.dy = ddy(mu.uv);
    float3x3 tbn;

    const bool usePbr = pbrB.w > 0.5f;
    float3 N = normalize(input.worldNormal);
    if (!isFront) N = -N;

    float4 texel = MappedTexel(input);
    const int spriteFlags = (int)(objPad.y + 0.5f);
    if ((spriteFlags & 1) != 0 && texel.a < 0.5f) discard;
    if (usePbr && surfA.x < 0.5f && !isFront) discard;            // Two Sided off
    const bool receive = objPad.z < 0.5f;
    const int shadowSel = (int)(opacityP.y + 0.5f);     // 0 = every light shadows this surface, n = only light n-1 does (Tile Map: Directional Light)

    // ---- Tile Map weight map: how much of this layer the pixel shows (layers are drawn as additive passes) ----
    float splatW = 1.0f;
    if (tileP[0].x > 0.5f)
    {
        const int    layerK = (int)(tileP[0].z + 0.5f);
        const int    sid = (int)((layerK < 4 ? tileP[0].x : tileP[0].y) + 0.5f);
        const float2 suv = float2(input.uv.x / tileP[1].x, 1.0f - input.uv.y / tileP[1].y);
        const float4 sw = gTex[sid].SampleLevel(sLinClamp, suv, 0);
        const int    ch = layerK & 3;
        splatW = (ch == 0) ? sw.r : ((ch == 1) ? sw.g : ((ch == 2) ? sw.b : sw.a));
        if (layerK > 0 && splatW < 0.004f) discard;
    }

    // ---- alpha: masked = alpha test, translucent = blended ----
    float srcA = texel.a;
    if (alphaP.y > 0.5f)
    {
        float lum = pow(saturate(dot(texel.rgb, float3(0.2126f, 0.7152f, 0.0722f))), 1.0f / 2.2f);
        srcA = (alphaP.y < 1.5f) ? lum : (1.0f - lum);
    }
    const int blend = usePbr ? (int)(alphaP.x + 0.5f) : 0;
    float opacity = 1.0f;
    if (blend == 1) clip(srcA * MapValue(MAP_MASK, mu, 1.0f) - alphaP.z);
    else if (blend == 2) opacity = saturate(opacityP.x * MapValue(MAP_OPACITY, mu, 1.0f) * srcA);

    float3 albedo = pow(input.color.rgb, 2.2f) * texel.rgb;

    if (tileP[1].z > 0.5f)                // a Tile Map with no layers yet: checker and tile lines, so the tiles can be seen
    {
        albedo *= (((int)(floor(input.uv.x) + floor(input.uv.y)) & 1) != 0) ? 1.0f : 0.75f;
        const float2 fr = frac(input.uv);
        const float edge = min(min(fr.x, 1.0f - fr.x), min(fr.y, 1.0f - fr.y));
        albedo *= lerp(0.5f, 1.0f, smoothstep(0.0f, 0.03f, edge));
    }

    // ---- surface values: slider, or the map ----
    const float metallic = saturate(MapValue(MAP_METALLIC, mu, saturate(pbrA.x)));
    const float rough = clamp(MapValue(MAP_ROUGHNESS, mu, pbrA.y), 0.06f, 1.0f);
    const float alpha = rough * rough;
    const float specular = saturate(MapValue(MAP_SPECULAR, mu, saturate(pbrA.z)));
    const float ao = saturate(MapValue(MAP_AO, mu, 1.0f));
    const float sss = surfB.w * MapValue(MAP_CUSTOM, mu, 1.0f);
    const float aniso = clamp(surfA.w, -0.95f, 0.95f);

    // ---- normal + tangent frame ----
    tbn = CotangentFrame(N, input.worldPos, mu.uv);
    const float3 Ng = N;                                          // geometric normal, used for shadow offsets
    if (usePbr && HasMap(MAP_NORMAL))
    {
        float3 nt = FetchMap(MAP_NORMAL, mu).xyz * 2.0f - 1.0f;
        if (maps[MAP_NORMAL].w > 0.5f) nt.y = -nt.y;              // flip green (OpenGL style maps)
        nt = normalize(float3(nt.xy * maps[MAP_NORMAL].y, max(nt.z, 1e-3f)));
        N = normalize(mul(nt, tbn));
    }

    float3 Tdir = normalize(tbn[0] - N * dot(tbn[0], N));
    if (usePbr && HasMap(MAP_TANGENT))
    {
        float2 tv = FetchMap(MAP_TANGENT, mu).xy * 2.0f - 1.0f;
        float3 Tm = tbn[0] * tv.x + tbn[1] * tv.y;
        Tm = (dot(Tm, Tm) > 1e-6f) ? normalize(Tm) : tbn[0];
        Tdir = normalize(lerp(Tdir, Tm, saturate(maps[MAP_TANGENT].y)));
        Tdir = normalize(Tdir - N * dot(Tdir, N));
    }
    const float3 Bdir = cross(N, Tdir);

    // ---- view, reflectance (IOR from the slider or the refraction map) ----
    float3 eye = -(view[0].xyz * view[0].w + view[1].xyz * view[1].w + view[2].xyz * view[2].w);
    float3 V = normalize(eye - input.worldPos);
    float  NoV = max(dot(N, V), 1e-3f);
    float  ior = surfA.y;
    if (HasMap(MAP_REFRACTION)) ior = lerp(ior, 1.0f + 1.5f * MapRaw(MAP_REFRACTION, mu), saturate(maps[MAP_REFRACTION].y));
    const float f0d = pow((ior - 1.0f) / (ior + 1.0f), 2.0f) * 2.0f * specular;   // IOR 1.5 and specular 0.5 give the old 0.04
    float3 f0 = lerp(float3(f0d, f0d, f0d), albedo, metallic);

    float3 diffuseSum = float3(0.0f, 0.0f, 0.0f);
    float3 specSum = float3(0.0f, 0.0f, 0.0f);

    for (uint i = 0; i < lightCount; i++)
    {
        Light  L = lights[i];
        int    type = (int)(L.dirType.w + 0.5f);
        float3 radiance = L.colorIntensity.rgb * L.colorIntensity.w;
        float3 toLight = float3(0, 1, 0);
        float  atten = 1.0f;

        if (type == 0)
        {
            toLight = -normalize(L.dirType.xyz);
        }
        else if (type == 1)
        {
            float3 d = L.posRange.xyz - input.worldPos;
            float  dist = length(d);
            toLight = d / max(dist, 1e-4f);
            atten = RangeFalloff(dist, L.posRange.w);
        }
        else if (type == 3)                                    // spot: cone from rectRight.w (cos outer) to rectUp.w (cos inner)
        {
            float3 d = L.posRange.xyz - input.worldPos;
            float  dist = length(d);
            toLight = d / max(dist, 1e-4f);
            float  cd = dot(-toLight, normalize(L.dirType.xyz));
            atten = RangeFalloff(dist, L.posRange.w) * smoothstep(L.rectRight.w, L.rectUp.w, cd);
        }
        else
        {
            float3 center = L.posRange.xyz;
            float3 faceN = normalize(L.dirType.xyz);
            float3 fromC = input.worldPos - center;
            if (dot(fromC, faceN) <= 0.0f) continue;
            float u = clamp(dot(fromC, L.rectRight.xyz), -L.rectRight.w, L.rectRight.w);
            float v = clamp(dot(fromC, L.rectUp.xyz),    -L.rectUp.w,    L.rectUp.w);
            float3 rep = center + L.rectRight.xyz * u + L.rectUp.xyz * v;
            float3 d = rep - input.worldPos;
            float  dist = length(d);
            toLight = d / max(dist, 1e-4f);
            atten = RangeFalloff(dist, L.posRange.w) * saturate(dot(-toLight, faceN));
        }

        float rawNoL = dot(N, toLight);
        float NoL = saturate(rawNoL);
        if (receive && NoL > 0.0f && atten > 0.0f && L.shadow.x >= 0.0f && (shadowSel == 0 || shadowSel == (int)i + 1))
            atten *= lerp(1.0f, ShadowFactor(L, input.worldPos, Ng, type, input.pos.xy), shadowGlobal.x);

        // diffuse, with tinted wrap lighting for skin (Custom Data / Subsurface)
        float wrapped = NoL;
        if (sss > 0.001f) wrapped = saturate((rawNoL + 0.5f * sss) / (1.0f + 0.5f * sss));
        diffuseSum += radiance * atten * (NoL + (wrapped - NoL) * surfB.rgb);

        if (usePbr && NoL > 0.0f)
        {
            float3 H = normalize(V + toLight);
            float NoH = saturate(dot(N, H)), VoH = saturate(dot(V, H));
            float3 spec;
            if (abs(aniso) > 0.001f)
                spec = SpecularGGXAniso(NoL, NoV, NoH, VoH, f0, clamp(alpha * (1.0f + aniso), 0.02f, 1.0f),
                                        clamp(alpha * (1.0f - aniso), 0.02f, 1.0f), dot(Tdir, H), dot(Bdir, H));
            else
                spec = SpecularGGX(NoL, NoV, NoH, VoH, f0, alpha);
            specSum += radiance * atten * NoL * PI * spec;
        }

        // ---- flicker layers (Point lights): extra light at the same spot with its own colour, texture and surface response ----
        if (type == 1 && NoL > 0.0f)
        {
            [unroll] for (int fl = 0; fl < 2; fl++)
            {
                const uint   fe = i * 6 + (uint)fl * 3;
                const float4 fa = flick[fe];
                if (fa.w <= 0.0001f) continue;
                const float4 fb = flick[fe + 1];
                const float4 fc = flick[fe + 2];
                float3 fcol = fa.rgb;
                float  fint = fa.w;
                const int ftex = (int)(fb.x + 0.5f);
                if (ftex > 0)                                   // the texture's hue picks the colour, its luminance the brightness
                {
                    const float2 fuv = float2(frac(flickGlobal.x * fb.y) * fb.z, frac(0.37f + 0.211f * (float)i + 0.173f * (float)fl));
                    const float3 ft = gTex[ftex].SampleLevel(sLinWrap, fuv, 0).rgb;
                    fcol *= lerp(float3(1, 1, 1), ft / max(max(ft.r, max(ft.g, ft.b)), 1e-3f), fb.w);
                    fint *= lerp(1.0f, dot(ft, float3(0.299f, 0.587f, 0.114f)), fb.w);
                }
                const float3 frad = fcol * fint;
                diffuseSum += frad * atten * NoL * fc.x;
                if (usePbr)
                {
                    const float3 Hf = normalize(V + toLight);
                    const float  NoHf = saturate(dot(N, Hf)), VoHf = saturate(dot(V, Hf));
                    const float  alF = (fc.z >= 0.0f) ? pow(clamp(fc.z, 0.06f, 1.0f), 2.0f) : alpha;
                    const float3 f0F = lerp(f0, albedo, saturate(fc.w));
                    specSum += frad * atten * NoL * PI * SpecularGGX(NoL, NoV, NoHf, VoHf, f0F, alF) * fc.y;
                }
            }
        }

    }

    float3 color;
    float  outA = 1.0f;
    if ((spriteFlags & 2) != 0)
    {
        color = albedo;
    }
    else if (!usePbr)
    {
        color = albedo * (ambientColor.rgb + diffuseSum);
    }
    else
    {
        // ambient reflection: the Reflection map (lat-long environment, blurred by roughness) or the flat ambient color
        float3 envRefl = ambientColor.rgb;
        if (HasMap(MAP_REFLECTION))
        {
            float3 R = reflect(-V, N);
            float ca = cos(surfA.z), sa = sin(surfA.z);
            R = float3(ca * R.x - sa * R.z, R.y, sa * R.x + ca * R.z);
            float2 euv = float2(atan2(R.x, R.z) / TWO_PI + 0.5f, acos(clamp(R.y, -1.0f, 1.0f)) / PI);
            const int rid = (int)(maps[MAP_REFLECTION].x + 0.5f);
            uint tw, th, lv;
            gTex[rid].GetDimensions(0, tw, th, lv);
            float3 env = pow(saturate(gTex[rid].SampleLevel(sLinWrap, euv, rough * (float)(lv - 1)).rgb), 2.2f);
            envRefl = env * maps[MAP_REFLECTION].y;
        }
        else if (probeInfo.x > 0.5f)                              // no Reflection map on the material: use the probes
            envRefl = ProbeLookup(reflect(-V, N), input.worldPos, rough, ambientColor.rgb, 0.0f);
        float3 ambDiff = ambientColor.rgb;
        if (probeInfo.x > 0.5f) ambDiff = ProbeLookup(N, input.worldPos, 1.0f, ambientColor.rgb, 1.0f);

        float3 ambSpec = envRefl * EnvBRDFApprox(f0, rough, NoV) * ao;
        float3 diff = albedo * (1.0f - metallic) * (ambDiff * ao + diffuseSum);

        float3 emissive = pow(saturate(pbrB.rgb), 2.2f) * pbrA.w;

        if (HasMap(MAP_EMISSIVE))
        {
            float3 em = pow(saturate(FetchMap(MAP_EMISSIVE, mu).rgb), 2.2f);
            float3 tint = (pbrB.r + pbrB.g + pbrB.b < 0.003f) ? float3(1, 1, 1) : pow(saturate(pbrB.rgb), 2.2f);
            emissive = em * tint * pbrA.w * maps[MAP_EMISSIVE].y;
        }

        if (blend == 2) { diff *= opacity; emissive *= opacity; outA = opacity; }   // premultiplied: specular stays at full strength
        color = diff + specSum + ambSpec + emissive;
    }


    if (objPad.x > 1.5f) color += float3(0.25f, 0.12f, 0.0f);
    // HDR: keep values above 1.0 (the tone-map pass decides what to do with them). The cap only guards against INF in the float target.
    return float4(pow(clamp(color, 0.0f, 1000.0f), 1.0f / 2.2f) * splatW, outA);
    //   return float4(pow(saturate(color), 1.0f / 2.2f) * splatW, outA);
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
    else if (type == 5)               // sky: a cloud
    {
        bool c1 = length(p - float2(-0.35f, -0.05f)) < 0.38f;
        bool c2 = length(p - float2(0.05f, 0.20f)) < 0.50f;
        bool c3 = length(p - float2(0.50f, -0.05f)) < 0.33f;
        bool c4 = abs(p.x - 0.08f) < 0.72f && p.y > -0.38f && p.y < 0.0f;
        inside = c1 || c2 || c3 || c4;
    }
    else if (type == 6)               // spot light: lamp head + cone
    {
        bool head = abs(p.x) < 0.30f && p.y > 0.25f && p.y < 0.85f;
        bool beam = p.y <= 0.25f && p.y > -0.9f && abs(p.x) < 0.30f + (0.25f - p.y) * 0.55f;
        inside = head || beam;
    }
    else if (type == 7)               // particle system: a flame
    {
        bool lower = length(p - float2(0.0f, -0.25f)) < 0.52f;
        bool tip = p.y >= -0.25f && p.y < 0.95f && abs(p.x) < (0.95f - p.y) * 0.45f;
        inside = lower || tip;
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
    // The offscreen image already holds gamma-encoded colors, so it is copied as is.
    // mapParams.w comes from the blit constant buffer: 0 = linear, 1 = point (Scaling filter in the View menu).
    // The offscreen target has a single mip, so SampleLevel(…, 0) is exact and avoids gradient warnings.
    float3 c = (mapParams.w > 0.5f) ? gTexture.SampleLevel(sPtClamp,  input.uv, 0).rgb
                                    : gTexture.SampleLevel(sLinClamp, input.uv, 0).rgb;
    return float4(c, 1.0f);
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
    // Always mip 0: UI text and icons never need the smaller mips, and those bleed neighbouring glyph cells together
    float4 t = gTexture.SampleLevel(sLinClamp, input.uv, 0);
    return float4(pow(saturate(t.rgb), 1.0f / 2.2f) * input.color.rgb, t.a * input.color.a);
}

// ---------------- Sky ----------------
struct SkyPSInput
{
    float4 pos : SV_POSITION;
    float3 dir : TEXCOORD0;
};

// Horizontal cross as seen from inside:   .   Top  .   .
//                                       Left Front Right Back      Front = +Z (where the camera starts)
//                                         .  Bottom .   .
float2 SkyCrossUV(float3 d, out float2 cell)
{
    float3 a = abs(d);
    float2 st;
    if (a.x >= a.y && a.x >= a.z)
    {
        if (d.x > 0.0f) { cell = float2(2, 1); st = float2(-d.z, d.y) / a.x; }
        else { cell = float2(0, 1); st = float2(d.z, d.y) / a.x; }
    }
    else if (a.y >= a.z)
    {
        if (d.y > 0.0f) { cell = float2(1, 0); st = float2(d.x, -d.z) / a.y; }
        else { cell = float2(1, 2); st = float2(d.x, d.z) / a.y; }
    }
    else
    {
        if (d.z > 0.0f) { cell = float2(1, 1); st = float2(d.x, d.y) / a.z; }
        else { cell = float2(3, 1); st = float2(-d.x, d.y) / a.z; }
    }
    return float2(st.x * 0.5f + 0.5f, 0.5f - st.y * 0.5f);
}

// ---------------- Sun body helpers ----------------
float Hash31(float3 p)
{
    p = frac(p * 0.1031f);
    p += dot(p, p.zyx + 31.32f);
    return frac((p.x + p.y) * p.z);
}

float Noise3(float3 x)
{
    float3 i = floor(x), f = frac(x);
    f = f * f * (3.0f - 2.0f * f);
    float n000 = Hash31(i), n100 = Hash31(i + float3(1, 0, 0));
    float n010 = Hash31(i + float3(0, 1, 0)), n110 = Hash31(i + float3(1, 1, 0));
    float n001 = Hash31(i + float3(0, 0, 1)), n101 = Hash31(i + float3(1, 0, 1));
    float n011 = Hash31(i + float3(0, 1, 1)), n111 = Hash31(i + float3(1, 1, 1));
    return lerp(lerp(lerp(n000, n100, f.x), lerp(n010, n110, f.x), f.y),
        lerp(lerp(n001, n101, f.x), lerp(n011, n111, f.x), f.y), f.z);
}

float Fbm3(float3 p)
{
    float a = 0.5f, s = 0.0f;
    [unroll] for (int i = 0; i < 4; i++) { s += a * Noise3(p); p = p * 2.03f + 17.1f; a *= 0.5f; }
    return s;
}

float3 PlasmaRamp(float v)           // dark red -> orange -> yellow -> white hot (display colours)
{
    v = saturate(v);
    float3 c = lerp(float3(0.55f, 0.08f, 0.01f), float3(1.0f, 0.38f, 0.04f), smoothstep(0.0f, 0.4f, v));
    c = lerp(c, float3(1.0f, 0.78f, 0.25f), smoothstep(0.35f, 0.75f, v));
    return lerp(c, float3(1.0f, 0.97f, 0.85f), smoothstep(0.7f, 1.0f, v));
}

// Tileable animated caustic web (period 1 in uv)
float SunCaustic(float2 uv, float t)
{
    const float TAU = 6.28318530718f;
    float2 p = uv * TAU;
    p = p - TAU * floor(p / TAU) - 250.0f;
    float2 i = p;
    float c = 1.0f;
    [unroll] for (int n = 0; n < 5; n++)
    {
        float tt = t * (1.0f - (3.5f / (float)(n + 1)));
        i = p + float2(cos(tt - i.x) + sin(tt + i.y), sin(tt - i.y) + cos(tt + i.x));
        float sx = sin(i.x + tt) / 0.005f, sy = cos(i.y + tt) / 0.005f;
        sx = abs(sx) < 1e-4f ? 1e-4f : sx;
        sy = abs(sy) < 1e-4f ? 1e-4f : sy;
        c += 1.0f / max(length(float2(p.x / sx, p.y / sy)), 1e-4f);
    }
    c /= 5.0f;
    c = 1.17f - pow(c, 1.4f);
    return pow(abs(c), 8.0f);
}


float4 SkyPS(SkyPSInput input) : SV_TARGET
{
    float3 d = normalize(input.dir);
    float  dd = max(length(ddx(d)), length(ddy(d)));          // angular size of a pixel, for the mip level

    float hh = sky[1].w;
    float gb = max(sky[3].w, 0.001f);
    const bool showGround = sky[12].y > 0.5f;
    float groundAmt = showGround ? 1.0f - smoothstep(hh - gb, hh, d.y) : 0.0f;
    const float dyEff = showGround ? d.y : hh + abs(d.y - hh);       // no ground: the sky mirrors below the horizon

    const int kind = (int)(sky[12].x + 0.5f);       // 0 gradient, 1 layer, 2 sun sprite, 3 moon sprite, 4 clouds
    if (kind == 0)
    {
        // ---- gradient, dusk band, sun, moon, ground ----
        float t = saturate((dyEff - hh) / max(sky[2].w, 0.01f));
        t = pow(t, max(sky[7].w, 0.05f));
        float3 col = lerp(sky[2].rgb, sky[1].rgb, t);

        float3 sh = float3(sky[0].x, 0.0f, sky[0].z);
        float3 dh = float3(d.x, 0.0f, d.z);
        float shl = length(sh), dhl = length(dh);
        float facing = (shl > 1e-4f && dhl > 1e-4f) ? 0.5f + 0.5f * dot(sh / shl, dh / dhl) : 0.5f;
        float band = 1.0f - saturate(max(dyEff, 0.0f) / 0.45f);
        col = lerp(col, sky[4].rgb, saturate(sky[4].w * band * band * (0.35f + 0.65f * facing)));

        float sd = dot(d, sky[0].xyz);
        float sunDisc = smoothstep(0.0f, 0.08f, (sd - sky[5].w) / max(1.0f - sky[5].w, 1e-6f));
        float glow = pow(saturate(sd), sky[8].y) * sky[8].x;
        col += sky[5].rgb * (sunDisc * sky[8].z * sky[14].z + glow) * sky[12].z;

        float md = dot(d, sky[6].xyz);
        float moonDisc = smoothstep(0.0f, 0.1f, (md - sky[6].w) / max(1.0f - sky[6].w, 1e-6f));
        float moonGlow = pow(saturate(md), sky[14].y) * sky[14].x * (1.0f - sky[0].w);   // glow only at night
        col += sky[7].rgb * (moonDisc * sky[14].w + moonGlow) * sky[12].w;

        col = lerp(col, sky[3].rgb, groundAmt);
        return float4(saturate(col * sky[8].w), 1.0f);
    }

    if (kind == 2 || kind == 3)          // sun / moon texture + thin halo ring (+ eclipse cutoff on the moon)
    {
        float3 sd = normalize(sky[13].xyz);
        float3 upRef = (abs(sd.y) > 0.99f) ? float3(0, 0, 1) : float3(0, 1, 0);
        float3 sr = normalize(cross(upRef, sd));
        float3 su = cross(sd, sr);
        float  z = dot(d, sd);
        float2 pr = float2(dot(d, sr), dot(d, su)) / (max(z, 1e-3f) * tan(max(sky[13].w, 1e-4f)));
        float  r = length(pr);
        float2 egx = ddx(pr), egy = ddy(pr);                         // gradients for the eclipse mask

        float haloStr = sky[11].x;
        float haloW = max(sky[11].y, 0.005f);
        float haloOv = sky[11].z;
        float haloC = 1.0f - haloOv + haloW * 0.5f;
        float ext = max(1.0f, 1.0f - haloOv + haloW) + 0.02f;

        float  rc = cos(sky[10].y), rs = sin(sky[10].y);
        float2 p = float2(rc * pr.x - rs * pr.y, rs * pr.x + rc * pr.y);
        float2 uv = float2(p.x * 0.5f + 0.5f, 0.5f - p.y * 0.5f);
        float2 gx = ddx(uv), gy = ddy(uv);
        if (z <= 0.0f || r > ext) discard;

        float4 t = float4(0, 0, 0, 0);
        if (abs(p.x) <= 1.0f && abs(p.y) <= 1.0f) t = gTexture.SampleGrad(sLinClamp, uv, gx, gy);
        float3 enc = pow(saturate(t.rgb), 1.0f / 2.2f);
        float  m = max(enc.r, max(enc.g, enc.b));
        float  mask = lerp(1.0f, smoothstep(0.02f, 0.25f, m), sky[10].w);
        float  aT = t.a * mask * sky[9].w;

        float ring = 1.0f - smoothstep(0.0f, haloW * 0.5f, abs(r - haloC));
        float aH = saturate(ring * haloStr * sky[11].w);

        // eclipse: a circle (or the cutoff texture, white = hidden) slid over the body
        float occ = 0.0f;
        if (sky[14].x < 50.0f)
        {
            float2 po = pr - float2(cos(sky[14].y), sin(sky[14].y)) * sky[14].x;
            int    eid = min((int)(sky[14].w + 0.5f), 255);
            if (eid > 0)
            {
                if (abs(po.x) <= 1.0f && abs(po.y) <= 1.0f)
                {
                    float4 et = gTex[eid].SampleGrad(sLinClamp, po * float2(0.5f, -0.5f) + 0.5f, egx * 0.5f, egy * 0.5f);
                    occ = saturate(et.a * dot(et.rgb, float3(0.299f, 0.587f, 0.114f)));
                }
            }
            else occ = 1.0f - smoothstep(1.0f - max(sky[14].z, 0.004f), 1.0f, length(po));
        }
        aT *= 1.0f - occ;
        aH *= 1.0f - occ;

        float  a = aH + aT * (1.0f - aH);
        float3 rgb = (sky[15].rgb * aH + enc * sky[9].rgb * aT * (1.0f - aH)) / max(a, 1e-4f);
        return float4(rgb, a * (1.0f - groundAmt));
    }

    if (kind == 4)                       // clouds: a texture on a plane overhead, scrolled in U / V
    {
        float  h = d.y;
        float2 cuv = d.xz / (max(h, 0.0f) + 0.12f) * sky[11].x + sky[10].yz;
        float2 cgx = ddx(cuv), cgy = ddy(cuv);
        float4 t = gTexture.SampleGrad(sLinWrap, cuv, cgx, cgy);
        float  v = (sky[10].x < 0.5f) ? t.a
            : pow(saturate(dot(t.rgb, float3(0.2126f, 0.7152f, 0.0722f))), 1.0f / 2.2f);
        float  cut = 1.0f - saturate(sky[10].w);                 // coverage
        float  dens = smoothstep(cut, cut + 0.4f, v);
        // translucency changes as the clouds move: sky-fixed bands that also drift with the scroll
        float  ph = dot(d.xz, float2(1.3f, 0.9f)) * sky[11].w + (sky[10].y + sky[10].z) * 0.5f;
        float  trans = lerp(1.0f, 0.5f + 0.5f * sin(6.2831853f * ph), sky[11].z);
        float  horizon = smoothstep(0.0f, max(sky[11].y, 1e-3f), h);
        float3 enc = pow(saturate(t.rgb), 1.0f / 2.2f);
        return float4(enc * sky[9].rgb, dens * sky[9].w * trans * horizon * (1.0f - groundAmt));
    }

    if (kind == 5)                       // sun body: plasma + volume scattering + refraction + caustics + subsurface
    {
        float3 sd = normalize(sky[13].xyz);
        float3 upRef = (abs(sd.y) > 0.99f) ? float3(0, 0, 1) : float3(0, 1, 0);
        float3 sr = normalize(cross(upRef, sd));
        float3 su = cross(sd, sr);
        float  z = dot(d, sd);
        float2 pr = float2(dot(d, sr), dot(d, su)) / (max(z, 1e-3f) * tan(max(sky[13].w, 1e-4f)));
        float2 pgx = ddx(pr), pgy = ddy(pr);
        if (z <= 0.0f || dot(pr, pr) > 1.5f) discard;

        float  r = length(pr);
        float  tm = sky[10].x * sky[10].y;                         // time x plasma speed
        float  turb = sky[10].z;
        int    heatId = min((int)(sky[10].w + 0.5f), 255);
        float  scatter = sky[11].x, density = sky[11].y, refr = sky[11].z, caus = sky[11].w;
        float  pScale = sky[14].x, cScale = sky[14].y, cTime = sky[10].x * sky[14].z, heatAmt = sky[14].w;
        float  heatTile = sky[15].x, sss = sky[15].y, edge = max(sky[15].z, 0.005f), limb = sky[15].w;

        // refraction: the silhouette ripples, and the inside is seen through a warped lens
        float  rim = Noise3(float3(pr * 3.0f, tm * 2.0f + 1.7f)) - 0.5f;
        float  rd = r + rim * refr * 0.12f;
        float  mask = 1.0f - smoothstep(1.0f - edge, 1.0f, rd);
        if (mask <= 0.002f) discard;

        float  zf = sqrt(saturate(1.0f - r * r));                  // sphere height under this pixel (1 centre, 0 rim)
        float2 wob = float2(Noise3(float3(pr * 2.5f, tm + 3.7f)),
            Noise3(float3(pr * 2.5f + 9.1f, tm + 1.3f))) - 0.5f;
        float2 pw = pr * (1.0f - (1.0f - zf) * refr * 0.6f) + wob * refr * 0.5f;

        // volume: 4 slices from the front of the sun to the back, domain-warped plasma in each
        float3 acc = float3(0, 0, 0);
        float  T = 1.0f, vSurf = 0.0f;
        const float dz = 2.0f * max(zf, 0.05f) / 4.0f;
        [unroll] for (int k = 0; k < 4; k++)
        {
            float  f = ((float)k + 0.5f) / 4.0f;                   // 0 = front, 1 = back
            float3 q = float3(pw, zf * (1.0f - 2.0f * f)) * pScale;
            float3 warp = float3(Noise3(q + float3(tm, 0.0f, 0.0f)),
                Noise3(q + float3(0.0f, tm, 3.1f)),
                Noise3(q + float3(5.2f, 1.7f, tm))) - 0.5f;
            float  v = Fbm3(q + warp * turb * 2.0f + float3(0.0f, 0.0f, tm));
            if (heatId > 0)                                        // heat texture drives the plasma, scrolling through the layers
            {
                float2 uvH = (pw * 0.5f + 0.5f) * heatTile + float2(tm * 0.35f + f * 0.21f, tm * 0.2f - f * 0.13f);
                float  hv = dot(gTex[heatId].SampleGrad(sLinWrap, uvH, pgx * 0.5f * heatTile, pgy * 0.5f * heatTile).rgb,
                    float3(0.299f, 0.587f, 0.114f));
                v = lerp(v, hv, heatAmt);
            }
            if (k == 0) vSurf = v;
            float  sig = density * (0.4f + saturate(v * 1.3f)) * (0.35f + scatter);
            float  stepT = exp(-sig * dz);
            float3 e = PlasmaRamp(v * 1.35f);
            acc += T * (1.0f - stepT) * e * (1.0f + scatter * (1.0f - f) * 1.5f);   // front slices glow brighter: depth
            T *= stepT;
        }
        float3 col = acc / max(1.0f - T, 1e-3f);

        col *= 1.0f - limb * (1.0f - sqrt(zf));                    // limb darkening

        float2 dirp = pr / max(r, 0.05f);                          // radial light shafts
        float  shafts = Noise3(float3(dirp * 2.5f, r * 2.5f - tm * 2.5f));
        col += PlasmaRamp(0.8f) * shafts * scatter * saturate(1.0f - r) * 0.45f;

        float2 cuv = pw * 0.5f * cScale;                           // caustics, bent by the refraction
        float  cau = SunCaustic(cuv, cTime + 10.0f) + 0.6f * SunCaustic(cuv * 1.7f + 0.31f, cTime * 0.7f + 3.0f);
        col += float3(1.0f, 0.82f, 0.45f) * cau * caus * (0.4f + 0.6f * zf);

        float  limbK = pow(saturate(1.0f - zf), 2.2f);             // subsurface: glow from within, rim takes the colour
        float  inner = exp(-r * r * 3.0f) * (1.0f - T);
        col += sky[8].rgb * sss * (limbK * (0.6f + 0.8f * vSurf) + 0.5f * inner);
        col = lerp(col, col * sky[8].rgb * 2.0f, saturate(sss * limbK * 0.5f));

        return float4(saturate(col * sky[9].rgb), mask * sky[9].w * (1.0f - groundAmt));
    }

    if (kind == 6 || kind == 7)          // glare: overexposed core with a hard cutoff, stronger when the camera looks at it
    {
        float3 sd = normalize(sky[13].xyz);
        float  ang = atan2(length(cross(d, sd)), dot(d, sd));      // precise at tiny angles
        float  x = ang / max(sky[10].x, 1e-3f);
        float  look = smoothstep(0.35f, 0.98f, dot(normalize(view[2].xyz), sd));   // 1 when it is in the middle of the view
        float  gain = lerp(1.0f, lerp(0.4f, 2.2f, look), sky[10].z);
        float  I = sky[9].w * gain * (exp(-x * x * 6.0f) + 0.25f / (1.0f + 14.0f * x * x));
        float  v = I * sky[10].y;                                  // overexposure: above 1 it clips to solid white
        float3 c = lerp(sky[9].rgb, float3(1, 1, 1), saturate((v - 0.5f) * 1.5f));
        return float4(c, saturate(v) * (1.0f - groundAmt));
    }


    // ---- a texture layer: black becomes transparent ----
    float3 q = d;
    if (sky[10].z > 0.5f) q.x = -q.x;                          // flip horizontal
    float cs = cos(sky[10].y), sn = sin(sky[10].y);
    q = float3(cs * q.x - sn * q.z, q.y, sn * q.x + cs * q.z); // rotate around Y

    float texW = sky[11].z, texH = sky[11].w;
    float3 lin;
    if (sky[10].x < 0.5f)                                      // cube cross
    {
        float2 cell;
        float2 f = SkyCrossUV(q, cell);
        float cw = texW * 0.25f, ch = texH / 3.0f;
        float lod = log2(max(dd * 0.75f * cw, 1.0f));
        float2 inset = min(0.5f * exp2(lod) / float2(cw, ch), 0.49f);   // keeps filtering inside the face
        float2 uv = (cell + clamp(f, inset, 1.0f - inset)) / float2(4.0f, 3.0f);
        lin = gTexture.SampleLevel(sLinClamp, uv, lod).rgb;
    }
    else                                                       // equirectangular
    {
        float2 uv = float2(atan2(q.x, q.z) / 6.2831853f + 0.5f, acos(clamp(q.y, -1.0f, 1.0f)) / 3.14159265f);
        float lod = log2(max(dd * texW / 6.2831853f, 1.0f));
        lin = gTexture.SampleLevel(sLinWrap, uv, lod).rgb;
    }

    float3 enc = pow(saturate(lin), 1.0f / 2.2f);              // the target holds display colors
    float m = max(enc.r, max(enc.g, enc.b));
    float mask = lerp(1.0f, smoothstep(sky[11].x, sky[11].x + max(sky[11].y, 1e-4f), m), sky[10].w);
    return float4(enc * sky[9].rgb, sky[9].w * mask * (1.0f - groundAmt));
}

// ---------------- Shadows: depth-only pass ----------------
cbuffer ShadowParams : register(b2)
{
    row_major float4x4 lightVP;
    float4 lightPosFar;
    float4 shadowMode;
};

struct ShadowPSInput
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
    float3 worldPos : TEXCOORD1;
};

float ShadowPS(ShadowPSInput input) : SV_Depth
{
    // sprites cast the shadow of their opaque pixels (same frame mapping as the main pass)
    if (((int)(objPad.y + 0.5f) & 1) != 0)
    {
        float2 uv = (input.uv - 0.5f) * uvParams.xy + 0.5f + uvParams.zw;
        if (gTexture.SampleLevel(sPtClamp, uv, 0).a < 0.5f) discard;
    }
    if (shadowMode.x > 0.5f)                                  // point light: distance / far
        return saturate(length(input.worldPos - lightPosFar.xyz) / max(lightPosFar.w, 1e-3f));
    return input.pos.z;                                       // directional: orthographic depth
}


