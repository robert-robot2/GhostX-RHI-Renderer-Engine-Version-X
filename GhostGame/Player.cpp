#include "Game.h"

static float g_vel[3];
static float g_coyote, g_jumpBuf;
static bool  g_grounded;
static float g_baseScale[3] = { 1, 1, 1 };

static float Approach(float v, float target, float maxStep)
{
    float d = target - v;
    if (d > maxStep) d = maxStep;
    if (d < -maxStep) d = -maxStep;
    return v + d;
}

void Player_Init() { G->GetScale(S.player, g_baseScale); }

void Player_Reset()
{
    memset(g_vel, 0, sizeof g_vel);
    g_coyote = g_jumpBuf = 0.0f;
    g_grounded = false;
    S.error = nullptr;
}

static void Grow(Body& b)
{
    if (S.big) { S.coins++; return; }              // already big: the power-up is worth a coin
    S.big = true;
    float s[3] = { g_baseScale[0] * Cfg::GrowFactor, g_baseScale[1] * Cfg::GrowFactor, g_baseScale[2] * Cfg::GrowFactor };
    G->SetScale(S.player, s);
    b.pos[1] += b.half[1] * (Cfg::GrowFactor - 1.0f) + 0.01f;      // feet stay where they were
}

static void Shrink(Body& b)
{
    S.big = false;
    G->SetScale(S.player, g_baseScale);
    b.pos[1] -= b.half[1] * (1.0f - 1.0f / Cfg::GrowFactor);
}

void Player_Update(float dt)
{
    Body b;
    if (!LoadBody(S.player, b)) { S.error = "The Player needs a collider (Details > Gameplay > Collider)"; return; }
    S.error = nullptr;

    // ---- Input: camera-relative movement ----
    float ix = (G->KeyDown('D') ? 1.0f : 0.0f) - (G->KeyDown('A') ? 1.0f : 0.0f);
    float iz = (G->KeyDown('W') ? 1.0f : 0.0f) - (G->KeyDown('S') ? 1.0f : 0.0f);
    float yaw = S.camYaw * 0.0174532925f;
    float sy = sinf(yaw), cy = cosf(yaw);
    float dx = sy * iz + cy * ix;                  // forward = (sin, cos), right = (cos, -sin)
    float dz = cy * iz - sy * ix;
    float len = sqrtf(dx * dx + dz * dz);
    if (len > 1.0f) { dx /= len; dz /= len; }

    const float speed = G->KeyDown(VK_SHIFT) ? Cfg::Run : Cfg::Walk;
    const float accel = (g_grounded ? Cfg::GroundAccel : Cfg::AirAccel) * dt;
    g_vel[0] = Approach(g_vel[0], dx * speed, accel);
    g_vel[2] = Approach(g_vel[2], dz * speed, accel);

    // ---- Jump: buffered, with coyote time and variable height ----
    g_jumpBuf = G->KeyPressed(VK_SPACE) ? Cfg::JumpBuffer : g_jumpBuf - dt;
    g_coyote = g_grounded ? Cfg::CoyoteTime : g_coyote - dt;
    if (g_jumpBuf > 0.0f && g_coyote > 0.0f) { g_vel[1] = Cfg::Jump; g_jumpBuf = 0.0f; g_coyote = 0.0f; }
    if (G->KeyReleased(VK_SPACE) && g_vel[1] > 0.0f) g_vel[1] *= 0.5f;

    g_vel[1] -= Cfg::Gravity * dt;
    if (g_vel[1] < -Cfg::MaxFall) g_vel[1] = -Cfg::MaxFall;

    // ---- Move ----
    const float feetBefore = b.pos[1] - b.half[1];
    const float preVy = g_vel[1];
    memcpy(b.vel, g_vel, sizeof g_vel);
    MoveResult r = MoveBody(b, dt);
    memcpy(g_vel, b.vel, sizeof g_vel);
    g_grounded = r.hitFloor;
    if (r.hitCeiling) Blocks_Bump(r.ceilingId);

    // ---- What are we touching? ----
    int ids[32];
    int n = QueryBody(b, ids, 32);
    for (int i = 0; i < n; i++)
    {
        char tag[32];
        if (!G->GetTag(ids[i], tag, sizeof tag)) continue;

        if (strcmp(tag, "Coin") == 0) Coin_Collect(ids[i]);
        else if (strcmp(tag, "Goal") == 0) S.complete = true;
        else if (strcmp(tag, "PowerUp") == 0) { PowerUp_Remove(ids[i]); Grow(b); }
        else if (strcmp(tag, "Enemy") == 0)
        {
            float emn[3], emx[3];
            if (!G->GetBounds(ids[i], emn, emx)) continue;

            const bool stomp = preVy < 0.0f && feetBefore >= emx[1] - 0.12f;     // we were above it and falling
            if (stomp)
            {
                Enemy_Kill(ids[i]);
                g_vel[1] = G->KeyDown(VK_SPACE) ? Cfg::StompBounceHeld : Cfg::StompBounce;
            }
            else if (S.invuln <= 0.0f)
            {
                if (S.big) { Shrink(b); S.invuln = Cfg::Invulnerable; S.hurt = 0.45f; g_vel[1] = 6.0f; }
                else { Game_Restart(true); return; }
            }
        }
    }

    // ---- Falling off the world ----
    if (b.pos[1] < Cfg::FallLimit) { Game_Restart(true); return; }

    G->SetPosition(S.player, b.pos);
    // ---- Sprite animation (does nothing if the Player is a plain cube) ----
    {
        const float yaw2 = S.camYaw * 0.0174532925f;
        Sprite_FaceScreen(S.player, g_vel[0] * cosf(yaw2) - g_vel[2] * sinf(yaw2));   // left / right as seen on screen

        const float planar = sqrtf(g_vel[0] * g_vel[0] + g_vel[2] * g_vel[2]);
        if (S.hurt > 0.0f)        Sprite_Play(S.player, "Hurt", "Idle");
        else if (!g_grounded)     Sprite_Play(S.player, g_vel[1] > 0.0f ? "Jump" : "Fall", "Jump");
        else if (planar > 0.4f)   Sprite_Play(S.player, "Walk", "Idle");
        else                      Sprite_Play(S.player, "Idle", nullptr);

        G->SpriteSetSpeed(S.player, (g_grounded && planar > 0.4f && G->KeyDown(VK_SHIFT)) ? 1.6f : 1.0f);   // running looks faster
        if (S.hurt > 0.0f) S.hurt -= dt;
    }
    // ---- Blink while invulnerable ----
    if (S.invuln > 0.0f)
    {
        S.invuln -= dt;
        G->SetVisible(S.player, (S.invuln > 0.0f && (((int)(S.invuln * 12.0f)) & 1)) ? 0 : 1);
    }
}