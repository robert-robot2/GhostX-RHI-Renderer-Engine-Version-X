#pragma once
#include <Windows.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include "../GhostX/include/GhostGameAPI.h"

// Game.h

extern const GhostGameAPI* G;
extern float g_faceDir;
void Shell_Kick(int id, float dx, float dz);   // start sliding along (dx, dz)
void Shell_Hold(int id);                       // Mario picks it up
bool Shell_Ready(int id);                      // is a loose, stationary shell past its grace time

namespace Cfg
{
    constexpr float Gravity = 25.0f, MaxFall = 30.0f;
    constexpr float Walk = 4.0f, Run = 7.0f, Jump = 15.0f;
    constexpr float GroundAccel = 60.0f, AirAccel = 30.0f;
    constexpr float CoyoteTime = 0.10f, JumpBuffer = 0.12f;
    constexpr float EnemySpeed = 1.5f, EnemyPatrol = 3.0f;
    constexpr float StompBounce = 7.0f, StompBounceHeld = 11.0f;
    constexpr float GrowFactor = 1.4f, Invulnerable = 1.5f;
    constexpr float FallLimit = -50.0f;
    constexpr float CamDistance = 7.0f, CamPitch = -25.0f, CamTargetHeight = 0.6f;
    constexpr float CamTurnSpeed = 100.0f, CamSmooth = 10.0f;
    constexpr bool SideScroller = false;        // true: W/S (up/down) don't move the player, only left/right
    constexpr bool SmallBreaksBricks = false;   // false = small Mario only bumps bricks, like the original
    constexpr float StarTime = 10.0f, PickupSize = 0.6f, PickupRise = 1.5f, PickupSpeed = 2.0f, StarBounce = 9.0f;
    constexpr float PowerScaleX[3] = { 1.0f, 1.0f, 1.0f };   // small, big, fire: match your art's width ratio
    constexpr float PowerScaleY[3] = { 1.0f, 2.0f, 2.0f };   // 16x16 -> 16x32 art = 2.0. The collider follows the scale.
    constexpr float NightDepth = -2.0f;      // Mario below this Y: the sky goes to night
    constexpr float NightHour = 22.0f;
    constexpr float NightFadeSpeed = 8.0f;   // hours per second (8 = about 1.2 s to night). Use 1000 to snap instantly.
    constexpr float TimeStart = 300.0f;   // matches your HUD's "Timer 300"
    constexpr float TimeTick = 1.0f;     // real seconds per timer unit (SMB1 = 0.4)
    constexpr float PoleSlide = 4.0f;   // units per second Mario slides down the pole
}

enum { MODE_MENU = 0, MODE_PLAY, MODE_OVER };

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
    int   playerMaxCoins = 100;   // ADD THIS — 1-UP threshold, never touched by level data
    int   enemiesDown = 0, totalEnemies = 0;
    int   deaths = 0;
    bool  big = false;
    float invuln = 0.0f;
    bool  complete = false;
    const char* error = nullptr;
    int   level = -1;                  // index in kLevels, -1 = not one of them
    bool  lastLevel = true;
    float advance = 0.0f;              // seconds since the Goal, for auto-advancing
    float portalCool = 0.0f;
    bool fire = false; float starTime = 0.0f; float attackCool = 0.0f; int   heldShell = 0;        // id of shell Mario is holding, 0 = none
    bool  shellSliding = false; int lives = 3;
    int score = 0, stompChain = 0, starChain = 0, timeBonus = 0;
    float timeLeft = Cfg::TimeStart;
    int mode = MODE_MENU;
    bool  onPole = false;
    float poleSlide = 0.0f;   // how far Mario has slid down
    int   poleId = 0;
    float poleTop = 0.0f, poleBot = 0.0f;
};
extern GameState S;
enum { PU_NONE = 0, PU_MUSHROOM, PU_FLOWER, PU_STAR, PU_ONEUP };
inline int PowerLevel() { return S.big ? (S.fire ? 2 : 1) : 0; }
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
void Movers_Update(float dt);
void Coins_Add(int n);
void Fireball_Spawn();
void Life_Add();
bool Pickup_TypeFromTag(const char* tag, int& type);
bool Hidden_Try(Body& b, float vy, float dt);
void Sky_Update(float dt);
void Score_Add(int pts);
void Score_Enemy(bool star);
void Goal_Reached(int poleId, float hitY, float poleTop, float poleBot);

// ---- Game.cpp ----
void Game_Restart(bool died);
// Sprite helpers: they do nothing (and return 0) if the object is a plain cube
int  Sprite_Play(int id, const char* clip, const char* fallback);   // 1 if a clip was set
void Sprite_FaceScreen(int id, float screenVx);                     // > 0 faces right, < 0 faces left