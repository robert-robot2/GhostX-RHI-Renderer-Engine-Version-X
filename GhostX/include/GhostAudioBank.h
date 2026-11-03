// GhostAudioBank.h — Content/Audio/Sounds.ghostaudio: names -> file, bus, volume, pitch variation, enabled
#pragma once
#include <string>

enum GhostSoundKind { GHOST_SND_SFX = 0, GHOST_SND_MUSIC = 1, GHOST_SND_AMBIENCE = 2 };

struct GhostSoundDef
{
    std::string name;            // the key the game uses: "jump", "coin", "music_overworld" ...
    std::string file;            // project-relative ("Content/Audio/jump.wav") or absolute
    int   kind = GHOST_SND_SFX;
    int   bus = 2;               // 1 music, 2 sfx, 3 ambience
    float volume = 1.0f;         // 0..2
    float pitchVar = 0.0f;       // sfx: random pitch spread, 0.05 = +-5 %
    bool  enabled = true;
};

// Starts from the built-in defaults, then merges the project's file (if there is one). "" = defaults only.
void GhostAudioBank_Load(const std::string& projectRoot);
bool GhostAudioBank_Save(std::string& err);

int            GhostAudioBank_Count();
GhostSoundDef* GhostAudioBank_At(int i);
int            GhostAudioBank_Find(const char* name);              // -1 = not found
std::string    GhostAudioBank_Resolve(const GhostSoundDef& d);     // absolute path ("" if it can't be resolved)

// Plays by kind: sfx = one-shot, music / ambience = looping with crossfade. A disabled entry is skipped unless force.
bool GhostAudioBank_PlayIndex(int i, float fadeSeconds, bool force);
bool GhostAudioBank_Play(const char* name, float fadeSeconds);