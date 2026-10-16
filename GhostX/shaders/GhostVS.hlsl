cbuffer MVPBuffer : register(b0)
{
    row_major float4x4 model;
    row_major float4x4 view;
    row_major float4x4 proj;
    float4 baseColor;
    float  useVertexColor;
    float3 padObj;
};

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
    float4 viewPos = mul(view, worldPos);
    output.pos = mul(proj, viewPos);
    output.worldPos = worldPos.xyz;
    output.worldNormal = mul(model, float4(input.normal, 0.0f)).xyz;
    // Object-local position scaled by the object's scale, so planar/box projections keep a
// constant texel size when you scale the object
    float3 sc = float3(length(float3(model[0][0], model[1][0], model[2][0])),
        length(float3(model[0][1], model[1][1], model[2][1])),
        length(float3(model[0][2], model[1][2], model[2][2])));
    output.localPos = input.pos * sc;
    output.localNormal = input.normal;
    output.color = (useVertexColor > 0.5f) ? input.color : float4(baseColor.rgb, 1.0f);
    if (padObj.x > 0.5f)
        output.color.rgb = lerp(output.color.rgb, float3(1.0f, 0.6f, 0.1f),
            padObj.x > 1.5f ? 0.6f : 0.35f);
    // Pass UV through for 1.5
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