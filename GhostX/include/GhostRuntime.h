#pragma once
#include "GhostScene.h"
#include <string.h>

enum class GhostPlayState : int { Editing = 0, Playing = 1, Paused = 2 };

constexpr float GHOST_FIXED_DT = 1.0f / 60.0f;
constexpr int   GHOST_MAX_STEPS_PER_FRAME = 5;

// Key state sampled once per rendered frame. "pressed" / "released" stay set until a fixed step has seen them.
struct GhostInput
{
    bool down[256];
    bool pressed[256];
    bool released[256];
    bool prev[256];

    GhostInput() { Reset(); }
    void Reset()
    {
        memset(down, 0, sizeof down); memset(pressed, 0, sizeof pressed);
        memset(released, 0, sizeof released); memset(prev, 0, sizeof prev);
    }
    void Sample(bool enabled);     // enabled = the viewport has keyboard focus
    void EndStep();                // call after every fixed step
};

// Turns variable frame times into a whole number of fixed steps
struct GhostStepper
{
    float accum = 0.0f;
    void Reset() { accum = 0.0f; }
    int Advance(float dt)
    {
        if (dt > 0.25f) dt = 0.25f;                       // a long stall (debugger, drag) doesn't fast-forward the game
        accum += dt;
        int steps = 0;
        while (accum >= GHOST_FIXED_DT && steps < GHOST_MAX_STEPS_PER_FRAME) { accum -= GHOST_FIXED_DT; steps++; }
        if (steps == GHOST_MAX_STEPS_PER_FRAME && accum > GHOST_FIXED_DT) accum = 0.0f;   // fell behind: drop the rest
        return steps;
    }
};

// ---- TEMPORARY test behavior, replaced by the game module in 4C ----
struct GhostTestState
{
    int   objectId = 0;
    float groundY = 0.0f;
    float vy = 0.0f;
    bool  grounded = true;
};
void GhostRuntime_TestBegin(const GhostScene& s, GhostTestState& t);
void GhostRuntime_TestStep(GhostScene& s, const GhostInput& in, GhostTestState& t, float dt);