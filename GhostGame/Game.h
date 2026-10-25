#pragma once
#include <Windows.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include "../GhostX/include/GhostGameAPI.h"

extern const GhostGameAPI* G;

namespace Cfg
{
    constexpr float Gravity = 25.0f, MaxFall = 30.0f;
    constexpr float Walk = 4.0f, Run = 7.0f, Jump = 9.0f;
    constexpr float GroundAccel = 60.0f, AirAccel = 30.0f;
    constexpr float CoyoteTime = 0.10f, JumpBuffer = 0.12f;
    constexpr float EnemySpeed = 1.5f, EnemyPatrol = 3.0f;
    constexpr float StompBounce = 7.0f, StompBounceHeld = 11.0f;
    constexpr float GrowFactor = 1.4f, Invulnerable = 1.5f;
    constexpr float FallLimit = -15.0f;
    constexpr float CamDistance = 7.0f, CamPitch = -25.0f, CamTargetHeight = 0.6f;
    constexpr float CamTurnSpeed = 100.0f, CamSmooth = 10.0f;
}

struct GameState
{
    int   player = 0;
    int   camId = 0;
    float camYaw = 0.0f;
    float camPos[3] = { 0, 0, 0 };
    bool  camSnap = true;
    bool  orbit = false;               // the engine moves the camera (Orbit mode)
    float hurt = 0.0f;                 // seconds left of the Hurt pose
    int   coins = 0, totalCoins = 0;
    int   enemiesDown = 0, totalEnemies = 0;
    int   deaths = 0;
    bool  big = false;
    float invuln = 0.0f;
    bool  complete = false;
    const char* error = nullptr;
    int   level = -1;                  // index in kLevels, -1 = not one of them
    bool  lastLevel = true;
    float advance = 0.0f;              // seconds since the Goal, for auto-advancing
};
extern GameState S;

// ---- Collision.cpp ----
struct Body
{
    int   id = 0;
    float pos[3] = { 0, 0, 0 };        // collider center
    float half[3] = { 0.5f, 0.5f, 0.5f };
    float vel[3] = { 0, 0, 0 };
    float off[3] = { 0, 0, 0 };        // collider center minus the object's position
};
struct MoveResult
{
    bool hitX = false, hitZ = false, hitFloor = false, hitCeiling = false;
    int  ceilingId = 0;
};
bool       IsSolid(int id);                                   // Static and Block objects, plus any custom tag that isn't an actor
bool       LoadBody(int id, Body& b);                         // false if the object is missing or has no collider
MoveResult MoveBody(Body& b, float dt);                       // moves by vel * dt: X, then Z, then Y
int        QueryBody(const Body& b, int* ids, int maxCount);  // colliders overlapping the body (never itself)
void       StoreBody(const Body& b);                          // writes the object's position back (center minus the offset)
// ---- Player.cpp ----
void Player_Init();
void Player_Reset();
void Player_Update(float dt);

// ---- Entities.cpp ----
void Level_Init();
void Level_Reset();
void Entities_Update(float dt);
void Coin_Collect(int id);
void Enemy_Kill(int id);
void Blocks_Bump(int id);
void PowerUp_Remove(int id);

// ---- Game.cpp ----
void Game_Restart(bool died);
// Sprite helpers: they do nothing (and return 0) if the object is a plain cube
int  Sprite_Play(int id, const char* clip, const char* fallback);   // 1 if a clip was set
void Sprite_FaceScreen(int id, float screenVx);                     // > 0 faces right, < 0 faces left