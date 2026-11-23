// GhostFog.hlsl — volumetric fog + god rays. Includes the scene shader for the shared cbuffer, shadow and probe helpers.
#include "GhostPS.hlsl"

Texture2D<float>  gFogDepth : register(t10);     // scene depth (R24_UNORM_X8)
Texture2D<float4> gFogScene : register(t11);     // copy of the scene colour (gamma-encoded HDR)

// 4 * pi * Henyey-Greenstein: 1 for isotropic scattering. cosT = dot(ray direction, direction to the light)
float HGPhase(float cosT, float g)
{
    float g2 = g * g;
    return (1.0f - g2) / pow(max(1.0f + g2 - 2.0f * g * cosT, 1e-3f), 1.5f);
}

// Cheap shadow lookup for fog samples: one bilinear tap, no normal offset
float ShadowFactorFog(Light L, float3 worldPos, int type)
{
    const float  farW = max(L.shadow.y, 1e-3f);
    const float3 rel = worldPos - L.posRange.xyz;
    const float  dist = length(rel);
    const float  texel = ((type == 0) ? L.shadow.z : L.shadow.z * dist) * 1.5f;
    const float  bias = (texel * 2.0f * shadowTune.z) / farW;

    float  slice = L.shadow.x;
    float4 clip;
    float  refDepth;
    if (type == 0)
    {
        clip = mul(shadowVP[(int)slice], float4(worldPos, 1.0f));
        refDepth = min(clip.z, 1.0f) - bias;
    }
    else if (type == 3)
    {
        clip = mul(shadowVP[(int)slice], float4(worldPos, 1.0f));
        refDepth = dist / farW - bias;
    }
    else
    {
        float3 a = abs(rel);
        int face = (a.x >= a.y && a.x >= a.z) ? (rel.x > 0.0f ? 0 : 1)
            : ((a.y >= a.z) ? (rel.y > 0.0f ? 2 : 3) : (rel.z > 0.0f ? 4 : 5));
        slice += face;
        clip = mul(shadowVP[(int)slice], float4(worldPos, 1.0f));
        refDepth = dist / farW - bias;
    }
    if (clip.w <= 1e-4f) return 1.0f;
    float2 uv = float2(clip.x / clip.w * 0.5f + 0.5f, 0.5f - clip.y / clip.w * 0.5f);
    if (uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f) return 1.0f;
    return ShadowTapBilinear(uv, slice, refDepth);
}

// Light scattered toward the camera at point p (sum over the lights that have god rays on)
float3 FogLightScatter(float3 p, float3 rd, float g)
{
    float3 sum = float3(0.0f, 0.0f, 0.0f);
    [loop] for (uint i = 0; i < lightCount; i++)
    {
        Light L = lights[i];
        const float vk = L.shadow.w;
        if (vk <= 0.0f) continue;
        const int type = (int)(L.dirType.w + 0.5f);
        float3 toLight = float3(0.0f, 1.0f, 0.0f);
        float  atten = 1.0f;
        if (type == 0) toLight = -normalize(L.dirType.xyz);
        else
        {
            float3 d = L.posRange.xyz - p;
            float  dist = length(d);
            toLight = d / max(dist, 1e-4f);
            atten = RangeFalloff(dist, L.posRange.w);
            if (type == 3) atten *= smoothstep(L.rectRight.w, L.rectUp.w, dot(-toLight, normalize(L.dirType.xyz)));
            else if (type == 2) atten *= saturate(dot(-toLight, normalize(L.dirType.xyz)));
        }
        if (atten <= 0.001f) continue;
        if (L.shadow.x >= 0.0f && shadowGlobal.x > 0.0f)
            atten *= lerp(1.0f, ShadowFactorFog(L, p, type), shadowGlobal.x);
        sum += L.colorIntensity.rgb * L.colorIntensity.w * (atten * vk * HGPhase(dot(rd, toLight), g));
    }
    return sum;
}

// Two tiled samples of a noise texture (world XZ and a skewed XY/ZY plane). Averages ~1 for a mid-grey noise texture.
float FogNoise(int texId, float3 p, float scale, float2 off)
{
    const int id = clamp(texId, 0, 255);
    float a = gTex[id].SampleLevel(sLinWrap, p.xz * scale + off, 0).r;
    float b = gTex[id].SampleLevel(sLinWrap, float2(p.x + p.y * 0.7f, p.z - p.y * 0.4f) * (scale * 0.83f) - off * 0.7f + 0.37f, 0).r;
    return clamp(a + b, 0.0f, 2.0f);
}

float4 FogPS(float4 pos : SV_POSITION) : SV_TARGET
{
    const int2   px = int2(pos.xy);
    const float3 sceneEnc = gFogScene.Load(int3(px, 0)).rgb;
    const bool   globalOn = fogA.x > 0.5f;
    const int    nVol = min((int)(fogG.w + 0.5f), MAX_FOGVOLS);
    if (!globalOn && nVol == 0) return float4(sceneEnc, 1.0f);

    // ---- the ray through this pixel (uses the same, possibly jittered, projection the scene was drawn with) ----
    const float2 ndc = float2(pos.x / fogCam[5].z * 2.0f - 1.0f, 1.0f - pos.y / fogCam[5].w * 2.0f);
    const float3 rdU = fogCam[1].xyz * ((ndc.x - fogCam[4].z) / fogCam[4].x)
                     + fogCam[2].xyz * ((ndc.y - fogCam[4].w) / fogCam[4].y)
                     + fogCam[3].xyz;
    const float  rl = length(rdU);
    const float3 rd = rdU / rl;
    const float3 ro = fogCam[0].xyz;

    const float d = gFogDepth.Load(int3(px, 0));
    const bool  isSky = d >= 0.99999f;
    float tHit = fogC.y;
    if (!isSky) tHit = (fogCam[5].y / (d - fogCam[5].x)) * rl;           // linear view depth -> distance along the ray
    const float tStart = fogC.x;
    const float tEnd = min(tHit, fogC.y);
    if (tEnd <= tStart + 0.001f) return float4(sceneEnc, 1.0f);

    const int   steps = clamp((int)(fogD.z + 0.5f), 4, 64);
    const float ds = (tEnd - tStart) / (float)steps;
    const float ign = frac(52.9829189f * frac(dot(pos.xy + fogD.w, float2(0.06711056f, 0.00583715f))));

    // ---- fog colour: flat colour, a reflection capture, or a lat-long texture, along the view ray ----
    float3 baseCol = fogB.rgb;
    const int envMode = (int)(fogE.x + 0.5f);
    if (envMode == 1 && probeInfo.x > 0.5f)
        baseCol = lerp(baseCol, ProbeLookup(rd, ro + rd * (0.5f * (tStart + tEnd)), 1.0f, baseCol, 0.0f), fogE.y);
    else if (envMode == 2 && fogE.z > 0.5f)
    {
        const float ca = cos(fogE.w), sa = sin(fogE.w);
        const float3 dd = float3(ca * rd.x - sa * rd.z, rd.y, sa * rd.x + ca * rd.z);
        const float2 euv = float2(atan2(dd.x, dd.z) / TWO_PI + 0.5f, acos(clamp(dd.y, -1.0f, 1.0f)) / PI);
        const int eid = clamp((int)(fogE.z + 0.5f), 0, 255);
        uint tw, th, lv;
        gTex[eid].GetDimensions(0, tw, th, lv);
        const float3 env = pow(saturate(gTex[eid].SampleLevel(sLinWrap, euv, (float)(lv - 1) * 0.6f).rgb), 2.2f);
        baseCol = lerp(baseCol, env, fogE.y);
    }
    const float ambK = saturate(dot(ambientColor.rgb, float3(0.3333f, 0.3333f, 0.3333f)) / 0.15f);   // 0.15 = the default ambient
    const float bright = fogB.w * lerp(1.0f, ambK, fogD.y);
    const bool  volOn = fogD.x > 0.5f;
    const int   gNoise = (int)(fogF.x + 0.5f);

    // ---- march ----
    float  T = 1.0f;
    float3 S = float3(0.0f, 0.0f, 0.0f);
    [loop] for (int i = 0; i < steps; i++)
    {
        const float  t = tStart + ((float)i + ign) * ds;
        const float3 p = ro + rd * t;

        float  sig = 0.0f;
        float3 colAcc = float3(0.0f, 0.0f, 0.0f);
        float  lightAcc = 0.0f;

        if (globalOn)
        {
            float sg = fogA.y;
            if (fogA.w > 0.0001f) sg *= exp(-fogA.w * max(p.y - fogA.z, 0.0f));
            if (gNoise > 0 && fogF.y > 0.001f) sg *= lerp(1.0f, FogNoise(gNoise, p, fogF.z, fogG.xy), fogF.y);
            sg = max(sg, 0.0f);
            sig += sg;
            colAcc += baseCol * sg;
            lightAcc += sg;
        }

        [loop] for (int v = 0; v < nVol; v++)
        {
            const float3 lp = float3(dot(fogVols[v].row0.xyz, p) + fogVols[v].row0.w,
                                     dot(fogVols[v].row1.xyz, p) + fogVols[v].row1.w,
                                     dot(fogVols[v].row2.xyz, p) + fogVols[v].row2.w);
            const float blend = max(fogVols[v].dens.y, 0.001f);
            float w, hy;
            if (fogVols[v].shp.x < 0.5f)
            {
                const float3 dIn = fogVols[v].shp.yzw - abs(lp);
                w = saturate(min(dIn.x, min(dIn.y, dIn.z)) / blend);
                hy = fogVols[v].shp.z;
            }
            else
            {
                w = saturate((fogVols[v].shp.y - length(lp)) / blend);
                hy = fogVols[v].shp.y;
            }
            if (w <= 0.0f) continue;

            float sv = fogVols[v].dens.x * w;
            const float grad = fogVols[v].dens.z;
            if (grad > 0.001f) sv *= lerp(1.0f, 1.0f - saturate((lp.y + hy) / (2.0f * hy)), grad);
            const int nt = (int)(fogVols[v].nz.x + 0.5f);
            if (nt > 0 && fogVols[v].nz.y > 0.001f)
                sv *= lerp(1.0f, FogNoise(nt, p, fogVols[v].nz.z, fogVols[v].scr.xy), fogVols[v].nz.y);
            sv = max(sv, 0.0f);
            sig += sv;
            colAcc += fogVols[v].col.rgb * sv;
            lightAcc += fogVols[v].col.w * sv;
        }

        if (sig <= 1e-6f) continue;
        const float3 col = colAcc / sig;
        const float  stepT = exp(-sig * ds);
        float3 Ls = col * bright;                                     // self-lit fog colour
        if (volOn)
        {
            const float lk = lightAcc / sig;
            if (lk > 0.001f) Ls += col * (FogLightScatter(p, rd, fogC.w) * lk);
        }
        Ls = min(Ls, float3(64.0f, 64.0f, 64.0f));
        S += T * Ls * (1.0f - stepT);                                 // energy-conserving in-scatter for this step
        T *= stepT;
        if (T < 0.004f) break;
    }

    if (isSky) { const float k = saturate(fogC.z); T = lerp(1.0f, T, k); S *= k; }

    // The scene holds gamma-encoded floats: decode, apply the fog in linear light, encode again
    const float3 lin = pow(max(sceneEnc, 0.0f), 2.2f) * T + S;
    return float4(pow(clamp(lin, 0.0f, 1000.0f), 1.0f / 2.2f), 1.0f);
}