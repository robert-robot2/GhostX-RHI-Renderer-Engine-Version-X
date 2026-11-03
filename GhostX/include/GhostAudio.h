// GhostAudio.h — engine audio: four buses, voice pool, sound cache, looping music + ambience with crossfade
#pragma once
#include <string>

enum GhostBus { GHOST_BUS_MASTER = 0, GHOST_BUS_MUSIC = 1, GHOST_BUS_SFX = 2, GHOST_BUS_AMBIENCE = 3, GHOST_BUS_COUNT = 4 };

bool GhostAudio_Init();                       // false = no audio device: every other call becomes a harmless no-op
void GhostAudio_Shutdown();
bool GhostAudio_Ready();
void GhostAudio_Update(float dt);             // once per frame: finishes fade-outs
void GhostAudio_SetPaused(bool paused);       // pauses everything (device stops), resumes where it was
bool GhostAudio_IsPaused();
void GhostAudio_StopAll(float fadeSeconds);   // one-shots stop now, music and ambience fade out

// Buses: 0 Master, 1 Music, 2 SFX, 3 Ambience. Volume 0..1 (values above 1 boost).
void  GhostAudio_SetBusVolume(int bus, float v);
float GhostAudio_GetBusVolume(int bus);
void  GhostAudio_SetBusMute(int bus, bool mute);
bool  GhostAudio_GetBusMute(int bus);

// Paths are absolute UTF-8. Returns false if the file is missing or can't be decoded.
bool GhostAudio_PlayFile(const char* absPath, int bus, float volume, float pitch);      // one-shot, fully decoded + cached
void GhostAudio_StopOneShots();

bool GhostAudio_PlayMusic(const char* absPath, float volume, float fadeSeconds, bool loop);
void GhostAudio_StopMusic(float fadeSeconds);
const char* GhostAudio_MusicPath();           // "" if none

bool GhostAudio_PlayAmbience(const char* absPath, float volume, float fadeSeconds, bool loop);
void GhostAudio_StopAmbience(float fadeSeconds);
const char* GhostAudio_AmbiencePath();

void GhostAudio_ClearMissing();               // forget "file not found" results (call after the bank changes)
std::wstring GhostAudio_Wide(const char* utf8);