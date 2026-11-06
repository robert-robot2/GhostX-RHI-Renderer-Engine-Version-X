// GhostAudioBank.cpp
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../include/GhostAudioBank.h"
#include "../include/GhostAudio.h"
#include "../include/json.hpp"
#include <filesystem>
#include <fstream>
#include <vector>

using json = nlohmann::json;
namespace fs = std::filesystem;

static std::vector<GhostSoundDef> g_bank;
static std::string g_root;

struct Def { const char* name; int kind; int bus; float vol; float pitch; const char* file; };
static const Def kDefaults[] =
{
    { "jump",             0, 2, 0.8f, 0.03f, "Content/Audio/jump.wav" },
    { "big_jump",         0, 2, 0.8f, 0.03f, "Content/Audio/big_jump.wav" },
    { "coin",             0, 2, 0.7f, 0.00f, "Content/Audio/coin.wav" },
    { "stomp",            0, 2, 0.9f, 0.05f, "Content/Audio/stomp.wav" },
    { "kick",             0, 2, 0.9f, 0.03f, "Content/Audio/kick.wav" },
    { "bump",             0, 2, 0.9f, 0.00f, "Content/Audio/bump.wav" },
    { "brick_break",      0, 2, 0.9f, 0.05f, "Content/Audio/brick_break.wav" },
    { "powerup_appear",   0, 2, 0.9f, 0.00f, "Content/Audio/powerup_appear.wav" },
    { "powerup_collect",  0, 2, 0.9f, 0.00f, "Content/Audio/powerup_collect.wav" },
    { "oneup",            0, 2, 0.9f, 0.00f, "Content/Audio/oneup.wav" },
    { "fireball",         0, 2, 0.8f, 0.03f, "Content/Audio/fireball.wav" },
    { "pipe",             0, 2, 0.9f, 0.00f, "Content/Audio/pipe.wav" },
    { "hurt",             0, 2, 0.9f, 0.00f, "Content/Audio/hurt.wav" },
    { "death",            0, 2, 1.0f, 0.00f, "Content/Audio/death.wav" },
    { "goal",             0, 2, 1.0f, 0.00f, "Content/Audio/goal.wav" },
    { "game_over",        0, 2, 1.0f, 0.00f, "Content/Audio/game_over.wav" },
    { "music_overworld",  1, 1, 0.8f, 0.0f,  "Content/Audio/music_overworld.mp3" },
    { "music_underground",1, 1, 0.8f, 0.0f,  "Content/Audio/music_underground.mp3" },
    { "music_star",       1, 1, 0.8f, 0.0f,  "Content/Audio/music_star.mp3" },
    { "music_castle",     1, 1, 0.8f, 0.0f,  "Content/Audio/music_castle.mp3" },
    { "amb_wind",         2, 3, 0.7f, 0.0f,  "Content/Audio/amb_wind.mp3" },
    { "amb_cave_drip",    2, 3, 0.7f, 0.0f,  "Content/Audio/amb_cave_drip.mp3" },
    { "amb_birds",        2, 3, 0.7f, 0.0f,  "Content/Audio/amb_birds.mp3" },
    { "amb_crickets",     2, 3, 0.7f, 0.0f,  "Content/Audio/amb_crickets.mp3" },
        { "hurry_up",         0, 2, 0.9f, 0.00f, "Content/Audio/hurryup.wav" },
};

static const char* BusName(int b) { return b == 1 ? "music" : (b == 3 ? "ambience" : "sfx"); }
static int BusFrom(const std::string& s) { return s == "music" ? 1 : (s == "ambience" ? 3 : 2); }
static float Clampf(float v, float lo, float hi) { return v != v ? lo : (v < lo ? lo : (v > hi ? hi : v)); }

static fs::path BankPath()
{
    return fs::path(GhostAudio_Wide(g_root.c_str())) / L"Content" / L"Audio" / L"Sounds.ghostaudio";
}

void GhostAudioBank_Load(const std::string& projectRoot)
{
    g_root = projectRoot;
    g_bank.clear();
    for (const Def& d : kDefaults)
    {
        GhostSoundDef s;
        s.name = d.name; s.file = d.file; s.kind = d.kind; s.bus = d.bus; s.volume = d.vol; s.pitchVar = d.pitch;
        g_bank.push_back(s);
    }
    GhostAudio_ClearMissing();
    if (g_root.empty()) return;

    try
    {
        std::ifstream in(BankPath(), std::ios::binary);
        if (!in) return;
        json j = json::parse(in);
        if (!j.is_object() || j.value("format", std::string()) != "ghostaudio" || !j.contains("sounds")) return;

        for (const json& js : j["sounds"])
        {
            const std::string name = js.value("name", std::string());
            if (name.empty()) continue;
            int idx = GhostAudioBank_Find(name.c_str());
            if (idx < 0) { GhostSoundDef n; n.name = name; g_bank.push_back(n); idx = (int)g_bank.size() - 1; }   // a custom sound
            GhostSoundDef& s = g_bank[(size_t)idx];
            s.file = js.value("file", s.file);
            s.kind = js.value("kind", s.kind);
            s.kind = s.kind < 0 ? 0 : (s.kind > 2 ? 2 : s.kind);
            s.bus = BusFrom(js.value("bus", std::string(BusName(s.bus))));
            s.volume = Clampf(js.value("volume", s.volume), 0.0f, 2.0f);
            s.pitchVar = Clampf(js.value("pitchVariation", s.pitchVar), 0.0f, 0.5f);
            s.enabled = js.value("enabled", s.enabled);
        }
    }
    catch (...) { /* damaged file: the defaults stay */ }
}

bool GhostAudioBank_Save(std::string& err)
{
    if (g_root.empty()) { err = "No project is open."; return false; }
    try
    {
        json j;
        j["format"] = "ghostaudio";
        j["version"] = 1;
        json arr = json::array();
        for (const GhostSoundDef& s : g_bank)
        {
            json o;
            o["name"] = s.name;
            o["file"] = s.file;
            o["kind"] = s.kind;
            o["bus"] = BusName(s.bus);
            o["volume"] = s.volume;
            o["pitchVariation"] = s.pitchVar;
            o["enabled"] = s.enabled;
            arr.push_back(o);
        }
        j["sounds"] = arr;

        const fs::path out = BankPath();
        fs::create_directories(out.parent_path());
        fs::path tmp = out;
        tmp += L".tmp";
        {
            std::ofstream f(tmp, std::ios::binary);
            if (!f) { err = "Could not write Content/Audio/Sounds.ghostaudio."; return false; }
            f << j.dump(2);
            if (!f) { err = "Writing the sound bank failed."; return false; }
        }
        fs::rename(tmp, out);
        return true;
    }
    catch (const std::exception& e) { err = e.what(); return false; }
}

int GhostAudioBank_Count() { return (int)g_bank.size(); }
GhostSoundDef* GhostAudioBank_At(int i) { return (i >= 0 && i < (int)g_bank.size()) ? &g_bank[(size_t)i] : nullptr; }

int GhostAudioBank_Find(const char* name)
{
    if (!name) return -1;
    for (size_t i = 0; i < g_bank.size(); i++) if (g_bank[i].name == name) return (int)i;
    return -1;
}

std::string GhostAudioBank_Resolve(const GhostSoundDef& d)
{
    std::string p = d.file;
    if (p.empty()) return "";
    const bool absolute = p.size() > 1 && (p[1] == ':' || p[0] == '\\' || p[0] == '/');
    if (!absolute)
    {
        if (g_root.empty()) return "";
        for (char& c : p) if (c == '/') c = '\\';
        p = g_root + "\\" + p;
    }
    return p;
}

static unsigned g_rng = 2463534242u;
static float Rand01() { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return (float)(g_rng & 0xFFFFFF) / 16777215.0f; }

bool GhostAudioBank_PlayIndex(int i, float fade, bool force)
{
    GhostSoundDef* d = GhostAudioBank_At(i);
    if (!d || (!d->enabled && !force)) return false;
    const std::string p = GhostAudioBank_Resolve(*d);
    if (p.empty()) return false;

    if (d->kind == GHOST_SND_MUSIC)    return GhostAudio_PlayMusic(p.c_str(), d->volume, fade, true);
    if (d->kind == GHOST_SND_AMBIENCE) return GhostAudio_PlayAmbience(p.c_str(), d->volume, fade, true);
    const float pitch = 1.0f + (Rand01() * 2.0f - 1.0f) * d->pitchVar;
    return GhostAudio_PlayFile(p.c_str(), d->bus, d->volume, pitch);
}

bool GhostAudioBank_Play(const char* name, float fade) { return GhostAudioBank_PlayIndex(GhostAudioBank_Find(name), fade, false); }