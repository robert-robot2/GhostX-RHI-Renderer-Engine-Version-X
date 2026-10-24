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
#include <fstream>

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
static float Sat01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

static std::string LowerExt(const std::string& s)
{
    std::string e = s;
    for (char& c : e) c = (char)tolower((unsigned char)c);
    return e;
}

static bool SupportedExt(const std::string& ext)
{
    const std::string e = LowerExt(ext);
    return e == ".png" || e == ".jpg" || e == ".jpeg" || e == ".bmp" || e == ".tga";
}

// Looks for a texture the way exporters leave them: next to the model, in textures folders, in <name>.fbm, then anywhere below
static std::string FindTextureFile(const std::string& modelPath, const std::string& raw)
{
    namespace fs = std::filesystem;
    const fs::path model(GhostUtf8ToWide(modelPath));
    const fs::path dir = model.parent_path();
    const fs::path rawPath(GhostUtf8ToWide(raw));
    const fs::path name = rawPath.filename();
    if (name.empty()) return "";
    std::error_code ec;

    std::vector<fs::path> tries;
    tries.push_back(rawPath.is_absolute() ? rawPath : dir / rawPath);
    tries.push_back(dir / name);
    tries.push_back(dir / L"textures" / name);
    tries.push_back(dir / L"Textures" / name);
    tries.push_back(dir / (model.stem().wstring() + L".fbm") / name);
    tries.push_back(dir.parent_path() / L"textures" / name);
    tries.push_back(dir.parent_path() / L"Textures" / name);
    for (const fs::path& t : tries)
        if (fs::is_regular_file(t, ec)) return GhostWideToUtf8(t.lexically_normal().wstring());

    int visited = 0;
    for (fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
        !ec && it != end && visited < 5000; it.increment(ec), ++visited)
        if (it->is_regular_file(ec) && _wcsicmp(it->path().filename().c_str(), name.c_str()) == 0)
            return GhostWideToUtf8(it->path().lexically_normal().wstring());
    return "";
}

static void ReadMaterial(const aiScene* scene, const aiMaterial* mat, const std::string& modelPath,
    const std::string& slot, GhostModelMat& out, std::string& warn)
{
    out.hasMat = true;

    aiColor3D c;
    if (mat->Get(AI_MATKEY_COLOR_DIFFUSE, c) == aiReturn_SUCCESS)
    {
        out.tint[0] = Sat01(c.r); out.tint[1] = Sat01(c.g); out.tint[2] = Sat01(c.b);
    }

    float f = 0.0f;
    if (mat->Get(AI_MATKEY_SHININESS, f) == aiReturn_SUCCESS && f > 1.0f)
    {
        const float r = powf(2.0f / (f + 2.0f), 0.25f);          // Blinn-Phong exponent -> the roughness our shader uses
        out.roughness = r < 0.06f ? 0.06f : (r > 1.0f ? 1.0f : r);
    }
    if (mat->Get(AI_MATKEY_METALLIC_FACTOR, f) == aiReturn_SUCCESS) out.metallic = Sat01(f);
    if (mat->Get(AI_MATKEY_ROUGHNESS_FACTOR, f) == aiReturn_SUCCESS) out.roughness = f < 0.06f ? 0.06f : (f > 1.0f ? 1.0f : f);

    aiColor3D e;
    if (mat->Get(AI_MATKEY_COLOR_EMISSIVE, e) == aiReturn_SUCCESS && (e.r > 0.01f || e.g > 0.01f || e.b > 0.01f))
    {
        out.emissive[0] = Sat01(e.r); out.emissive[1] = Sat01(e.g); out.emissive[2] = Sat01(e.b); out.emissiveStrength = 1.0f;
    }

    if (mat->Get(AI_MATKEY_OPACITY, f) == aiReturn_SUCCESS && f > 0.0f && f < 0.99f)
        warn += "Material \"" + slot + "\" is transparent. GhostX draws it opaque.\n";

    // ---- the diffuse texture ----
    aiTextureType type = aiTextureType_DIFFUSE;
    if (mat->GetTextureCount(type) == 0) type = aiTextureType_BASE_COLOR;
    aiString tp;
    if (mat->GetTextureCount(type) == 0 || mat->GetTexture(type, 0, &tp) != aiReturn_SUCCESS) return;
    const std::string raw = tp.C_Str();
    if (raw.empty()) return;

    if (const aiTexture* et = scene->GetEmbeddedTexture(raw.c_str()))
    {
        if (et->mHeight != 0 || !et->pcData || et->mWidth == 0 || et->mWidth > 64u * 1024u * 1024u)
        {
            warn += "Material \"" + slot + "\": its embedded texture is in a format GhostX can't read.\n"; return;
        }
        std::string ext = et->achFormatHint;
        if (ext.empty()) ext = "png";
        if (ext == "jpeg") ext = "jpg";
        if (!SupportedExt("." + ext))
        {
            warn += "Material \"" + slot + "\": its embedded texture is ." + ext + ", which GhostX can't read.\n"; return;
        }
        out.texData.assign((const unsigned char*)et->pcData, (const unsigned char*)et->pcData + et->mWidth);
        out.texExt = ext;
        return;
    }

    const std::string file = FindTextureFile(modelPath, raw);
    if (file.empty())
    {
        warn += "Material \"" + slot + "\": the texture \"" + raw + "\" was not found next to the model "
            "(looked beside it, in textures folders and below).\n";
        return;
    }
    const std::string ext = std::filesystem::path(GhostUtf8ToWide(file)).extension().string();
    if (!SupportedExt(ext))
    {
        warn += "Material \"" + slot + "\": the texture " + raw + " is a " + ext + " file. Only PNG, JPG, BMP and TGA can be loaded.\n"; return;
    }
    out.texSource = file;
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

    if (totalTris > GHOST_MODEL_WARN_TRIANGLES)
        out.warn += "This is a large model (" + std::to_string(totalTris) + " triangles). Expect slower editing and shadows.\n";

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
        if (opt.importMaterials && scene->mMaterials && g.first < scene->mNumMaterials)
            ReadMaterial(scene, scene->mMaterials[g.first], path, unique, sub.mat, out.warn);
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
    out.opts = opt;
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

// ============================================================
//  .ghostmesh  (all values little-endian)
//  "GMSH", version, vertexCount, indexCount, subCount, bmin[3], bmax[3],
//  name, source, per sub: name, firstIndex, indexCount, then the vertices and the 32-bit indices
// ============================================================
static const char         kMeshMagic[4] = { 'G', 'M', 'S', 'H' };

static const unsigned int kMeshFileVersion = 2;

bool GhostMeshFile_Save(const std::string& path, const GhostModelResult& r, const std::string& source, std::string& err)
{
    try
    {
        namespace fs = std::filesystem;
        fs::path out(GhostUtf8ToWide(path));
        if (out.has_parent_path()) fs::create_directories(out.parent_path());
        fs::path tmp = out;
        tmp += L".tmp";                                            // a failed save never leaves a half-written .ghostmesh
        {
            std::ofstream f(tmp, std::ios::binary);
            if (!f) { err = "Could not write to " + path; return false; }
            auto U = [&](unsigned int v) { f.write((const char*)&v, 4); };
            auto F = [&](float v) { f.write((const char*)&v, 4); };
            auto S = [&](const std::string& s) { U((unsigned int)s.size()); f.write(s.data(), (std::streamsize)s.size()); };

            f.write(kMeshMagic, 4);
            U(kMeshFileVersion);
            U((unsigned int)r.vertices.size());
            U((unsigned int)r.indices.size());
            U((unsigned int)r.subs.size());
            for (int k = 0; k < 3; k++) F(r.bmin[k]);
            for (int k = 0; k < 3; k++) F(r.bmax[k]);
            F(r.opts.scale);
            F(r.opts.fitSize);
            U((unsigned int)r.opts.upAxis);
            U((r.opts.flipWinding ? 1u : 0u) | (r.opts.importMaterials ? 0u : 2u));
            S(r.name);
            S(source);
            for (const GhostModelSub& s : r.subs)
            {
                const GhostModelMat& m = s.mat;
                S(s.name); U(s.firstIndex); U(s.indexCount);
                U(m.hasMat ? 1u : 0u);
                for (int k = 0; k < 3; k++) F(m.tint[k]);
                F(m.metallic); F(m.roughness); F(m.specular);
                for (int k = 0; k < 3; k++) F(m.emissive[k]);
                F(m.emissiveStrength);
                S(m.texPath);
            }
            f.write((const char*)r.vertices.data(), (std::streamsize)(r.vertices.size() * sizeof(GhostMeshVertex)));
            f.write((const char*)r.indices.data(), (std::streamsize)(r.indices.size() * sizeof(unsigned int)));
            if (!f) { err = "Writing the mesh file failed (disk full?)."; return false; }
        }
        fs::rename(tmp, out);
        return true;
    }
    catch (const std::exception& e)
    {
        err = std::string("Saving the mesh failed: ") + e.what();
        return false;
    }
}

bool GhostMeshFile_Load(const std::string& path, GhostModelResult& out, std::string& source, std::string& err)
{
    out = GhostModelResult();
    source.clear();
    try
    {
        std::ifstream f(std::filesystem::path(GhostUtf8ToWide(path)), std::ios::binary | std::ios::ate);
        if (!f) { err = "Could not open " + path; return false; }
        const long long size = (long long)f.tellg();
        f.seekg(0);

        auto left = [&]() -> long long { return size - (long long)f.tellg(); };
        auto U = [&](unsigned int& v) -> bool { f.read((char*)&v, 4); return (bool)f; };
        auto F = [&](float& v) -> bool { f.read((char*)&v, 4); return (bool)f && v == v; };   // NaN is rejected
        auto S = [&](std::string& s) -> bool
            {
                unsigned int n = 0;
                if (!U(n) || n > 4096 || (long long)n > left()) return false;
                s.resize(n);
                if (n) f.read(&s[0], (std::streamsize)n);
                return (bool)f;
            };
        const std::string bad = "The mesh file is damaged: ";

        char magic[4] = {};
        f.read(magic, 4);
        if (!f || memcmp(magic, kMeshMagic, 4) != 0) { err = "This is not a GhostX mesh file."; return false; }

        unsigned int ver = 0, vc = 0, ic = 0, sc = 0;
        if (!U(ver) || !U(vc) || !U(ic) || !U(sc)) { err = bad + "it is cut short."; return false; }
        if (ver > kMeshFileVersion) { err = "This mesh was saved by a newer version of GhostX."; return false; }
        if (vc == 0 || vc > 3000000u || ic < 3 || ic > 3000000u || ic % 3 != 0 || sc == 0 || sc > (unsigned)GHOST_MODEL_MAX_SLOTS)
        {
            err = bad + "the counts are out of range."; return false;
        }

        for (int k = 0; k < 3; k++) if (!F(out.bmin[k])) { err = bad + "it is cut short."; return false; }
        for (int k = 0; k < 3; k++) if (!F(out.bmax[k])) { err = bad + "it is cut short."; return false; }

        if (ver >= 2)
        {
            unsigned int up = 0, flags = 0;
            if (!F(out.opts.scale) || !F(out.opts.fitSize) || !U(up) || !U(flags)) { err = bad + "the import options are unreadable."; return false; }
            out.opts.upAxis = up > 2 ? 0 : (int)up;
            out.opts.flipWinding = (flags & 1) != 0;
            out.opts.importMaterials = (flags & 2) == 0;
        }
        if (!S(out.name) || !S(source)) { err = bad + "a name is unreadable."; return false; }

        out.subs.resize(sc);
        for (unsigned int i = 0; i < sc; i++)
        {
            GhostModelSub& s = out.subs[i];
            if (!S(s.name) || !U(s.firstIndex) || !U(s.indexCount)) { err = bad + "the material slots are unreadable."; return false; }
            if (s.indexCount == 0 || s.firstIndex % 3 != 0 || s.indexCount % 3 != 0 ||
                (unsigned long long)s.firstIndex + s.indexCount > ic)
            {
                err = bad + "a material slot is out of range."; return false;
            }

            if (ver >= 2)
            {
                GhostModelMat& m = s.mat;
                unsigned int has = 0;
                bool ok = U(has);
                for (int k = 0; k < 3 && ok; k++) ok = F(m.tint[k]);
                ok = ok && F(m.metallic) && F(m.roughness) && F(m.specular);
                for (int k = 0; k < 3 && ok; k++) ok = F(m.emissive[k]);
                ok = ok && F(m.emissiveStrength) && S(m.texPath);
                if (!ok) { err = bad + "a material is unreadable."; return false; }
                m.hasMat = has != 0;
                for (int k = 0; k < 3; k++) { m.tint[k] = Sat01(m.tint[k]); m.emissive[k] = Sat01(m.emissive[k]); }
                m.metallic = Sat01(m.metallic);
                m.specular = Sat01(m.specular);
                m.roughness = m.roughness < 0.06f ? 0.06f : (m.roughness > 1.0f ? 1.0f : m.roughness);
                m.emissiveStrength = m.emissiveStrength < 0.0f ? 0.0f : (m.emissiveStrength > 20.0f ? 20.0f : m.emissiveStrength);
            }
        }

        const long long need = (long long)vc * (long long)sizeof(GhostMeshVertex) + (long long)ic * 4;
        if (left() != need) { err = bad + "its size doesn't match its contents."; return false; }

        out.vertices.resize(vc);
        out.indices.resize(ic);
        f.read((char*)out.vertices.data(), (std::streamsize)(vc * sizeof(GhostMeshVertex)));
        f.read((char*)out.indices.data(), (std::streamsize)(ic * sizeof(unsigned int)));
        if (!f) { err = bad + "it is cut short."; return false; }

        for (unsigned int i = 0; i < ic; i++)                      // a bad index would crash the GPU, so check them all
            if (out.indices[i] >= vc) { err = bad + "a triangle points past the vertices."; return false; }

        out.triangleCount = ic / 3;
        return true;
    }
    catch (const std::exception& e)
    {
        err = std::string("Reading the mesh failed: ") + e.what();
        return false;
    }
}