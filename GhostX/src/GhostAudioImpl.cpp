// GhostAudioImpl.cpp — the one place miniaudio's implementation is compiled
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define MA_NO_ENCODING        // we only play sounds, never write them
#define MA_NO_GENERATION      // no noise / waveform generators
#define MINIAUDIO_IMPLEMENTATION
#include "../include/miniaudio.h"