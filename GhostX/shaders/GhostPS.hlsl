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

struct PSInput
{
    float4 pos : SV_POSITION;
    float4 color : COLOR;
    float2 uv : TEXCOORD0;
    float3 worldPos : TEXCOORD1;
    float3 worldNormal : TEXCOORD2;
};

float RangeFalloff(float dist, float range)
{
    float r = saturate(1.0f - pow(saturate(dist / max(range, 0.001f)), 4.0f));
    return (r * r) / (dist * dist + 1.0f);
}

float4 PSMain(PSInput input, bool isFront : SV_IsFrontFace) : SV_TARGET
{
    // Culling is off, so flip the normal on back faces
    float3 N = normalize(input.worldNormal);
    if (!isFront) N = -N;

    // Light in linear space, encode to gamma at the end
    float3 albedo = pow(input.color.rgb, 2.2f);
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