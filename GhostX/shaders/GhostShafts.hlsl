// GhostShafts.hlsl - screen-space light shafts. Put in shaders/ (copy to output like the other .hlsl files).
// Every pixel marches toward the light's screen position and collects the light that the scene emits along the way
// (bright pixels = open sky / sun / lit clouds). Trees and mountains block the rays, which makes the shafts.

Texture2D<float4> gScene : register(t0);     // copy of the HDR scene colour
Texture2D<float4> gDepth : register(t1);     // scene depth, .r = hardware depth (sky = 1.0)
SamplerState      gLin : register(s0);

cbuffer ShaftCB : register(b0)
{
    float4 tint;    // rgb = light colour * intensity * bloom scale * bloom tint * fade, w = 1 when the depth occlusion mask is on
    float4 sun;     // xy = light position on screen (uv, may be off-screen), z = march length (0..1), w = per-step decay
    float4 occl;    // x = occluder darkness (1 = none), y = occlusion depth range (world), z = threshold, w = max brightness
    float4 proj;    // x = proj[2][2], y = proj[2][3], z = width, w = height
    float4 misc;    // x = steps, y = noise phase
};

struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };

VSOut VSMain(uint id : SV_VertexID)
{
    VSOut o;
    o.uv = float2((id << 1) & 2, id & 2);
    o.pos = float4(o.uv.x * 2.0 - 1.0, 1.0 - o.uv.y * 2.0, 0.0, 1.0);
    return o;
}

float LinearDepth(float d) { return proj.y / (d - proj.x); }      // LH perspective: d = 1 -> far plane

// What a pixel contributes to the rays
float3 Source(float2 uv)
{
    if (uv.x < 0.0 || uv.y < 0.0 || uv.x >= 1.0 || uv.y >= 1.0) return 0.0.xxx;

    int2 px = min(int2(uv * proj.zw), int2(proj.zw) - 1);
    float3 c = gScene.Load(int3(px, 0)).rgb;

    // bloom: only the part of the brightness above the threshold, clamped to the max brightness
    float  l = dot(c, float3(0.299, 0.587, 0.114));
    float  k = min(max(l - occl.z, 0.0), occl.w) / max(l, 1e-4);
    float3 s = c * k;

    // occlusion: anything in front of the depth range blocks the light (sky = depth 1 never blocks)
    if (tint.w > 0.5)
    {
        float d = gDepth.Load(int3(px, 0)).r;
        float open = (d >= 0.99999 || LinearDepth(d) >= occl.y) ? 1.0 : occl.x;
        s *= open;
    }
    return s;
}

float4 PSMain(VSOut i) : SV_Target
{
    float2 uv = i.uv;
    int    steps = (int)misc.x;
    float2 stepv = (sun.xy - uv) * sun.z / (float)steps;

    // jitter the start so the steps don't band (the TAA / frame noise phase moves it)
    float jit = frac(sin(dot(i.pos.xy, float2(12.9898, 78.233))) * 43758.5453 + misc.y);

    float2 p = uv + stepv * jit;
    float  illum = 1.0;
    float3 acc = 0.0.xxx;

    [loop]
    for (int s = 0; s < steps; s++)
    {
        p += stepv;
        acc += Source(p) * illum;
        illum *= sun.w;
    }
    acc /= (float)steps;

    return float4(acc * tint.rgb, 0.0);
}