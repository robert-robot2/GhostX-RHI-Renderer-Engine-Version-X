#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../include/GhostModelImport.h"
#include "../include/GhostImage.h"          // UTF-8 helpers (also pulls in Windows.h)
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/config.h>
#include <assimp/metadata.h>
#include <filesystem>
#include <map>
#include <set>
#include <exception>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

static bool MetaInt(const aiMetadata* md, const char* key, int& out)
{
    if (!md) return false;
    for (unsigned int i = 0; i < md->mNumProperties; i++)
    {
        if (strcmp(md->mKeys[i].C_Str(), key) != 0) continue;
        const aiMetadataEntry& e = md->mValues[i];
        if (!e.mData) return false;
        switch (e.mType)
        {
        case AI_INT32:  out = (int)*(const int32_t*)e.mData;  return true;
        case AI_UINT32: out = (int)*(const uint32_t*)e.mData; return true;
        case AI_INT64:  out = (int)*(const int64_t*)e.mData;  return true;
        case AI_UINT64: out = (int)*(const uint64_t*)e.mData; return true;
        case AI_FLOAT:  out = (int)*(const float*)e.mData;    return true;
        case AI_DOUBLE: out = (int)*(const double*)e.mData;   return true;
        default:        return false;
        }
    }
    return false;
}

// Source (right-handed) -> engine (left-handed, Y up). Both mappings mirror the model,
// so the triangle winding is swapped below.
static void Convert(bool zUp, float x, float y, float z, float& ox, float& oy, float& oz)
{
    if (zUp) { ox = x; oy = z; oz = y; }     // Z-up (Blender style): up becomes Y, forward becomes +Z
    else { ox = x; oy = y; oz = -z; }    // Y-up: mirror Z
}

bool GhostModel_Load(const std::string& path, const GhostModelOptions& opt, GhostModelResult& out, std::string& err)
{
    out = GhostModelResult();

    Assimp::Importer imp;
    imp.SetPropertyInteger(AI_CONFIG_PP_SBP_REMOVE, aiPrimitiveType_POINT | aiPrimitiveType_LINE);
    imp.SetPropertyBool(AI_CONFIG_IMPORT_FBX_READ_ANIMATIONS, false);
    imp.SetPropertyBool(AI_CONFIG_IMPORT_FBX_READ_CAMERAS, false);
    imp.SetPropertyBool(AI_CONFIG_IMPORT_FBX_READ_LIGHTS, false);

    const unsigned int flags =
        aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_GenSmoothNormals |
        aiProcess_PreTransformVertices | aiProcess_SortByPType | aiProcess_FindDegenerates |
        aiProcess_FindInvalidData;

    const aiScene* scene = nullptr;
    try { scene = imp.ReadFile(path.c_str(), flags); }
    catch (const std::exception& e) { err = std::string("The importer crashed on this file:\n") + e.what(); return false; }
    if (!scene) { err = std::string("Could not read the model:\n") + imp.GetErrorString(); return false; }

    // ---- group triangle meshes by material ----
    std::map<unsigned int, std::vector<const aiMesh*>> groups;
    unsigned long long totalTris = 0;
    for (unsigned int i = 0; i < scene->mNumMeshes; i++)
    {
        const aiMesh* m = scene->mMeshes[i];
        if (!m || m->mNumFaces == 0 || m->mNumVertices == 0 || !(m->mPrimitiveTypes & aiPrimitiveType_TRIANGLE)) continue;
        groups[m->mMaterialIndex].push_back(m);
        totalTris += m->mNumFaces;
    }
    if (groups.empty()) { err = "The file has no triangle geometry."; return false; }
    if (totalTris > GHOST_MODEL_MAX_TRIANGLES)
    {
        err = "The model has " + std::to_string(totalTris) + " triangles. The limit is " +
            std::to_string(GHOST_MODEL_MAX_TRIANGLES) + ".";
        return false;
    }

    // ---- up axis ----
    bool zUp = (opt.upAxis == 2);
    bool fromFile = false;
    if (opt.upAxis == 0)
    {
        int ua = 1;
        if (MetaInt(scene->mMetaData, "UpAxis", ua)) { zUp = (ua == 2); fromFile = true; }
    }

    // ---- build vertices and indices ----
    std::vector<GhostMeshVertex>& V = out.vertices;
    std::vector<unsigned int>& I = out.indices;
    float mn[3] = { 1e30f, 1e30f, 1e30f }, mx[3] = { -1e30f, -1e30f, -1e30f };
    std::set<std::string> used;

    for (auto& g : groups)
    {
        const unsigned int firstIndex = (unsigned int)I.size();

        for (const aiMesh* m : g.second)
        {
            const unsigned int base = (unsigned int)V.size();
            const bool hasUV = m->HasTextureCoords(0);
            const bool hasN = m->HasNormals();

            for (unsigned int i = 0; i < m->mNumVertices; i++)
            {
                GhostMeshVertex v = {};
                Convert(zUp, m->mVertices[i].x, m->mVertices[i].y, m->mVertices[i].z, v.x, v.y, v.z);
                v.r = v.g = v.b = v.a = 1.0f;
                if (hasN)
                {
                    Convert(zUp, m->mNormals[i].x, m->mNormals[i].y, m->mNormals[i].z, v.nx, v.ny, v.nz);
                    const float l = sqrtf(v.nx * v.nx + v.ny * v.ny + v.nz * v.nz);
                    if (l > 1e-8f) { v.nx /= l; v.ny /= l; v.nz /= l; }
                    else { v.nx = 0; v.ny = 1; v.nz = 0; }
                }
                else { v.nx = 0.0f; v.ny = 1.0f; v.nz = 0.0f; }
                if (hasUV) { v.u = m->mTextureCoords[0][i].x; v.v = 1.0f - m->mTextureCoords[0][i].y; }

                const float p[3] = { v.x, v.y, v.z };
                for (int k = 0; k < 3; k++) { if (p[k] < mn[k]) mn[k] = p[k]; if (p[k] > mx[k]) mx[k] = p[k]; }
                V.push_back(v);
            }

            for (unsigned int f = 0; f < m->mNumFaces; f++)
            {
                const aiFace& face = m->mFaces[f];
                if (face.mNumIndices != 3) continue;
                if (face.mIndices[0] >= m->mNumVertices || face.mIndices[1] >= m->mNumVertices ||
                    face.mIndices[2] >= m->mNumVertices) continue;
                const unsigned int a = base + face.mIndices[0], b = base + face.mIndices[1], c = base + face.mIndices[2];
                if (opt.flipWinding) { I.push_back(a); I.push_back(b); I.push_back(c); }
                else { I.push_back(a); I.push_back(c); I.push_back(b); }   // mirrored: swap to keep the front face
            }
        }
        if (I.size() == firstIndex) continue;

        std::string nm;
        if (scene->mMaterials && g.first < scene->mNumMaterials)
        {
            aiString s;
            if (scene->mMaterials[g.first]->Get(AI_MATKEY_NAME, s) == aiReturn_SUCCESS) nm = s.C_Str();
        }
        if (nm.empty()) nm = "Material " + std::to_string(g.first);
        std::string unique = nm;
        for (int k = 2; used.count(unique); k++) unique = nm + " " + std::to_string(k);
        used.insert(unique);

        GhostModelSub sub;
        sub.name = unique;
        sub.firstIndex = firstIndex;
        sub.indexCount = (unsigned int)I.size() - firstIndex;
        out.subs.push_back(sub);
    }
    if (V.empty() || I.size() < 3 || out.subs.empty()) { err = "The file has no usable triangles."; return false; }

    // ---- more material slots than an object can hold: the tail shares the last slot ----
    if ((int)out.subs.size() > GHOST_MODEL_MAX_SLOTS)
    {
        const size_t last = GHOST_MODEL_MAX_SLOTS - 1;
        const size_t extra = out.subs.size() - last;
        unsigned int total = 0;
        for (size_t i = last; i < out.subs.size(); i++) total += out.subs[i].indexCount;
        out.subs[last].name = "Merged (" + std::to_string(extra) + " materials)";
        out.subs[last].indexCount = total;
        out.subs.resize(GHOST_MODEL_MAX_SLOTS);
        out.warn += "The file has more than 32 materials: the last " + std::to_string(extra) + " share one slot.\n";
    }

    // ---- scale ----
    float ext[3] = { mx[0] - mn[0], mx[1] - mn[1], mx[2] - mn[2] };
    float largest = ext[0] > ext[1] ? ext[0] : ext[1];
    if (ext[2] > largest) largest = ext[2];
    if (largest < 1e-9f) { err = "The model has no size (all points are in the same place)."; return false; }

    float s = (opt.fitSize > 0.0f) ? opt.fitSize / largest : 1.0f;
    s *= (opt.scale > 0.0f) ? opt.scale : 1.0f;
    for (GhostMeshVertex& v : V) { v.x *= s; v.y *= s; v.z *= s; }
    for (int k = 0; k < 3; k++) { out.bmin[k] = mn[k] * s; out.bmax[k] = mx[k] * s; }

    out.triangleCount = (unsigned int)(I.size() / 3);
    out.name = GhostWideToUtf8(std::filesystem::path(GhostUtf8ToWide(path)).stem().wstring());
    if (out.name.empty()) out.name = "Model";

    const char* upText = (opt.upAxis == 1) ? "Y (chosen)" : (opt.upAxis == 2) ? "Z (chosen)"
        : fromFile ? (zUp ? "Z (from the file)" : "Y (from the file)") : "Y (the file has no up-axis info)";
    char buf[420];
    snprintf(buf, sizeof buf,
        "%u triangles, %u vertices, %u material slot%s\nSize %.3g x %.3g x %.3g units (scale x%.4g applied)\nUp axis: %s",
        out.triangleCount, (unsigned int)V.size(), (unsigned int)out.subs.size(), out.subs.size() == 1 ? "" : "s",
        ext[0] * s, ext[1] * s, ext[2] * s, s, upText);
    out.info = buf;
    return true;
}