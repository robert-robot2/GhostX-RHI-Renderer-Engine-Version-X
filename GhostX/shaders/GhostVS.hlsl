cbuffer MVPBuffer : register(b0)          // the full object layout (same as ObjectBuffer in GhostPS.hlsl)
{
    row_major float4x4 model;
    row_major float4x4 view;
    row_major float4x4 proj;
    float4 baseColor;
    float  useVertexColor;
    float3 padObj;
    float4 uvParams;
    float4 mapParams;
    float4 sky[16];
    float4 pbrA;
    float4 pbrB;
    float4 alphaP;
    float4 mapUV;
    float4 maps[13];
    float4 surfA;
    float4 surfB;
    float4 wpoP;
    float4 opacityP;
    float4 tileP[2];      // Tile Map data (the PS reads it). Kept here so the layout lines up
    float4 foliP[3];      // foliage: [0] flags, color variation, cols, rows / [1] anim fps, mesh min Y, mesh height, normal-up / [2] fade start, fade end
};

Texture2D    gTex[256] : register(t0, space1);
SamplerState sLinWrap : register(s0);

float PickChannel(float4 t, float ch)
{
    int c = (int)(ch + 0.5f);
    if (c == 0) return t.r;
    if (c == 1) return t.g;
    if (c == 2) return t.b;
    if (c == 3) return t.a;
    return dot(t.rgb, float3(0.299f, 0.587f, 0.114f));
}

struct VSInput
{
    float3 pos : POSITION;
    float4 color : COLOR;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD;
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

PSInput VSMain(VSInput input)
{
    PSInput output;

    float4 worldPos = mul(model, float4(input.pos, 1.0f));

    // ---- World Position Offset: map displacement along the normal, plus wind sway weighted by the map ----
    if (pbrB.w > 0.5f && (maps[9].x > 0.5f || wpoP.x != 0.0f))
    {
        float mask = 1.0f, disp = 0.0f;
        if (maps[9].x > 0.5f)
        {
            float2 wuv = input.uv * mapUV.xy + mapUV.zw;
            float v = PickChannel(gTex[(int)(maps[9].x + 0.5f)].SampleLevel(sLinWrap, wuv, 0), maps[9].z);
            if (maps[9].w > 0.5f) v = 1.0f - v;
            mask = v;
            disp = (v - wpoP.z) * maps[9].y;
        }
        float3 nW = normalize(mul(model, float4(input.normal, 0.0f)).xyz);
        worldPos.xyz += nW * disp;
        float ph = wpoP.w * wpoP.y;
        worldPos.x += sin(ph + worldPos.y * 2.0f + worldPos.z * 1.3f) * wpoP.x * mask;
        worldPos.z += cos(ph * 0.83f + worldPos.y * 1.7f + worldPos.x * 1.1f) * wpoP.x * mask;
    }

    float4 viewPos = mul(view, worldPos);
    output.pos = mul(proj, viewPos);
    output.worldPos = worldPos.xyz;
    output.worldNormal = mul(model, float4(input.normal, 0.0f)).xyz;
    float3 sc = float3(length(float3(model[0][0], model[1][0], model[2][0])),
        length(float3(model[0][1], model[1][1], model[2][1])),
        length(float3(model[0][2], model[1][2], model[2][2])));
    output.localPos = input.pos * sc;
    output.localNormal = input.normal;
    output.color = (useVertexColor > 0.5f) ? input.color : float4(baseColor.rgb, 1.0f);
    if (padObj.x > 0.5f)
        output.color.rgb = lerp(output.color.rgb, float3(1.0f, 0.6f, 0.1f),
            padObj.x > 1.5f ? 0.6f : 0.35f);
    output.uv = input.uv;
    return output;
}

// ---------------- Icons ----------------
// Icon draws reuse MVPBuffer (b0). Field mapping:
//   model row 0 = world center xyz, icon type (0 sun, 1 bulb, 2 rect, 3 arrow)
//   model row 1 = viewport width, height, state (0 normal, 1 hover, 2 selected), half size in pixels
//   model row 2 = direction xyz (arrow only)
//   baseColor   = icon color
struct IconPSInput
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR;
    nointerpolation float2 info : TEXCOORD1;   // x = type, y = state
};

static const float2 kCorners[6] =
{
    float2(-1, -1), float2(-1, 1), float2(1, 1),
    float2(-1, -1), float2(1, 1),  float2(1, -1)
};

IconPSInput IconVS(uint vid : SV_VertexID)
{
    float3 center = model[0].xyz;
    float  iconType = model[0].w;
    float2 viewSize = model[1].xy;
    float  state = model[1].z;
    float  halfPx = model[1].w;
    float3 dir = model[2].xyz;

    float3 vc = mul(view, float4(center, 1.0f)).xyz;
    float4 clipC = mul(proj, float4(vc, 1.0f));

    IconPSInput o;
    o.uv = kCorners[vid];
    o.color = baseColor;
    o.info = float2(iconType, state);

    if (vc.z < 0.1f)                 // behind the camera: push outside the clip volume
    {
        o.pos = float4(0, 0, 2, 1);
        return o;
    }

    float2 offsetPx = o.uv * halfPx;           // constant screen size

    if (iconType > 2.5f && iconType < 3.5f)   
    {
        float3 vd = mul(view, float4(dir, 0.0f)).xyz;
        float2 d = float2(proj[0][0] * (vd.x * vc.z - vc.x * vd.z) * viewSize.x,
            proj[1][1] * (vd.y * vc.z - vc.y * vd.z) * viewSize.y);
        d = (length(d) > 1e-6f) ? normalize(d) : float2(0, 1);
        float2 perp = float2(-d.y, d.x);
        float  len = halfPx * 4.0f;
        float2 mid = d * (halfPx * 0.7f + len * 0.5f);
        offsetPx = mid + d * (o.uv.x * len * 0.5f) + perp * (o.uv.y * halfPx * 0.6f);
    }

    float2 ndcOffset = offsetPx * 2.0f / viewSize;
    o.pos = float4(clipC.xy + ndcOffset * clipC.w, 0.0f, clipC.w);
    return o;
}
// ---------------- Blit: fullscreen quad that shows the offscreen image ----------------
struct BlitPSInput
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

BlitPSInput BlitVS(uint vid : SV_VertexID)
{
    float2 c = kCorners[vid];
    BlitPSInput o;
    o.pos = float4(c, 0.0f, 1.0f);
    o.uv = float2(c.x * 0.5f + 0.5f, 0.5f - c.y * 0.5f);
    return o;
}

// ---------------- 2D UI ----------------
struct UIVSInput
{
    float2 pos : POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR;
};

struct UIPSInput
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR;
};

UIPSInput UIVS(UIVSInput i)
{
    UIPSInput o;
    o.pos = float4(i.pos, 0.0f, 1.0f);      // positions arrive already in clip space
    o.uv = i.uv;
    o.color = i.color;
    return o;
}
// ---------------- Sky: one full-screen triangle, the view direction per pixel ----------------
struct SkyPSInput
{
    float4 pos : SV_POSITION;
    float3 dir : TEXCOORD0;
};

SkyPSInput SkyVS(uint vid : SV_VertexID)
{
    float2 c = float2((vid << 1) & 2, vid & 2);        // (0,0) (2,0) (0,2)
    float2 ndc = c * 2.0f - 1.0f;                        // (-1,-1) (3,-1) (-1,3)

    float3 v = float3(ndc.x / proj[0][0], ndc.y / proj[1][1], 1.0f);
    SkyPSInput o;
    o.pos = float4(ndc, 0.5f, 1.0f);
    o.dir = view[0].xyz * v.x + view[1].xyz * v.y + view[2].xyz * v.z;   // view rows are the camera axes
    return o;
}

// ---------------- Shadows: depth-only pass from a light ----------------
cbuffer ShadowParams : register(b2)
{
    row_major float4x4 lightVP;
    float4 lightPosFar;          // xyz = light position, w = far distance (point lights)
    float4 shadowMode;           // x: 0 = directional (depth = clip z), 1 = point (depth = distance / far)
};

struct ShadowPSInput
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
    float3 worldPos : TEXCOORD1;
};

ShadowPSInput ShadowVS(VSInput input)
{
    float4 wp = mul(model, float4(input.pos, 1.0f));
    ShadowPSInput o;
    o.pos = mul(lightVP, wp);
    o.uv = input.uv;
    o.worldPos = wp.xyz;
    return o;
}

// ---------------- Overlay: viewport lines, gizmos and text (position is already in clip space) ----------------
struct OverlayVSInput
{
    float3 pos : POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR;
};

UIPSInput OverlayVS(OverlayVSInput i)
{
    UIPSInput o;
    o.pos = float4(i.pos, 1.0f);
    o.uv = i.uv;
    o.color = i.color;
    return o;
}

// ---------------- Fog: one full-screen triangle (everything is reconstructed from depth in the pixel shader) ----------------
float4 FogVS(uint vid : SV_VertexID) : SV_POSITION
{
    float2 c = float2((vid << 1) & 2, vid & 2);
    return float4(c * 2.0f - 1.0f, 0.0f, 1.0f);
}

// ---------------- Foliage: instanced. Vertex stream 0 = mesh, stream 1 = one 32 byte record per instance ----------------
struct FoliVSInput
{
    float3 pos : POSITION;
    float4 color : COLOR;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD;
    float4 iPos : INSTPOS;      // xyz position, w uniform scale
    float4 iRot : INSTROT;      // x yaw (rad), y / z tilt (horizontal offset of the up axis), w seed 0..1
};

struct FoliOut { float3 wp; float3 n; float2 uv; float3 lp; bool dead; };

void FoliBasis(float yaw, float tx, float tz, out float3 R, out float3 U, out float3 F)
{
    U = normalize(float3(tx, 1.0f, tz));
    float3 f0 = float3(sin(yaw), 0.0f, cos(yaw));
    R = normalize(cross(U, f0));       // left handed: cross(up, forward) = right
    F = cross(R, U);
}

FoliOut FoliCore(FoliVSInput i)
{
    FoliOut o;
    const uint  flags = (uint)(foliP[0].x + 0.5f);
    const float3 eye = -(view[0].xyz * view[0].w + view[1].xyz * view[1].w + view[2].xyz * view[2].w);
    const float3 ip = i.iPos.xyz;
    const float  s = i.iPos.w;

    // distance fade: the far part thins out (instances drop out by their seed), then everything is gone
    const float fadeStart = foliP[2].x, fadeEnd = foliP[2].y;
    float keep = 1.0f;
    if (fadeEnd > 0.0f) keep = saturate((fadeEnd - distance(ip, eye)) / max(fadeEnd - fadeStart, 0.001f));
    o.dead = (keep <= 0.0f) || (i.iRot.w >= keep);

    float3 R, U, F;
    if ((flags & 1u) != 0u)            // billboard: turns around Y toward the camera. Quad front is -Z, so +Z points away from the eye
    {
        U = float3(0.0f, 1.0f, 0.0f);
        float3 rr = float3(view[0].x, 0.0f, view[0].z);
        R = rr / max(length(rr), 1e-4f);
        F = cross(R, U);
    }
    else FoliBasis(i.iRot.x, i.iRot.y, i.iRot.z, R, U, F);

    const float3 lp = i.pos * s;
    const float  h = saturate((i.pos.y - foliP[1].y) / max(foliP[1].z, 0.001f));     // wind weight: 0 at the base, 1 at the tip
    float3 wp = ip + R * lp.x + U * lp.y + F * lp.z;
    const float ph = wpoP.w * wpoP.y + i.iRot.w * 6.2831853f;                         // wpoP = the material's WPO: sway / speed / time
    const float sway = wpoP.x * h * h;
    wp.x += sin(ph + ip.x * 0.7f + ip.z * 0.5f) * sway;
    wp.z += cos(ph * 0.83f + ip.z * 0.6f + ip.x * 0.4f) * sway;

    float3 n = normalize(R * i.normal.x + U * i.normal.y + F * i.normal.z);
    n = normalize(lerp(n, float3(0.0f, 1.0f, 0.0f), foliP[1].w));

    float2 uv = i.uv;
    if ((flags & 2u) != 0u)            // sprite sheet: each instance runs its own frame (fps 0 = a fixed random frame per instance)
    {
        const float cols = max(foliP[0].z, 1.0f), rows = max(foliP[0].w, 1.0f), frames = cols * rows;
        float fr = floor(wpoP.w * foliP[1].x + i.iRot.w * frames);
        fr = fr - frames * floor(fr / frames);
        const float c = fr - cols * floor(fr / cols);
        const float r = floor(fr / cols);
        uv = (i.uv + float2(c, r)) / float2(cols, rows);
    }
    o.wp = wp; o.n = n; o.uv = uv; o.lp = lp;
    return o;
}

PSInput FoliageVS(FoliVSInput i)
{
    PSInput o = (PSInput)0;
    FoliOut f = FoliCore(i);
    if (f.dead) { o.pos = float4(2.0f, 2.0f, 2.0f, 1.0f); return o; }       // outside the clip volume
    o.pos = mul(proj, mul(view, float4(f.wp, 1.0f)));
    o.worldPos = f.wp;
    o.worldNormal = f.n;
    o.localPos = f.lp;
    o.localNormal = i.normal;
    o.uv = f.uv;
    const float v = frac(sin(i.iRot.w * 91.3458f) * 47453.5453f);
    const float3 tint = max(baseColor.rgb * (1.0f + (v - 0.5f) * 2.0f * foliP[0].y), 0.0f);
    o.color = (useVertexColor > 0.5f) ? i.color : float4(tint, 1.0f);
    return o;
}

ShadowPSInput FoliageShadowVS(FoliVSInput i)
{
    ShadowPSInput o = (ShadowPSInput)0;
    FoliOut f = FoliCore(i);
    if (f.dead) { o.pos = float4(2.0f, 2.0f, 2.0f, 1.0f); return o; }
    o.pos = mul(lightVP, float4(f.wp, 1.0f));
    o.uv = f.uv;
    o.worldPos = f.wp;
    return o;
}