#pragma once
#include <Windows.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include "../GhostX/include/GhostGameAPI.h"
#include "../GhostGame/include/Bindings.h"

// Game.h

extern const GhostGameAPI* G;
extern float g_faceDir;
extern float g_moveYawDeg;      // world yaw Mario last moved toward
extern bool  g_haveMoveYaw;
// Plays a sound from the bank by name. Safe when the sound is disabled, missing, or audio is off.
inline void Sfx(const char* name) { if (G && G->PlaySfx) G->PlaySfx(name); }
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
    constexpr float CastleFlagRise = 1.0f;    // starts this far below its placed spot (one block)
    constexpr float CastleFlagSpeed = 0.5f;   // units per second it rises
    constexpr float BridgeTileDelay = 0.15f;   // seconds between bridge tiles falling
    constexpr float BossHopInterval = 1.4f;    // seconds between boss hops
    constexpr float FireYawOffset = 0.0f;   // was 90.0f; rotation now lives in Player.cpp
    constexpr float DeathTime = 2.5f;        // seconds the Death animation plays before the restart / Game Over
    constexpr float DeathHop = 12.0f;        // hop speed when he dies on screen
    constexpr float DeathPop = 0.65f;        // pit death: how high the pop reaches (fraction of the screen height)
    constexpr float TransformTime = 0.9f;    // seconds the game freezes while the Transform animation plays
    constexpr float ShellKickSafe = 0.3f;    // a freshly kicked or thrown shell can't hurt Mario for this long
    constexpr float HurryTime = 100.0f;      // the hurry-up jingle plays when the timer drops to this
    constexpr float RunJump = 20.0f;   // jump speed while sprinting (Jump = 15 otherwise)
    constexpr float CamFocusPitch = -15.0f;   // Z-reset tilt (negative = camera above Mario, looking down, same as CamPitch)
    constexpr float CamFocusPitchSpeed = 300.0f;   // degrees per second the tilt eases back
    constexpr float GrowPop = 0.1f;   // extra lift when growing, so the bigger collider clears the floor
    constexpr float ShrinkTime = 0.9f;   // freeze while the Shrink animation plays
    constexpr float SkidMinSpeed = 4.5f;    // planar speed needed to skid. Walk = 4.0, so only sprinting skids. Lower it (e.g. 2.0) to skid from a walk too
    constexpr float SkidExitSpeed = 0.5f;    // skid ends when speed drops below this
    constexpr float SkidFriction = 28.0f;   // braking, units/s^2 (GroundAccel is 60). Lower = longer slide
    constexpr float SkidOpposeCos = -0.6f;   // input must oppose velocity this much (-1 = exactly opposite)
    constexpr bool  SkidFlipSprite = false;   // true if your skid art is drawn facing the other way
}

enum { MODE_MENU = 0, MODE_PLAY, MODE_OVER, MODE_SELECT, MODE_OPTIONS };

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
    int  character = 0;                // 0 Mario, 1 Luigi
    bool twoPlayer = false;            // alternates Mario / Luigi per level
    bool tutorial = false;             // running Tutorial.ghostscene: no timer, no time bonus
    bool  onPole = false;  
    bool  bossCut = false;             // axe hit: Mario frozen, timer stopped
    float poleSlide = 0.0f;   // how far Mario has slid down
    int   poleId = 0;
    float poleTop = 0.0f, poleBot = 0.0f;
    float deathTimer = 0.0f;                 // > 0 while the Death animation plays
    int   deathActor = 0;                    // the spawned sprite that plays it
    float deathVy = 0.0f;
    float transformTimer = 0.0f;             // > 0 while the power-up freeze plays
    bool  hurryPlayed = false;               // the jingle plays once per life
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
void Enemy_Kill(int id, bool stomp = false);   // stomp = true: a winged enemy loses its wings first
void Blocks_Bump(int id);
void PowerUp_Remove(int id);
void Movers_Update(float dt);
void Coins_Add(int n);
void Fireball_Spawn(float dx, float dy, float dz);
void Life_Add();
bool Pickup_TypeFromTag(const char* tag, int& type);
bool Hidden_Try(Body& b, float vy, float dt);
void Sky_Update(float dt);
void Score_Add(int pts);
void Score_Enemy(bool star);
void Goal_Reached(int poleId, float hitY, float poleTop, float poleBot);
void Boss_AxeHit(int axeId);
// ---- Player.cpp ----
void Player_Die();                           // starts the Death animation; the restart happens when it ends
// ---- Entities.cpp ----
bool Shell_Dangerous(int id);                // a sliding shell that can hurt Mario
void Shell_Stop(int id);                     // stomped: stops sliding and becomes a loose shell
// ---- Game.cpp ----
void Game_Restart(bool died);
// Sprite helpers: they do nothing (and return 0) if the object is a plain cube
int  Sprite_Play(int id, const char* clip, const char* fallback);   // 1 if a clip was set
void Sprite_FaceScreen(int id, float screenVx);                     // > 0 faces right, < 0 faces left