// GhostParticleMan.h
// Particle systems: emitters, simulation and the per-frame draw list.
//
// The emitter DATA lives in GhostScene::particles (so save / load, undo, Play restore and the
// unsaved-changes check all work like they do for objects). This module only owns the live particles.
// The module never calls D3D: it produces GhostParticleDraw entries and GhostEngine.cpp draws them.
#pragma once
#include "GhostMath.h"
#include <string>
#include <vector>

enum GhostParticleType
{
    GHOST_PFX_FIRE = 0,       // 2D sprite (animated clip) billboard, additive
    GHOST_PFX_SNOW = 1,       // 3D volume box, sprite or mesh flakes
    GHOST_PFX_CUSTOM = 2,
    GHOST_PFX_TYPE_COUNT
};

constexpr int          GHOST_PFX_MAX_EMITTERS = 32;
constexpr int          GHOST_PFX_MAX_PER_EMITTER = 1000;
constexpr unsigned int GHOST_PFX_MAX_DRAWS = 384;      // constant-buffer slots reserved for particles in every viewport

// ---------------------------------------------------------------
//  Everything the editor can change. Plain data (no strings) so C# can mirror it.
//  Only ever add fields at the END. Strings (animation path, clip) are in GhostParticleDef.
// ---------------------------------------------------------------
struct GhostParticleParams
{
    int   id, type;                         // type is fixed at creation (ApplyParticlePreset changes it)
    float px, py, pz;                       // emitter position (world)
    int   enabled;                          // 1 = emitting

    // ---- emission ----
    float rate;                             // particles per second
    int   maxParticles;
    float lifeMin, lifeMax;                 // seconds
    int   prewarm;                          // 1 = starts already full
    int   spawnMode;                        // 0 anywhere in the box, 1 top face, 2 bottom face
    float boxX, boxY, boxZ;                 // spawn volume, full size, centered on the position
    int   killOutside;                      // 1 = a particle that falls below the box dies

    // ---- motion ----
    float velMinX, velMinY, velMinZ;
    float velMaxX, velMaxY, velMaxZ;
    float accelX, accelY, accelZ;           // units / s^2
    float drag;                             // 0 = none
    float swayAmp, swayFreq;                // sideways drift (snow)

    // ---- look ----
    float sizeMin, sizeMax;
    float sizeEnd;                          // size multiplier at the end of life (1 = unchanged, 0 = shrinks away)
    float r0, g0, b0, r1, g1, b1;           // color at birth / at death
    float alpha0, alpha1;                   // fade (only additive particles fade; cutout ones shrink with sizeEnd)
    float rotMin, rotMax, spinMin, spinMax; // degrees, degrees / s. Billboards roll, meshes turn around Y
    int   billboard, unlit, additive, alphaClip, pixelArt;

    // ---- sprite / mesh ----
    int   textureId;                        // 0 = the built-in soft particle texture (sprites) or white (meshes)
    int   source, cols, rows;               // source 0 = single image, 1 = sprite sheet
    float animFps;                          // sprite sheet playback speed
    float animSpeed;                        // multiplier (sheet and animation clip)
    int   animOverLife;                     // 1 = the animation plays once from birth to death
    int   randomStartFrame;
    int   meshId;                           // -1 = sprite quad, otherwise a mesh id from the registry
    float drawDistance;                     // not drawn farther than this from the camera
};

// One emitter as the scene stores it
struct GhostParticleDef
{
    GhostParticleParams p = {};
    std::string anim;                       // stored path of a .ghostanim ("" = none)
    std::string clip;                       // clip name ("" = the animation's first clip)
};

// The plain number fields the scene file stores by name (id, type, textureId and meshId are handled by hand,
// because textures and meshes have to be remapped when a scene loads)
#define GHOST_PARTICLE_FIELDS(X) \
    X(px) X(py) X(pz) X(enabled) \
    X(rate) X(maxParticles) X(lifeMin) X(lifeMax) X(prewarm) X(spawnMode) \
    X(boxX) X(boxY) X(boxZ) X(killOutside) \
    X(velMinX) X(velMinY) X(velMinZ) X(velMaxX) X(velMaxY) X(velMaxZ) \
    X(accelX) X(accelY) X(accelZ) X(drag) X(swayAmp) X(swayFreq) \
    X(sizeMin) X(sizeMax) X(sizeEnd) \
    X(r0) X(g0) X(b0) X(r1) X(g1) X(b1) X(alpha0) X(alpha1) \
    X(rotMin) X(rotMax) X(spinMin) X(spinMax) \
    X(billboard) X(unlit) X(additive) X(alphaClip) X(pixelArt) \
    X(source) X(cols) X(rows) X(animFps) X(animSpeed) X(animOverLife) X(randomStartFrame) \
    X(drawDistance)

// What the engine tells us about one sprite clip of a .ghostanim
struct GhostParticleClipInfo
{
    int   textureId;                        // 0 = keep the emitter's texture
    int   cols, rows;
    int   frameCount;
    float duration;                         // seconds
    int   loop;
};

// Callbacks GhostEngine.cpp installs, so this module never needs to know about g_anims
struct GhostParticleHost
{
    int (*ResolveClip)(const char* animPath, const char* clip, GhostParticleClipInfo* out);   // handle >= 0, -1 = failed
    int (*FrameAt)(int handle, float time);                                                    // frame (cell) index at a time
    int (*ClipNames)(const char* animPath, char* buf, int bufSize);                            // names joined with '|'
};

// One drawn particle (already final: the engine only fills a constant buffer from it)
struct GhostParticleDraw
{
    float x, y, z;
    float size;
    float rot;                              // degrees (roll for billboards, Y turn for meshes)
    float r, g, b;                          // tint (additive particles are already multiplied by their fade)
    int   meshId;                           // GHOST_MESH_QUAD for sprites
    int   textureId;
    int   billboard, unlit, additive, alphaClip, pixelArt;
    float uvScaleX, uvScaleY, uvOffX, uvOffY;   // same meaning as GhostObjectCB::uvParams
};

// ---- lifecycle ----
void GhostParticles_Bind(std::vector<GhostParticleDef>* defs);                      // the scene's emitter list (once, at Init)
void GhostParticles_Shutdown();
void GhostParticles_SetHost(const GhostParticleHost* host);
void GhostParticles_SetDefaultTexture(int textureId);
void GhostParticles_MakeSoftTexture(std::vector<unsigned char>& rgba, int size);    // RGBA8, soft round blob
void GhostParticles_InvalidateClips();                                              // call after animations were reloaded
void GhostParticles_ResetAll();                                                     // drops every live particle (a different scene was loaded)

// ---- emitters ----
void GhostParticles_Clamp(GhostParticleParams& p);                                  // used by the scene loader too
void GhostParticles_FillPreset(int type, GhostParticleParams& p);                   // keeps id and position
int  GhostParticles_Add(int type, float x, float y, float z);                       // new id, 0 = failed. x,y,z = a ground point
void GhostParticles_Clear();

// ---- per frame ----
void GhostParticles_Update(float dt);                                               // dt 0 = paused
int  GhostParticles_BuildDraws(const GhostVec3& eye, std::vector<GhostParticleDraw>& out, unsigned int maxDraws);
GhostMat4 GhostParticles_Model(const GhostParticleDraw& d, const GhostVec3& camRight, const GhostVec3& camUp, const GhostVec3& camFwd);