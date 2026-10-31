#include "Game.h"

//Player.cpp

static float g_vel[3];
static float g_coyote, g_jumpBuf;
static bool  g_grounded;
static float g_baseScale[3] = { 1, 1, 1 };
float g_faceDir = 1.0f;

static float Approach(float v, float target, float maxStep)
{
    float d = target - v;
    if (d > maxStep) d = maxStep;
    if (d < -maxStep) d = -maxStep;
    return v + d;
}

void Player_Init() { G->GetScale(S.player, g_baseScale); }

static const char* kPowerAnim[3] = { "Content/Animations/MarioSmall.ghostanim",
                                     "Content/Animations/MarioBig.ghostanim",
                                     "Content/Animations/MarioFire.ghostanim" };

static const char* kLuigiAnim[3] = { "Content/Animations/LuigiSmall.ghostanim",
                                     "Content/Animations/LuigiBig.ghostanim",
                                     "Content/Animations/MarioFire.ghostanim" };
static const char* PowerAnim(int level) { return (S.character == 1 ? kLuigiAnim : kPowerAnim)[level]; }


void Player_Reset()
{
    memset(g_vel, 0, sizeof g_vel);
    g_coyote = g_jumpBuf = 0.0f;
    g_grounded = false;
    S.error = nullptr;
    S.heldShell = 0;
    S.shellSliding = false;
    G->SpriteSetAnim(S.player, PowerAnim(0));
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
    G->SetScale(S.player, s);
    G->SpriteSetAnim(S.player, PowerAnim(level));
    b.pos[1] += b.half[1] * (ky - 1.0f) + (ky > 1.0f ? 0.01f : 0.0f);
    b.half[0] *= kx; b.half[2] *= kx; b.half[1] *= ky;
    b.off[0] *= kx;  b.off[2] *= kx;  b.off[1] *= ky;
}

static void Collect(Body& b, int type)
{
    if (type != PU_ONEUP) Score_Add(1000);
    switch (type)
    {
    case PU_MUSHROOM: if (PowerLevel() == 0) SetPower(b, 1); else Coins_Add(1); break;
    case PU_FLOWER:   if (PowerLevel() < 2) SetPower(b, PowerLevel() + 1); else Coins_Add(1); break;
    case PU_STAR:     S.starTime = Cfg::StarTime; S.starChain = 0; break;
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

// ---------------------------------------------------------------------------
// Shells: grab (hold Shift near one), throw (release Shift), kick (walk into it)
// ---------------------------------------------------------------------------

// World-space direction Mario faces = screen-right axis * facing sign (works for any camera yaw)
static void FaceVec(float& fx, float& fz)
{
    const float yaw = S.camYaw * 0.0174532925f;
    fx = cosf(yaw) * g_faceDir;
    fz = -sinf(yaw) * g_faceDir;
}

// Close enough to grab / bump? Uses a padded box so "near" counts, not only exact overlap
static bool ShellNear(int id, const Body& b)
{
    const float padXZ = 0.35f, padY = 0.1f;
    float mn[3], mx[3];
    if (!G->GetBounds(id, mn, mx))
    {
        float p[3];
        if (!G->GetPosition(id, p)) return false;
        for (int i = 0; i < 3; i++) { mn[i] = p[i] - 0.4f; mx[i] = p[i] + 0.4f; }
    }
    for (int i = 0; i < 3; i++)
    {
        const float pad = (i == 1) ? padY : padXZ;
        if (b.pos[i] - b.half[i] - pad > mx[i]) return false;
        if (b.pos[i] + b.half[i] + pad < mn[i]) return false;
    }
    return true;
}

static void Shells_Update(const Body& b)
{
    float fx, fz;
    FaceVec(fx, fz);
    const bool shift = G->KeyDown(VK_SHIFT);

    // ---- Holding a shell ----
    if (S.heldShell)
    {
        char tag[32];
        if (!G->GetTag(S.heldShell, tag, sizeof tag) || strcmp(tag, "HeldShell") != 0)
        {
            S.heldShell = 0;            // destroyed or changed by something else
            return;
        }
        if (!shift)                     // Shift released: throw
        {
            Shell_Kick(S.heldShell, fx, fz);
            S.shellSliding = true;
            S.heldShell = 0;
            return;
        }
        float mp[3];
        if (G->GetPosition(S.player, mp))
        {
            mp[0] += fx * 0.8f;
            mp[2] += fz * 0.8f;
            G->SetPosition(S.heldShell, mp);
        }
        Sprite_Play(S.heldShell, "Shell", "Shell");
        return;
    }

    // ---- Not holding: grab (Shift) or kick (no Shift) ----
    int ids[32];
    const int n = G->FindAllByTag("Shell", ids, 32);
    for (int i = 0; i < n; i++)
    {
        if (!Shell_Ready(ids[i]) || !ShellNear(ids[i], b)) continue;

        if (shift)
        {
            Shell_Hold(ids[i]);
            S.heldShell = ids[i];
            Score_Add(500);
            return;
        }

        // Kick away from Mario along the dominant horizontal axis
        float sp[3];
        if (!G->GetPosition(ids[i], sp)) continue;
        const float dx = sp[0] - b.pos[0], dz = sp[2] - b.pos[2];
        float kx = fx, kz = fz;
        if (fabsf(dx) > 0.05f || fabsf(dz) > 0.05f)
        {
            if (fabsf(dx) >= fabsf(dz)) { kx = dx >= 0.0f ? 1.0f : -1.0f; kz = 0.0f; }
            else { kz = dz >= 0.0f ? 1.0f : -1.0f; kx = 0.0f; }
        }
        Shell_Kick(ids[i], kx, kz);
        return;
    }
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
    float dx = sy * iz + cy * ix;
    float dz = cy * iz - sy * ix;
    float len = sqrtf(dx * dx + dz * dz);
    if (len > 1.0f) { dx /= len; dz /= len; }

    const float speed = G->KeyDown(VK_SHIFT) ? Cfg::Run : Cfg::Walk;
    const float accel = (g_grounded ? Cfg::GroundAccel : Cfg::AirAccel) * dt;
    g_vel[0] = Approach(g_vel[0], dx * speed, accel);
    g_vel[2] = Approach(g_vel[2], dz * speed, accel);

    // ---- Jump ----
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
    if (g_grounded) S.stompChain = 0;
    if (r.hitCeiling) Blocks_Bump(r.ceilingId);

    // ---- What are we touching? ----
    int ids[32];
    int n = QueryBody(b, ids, 32);
    for (int i = 0; i < n; i++)
    {
        int ptype = 0;
        char tag[32];
        if (!G->GetTag(ids[i], tag, sizeof tag)) continue;

        if (strcmp(tag, "Coin") == 0) Coin_Collect(ids[i]);
        else if (strcmp(tag, "Goal") == 0 && !S.complete && !S.onPole)
        {
            float mn[3], mx[3];
            if (G->GetBounds(ids[i], mn, mx))
                Goal_Reached(ids[i], b.pos[1] - b.half[1], mx[1], mn[1]);
        }
        else if (Pickup_TypeFromTag(tag, ptype)) { PowerUp_Remove(ids[i]); Collect(b, ptype); }
        else if (strcmp(tag, "Projectile") == 0) { if (Hurt(b)) return; }
        else if (strcmp(tag, "Enemy") == 0)
        {
            float emn[3], emx[3];
            if (!G->GetBounds(ids[i], emn, emx)) continue;

            if (S.starTime > 0.0f) { Enemy_Kill(ids[i]); continue; }
            const bool stomp = preVy < 0.0f && feetBefore >= emx[1] - 0.25f;
            if (stomp) { Enemy_Kill(ids[i]); g_vel[1] = G->KeyDown(VK_SPACE) ? Cfg::StompBounceHeld : Cfg::StompBounce; }
            else if (Hurt(b)) return;
        }
    }

    if (S.portalCool > 0.0f) S.portalCool -= dt;
    else
    {
        Body e = b;
        for (int i = 0; i < 3; i++) e.half[i] += 0.1f;
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

    if (S.attackCool > 0.0f) S.attackCool -= dt;

    if (G->KeyPressed(VK_LBUTTON) && S.attackCool <= 0.0f)
    {
        S.attackCool = 0.4f;
        if (PowerLevel() == 0)      Sprite_Play(S.player, "SpinAttack", "Idle");
        else if (PowerLevel() == 1) Sprite_Play(S.player, "Punch", "Idle");
        else if (PowerLevel() == 2) Fireball_Spawn();
    }

    if (S.onPole)
    {
        float mn[3], mx[3];
        if (G->GetBounds(S.poleId, mn, mx)) { b.pos[0] = (mn[0] + mx[0]) * 0.5f; b.pos[2] = (mn[2] + mx[2]) * 0.5f; }
        memset(g_vel, 0, sizeof g_vel);

        const float floorY = S.poleBot + b.half[1] + 0.02f;
        if (b.pos[1] > floorY)
        {
            b.pos[1] -= Cfg::PoleSlide * dt;
            if (b.pos[1] < floorY) b.pos[1] = floorY;
            Sprite_Play(S.player, "Climb", "Idle");
        }
        else
        {
            Sprite_Play(S.player, "Idle", nullptr);
            S.poleSlide += dt;
            if (S.poleSlide >= 0.6f)              // short beat at the bottom, then the level completes
            {
                S.onPole = false;
                S.complete = true;
                S.timeBonus = (int)S.timeLeft * 50;
                Score_Add(S.timeBonus);
            }
        }

        const int flag = G->FindByTag("Flag");    // the flag drops a bit faster than Mario
        if (flag)
        {
            float fp[3], fs[3];
            G->GetPosition(flag, fp); G->GetScale(flag, fs);
            const float stop = S.poleBot + fs[1] * 0.5f + 0.05f;
            if (fp[1] > stop) { fp[1] -= Cfg::PoleSlide * 1.5f * dt; if (fp[1] < stop) fp[1] = stop; G->SetPosition(flag, fp); }
        }
        StoreBody(b);
        return;
    }
    StoreBody(b);

    // Shells run AFTER StoreBody so the held shell uses Mario's current position (no 1-frame lag)
    Shells_Update(b);

    // ---- Sprite animation ----
    {
        const float yaw2 = S.camYaw * 0.0174532925f;
        {
            float screenVx = g_vel[0] * cosf(yaw2) - g_vel[2] * sinf(yaw2);
            Sprite_FaceScreen(S.player, screenVx);
            if (screenVx > 0.2f)  g_faceDir = 1.0f;
            if (screenVx < -0.2f) g_faceDir = -1.0f;
        }

        const float planar = sqrtf(g_vel[0] * g_vel[0] + g_vel[2] * g_vel[2]);
        const bool shift = G->KeyDown(VK_SHIFT);
        const bool crouch = G->KeyDown(VK_DOWN) || G->KeyDown('S');
        if (S.hurt > 0.0f)        Sprite_Play(S.player, "Hurt", "Idle");
        else if (!g_grounded)     Sprite_Play(S.player, g_vel[1] > 0.0f ? "Jump" : "Fall", "Jump");
        else if (planar > 0.4f) { if (shift) Sprite_Play(S.player, "Run", "Walk"); else Sprite_Play(S.player, "Walk", "Idle"); }
        else if (crouch)          Sprite_Play(S.player, "Crouch", "Idle");
        else                      Sprite_Play(S.player, "Idle", nullptr);

        G->SpriteSetSpeed(S.player, (g_grounded && planar > 0.4f && G->KeyDown(VK_SHIFT)) ? 1.6f : 1.0f);
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