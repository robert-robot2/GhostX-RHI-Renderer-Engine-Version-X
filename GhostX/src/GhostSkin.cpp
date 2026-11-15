// GhostSkin.cpp - CPU skinning, 3D animation clips and the .ghostskin file (put in src/)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../include/GhostSkin.h"
#include "../include/GhostImage.h"          // UTF-8 helpers (also pulls in Windows.h)
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/config.h>
#include <algorithm>
#include <exception>
#include <filesystem>
#include <fstream>
#include <math.h>
#include <string.h>

// ============================================================
//  Math: row-major 4x4, column vectors (the layout assimp uses)
// ============================================================
static void Mul4(const float* a, const float* b, float* o)
{
    float t[16];
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            t[r * 4 + c] = a[r * 4 + 0] * b[0 * 4 + c] + a[r * 4 + 1] * b[1 * 4 + c] +
            a[r * 4 + 2] * b[2 * 4 + c] + a[r * 4 + 3] * b[3 * 4 + c];
    memcpy(o, t, sizeof t);
}

static void NormQ(float* q)
{
    const float l = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (l < 1e-8f) { q[0] = q[1] = q[2] = 0.0f; q[3] = 1.0f; return; }
    const float inv = 1.0f / l;
    q[0] *= inv; q[1] *= inv; q[2] *= inv; q[3] *= inv;
}

// m = T * R * S.  R = quaternion x y z w
static void Compose(const float* T, const float* R, const float* S, float* m)
{
    const float x = R[0], y = R[1], z = R[2], w = R[3];
    const float xx = x * x, yy = y * y, zz = z * z, xy = x * y, xz = x * z, yz = y * z, wx = w * x, wy = w * y, wz = w * z;
    m[0] = (1.0f - 2.0f * (yy + zz)) * S[0]; m[1] = 2.0f * (xy - wz) * S[1];          m[2] = 2.0f * (xz + wy) * S[2];          m[3] = T[0];
    m[4] = 2.0f * (xy + wz) * S[0];          m[5] = (1.0f - 2.0f * (xx + zz)) * S[1]; m[6] = 2.0f * (yz - wx) * S[2];          m[7] = T[1];
    m[8] = 2.0f * (xz - wy) * S[0];          m[9] = 2.0f * (yz + wx) * S[1];          m[10] = (1.0f - 2.0f * (xx + yy)) * S[2]; m[11] = T[2];
    m[12] = 0.0f; m[13] = 0.0f; m[14] = 0.0f; m[15] = 1.0f;
}

static void SampleVec3(const std::vector<float>& tt, const std::vector<float>& vv, float t, float* o)
{
    const size_t n = tt.size();
    if (n == 0 || vv.size() < n * 3) return;                       // no keys: leave the rest pose
    if (n == 1 || t <= tt[0]) { o[0] = vv[0]; o[1] = vv[1]; o[2] = vv[2]; return; }
    if (t >= tt[n - 1]) { const float* v = &vv[(n - 1) * 3]; o[0] = v[0]; o[1] = v[1]; o[2] = v[2]; return; }
    const size_t i1 = (size_t)(std::upper_bound(tt.begin(), tt.end(), t) - tt.begin());
    const size_t i0 = i1 - 1;
    const float d = tt[i1] - tt[i0];
    const float f = d > 1e-9f ? (t - tt[i0]) / d : 0.0f;
    for (int k = 0; k < 3; k++) o[k] = vv[i0 * 3 + k] + (vv[i1 * 3 + k] - vv[i0 * 3 + k]) * f;
}

static void SampleQuat(const std::vector<float>& tt, const std::vector<float>& vv, float t, float* o)
{
    const size_t n = tt.size();
    if (n == 0 || vv.size() < n * 4) return;
    if (n == 1 || t <= tt[0]) { memcpy(o, &vv[0], 4 * sizeof(float)); NormQ(o); return; }
    if (t >= tt[n - 1]) { memcpy(o, &vv[(n - 1) * 4], 4 * sizeof(float)); NormQ(o); return; }
    const size_t i1 = (size_t)(std::upper_bound(tt.begin(), tt.end(), t) - tt.begin());
    const size_t i0 = i1 - 1;
    const float d = tt[i1] - tt[i0];
    const float f = d > 1e-9f ? (t - tt[i0]) / d : 0.0f;
    const float* a = &vv[i0 * 4];
    float b[4] = { vv[i1 * 4], vv[i1 * 4 + 1], vv[i1 * 4 + 2], vv[i1 * 4 + 3] };
    float dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
    if (dot < 0.0f) { dot = -dot; for (int k = 0; k < 4; k++) b[k] = -b[k]; }
    float s0, s1;
    if (dot < 0.9995f)
    {
        const float om = acosf(dot), so = sinf(om);
        s0 = sinf((1.0f - f) * om) / so;
        s1 = sinf(f * om) / so;
    }
    else { s0 = 1.0f - f; s1 = f; }
    for (int k = 0; k < 4; k++) o[k] = a[k] * s0 + b[k] * s1;
    NormQ(o);
}

// ============================================================
//  Skeleton
// ============================================================
void GhostSkin_Finish(GhostSkinData& s)
{
    s.nodeMap.clear();
    for (int i = 0; i < s.NodeCount(); i++) s.nodeMap.emplace(s.nodeName[(size_t)i], i);
}

void GhostSkin_Pose(const GhostSkinData& s, const GhostSkelClip* clip, float time, std::vector<float>& boneMats)
{
    const int N = s.NodeCount(), B = s.BoneCount();
    static thread_local std::vector<float> T, R, S, G;
    T = s.nodeT; R = s.nodeR; S = s.nodeS;
    G.resize((size_t)N * 16);

    if (clip)
        for (const GhostSkelTrack& tr : clip->tracks)
        {
            auto it = s.nodeMap.find(tr.node);
            if (it == s.nodeMap.end()) continue;                    // a node this model doesn't have
            const size_t i = (size_t)it->second;
            SampleVec3(tr.pt, tr.pv, time, &T[i * 3]);
            SampleQuat(tr.rt, tr.rv, time, &R[i * 4]);
            SampleVec3(tr.st, tr.sv, time, &S[i * 3]);
        }

    float local[16];
    for (int i = 0; i < N; i++)
    {
        Compose(&T[(size_t)i * 3], &R[(size_t)i * 4], &S[(size_t)i * 3], local);
        const int p = s.nodeParent[(size_t)i];
        if (p < 0) memcpy(&G[(size_t)i * 16], local, sizeof local);
        else       Mul4(&G[(size_t)p * 16], local, &G[(size_t)i * 16]);
    }

    boneMats.resize((size_t)B * 16);
    for (int b = 0; b < B; b++)
        Mul4(&G[(size_t)s.boneNode[(size_t)b] * 16], &s.boneOffset[(size_t)b * 16], &boneMats[(size_t)b * 16]);
}

void GhostSkin_Apply(const GhostSkinData& s, const std::vector<float>& bm, float* out)
{
    const int B = s.BoneCount();
    if ((int)(bm.size() / 16) < B) return;
    const float sc = s.scale;
    const bool zUp = s.zUp;
    const size_t n = s.verts.size();

    for (size_t i = 0; i < n; i++)
    {
        const GhostSkinVert& v = s.verts[i];
        float m[12] = {};                                           // the top three rows of the blended matrix
        for (int k = 0; k < 4; k++)
        {
            if (v.w[k] <= 0.0f || (int)v.j[k] >= B) continue;
            const float* b = &bm[(size_t)v.j[k] * 16];
            for (int e = 0; e < 12; e++) m[e] += v.w[k] * b[e];
        }
        const float px = m[0] * v.p[0] + m[1] * v.p[1] + m[2] * v.p[2] + m[3];
        const float py = m[4] * v.p[0] + m[5] * v.p[1] + m[6] * v.p[2] + m[7];
        const float pz = m[8] * v.p[0] + m[9] * v.p[1] + m[10] * v.p[2] + m[11];
        float nx = m[0] * v.n[0] + m[1] * v.n[1] + m[2] * v.n[2];
        float ny = m[4] * v.n[0] + m[5] * v.n[1] + m[6] * v.n[2];
        float nz = m[8] * v.n[0] + m[9] * v.n[1] + m[10] * v.n[2];
        const float l = sqrtf(nx * nx + ny * ny + nz * nz);
        if (l > 1e-8f) { nx /= l; ny /= l; nz /= l; }
        else { nx = 0.0f; ny = 1.0f; nz = 0.0f; }

        float* o = out + i * 12;                                    // same axis conversion as the static importer
        if (zUp) { o[0] = px * sc; o[1] = pz * sc; o[2] = py * sc;  o[7] = nx; o[8] = nz; o[9] = ny; }
        else { o[0] = px * sc; o[1] = py * sc; o[2] = -pz * sc; o[7] = nx; o[8] = ny; o[9] = -nz; }
    }
}

// ============================================================
//  3D clips (FBX)
// ============================================================
bool GhostSkelClip_Load(const std::string& path, GhostSkelClip& out, std::string& err)
{
    out = GhostSkelClip();

    Assimp::Importer imp;
    imp.SetPropertyBool(AI_CONFIG_IMPORT_FBX_READ_CAMERAS, false);
    imp.SetPropertyBool(AI_CONFIG_IMPORT_FBX_READ_LIGHTS, false);
    // animations are read by default

    const aiScene* scene = nullptr;
    try { scene = imp.ReadFile(path.c_str(), 0); }
    catch (const std::exception& e) { err = std::string("The importer crashed on this file:\n") + e.what(); return false; }
    if (!scene) { err = std::string("Could not read the animation:\n") + imp.GetErrorString(); return false; }
    if (scene->mNumAnimations == 0 || !scene->mAnimations) { err = "The file has no animation in it."; return false; }

    const aiAnimation* best = nullptr;
    for (unsigned int i = 0; i < scene->mNumAnimations; i++)
    {
        const aiAnimation* a = scene->mAnimations[i];
        if (a && a->mNumChannels > 0 && (!best || a->mDuration > best->mDuration)) best = a;
    }
    if (!best) { err = "The animation has no keyframes."; return false; }

    const double tps = best->mTicksPerSecond > 0.0 ? best->mTicksPerSecond : 25.0;
    out.name = best->mName.C_Str();
    out.duration = (float)(best->mDuration / tps);

    for (unsigned int c = 0; c < best->mNumChannels; c++)
    {
        const aiNodeAnim* ch = best->mChannels[c];
        if (!ch) continue;
        GhostSkelTrack tr;
        tr.node = ch->mNodeName.C_Str();
        for (unsigned int k = 0; k < ch->mNumPositionKeys; k++)
        {
            const aiVectorKey& key = ch->mPositionKeys[k];
            tr.pt.push_back((float)(key.mTime / tps));
            tr.pv.push_back(key.mValue.x); tr.pv.push_back(key.mValue.y); tr.pv.push_back(key.mValue.z);
        }
        for (unsigned int k = 0; k < ch->mNumRotationKeys; k++)
        {
            const aiQuatKey& key = ch->mRotationKeys[k];
            tr.rt.push_back((float)(key.mTime / tps));
            tr.rv.push_back(key.mValue.x); tr.rv.push_back(key.mValue.y); tr.rv.push_back(key.mValue.z); tr.rv.push_back(key.mValue.w);
        }
        for (unsigned int k = 0; k < ch->mNumScalingKeys; k++)
        {
            const aiVectorKey& key = ch->mScalingKeys[k];
            tr.st.push_back((float)(key.mTime / tps));
            tr.sv.push_back(key.mValue.x); tr.sv.push_back(key.mValue.y); tr.sv.push_back(key.mValue.z);
        }
        out.tracks.push_back(std::move(tr));
    }
    return true;
}

// ============================================================
//  .ghostskin  (little-endian)
//  "GSKN", version, zUp, scale, nodes, bones, vertices
// ============================================================
static const char         kSkinMagic[4] = { 'G', 'S', 'K', 'N' };
static const unsigned int kSkinVersion = 1;

bool GhostSkinFile_Save(const std::string& path, const GhostSkinData& s, std::string& err)
{
    try
    {
        namespace fs = std::filesystem;
        fs::path out(GhostUtf8ToWide(path));
        if (out.has_parent_path()) fs::create_directories(out.parent_path());
        fs::path tmp = out;
        tmp += L".tmp";
        {
            std::ofstream f(tmp, std::ios::binary);
            if (!f) { err = "Could not write to " + path; return false; }
            auto U = [&](unsigned int v) { f.write((const char*)&v, 4); };
            auto I = [&](int v) { f.write((const char*)&v, 4); };
            auto F = [&](float v) { f.write((const char*)&v, 4); };

            f.write(kSkinMagic, 4);
            U(kSkinVersion);
            U(s.zUp ? 1u : 0u);
            F(s.scale);

            U((unsigned int)s.NodeCount());
            for (int i = 0; i < s.NodeCount(); i++)
            {
                const std::string& nm = s.nodeName[(size_t)i];
                U((unsigned int)nm.size());
                f.write(nm.data(), (std::streamsize)nm.size());
                I(s.nodeParent[(size_t)i]);
                for (int k = 0; k < 3; k++) F(s.nodeT[(size_t)i * 3 + k]);
                for (int k = 0; k < 4; k++) F(s.nodeR[(size_t)i * 4 + k]);
                for (int k = 0; k < 3; k++) F(s.nodeS[(size_t)i * 3 + k]);
            }

            U((unsigned int)s.BoneCount());
            for (int b = 0; b < s.BoneCount(); b++)
            {
                I(s.boneNode[(size_t)b]);
                for (int k = 0; k < 16; k++) F(s.boneOffset[(size_t)b * 16 + k]);
            }

            U((unsigned int)s.verts.size());
            for (const GhostSkinVert& v : s.verts)
            {
                for (int k = 0; k < 3; k++) F(v.p[k]);
                for (int k = 0; k < 3; k++) F(v.n[k]);
                f.write((const char*)v.j, 8);
                for (int k = 0; k < 4; k++) F(v.w[k]);
            }
            if (!f) { err = "Writing the skeleton file failed (disk full?)."; return false; }
        }
        fs::rename(tmp, out);
        return true;
    }
    catch (const std::exception& e)
    {
        err = std::string("Saving the skeleton failed: ") + e.what();
        return false;
    }
}

bool GhostSkinFile_Load(const std::string& path, GhostSkinData& s, std::string& err)
{
    s = GhostSkinData();
    try
    {
        std::ifstream f(std::filesystem::path(GhostUtf8ToWide(path)), std::ios::binary | std::ios::ate);
        if (!f) { err = "Could not open " + path; return false; }
        const long long size = (long long)f.tellg();
        f.seekg(0);

        auto left = [&]() -> long long { return size - (long long)f.tellg(); };
        auto U = [&](unsigned int& v) -> bool { f.read((char*)&v, 4); return (bool)f; };
        auto I = [&](int& v) -> bool { f.read((char*)&v, 4); return (bool)f; };
        auto F = [&](float& v) -> bool { f.read((char*)&v, 4); return (bool)f && v == v; };   // NaN is rejected
        const std::string bad = "The skeleton file is damaged: ";

        char magic[4] = {};
        f.read(magic, 4);
        if (!f || memcmp(magic, kSkinMagic, 4) != 0) { err = "This is not a GhostX skeleton file."; return false; }

        unsigned int ver = 0, zup = 0, nn = 0, nb = 0, nv = 0;
        float sc = 1.0f;
        if (!U(ver) || !U(zup) || !F(sc) || !U(nn)) { err = bad + "it is cut short."; return false; }
        if (ver > kSkinVersion) { err = "This skeleton was saved by a newer version of GhostX."; return false; }
        if (nn == 0 || nn > 8192) { err = bad + "the node count is out of range."; return false; }
        s.zUp = zup != 0;
        s.scale = sc;

        s.nodeName.resize(nn); s.nodeParent.resize(nn);
        s.nodeT.resize((size_t)nn * 3); s.nodeR.resize((size_t)nn * 4); s.nodeS.resize((size_t)nn * 3);
        for (unsigned int i = 0; i < nn; i++)
        {
            unsigned int len = 0;
            if (!U(len) || len > 1024 || (long long)len > left()) { err = bad + "a node name is unreadable."; return false; }
            s.nodeName[i].resize(len);
            if (len) f.read(&s.nodeName[i][0], (std::streamsize)len);
            int par = 0;
            if (!I(par) || par < -1 || par >= (int)i) { err = bad + "the node tree is invalid."; return false; }
            s.nodeParent[i] = par;
            bool ok = true;
            for (int k = 0; k < 3 && ok; k++) ok = F(s.nodeT[(size_t)i * 3 + k]);
            for (int k = 0; k < 4 && ok; k++) ok = F(s.nodeR[(size_t)i * 4 + k]);
            for (int k = 0; k < 3 && ok; k++) ok = F(s.nodeS[(size_t)i * 3 + k]);
            if (!ok) { err = bad + "a node is unreadable."; return false; }
        }

        if (!U(nb) || nb == 0 || nb > 65535u) { err = bad + "the bone count is out of range."; return false; }
        if ((long long)nb * 68 > left()) { err = bad + "it is cut short."; return false; }
        s.boneNode.resize(nb); s.boneOffset.resize((size_t)nb * 16);
        for (unsigned int b = 0; b < nb; b++)
        {
            int node = 0;
            if (!I(node) || node < 0 || node >= (int)nn) { err = bad + "a bone points at a missing node."; return false; }
            s.boneNode[b] = node;
            for (int k = 0; k < 16; k++)
                if (!F(s.boneOffset[(size_t)b * 16 + k])) { err = bad + "a bone is unreadable."; return false; }
        }

        if (!U(nv) || nv == 0 || nv > 3000000u) { err = bad + "the vertex count is out of range."; return false; }
        if ((long long)nv * 48 > left()) { err = bad + "it is cut short."; return false; }
        s.verts.resize(nv);
        for (unsigned int i = 0; i < nv; i++)
        {
            GhostSkinVert& v = s.verts[i];
            bool ok = true;
            for (int k = 0; k < 3 && ok; k++) ok = F(v.p[k]);
            for (int k = 0; k < 3 && ok; k++) ok = F(v.n[k]);
            f.read((char*)v.j, 8);
            for (int k = 0; k < 4 && ok; k++) ok = F(v.w[k]);
            if (!ok || !f) { err = bad + "a vertex is unreadable."; return false; }
            for (int k = 0; k < 4; k++)
                if ((unsigned int)v.j[k] >= nb) { err = bad + "a vertex points at a missing bone."; return false; }
        }

        GhostSkin_Finish(s);
        return true;
    }
    catch (const std::exception& e)
    {
        err = std::string("Reading the skeleton failed: ") + e.what();
        return false;
    }
}