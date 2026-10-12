#pragma once
#include "GhostScene.h"
#include <functional>
#include <string>

constexpr int GHOSTSCENE_VERSION = 1;

// Loads (or finds) a texture file; returns the engine texture id, or <= 0 on failure
using GhostLoadTexFn = std::function<int(const std::string& absPath, const std::string& name)>;

bool GhostScene_Save(const GhostScene& s, const std::string& projectRoot, const std::string& file,
    std::string& err);

// Fills s in place (textures registry is left alone). Returns false for a bad file, with err set.
// warn collects non-fatal problems such as missing textures.
bool GhostScene_Load(GhostScene& s, const std::string& projectRoot, const std::string& file,
    const GhostLoadTexFn& loadTex, size_t maxObjects, std::string& err, std::string& warn);

// Content hash for unsaved-changes detection (ignores selection and the main camera's position)
unsigned int GhostScene_Hash(const GhostScene& s);

std::string GhostPath_Normalize(const std::string& utf8Path);