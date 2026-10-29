#include "Game.h"

//Player.cpp

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

static const char* kPowerAnim[3] = { "Content/Animations/MarioSmall.ghostanim",   // set [0] to the file your Player uses today
                                     "Content/Animations/MarioBig.ghostanim",
                                     "Content/Animations/MarioFire.ghostanim" };

void Player_Reset()
{
    memset(g_vel, 0, sizeof g_vel);
    g_coyote = g_jumpBuf = 0.0f;
    g_grounded = false;
    S.error = nullptr;
    G->SpriteSetAnim(S.player, kPowerAnim[0]);
}



static void SetPower(Body& b, int level)
{
    const int old = PowerLevel();
    if (old == level) return;
    const float kx = Cfg::PowerScaleX[level] / Cfg::PowerScaleX[old];
    const float ky = Cfg::PowerScaleY[level] / Cfg::PowerScaleY[old];
    S.big = level >= 1; S.fire = level == 2;
    const float s[3] = { g_baseScale[0] * Cfg::PowerScaleX[level], g_baseScale[1] * Cfg::PowerScaleY[level],
                         g_baseScale[2] * Cfg::PowerScaleX[level] };
    G->SetScale(S.player, s);                              // the collider follows the scale
    G->SpriteSetAnim(S.player, kPowerAnim[level]);         // whole animation set swaps
    b.pos[1] += b.half[1] * (ky - 1.0f) + (ky > 1.0f ? 0.01f : 0.0f);   // feet stay put
    b.half[0] *= kx; b.half[2] *= kx; b.half[1] *= ky;
    b.off[0] *= kx;  b.off[2] *= kx;  b.off[1] *= ky;
}

static void Collect(Body& b, int type)
{
    if (type != PU_ONEUP) Score_Add(1000);
    switch (type)
    {
    case PU_MUSHROOM: if (PowerLevel() == 0) SetPower(b, 1); else Coins_Add(1); break;
    case PU_FLOWER:   if (PowerLevel() < 2) SetPower(b, PowerLevel() + 1); else Coins_Add(1); break;   // small -> big, big -> fire
    case PU_STAR:     S.starTime = Cfg::StarTime; S.starChain = 0; break;                                            // a second star resets the timer
    case PU_ONEUP:    Life_Add(); break;
    }
}

// true = Mario died (caller returns)
static bool Hurt(Body& b)
{
    if (S.starTime > 0.0f || S.invuln > 0.0f) return false;
    if (S.big) { SetPower(b, 0); S.invuln = Cfg::Invulnerable; S.hurt = 0.45f; g_vel[1] = 6.0f; return false; }
    Game_Restart(true);
    return true;
}

// Stand on or touch an Entry, then press its trigger. A Link ID pairs an Entry with an Exit.
// A Link ID ending in .ghostscene loads that scene instead.
static bool TryPortal(Body& b, int entry)
{
    GhostObjectProps pr = {};
    if (!G->GetObjectProps(entry, &pr)) return false;
    const bool go = pr.portalTrigger == 0 ||
        (pr.portalTrigger == 1 && (G->KeyPressed(VK_DOWN) || G->KeyPressed('S'))) ||
        (pr.portalTrigger == 2 && G->KeyPressed('F'));
    if (!go) return false;
    if (strstr(pr.link, ".ghostscene")) return G->LoadScene(pr.link) != 0;

    int ids[64];
    const int n = G->FindAllByTag("PortalExit", ids, 64);
    for (int i = 0; i < n; i++)
    {
        GhostObjectProps ex = {};
        if (!G->GetObjectProps(ids[i], &ex) || strcmp(ex.link, pr.link) != 0) continue;

        float mn[3], mx[3], p[3];
        G->GetPosition(ids[i], p);
        float cx = p[0], cz = p[2], top = p[1];
        if (G->GetBounds(ids[i], mn, mx)) { cx = (mn[0] + mx[0]) * 0.5f; cz = (mn[2] + mx[2]) * 0.5f; top = mx[1]; }
        b.pos[0] = cx;
        b.pos[1] = top + b.half[1] + 0.02f;
        b.pos[2] = cz;
        memset(g_vel, 0, sizeof g_vel);
        S.camSnap = true;

      
        S.portalCool = 0.4f;


        return true;
    }
    return false;
}

void Player_Update(float dt)
{
    Body b;
    if (!LoadBody(S.player, b)) { S.error = "The Player needs a collider (Details > Gameplay > Collider)"; return; }
    S.error = nullptr;

    // ---- Input: camera-relative movement ----
    float ix = ((G->KeyDown('D') || G->KeyDown(VK_RIGHT)) ? 1.0f : 0.0f) - ((G->KeyDown('A') || G->KeyDown(VK_LEFT)) ? 1.0f : 0.0f);
    float iz = Cfg::SideScroller ? 0.0f : ((G->KeyDown('W') ? 1.0f : 0.0f) - (G->KeyDown('S') ? 1.0f : 0.0f));
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
    if (Hidden_Try(b, g_vel[1], dt)) g_vel[1] = 0.0f;
    memcpy(b.vel, g_vel, sizeof g_vel);
    MoveResult r = MoveBody(b, dt);
    memcpy(g_vel, b.vel, sizeof g_vel);
    g_grounded = r.hitFloor;
    if (g_grounded) S.stompChain = 0;      // landing resets the stomp chain
    if (r.hitCeiling) Blocks_Bump(r.ceilingId);

    // ---- What are we touching? ----
    int ids[32];
    int n = QueryBody(b, ids, 32);
    for (int i = 0; i < n; i++)
    {
        int ptype = 0;   // declare at the top of the loop body
        char tag[32];
        if (!G->GetTag(ids[i], tag, sizeof tag)) continue;

        if (strcmp(tag, "Coin") == 0) Coin_Collect(ids[i]);
        else if (strcmp(tag, "Goal") == 0) { if (!S.complete) Goal_Reached(); }
        else if (Pickup_TypeFromTag(tag, ptype)) { PowerUp_Remove(ids[i]); Collect(b, ptype); } 
        else if (strcmp(tag, "Projectile") == 0) { if (Hurt(b)) return; }
      
        else if (strcmp(tag, "Enemy") == 0)
        {
            float emn[3], emx[3];
            if (!G->GetBounds(ids[i], emn, emx)) continue;

            if (S.starTime > 0.0f) { Enemy_Kill(ids[i]); continue; }                 // star: kills on contact
            const bool stomp = preVy < 0.0f && feetBefore >= emx[1] - 0.25f;         // wider than before, so fast falls still count
            if (stomp) { Enemy_Kill(ids[i]); g_vel[1] = G->KeyDown(VK_SPACE) ? Cfg::StompBounceHeld : Cfg::StompBounce; }
            else if (Hurt(b)) return;
        }
    }

    
    
    if (S.portalCool > 0.0f) S.portalCool -= dt;
    else
    {
        Body e = b;
        for (int i = 0; i < 3; i++) e.half[i] += 0.1f;       // a slightly bigger box, so standing on a pipe still counts
        int pid[16];
        const int pn = QueryBody(e, pid, 16);
        for (int i = 0; i < pn; i++)
        {
            char t[32];
            if (G->GetTag(pid[i], t, sizeof t) && strcmp(t, "PortalEntry") == 0 && TryPortal(b, pid[i])) break;
        }
    }
    

    // ---- Falling off the world ----
    if (b.pos[1] < Cfg::FallLimit) { Game_Restart(true); return; }

    StoreBody(b);
    // ---- Sprite animation (does nothing if the Player is a plain cube) ----
    {
        const float yaw2 = S.camYaw * 0.0174532925f;
        Sprite_FaceScreen(S.player, g_vel[0] * cosf(yaw2) - g_vel[2] * sinf(yaw2));   // left / right as seen on screen

        const float planar = sqrtf(g_vel[0] * g_vel[0] + g_vel[2] * g_vel[2]);
        const bool shift = G->KeyDown(VK_SHIFT);
        const bool crouch = G->KeyDown(VK_DOWN) || G->KeyDown('S');
        if (S.hurt > 0.0f)        Sprite_Play(S.player, "Hurt", "Idle");
        else if (!g_grounded)     Sprite_Play(S.player, g_vel[1] > 0.0f ? "Jump" : "Fall", "Jump");
        else if (planar > 0.4f) { if (shift) Sprite_Play(S.player, "Run", "Walk"); else Sprite_Play(S.player, "Walk", "Idle"); }
        else if (crouch)          Sprite_Play(S.player, "Crouch", "Idle");
        else                      Sprite_Play(S.player, "Idle", nullptr);   

        G->SpriteSetSpeed(S.player, (g_grounded && planar > 0.4f && G->KeyDown(VK_SHIFT)) ? 1.6f : 1.0f);   // running looks faster
        if (S.hurt > 0.0f) S.hurt -= dt;
    }

    if (S.starTime > 0.0f)
    {
        S.starTime -= dt;
        const bool on = S.starTime > 0.0f && (((int)(S.starTime * 16.0f)) & 1);
        const float yellow[3] = { 1.0f, 0.9f, 0.2f }, white[3] = { 1, 1, 1 };
        G->SetColor(S.player, on ? yellow : white);
    }
    // ---- Blink while invulnerable ----
    if (S.invuln > 0.0f)
    {
        S.invuln -= dt;
        G->SetVisible(S.player, (S.invuln > 0.0f && (((int)(S.invuln * 12.0f)) & 1)) ? 0 : 1);
    }
}