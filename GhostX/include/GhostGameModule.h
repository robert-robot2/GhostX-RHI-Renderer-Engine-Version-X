#pragma once
#include "GhostGameAPI.h"
#include <string>

enum class GhostModuleResult { NotFound, Loaded, Error };

// preferredUtf8 = the project's Binaries/GhostGame.dll (may be empty). message = path / reason.
GhostModuleResult GhostGameModule_Load(const std::string& preferredUtf8, const GhostGameAPI* api, std::string& message);
bool GhostGameModule_Update(float dt);       // false = the module crashed
void GhostGameModule_Unload();
bool GhostGameModule_IsLoaded();