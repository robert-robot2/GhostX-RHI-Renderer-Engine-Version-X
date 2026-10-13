#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../include/GhostGameModule.h"
#include "../include/GhostImage.h"          // UTF-8 helpers (also includes Windows.h)
#include <string>
#include <vector>

typedef int  (*PFN_Init)(const GhostGameAPI*);
typedef void (*PFN_Update)(float);
typedef void (*PFN_Shutdown)(void);

static HMODULE      g_mod = nullptr;
static PFN_Update   g_update = nullptr;
static PFN_Shutdown g_shutdown = nullptr;
static std::wstring g_tempCopy;

static bool Exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

static std::wstring DirOf(const std::wstring& p)
{
    size_t s = p.find_last_of(L"\\/");
    return s == std::wstring::npos ? L"" : p.substr(0, s + 1);
}

static std::wstring ModuleDir()             // folder GhostX.dll was loaded from
{
    HMODULE h = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        (LPCWSTR)&ModuleDir, &h);
    wchar_t buf[1024];
    DWORD n = GetModuleFileNameW(h, buf, 1024);
    return DirOf(std::wstring(buf, n));
}

static std::wstring ExeDir()                // folder of GhostShell.exe
{
    wchar_t buf[1024];
    DWORD n = GetModuleFileNameW(nullptr, buf, 1024);
    return DirOf(std::wstring(buf, n));
}

static std::wstring TempDir()
{
    wchar_t buf[MAX_PATH + 2];
    DWORD n = GetTempPathW(MAX_PATH, buf);
    std::wstring d(buf, n);
    d += L"GhostX\\";
    CreateDirectoryW(d.c_str(), nullptr);
    return d;
}

// Copies from crashed runs. Files still loaded by another GhostX window can't be deleted, which is what we want.
static void CleanStale(const std::wstring& dir)
{
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"GhostGame_*.dll").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do { DeleteFileW((dir + fd.cFileName).c_str()); } while (FindNextFileW(h, &fd));
    FindClose(h);
}

// SEH wrappers: a crash in game code stops Play instead of killing the editor.
// (No C++ objects with destructors in these functions, which __try requires.)
static bool CallInit(PFN_Init f, const GhostGameAPI* api, int* result)
{
    __try { *result = f(api); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static bool CallUpdate(PFN_Update f, float dt)
{
    __try { f(dt); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static void CallShutdown(PFN_Shutdown f)
{
    __try { f(); }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

GhostModuleResult GhostGameModule_Load(const std::string& preferredUtf8, const GhostGameAPI* api, std::string& msg)
{
    GhostGameModule_Unload();

    std::vector<std::wstring> candidates;
    if (!preferredUtf8.empty()) candidates.push_back(GhostUtf8ToWide(preferredUtf8));
    candidates.push_back(ModuleDir() + L"GhostGame.dll");
    candidates.push_back(ExeDir() + L"GhostGame.dll");

    std::wstring src;
    for (const std::wstring& c : candidates)
        if (Exists(c)) { src = c; break; }

    if (src.empty())
    {
        msg = "No GhostGame.dll found. Looked in:";
        for (const std::wstring& c : candidates) msg += "\n  " + GhostWideToUtf8(c);
        return GhostModuleResult::NotFound;
    }

    // Load a copy, so Visual Studio can rebuild the original while the editor is running
    std::wstring dir = TempDir();
    CleanStale(dir);
    wchar_t name[96];
    swprintf_s(name, L"GhostGame_%lu_%llu.dll", GetCurrentProcessId(), (unsigned long long)GetTickCount64());
    std::wstring dst = dir + name;

    if (!CopyFileW(src.c_str(), dst.c_str(), FALSE))
    {
        msg = "Could not copy GhostGame.dll to the temp folder (error " + std::to_string(GetLastError()) + ").";
        return GhostModuleResult::Error;
    }
    SetFileAttributesW(dst.c_str(), FILE_ATTRIBUTE_NORMAL);

    HMODULE m = LoadLibraryW(dst.c_str());
    if (!m)
    {
        msg = "Could not load GhostGame.dll (error " + std::to_string(GetLastError()) +
            "). It must be a 64-bit build, and it can't depend on other DLLs that aren't next to GhostShell.exe.";
        DeleteFileW(dst.c_str());
        return GhostModuleResult::Error;
    }

    PFN_Init   init = (PFN_Init)GetProcAddress(m, "GhostGame_Init");
    PFN_Update upd = (PFN_Update)GetProcAddress(m, "GhostGame_Update");
    if (!init || !upd)
    {
        msg = "GhostGame.dll must export GhostGame_Init and GhostGame_Update (use GHOST_GAME_EXPORT).";
        FreeLibrary(m); DeleteFileW(dst.c_str());
        return GhostModuleResult::Error;
    }

    int ok = 0;
    if (!CallInit(init, api, &ok))
    {
        msg = "GhostGame_Init crashed.";
        FreeLibrary(m); DeleteFileW(dst.c_str());
        return GhostModuleResult::Error;
    }
    if (!ok)
    {
        msg = "GhostGame_Init reported a failure (a mismatched GhostGameAPI.h version is the usual cause).";
        FreeLibrary(m); DeleteFileW(dst.c_str());
        return GhostModuleResult::Error;
    }

    g_mod = m;
    g_update = upd;
    g_shutdown = (PFN_Shutdown)GetProcAddress(m, "GhostGame_Shutdown");
    g_tempCopy = dst;
    msg = "Loaded: " + GhostWideToUtf8(src);
    return GhostModuleResult::Loaded;
}

bool GhostGameModule_Update(float dt)
{
    return g_update ? CallUpdate(g_update, dt) : true;
}

bool GhostGameModule_IsLoaded() { return g_mod != nullptr; }

void GhostGameModule_Unload()
{
    if (!g_mod) return;
    if (g_shutdown) CallShutdown(g_shutdown);
    FreeLibrary(g_mod);
    g_mod = nullptr; g_update = nullptr; g_shutdown = nullptr;
    if (!g_tempCopy.empty()) { DeleteFileW(g_tempCopy.c_str()); g_tempCopy.clear(); }
}