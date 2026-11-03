// GhostAudio.cpp
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../include/GhostAudio.h"
#include "../include/miniaudio.h"
#include <Windows.h>
#include <set>

static ma_engine       g_engine;
static ma_sound_group  g_group[GHOST_BUS_COUNT];
static bool            g_ready = false, g_paused = false;
static float           g_vol[GHOST_BUS_COUNT] = { 1.0f, 0.7f, 1.0f, 0.6f };
static bool            g_mute[GHOST_BUS_COUNT] = {};

std::wstring GhostAudio_Wide(const char* u8)
{
    if (!u8 || !*u8) return L"";
    const int n = MultiByteToWideChar(CP_UTF8, 0, u8, -1, nullptr, 0);
    if (n <= 1) return L"";
    std::wstring w((size_t)n - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, u8, -1, &w[0], n);
    return w;
}

// ---------------- one-shot voices ----------------
static const int MAX_VOICES = 32;
struct Voice { ma_sound snd; bool inited; unsigned serial; };
static Voice    g_voices[MAX_VOICES];
static unsigned g_serial = 0;

// ---------------- sound cache (decoded once, kept in memory) ----------------
static std::set<std::string> g_cached, g_missing;

static bool EnsureCached(const char* path)
{
    const std::string p = path;
    if (g_cached.count(p)) return true;
    if (g_missing.count(p)) return false;
    const std::wstring w = GhostAudio_Wide(path);
    if (GetFileAttributesW(w.c_str()) == INVALID_FILE_ATTRIBUTES) { g_missing.insert(p); return false; }
    if (ma_resource_manager_register_file_w(ma_engine_get_resource_manager(&g_engine), w.c_str(),
        MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_DECODE) != MA_SUCCESS)
    {
        g_missing.insert(p);
        return false;
    }
    g_cached.insert(p);
    return true;
}

void GhostAudio_ClearMissing() { g_missing.clear(); }

// ---------------- music / ambience: two slots per channel so one can fade out while the other fades in ----------------
struct Slot { ma_sound snd; bool inited; bool dying; bool loop; float left; std::string path; };
struct Channel { Slot s[2]; int cur; int bus; };
static Channel g_ch[2];                       // 0 = music, 1 = ambience

static void KillSlot(Slot& s)
{
    if (s.inited) { ma_sound_stop(&s.snd); ma_sound_uninit(&s.snd); }
    s.inited = false; s.dying = false; s.left = 0.0f; s.path.clear();
}

static void FadeOut(Slot& s, float fade)
{
    if (!s.inited) return;
    if (fade <= 0.01f) { KillSlot(s); return; }
    ma_sound_set_fade_in_milliseconds(&s.snd, -1.0f, 0.0f, (ma_uint64)(fade * 1000.0f));   // -1 = from the current level
    s.dying = true;
    s.left = fade + 0.05f;
}

static bool StartLoop(Channel& c, const char* path, float vol, float fade, bool loop)
{
    if (!g_ready || !path || !*path || g_paused) return false;
    if (c.cur >= 0)
    {
        Slot& s = c.s[c.cur];
        if (s.inited && !s.dying && s.path == path) { ma_sound_set_volume(&s.snd, vol); return true; }   // already playing it
    }
    const std::wstring w = GhostAudio_Wide(path);
    if (GetFileAttributesW(w.c_str()) == INVALID_FILE_ATTRIBUTES) return false;

    int next = !c.s[0].inited ? 0 : (!c.s[1].inited ? 1 : (c.cur == 0 ? 1 : 0));
    KillSlot(c.s[next]);
    Slot& n = c.s[next];
    if (ma_sound_init_from_file_w(&g_engine, w.c_str(), MA_SOUND_FLAG_STREAM | MA_SOUND_FLAG_NO_SPATIALIZATION,
        &g_group[c.bus], nullptr, &n.snd) != MA_SUCCESS)
        return false;

    n.inited = true; n.dying = false; n.loop = loop; n.path = path;
    ma_sound_set_looping(&n.snd, loop ? MA_TRUE : MA_FALSE);
    ma_sound_set_volume(&n.snd, vol);
    if (fade > 0.01f) ma_sound_set_fade_in_milliseconds(&n.snd, 0.0f, 1.0f, (ma_uint64)(fade * 1000.0f));
    ma_sound_start(&n.snd);

    if (c.cur >= 0) FadeOut(c.s[c.cur], fade);                 // crossfade
    c.cur = next;
    return true;
}

static void StopLoop(Channel& c, float fade)
{
    if (c.cur >= 0) FadeOut(c.s[c.cur], fade);
    c.cur = -1;
}

// ---------------- lifecycle ----------------
bool GhostAudio_Init()
{
    if (g_ready) return true;
    ma_engine_config cfg = ma_engine_config_init();
    if (ma_engine_init(&cfg, &g_engine) != MA_SUCCESS) return false;

    if (ma_sound_group_init(&g_engine, 0, nullptr, &g_group[GHOST_BUS_MASTER]) != MA_SUCCESS)
    {
        ma_engine_uninit(&g_engine);
        return false;
    }
    for (int b = 1; b < GHOST_BUS_COUNT; b++)
        if (ma_sound_group_init(&g_engine, 0, &g_group[GHOST_BUS_MASTER], &g_group[b]) != MA_SUCCESS)
        {
            for (int k = b - 1; k >= 0; k--) ma_sound_group_uninit(&g_group[k]);
            ma_engine_uninit(&g_engine);
            return false;
        }

    for (Channel& c : g_ch) { c.cur = -1; for (Slot& s : c.s) s.inited = false; }
    g_ch[0].bus = GHOST_BUS_MUSIC;
    g_ch[1].bus = GHOST_BUS_AMBIENCE;
    for (Voice& v : g_voices) v.inited = false;
    g_ready = true;
    g_paused = false;
    for (int b = 0; b < GHOST_BUS_COUNT; b++) ma_sound_group_set_volume(&g_group[b], g_mute[b] ? 0.0f : g_vol[b]);
    return true;
}

void GhostAudio_Shutdown()
{
    if (!g_ready) return;
    GhostAudio_StopOneShots();
    for (Channel& c : g_ch) { for (Slot& s : c.s) KillSlot(s); c.cur = -1; }
    for (int b = GHOST_BUS_COUNT - 1; b >= 0; b--) ma_sound_group_uninit(&g_group[b]);
    ma_engine_uninit(&g_engine);
    g_cached.clear(); g_missing.clear();
    g_ready = false;
}

bool GhostAudio_Ready() { return g_ready; }

void GhostAudio_Update(float dt)
{
    if (!g_ready || g_paused) return;
    for (Channel& c : g_ch)
        for (int i = 0; i < 2; i++)
        {
            Slot& s = c.s[i];
            if (!s.inited) continue;
            if (s.dying) { s.left -= dt; if (s.left <= 0.0f) KillSlot(s); }
            else if (!s.loop && ma_sound_at_end(&s.snd)) { KillSlot(s); if (c.cur == i) c.cur = -1; }   // a one-time track ended
        }
}

void GhostAudio_SetPaused(bool paused)
{
    if (!g_ready || paused == g_paused) return;
    g_paused = paused;
    if (paused) ma_engine_stop(&g_engine); else ma_engine_start(&g_engine);
}
bool GhostAudio_IsPaused() { return g_paused; }

void GhostAudio_StopAll(float fade)
{
    if (!g_ready) return;
    GhostAudio_StopOneShots();
    StopLoop(g_ch[0], fade);
    StopLoop(g_ch[1], fade);
}

// ---------------- buses ----------------
static void ApplyBus(int b) { if (g_ready) ma_sound_group_set_volume(&g_group[b], g_mute[b] ? 0.0f : g_vol[b]); }

void GhostAudio_SetBusVolume(int b, float v)
{
    if (b < 0 || b >= GHOST_BUS_COUNT) return;
    g_vol[b] = (v != v) ? 1.0f : (v < 0.0f ? 0.0f : (v > 2.0f ? 2.0f : v));
    ApplyBus(b);
}
float GhostAudio_GetBusVolume(int b) { return (b >= 0 && b < GHOST_BUS_COUNT) ? g_vol[b] : 0.0f; }
void  GhostAudio_SetBusMute(int b, bool m) { if (b < 0 || b >= GHOST_BUS_COUNT) return; g_mute[b] = m; ApplyBus(b); }
bool  GhostAudio_GetBusMute(int b) { return (b >= 0 && b < GHOST_BUS_COUNT) ? g_mute[b] : false; }

// ---------------- one-shots ----------------
bool GhostAudio_PlayFile(const char* path, int bus, float volume, float pitch)
{
    if (!g_ready || g_paused || !path || !*path) return false;
    if (bus < 1 || bus >= GHOST_BUS_COUNT) bus = GHOST_BUS_SFX;
    if (!EnsureCached(path)) return false;

    Voice* v = nullptr;
    for (Voice& x : g_voices)                                   // recycle finished voices, then look for a free one
        if (x.inited && ma_sound_at_end(&x.snd)) { ma_sound_uninit(&x.snd); x.inited = false; }
    for (Voice& x : g_voices) if (!x.inited) { v = &x; break; }
    if (!v)                                                     // all busy: steal the oldest
    {
        v = &g_voices[0];
        for (Voice& x : g_voices) if (x.serial < v->serial) v = &x;
        ma_sound_stop(&v->snd); ma_sound_uninit(&v->snd); v->inited = false;
    }

    const std::wstring w = GhostAudio_Wide(path);
    if (ma_sound_init_from_file_w(&g_engine, w.c_str(), MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_NO_SPATIALIZATION,
        &g_group[bus], nullptr, &v->snd) != MA_SUCCESS)
        return false;
    v->inited = true;
    v->serial = ++g_serial;
    ma_sound_set_volume(&v->snd, volume < 0.0f ? 0.0f : volume);
    ma_sound_set_pitch(&v->snd, pitch > 0.05f ? pitch : 1.0f);
    ma_sound_start(&v->snd);
    return true;
}

void GhostAudio_StopOneShots()
{
    if (!g_ready) return;
    for (Voice& v : g_voices)
        if (v.inited) { ma_sound_stop(&v.snd); ma_sound_uninit(&v.snd); v.inited = false; }
}

// ---------------- music / ambience ----------------
bool GhostAudio_PlayMusic(const char* p, float vol, float fade, bool loop) { return StartLoop(g_ch[0], p, vol, fade, loop); }
void GhostAudio_StopMusic(float fade) { if (g_ready) StopLoop(g_ch[0], fade); }
bool GhostAudio_PlayAmbience(const char* p, float vol, float fade, bool loop) { return StartLoop(g_ch[1], p, vol, fade, loop); }
void GhostAudio_StopAmbience(float fade) { if (g_ready) StopLoop(g_ch[1], fade); }

static const char* PathOf(const Channel& c) { return (c.cur >= 0 && c.s[c.cur].inited) ? c.s[c.cur].path.c_str() : ""; }
const char* GhostAudio_MusicPath() { return PathOf(g_ch[0]); }
const char* GhostAudio_AmbiencePath() { return PathOf(g_ch[1]); }