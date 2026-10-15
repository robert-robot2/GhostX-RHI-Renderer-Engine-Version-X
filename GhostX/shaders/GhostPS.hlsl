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
    float4 sky[16];                       // sky pass parameters (see GhostSky_Build)
    float4 pbrA;                          // metallic, roughness, specular, emissive strength
    float4 pbrB;                          // emissive rgb, w = 1 when this draw uses PBR
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


float4 PSMain(PSInput input, bool isFront : SV_IsFrontFace) : SV_TARGET
{
    // Culling is off, so flip the normal on back faces
    float3 N = normalize(input.worldNormal);
    if (!isFront) N = -N;

    // The texture view is sRGB, so the sample arrives already linear
    float4 texel = MappedTexel(input);
    const int spriteFlags = (int)(objPad.y + 0.5f);
    if ((spriteFlags & 1) != 0 && texel.a < 0.5f) discard;
    float3 albedo = pow(input.color.rgb, 2.2f) * texel.rgb;

    // ---- PBR inputs ----
    const bool  usePbr = pbrB.w > 0.5f;
    const float metallic = saturate(pbrA.x);
    const float rough = clamp(pbrA.y, 0.06f, 1.0f);
    const float alpha = rough * rough;
    float3 eye = -(view[0].xyz * view[0].w + view[1].xyz * view[1].w + view[2].xyz * view[2].w);
    float3 V = normalize(eye - input.worldPos);
    float  NoV = max(dot(N, V), 1e-3f);
    float3 f0 = lerp(float3(0.08f, 0.08f, 0.08f) * saturate(pbrA.z), albedo, metallic);

    float3 diffuseSum = float3(0.0f, 0.0f, 0.0f);
    float3 specSum = float3(0.0f, 0.0f, 0.0f);

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

        float NoL = saturate(dot(N, toLight));
        diffuseSum += radiance * NoL * atten;

        if (usePbr && NoL > 0.0f)
        {
            float3 H = normalize(V + toLight);
            // PI keeps the highlight in step with the diffuse term, which isn't divided by PI
            specSum += radiance * atten * NoL * PI *
                       SpecularGGX(NoL, NoV, saturate(dot(N, H)), saturate(dot(V, H)), f0, alpha);
        }
    }

    float3 color;
    if ((spriteFlags & 2) != 0)             // unlit
    {
        color = albedo;
    }
    else if (!usePbr)                       // sprites and anything without a material
    {
        color = albedo * (ambientColor.rgb + diffuseSum);
    }
    else
    {
        float3 ambSpec = ambientColor.rgb * EnvBRDFApprox(f0, rough, NoV);
        color = albedo * (1.0f - metallic) * (ambientColor.rgb + diffuseSum) + specSum + ambSpec;
    }

    if (usePbr) color += pow(saturate(pbrB.rgb), 2.2f) * pbrA.w;           // emissive
    if (objPad.x > 1.5f) color += float3(0.25f, 0.12f, 0.0f);              // selected-face glow, visible even when unlit
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
    else if (type == 5)               // sky: a cloud
    {
        bool c1 = length(p - float2(-0.35f, -0.05f)) < 0.38f;
        bool c2 = length(p - float2(0.05f, 0.20f)) < 0.50f;
        bool c3 = length(p - float2(0.50f, -0.05f)) < 0.33f;
        bool c4 = abs(p.x - 0.08f) < 0.72f && p.y > -0.38f && p.y < 0.0f;
        inside = c1 || c2 || c3 || c4;
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

float4 SkyPS(SkyPSInput input) : SV_TARGET
{
    float3 d = normalize(input.dir);
    float  dd = max(length(ddx(d)), length(ddy(d)));          // angular size of a pixel, for the mip level

    float hh = sky[1].w;
    float gb = max(sky[3].w, 0.001f);
    float groundAmt = 1.0f - smoothstep(hh - gb, hh, d.y);

    if (sky[12].x < 0.5f)
    {
        // ---- gradient, dusk band, sun, moon, ground ----
        float t = saturate((d.y - hh) / max(sky[2].w, 0.01f));
        t = pow(t, max(sky[7].w, 0.05f));
        float3 col = lerp(sky[2].rgb, sky[1].rgb, t);

        float3 sh = float3(sky[0].x, 0.0f, sky[0].z);
        float3 dh = float3(d.x, 0.0f, d.z);
        float shl = length(sh), dhl = length(dh);
        float facing = (shl > 1e-4f && dhl > 1e-4f) ? 0.5f + 0.5f * dot(sh / shl, dh / dhl) : 0.5f;
        float band = 1.0f - saturate(max(d.y, 0.0f) / 0.45f);
        col = lerp(col, sky[4].rgb, saturate(sky[4].w * band * band * (0.35f + 0.65f * facing)));

        float sd = dot(d, sky[0].xyz);
        float sunDisc = smoothstep(0.0f, 0.08f, (sd - sky[5].w) / max(1.0f - sky[5].w, 1e-6f));
        float glow = pow(saturate(sd), sky[8].y) * sky[8].x;
        col += sky[5].rgb * (sunDisc * sky[8].z + glow);

        float md = dot(d, sky[6].xyz);
        float moonDisc = smoothstep(0.0f, 0.1f, (md - sky[6].w) / max(1.0f - sky[6].w, 1e-6f));
        col += sky[7].rgb * moonDisc;

        col = lerp(col, sky[3].rgb, groundAmt);
        return float4(saturate(col * sky[8].w), 1.0f);
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