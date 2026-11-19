#include "Game.h"

//Player.cpp

static float g_vel[3];
static float g_coyote, g_jumpBuf;
static bool  g_grounded;
static float g_baseScale[3] = { 1, 1, 1 };
float g_faceDir = 1.0f;
GhostPlayerParams PP = GhostPlayerParams_Make();
bool  g_playerModel = false;       // the Player is an imported 3D model (not a Sprite Actor)
static float g_attackAnim = 0.0f;  // > 0 while the 3D player's Attack clip plays
static bool  g_attackHit = false;  // the swing has already connected
static float g_attackLen = 0.8f;   // seconds the current swing lasts (the real clip length)
float g_moveYawDeg = 0.0f;
bool  g_haveMoveYaw = false;


static float Approach(float v, float target, float maxStep)
{
    float d = target - v;
    if (d > maxStep) d = maxStep;
    if (d < -maxStep) d = -maxStep;
    return v + d;
}

void Model_Face(int id, float wx, float wz, float dt, float yawOffset, float turnSpeed)
{
    if (wx * wx + wz * wz < 0.04f) return;
    float r[3] = { 0, 0, 0 };
    if (!G->GetRotation(id, r)) return;
    const float target = atan2f(-wx, -wz) * 57.2957795f + yawOffset;   // an imported model's front is -Z
    float d = fmodf(target - r[1], 360.0f);
    if (d > 180.0f) d -= 360.0f;
    if (d < -180.0f) d += 360.0f;
    const float step = turnSpeed * dt;
    r[1] += (fabsf(d) <= step) ? d : (d > 0.0f ? step : -step);
    G->SetRotation(id, r);
}

void Player_Init()
{
    G->GetScale(S.player, g_baseScale);
    g_playerModel = G->IsModel && G->IsModel(S.player) != 0;
    GhostObjectProps pr = {};
    PP = G->GetObjectProps(S.player, &pr) ? pr.player : GhostPlayerParams_Make();   // each scene's Player carries its own settings
}

static const char* kPowerAnim[3] = { "Content/Animations/MarioSmall.ghostanim",
                                     "Content/Animations/MarioBig.ghostanim",
                                     "Content/Animations/MarioFire.ghostanim" };

static const char* kLuigiAnim[3] = { "Content/Animations/LuigiSmall.ghostanim",
                                     "Content/Animations/LuigiBig.ghostanim",
                                     "Content/Animations/MarioFire.ghostanim" };
static const char* PowerAnim(int level) { return (S.character == 1 ? kLuigiAnim : kPowerAnim)[level]; }

static const char* kFlagAnim[2] = { "Content/Animations/FlagSlideMario.ghostanim",
                                    "Content/Animations/FlagSlideLuigi.ghostanim" };
static bool g_poleAnim = false;       // the flag sheet is currently assigned
static bool g_shrinking = false;   // Shrink anim is playing; SetPower(0) happens when it ends
// ---------------------------------------------------------------------------
// Skid: running one way, pressing the other. Velocity keeps its old direction and brakes.
// The skid art lives on its own sheet, so the sheet is swapped in and back out.
// ---------------------------------------------------------------------------
static const char* kMarioSkid[3] = { "Content/Animations/SmallMarioSkid.ghostanim",
                                     "Content/Animations/BigMarioSkid.ghostanim",
                                     "Content/Animations/FireMarioSkid.ghostanim" };
static const char* kLuigiSkid[3] = { "Content/Animations/SmallLuigiSkid.ghostanim",
                                     "Content/Animations/BigLuigiSkid.ghostanim",
                                     "Content/Animations/FireMarioSkid.ghostanim" };   // no FireLuigiSkid: same as your fire sheet
static const char* SkidAnimFor(int level) { return (S.character == 1 ? kLuigiSkid : kMarioSkid)[level]; }

static bool  g_skidding = false;
static bool  g_skidAnim = false;                // the skid sheet is currently assigned
static float g_skidDir[2] = { 0, 0 };           // unit travel direction (world X, Z) when the skid began

static void Skid_End()
{
    if (g_skidAnim) { G->SpriteSetAnim(S.player, PowerAnim(PowerLevel())); g_skidAnim = false; }
    g_skidding = false;
}

// Call before the normal ground acceleration. Returns true if it handled velocity this frame.
static bool Skid_Update(float dx, float dz, float dt)
{
    if (S.transformTimer > 0.0f) return false;               // Transform_Begin already ended any skid
    if (S.onPole || S.bossCut) { Skid_End(); return false; }

    const float speed = sqrtf(g_vel[0] * g_vel[0] + g_vel[2] * g_vel[2]);
    const float il = sqrtf(dx * dx + dz * dz);

    bool opposed = false;                                    // is the input pushing against the motion?
    if (il > 0.3f && speed > 0.001f)
    {
        const float cosA = (g_vel[0] * dx + g_vel[2] * dz) / (speed * il);
        opposed = cosA < PP.skidOpposeCos;
    }

    if (!g_skidding)
    {
        if (!(g_grounded && opposed && speed >= PP.skidMinSpeed)) return false;
        g_skidding = true;
        g_skidDir[0] = g_vel[0] / speed;
        g_skidDir[1] = g_vel[2] / speed;
        g_skidAnim = G->SpriteSetAnim(S.player, SkidAnimFor(PowerLevel())) != 0;   // no file: falls back to the normal sheet
        Sfx("skid");                                         // safe if the bank has no "skid" sound
    }
    else if (!g_grounded || !opposed || speed < PP.skidExitSpeed)
    {
        Skid_End();                                          // normal movement takes over and accelerates the new way
        return false;
    }

    const float ns = fmaxf(0.0f, speed - PP.skidFriction * dt);   // brake, don't zero
    g_vel[0] = g_skidDir[0] * ns;
    g_vel[2] = g_skidDir[1] * ns;
    return true;
}

void Player_Reset()
{
    memset(g_vel, 0, sizeof g_vel);
    g_coyote = g_jumpBuf = 0.0f;
    g_grounded = false;
    S.error = nullptr;
    S.heldShell = 0;
    S.shellSliding = false;
    G->SpriteSetAnim(S.player, PowerAnim(0));
    g_haveMoveYaw = false;
    g_poleAnim = false;
    S.transformTimer = 0.0f;
    g_shrinking = false; 
    g_skidding = false; g_skidAnim = false;
    g_attackAnim = 0.0f; g_attackHit = false;
}

static void PowerScale(int level, float& sx, float& sy)
{
    if (g_playerModel) { sx = sy = (level == 0) ? 1.0f : (level == 1 ? PP.modelBigScale : PP.modelFireScale); return; }
    sx = (level == 0) ? 1.0f : (level == 1 ? PP.bigScaleX : PP.fireScaleX);
    sy = (level == 0) ? 1.0f : (level == 1 ? PP.bigScaleY : PP.fireScaleY);
}

static void SetPower(Body& b, int level)
{
    const int old = PowerLevel();
    if (old == level) return;
    Skid_End();
    float osx, osy, nsx, nsy;
    PowerScale(old, osx, osy);
    PowerScale(level, nsx, nsy);
    const float kx = nsx / osx;
    const float ky = nsy / osy;
    S.big = level >= 1; S.fire = level == 2;
    const float s[3] = { g_baseScale[0] * nsx, g_baseScale[1] * nsy, g_baseScale[2] * nsx };
    G->SetScale(S.player, s);
    G->SpriteSetAnim(S.player, PowerAnim(level));
    b.pos[1] += b.half[1] * (ky - 1.0f) + (ky > 1.0f ? PP.growPop : 0.0f);
    b.half[0] *= kx; b.half[2] *= kx; b.half[1] *= ky;
    b.off[0] *= kx;  b.off[2] *= kx;  b.off[1] *= ky;
}

static const char* kTransformAnim[2] = { "Content/Animations/MarioTransform.ghostanim",
                                         "Content/Animations/LuigiTransform.ghostanim" };

static const char* kShrinkAnim[2] = { "Content/Animations/MarioShrink.ghostanim",
                                      "Content/Animations/LuigiShrink.ghostanim" };

static bool Transform_Begin(bool shrink)
{
    Skid_End();
    const int c = (S.character == 1) ? 1 : 0;
    if (!G->SpriteSetAnim(S.player, shrink ? kShrinkAnim[c] : kTransformAnim[c])) return false;   // no file: skip
    S.transformTimer = shrink ? PP.shrinkTime : PP.transformTime;
    memset(g_vel, 0, sizeof g_vel);
    if (shrink) Sprite_Play(S.player, "Shrink", "Transform");
    else        Sprite_Play(S.player, "Transform", "Idle");
    return true;
}

static void Collect(Body& b, int type)
{
    const int before = PowerLevel();
    G->PadRumble(0.3f, 0.3f, 0.15f);
    if (type != PU_ONEUP) { Sfx("powerup_collect"); Score_Add(1000); }     // the 1-up sound plays inside Life_Add
    switch (type)
    {
    case PU_MUSHROOM: if (PowerLevel() == 0) SetPower(b, 1); else Coins_Add(1); break;
    case PU_FLOWER:   if (PowerLevel() < 2) SetPower(b, PowerLevel() + 1); else Coins_Add(1); break;
    case PU_STAR:     S.starTime = PP.starTime; S.starChain = 0; break;
    case PU_ONEUP:    Life_Add(); break;
    }
    if (before == 0 && PowerLevel() == 1) Transform_Begin(false);
}

// true = Mario died (caller returns)
static bool Hurt(Body& b)
{
    if (S.starTime > 0.0f || S.invuln > 0.0f) return false;
    G->PadRumble(S.big ? 0.6f : 1.0f, 0.8f, S.big ? 0.25f : 0.5f);
    if (S.big)
    {
        S.invuln = PP.invulnerable;
        if (Transform_Begin(true))            // Shrink anim exists: shrink for real when it ends
        {
            g_shrinking = true;
            Sfx("powerup_collect");           // same sound as the transform
            return false;
        }
        Sfx("hurt"); SetPower(b, 0); S.hurt = PP.hurtTime; g_vel[1] = PP.hurtHop; return false;
    }
    Player_Die();
    return true;
}
// ---------------------------------------------------------------------------
// Death: a spawned sprite plays the "Death" clip. Below the screen (pit fall) it pops up from the bottom edge,
// otherwise it hops where Mario stood. The real Mario stays hidden (so the camera holds still).
// ---------------------------------------------------------------------------
static void DeathStart(float feet, const float* pos, float& outFeet, float& outVy)
{
    outFeet = feet;
    outVy = PP.deathHop;
    if (!S.camId) return;

    float cp[3], yp[2], fov = 45.0f;
    if (!G->GetCamera(S.camId, cp, yp, &fov)) return;

    const float d2r = 0.0174532925f, yaw = yp[0] * d2r;
    const float dh = (pos[0] - cp[0]) * sinf(yaw) + (pos[2] - cp[2]) * cosf(yaw);   // how far ahead of the camera
    if (dh < 1.0f) return;

    auto edge = [&](float deg) { return cp[1] + dh * tanf(fminf(fmaxf(deg, -85.0f), 85.0f) * d2r); };
    const float bottom = edge(yp[1] - fov * 0.5f), top = edge(yp[1] + fov * 0.5f);
    if (feet >= bottom) return;                                  // still on screen: hop in place

    outFeet = bottom - 0.6f;                                     // just under the bottom edge
    outVy = sqrtf(2.0f * PP.gravity * (PP.deathPop * (top - bottom) + 0.6f));
}

void Player_Die()
{
    if (S.deathTimer > 0.0f || S.complete) return;

    float pos[3] = { 0, 0, 0 };
    G->GetPosition(S.player, pos);
    Body b;
    const float feet = LoadBody(S.player, b) ? b.pos[1] - b.half[1] : pos[1] - g_baseScale[1] * 0.5f;

    float startFeet, vy;
    DeathStart(feet, pos, startFeet, vy);

    S.deathTimer = PP.deathTime;
    S.deathVy = vy;
    memset(g_vel, 0, sizeof g_vel);
    Sfx("death");

    const float at[3] = { pos[0], startFeet + g_baseScale[1] * 0.5f, pos[2] };
    S.deathActor = g_playerModel ? 0 : G->Spawn(GHOST_API_MESH_QUAD, at);   // a model plays its own Death clip instead
    if (S.deathActor)
    {
        G->SetScale(S.deathActor, g_baseScale);                  // always the small size
        G->SetTag(S.deathActor, "Broken");                       // not solid, nothing reacts to it
        G->SpriteSetAnim(S.deathActor, PowerAnim(0));            // MarioSmall / LuigiSmall
        G->SpriteSetFlip(S.deathActor, g_faceDir < 0.0f ? 1 : 0);
        Sprite_Play(S.deathActor, "Death", "Hurt");
        G->SetVisible(S.player, 0);
    }
    else Sprite_Play(S.player, "Death", "Hurt");                 // object limit reached: play it on Mario himself
}

static void Death_Update(float dt)
{
    S.deathTimer -= dt;
    float p[3];
    if (S.deathActor && G->GetPosition(S.deathActor, p))
    {
        S.deathVy -= PP.gravity * dt;
        p[1] += S.deathVy * dt;
        G->SetPosition(S.deathActor, p);
    }
    if (S.deathTimer <= 0.0f) Game_Restart(true);                // removes the actor, takes the life, restarts or Game Over
}
static bool TryPortal(Body& b, int entry)
{
    GhostObjectProps pr = {};
    if (!G->GetObjectProps(entry, &pr)) return false;
    const bool go = pr.portalTrigger == 0 ||
        (pr.portalTrigger == 1 && (G->KeyPressed(VK_DOWN) || Bind_Pressed(ACT_BACK))) ||
        (pr.portalTrigger == 2 && G->KeyPressed('F'));
    if (!go) return false;
    if (strstr(pr.link, ".ghostscene")) { const bool ok = G->LoadScene(pr.link) != 0; if (ok) Sfx("pipe"); return ok; }

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
        Sfx("pipe");
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
    const bool shift = Bind_Down(ACT_SPRINT);

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
            if (g_playerModel) mp[1] = b.pos[1];                 // a model's origin is at its feet: hold the shell at body height
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

// The swing of the 3D player's Attack clip: defeats enemies in a box in front of her
static void Melee_Hit(const Body& b)
{
    float r[3] = { 0, 0, 0 };
    G->GetRotation(S.player, r);
    const float yaw = (r[1] - PP.modelYawOffset) * 0.0174532925f;

    const float fx = -sinf(yaw), fz = -cosf(yaw);                 // the way she faces (front is -Z at yaw 0)

    const float reach = PP.modelAttackReach;
    const float cx = b.pos[0] + fx * (b.half[0] + reach * 0.5f);
    const float cz = b.pos[2] + fz * (b.half[2] + reach * 0.5f);
    const float hw = reach * 0.5f + 0.3f;
    const float mn[3] = { cx - hw, b.pos[1] - b.half[1], cz - hw };
    const float mx[3] = { cx + hw, b.pos[1] + b.half[1], cz + hw };

    int ids[16];
    const int n = G->QueryBox(mn, mx, ids, 16);
    int hits = 0;
    for (int i = 0; i < n; i++)
    {
        char tag[32];
        if (!G->GetTag(ids[i], tag, sizeof tag) || strcmp(tag, "Enemy") != 0) continue;
        Enemy_Kill(ids[i]);
        hits++;
    }
    if (hits) G->PadRumble(0.0f, 0.5f, 0.1f);
}

void Player_Update(float dt)
{
    if (S.deathTimer > 0.0f) { Death_Update(dt); return; }
    Body b;
    if (!LoadBody(S.player, b)) { S.error = "The Player needs a collider (Details > Gameplay > Collider)"; return; }
    S.error = nullptr;

    // ---- Input: camera-relative movement ----
    float ix = (Bind_Down(ACT_RIGHT) ? 1.0f : 0.0f) - (Bind_Down(ACT_LEFT) ? 1.0f : 0.0f);
    float iz = PP.sideScroller ? 0.0f : ((Bind_Down(ACT_FORWARD) ? 1.0f : 0.0f) - (Bind_Down(ACT_BACK) ? 1.0f : 0.0f));
    if (G->PadConnected())                          // the stick overrides the keys, so a light push walks slowly
    {
        const float sx = G->PadAxis(GHOST_PAD_AXIS_LX), sy2 = G->PadAxis(GHOST_PAD_AXIS_LY);
        if (sx != 0.0f || sy2 != 0.0f) { ix = sx; iz = PP.sideScroller ? 0.0f : sy2; }
    }
    float yaw = S.camYaw * 0.0174532925f;
    float sy = sinf(yaw), cy = cosf(yaw);
    float dx = sy * iz + cy * ix;
    float dz = cy * iz - sy * ix;
    float len = sqrtf(dx * dx + dz * dz);
    if (len > 1.0f) { dx /= len; dz /= len; }
    if (len > 0.3f) { g_moveYawDeg = atan2f(dx, dz) * 57.2957795f; g_haveMoveYaw = true; }
    const float speed = Bind_Down(ACT_SPRINT) ? PP.run : PP.walk;
    const float accel = (g_grounded ? PP.groundAccel : PP.airAccel) * dt;
    if (!Skid_Update(dx, dz, dt))
    {
        g_vel[0] = Approach(g_vel[0], dx * speed, accel);
        g_vel[2] = Approach(g_vel[2], dz * speed, accel);
    }

    // ---- Jump ----
    g_jumpBuf = Bind_Pressed(ACT_JUMP) ? PP.jumpBuffer : g_jumpBuf - dt;
    g_coyote = g_grounded ? PP.coyoteTime : g_coyote - dt;
    if (g_jumpBuf > 0.0f && g_coyote > 0.0f)
    {
        Skid_End();
        const float planarNow = sqrtf(g_vel[0] * g_vel[0] + g_vel[2] * g_vel[2]);
        const bool sprintJump = Bind_Down(ACT_SPRINT) && planarNow > PP.walk + 0.5f;   // Shift AND actually moving faster than a walk
        g_vel[1] = sprintJump ? PP.runJump : PP.jump;
        g_jumpBuf = 0.0f; g_coyote = 0.0f;
        Sfx(S.big ? "big_jump" : "jump");
    }
    if (Bind_Released(ACT_JUMP) && g_vel[1] > 0.0f) g_vel[1] *= PP.jumpCut;

    g_vel[1] -= PP.gravity * dt;
    if (g_vel[1] < -PP.maxFall) g_vel[1] = -PP.maxFall;

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
        else if (strcmp(tag, "Boss") == 0) { if (Hurt(b)) return; }          // the boss hurts on touch, stomping does nothing
        else if (strcmp(tag, "BossAxe") == 0) Boss_AxeHit(ids[i]);
        else if (strcmp(tag, "Enemy") == 0)
        {
            float emn[3], emx[3];
            if (!G->GetBounds(ids[i], emn, emx)) continue;

            if (S.starTime > 0.0f) { Enemy_Kill(ids[i]); continue; }
            const bool stomp = preVy < 0.0f && feetBefore >= emx[1] - PP.stompTolerance;
            if (stomp) { Enemy_Kill(ids[i],true); g_vel[1] = Bind_Down(ACT_JUMP) ? PP.stompBounceHeld : PP.stompBounce; G->PadRumble(0.0f, 0.4f, 0.08f); }
            else if (Hurt(b)) return;
        }
        else if (strcmp(tag, "SlidingShell") == 0)
        {
            float smn[3], smx[3];
            if (!G->GetBounds(ids[i], smn, smx)) continue;
            const bool stomp = preVy < 0.0f && feetBefore >= smx[1] - PP.stompTolerance;
            if (stomp)                                    // land on a moving shell: it stops
            {
                Shell_Stop(ids[i]);
                Score_Add(400);
                Sfx("stomp");
                g_vel[1] = Bind_Down(ACT_JUMP) ? PP.stompBounceHeld : PP.stompBounce;
            }
            else if (Shell_Dangerous(ids[i]) && Hurt(b)) return;   // side contact hurts (big shrinks, small dies)
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
    if (b.pos[1] < PP.fallLimit) { Player_Die(); return; }

    if (S.attackCool > 0.0f) S.attackCool -= dt;

    if (g_attackAnim > 0.0f)                                      // the 3D swing is running
    {
        g_attackAnim -= dt;
        if (!g_attackHit && g_attackAnim <= g_attackLen * (1.0f - PP.modelAttackHitAt))
        {
            g_attackHit = true;
            if (PP.modelMeleeDamage && PowerLevel() < 2) Melee_Hit(b);
        }
        if (g_attackAnim < 0.0f) g_attackAnim = 0.0f;
    }

    if (Bind_Pressed(ACT_ATTACK) && S.attackCool <= 0.0f)
    {
        S.attackCool = PP.attackCooldown;
        if (g_playerModel)
        {
            g_attackLen = ClipSeconds(S.player, "Attack", PP.modelAttackTime);   // the real length of her Attack clip
            g_attackAnim = g_attackLen;
            g_attackHit = false;
            G->SpriteSetClip(S.player, "");                       // so the Attack clip always restarts from its first frame
            S.attackCool = fmaxf(PP.attackCooldown, g_attackLen);
        }
        if (PowerLevel() == 0) { if (!g_playerModel) Sprite_Play(S.player, "SpinAttack", "Idle"); }
        else if (PowerLevel() == 1) { if (!g_playerModel) Sprite_Play(S.player, "Punch", "Idle"); }
        else if (PowerLevel() == 2)
        {
            const float up = (G->KeyDown(VK_UP) ? 1.0f : 0.0f) - (G->KeyDown(VK_DOWN) ? 1.0f : 0.0f);
            float ax = 0.0f, az = 0.0f;
            const float hl = sqrtf(dx * dx + dz * dz);
            if (hl > 0.1f) { ax = dx / hl; az = dz / hl; }
            else if (up == 0.0f) { ax = 0.0f; az = 1.0f; }   // world forward
            Fireball_Spawn(ax, up, az);
        }
    }

    if (S.onPole)
    {
        if (!g_poleAnim)                                          // swap to the Flag Slide sheet once
        {
            G->SpriteSetAnim(S.player, kFlagAnim[S.character == 1 ? 1 : 0]);
            g_poleAnim = true;
        }

        float mn[3], mx[3];
        if (G->GetBounds(S.poleId, mn, mx)) { b.pos[0] = (mn[0] + mx[0]) * 0.5f; b.pos[2] = (mn[2] + mx[2]) * 0.5f; }
        memset(g_vel, 0, sizeof g_vel);

        const float floorY = S.poleBot + b.half[1] + 0.02f;
        if (b.pos[1] > floorY)
        {
            b.pos[1] -= Cfg::PoleSlide * dt;
            if (b.pos[1] < floorY) b.pos[1] = floorY;
            Sprite_Play(S.player, "FlagSlide", "Climb");
        }
        else
        {
            Sprite_Play(S.player, "FlagIdle", "FlagSlide");       // hold at the bottom
            S.poleSlide += dt;
            if (S.poleSlide >= 0.6f)              // short beat at the bottom, then the level completes
            {
                S.onPole = false;
                S.complete = true;
                S.timeBonus = (int)S.timeLeft * 50;
                Score_Add(S.timeBonus);
                G->SpriteSetAnim(S.player, PowerAnim(PowerLevel()));   // back to the normal sheet (delete to keep the last pole frame)
                g_poleAnim = false;
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
    if (S.bossCut)                                    // frozen while the bridge falls
    {
        memset(g_vel, 0, sizeof g_vel);
        Sprite_Play(S.player, "Idle", nullptr);
        return;
    }

    if (S.transformTimer > 0.0f)                                  // frozen while Transform / Shrink plays
    {
        S.transformTimer -= dt;
        if (S.transformTimer <= 0.0f)
        {
            S.transformTimer = 0.0f;
            if (g_shrinking) { g_shrinking = false; SetPower(b, 0); }                // now go small
            else G->SpriteSetAnim(S.player, PowerAnim(PowerLevel()));
        }
        StoreBody(b);                                             // <-- the ground-sink fix
        return;
    }
    StoreBody(b);

    // Shells run AFTER StoreBody so the held shell uses Mario's current position (no 1-frame lag)
    Shells_Update(b);

    // ---- Sprite animation ----
    if (S.transformTimer <= 0.0f)        // was a bare { ... } block
    {
        const float yaw2 = S.camYaw * 0.0174532925f;
        {
            float screenVx = g_vel[0] * cosf(yaw2) - g_vel[2] * sinf(yaw2);
            Sprite_FaceScreen(S.player, screenVx);
            if (screenVx > 0.2f)  g_faceDir = 1.0f;
            if (screenVx < -0.2f) g_faceDir = -1.0f;
        }
        if (g_playerModel) Model_Face(S.player, g_vel[0], g_vel[2], dt, PP.modelYawOffset, PP.modelTurnSpeed);
        const float planar = sqrtf(g_vel[0] * g_vel[0] + g_vel[2] * g_vel[2]);
        const bool shift = Bind_Down(ACT_SPRINT);
        const bool crouch = G->KeyDown(VK_DOWN) || Bind_Down(ACT_BACK);
        if (g_skidding && (!g_grounded || S.hurt > 0.0f)) Skid_End();

        if (S.hurt > 0.0f)        Sprite_Play(S.player, "Hurt", "Idle");
        else if (g_attackAnim > 0.0f && Sprite_Play(S.player, "Attack", nullptr)) {}   // 3D: swing in place (and in the air)
        else if (!g_grounded)     Sprite_Play(S.player, g_vel[1] > 0.0f ? "Jump" : "Fall", "Jump");        
        else if (g_skidding)      Sprite_Play(S.player, "Skid", "Idle");
        else if (planar > 0.4f) { if (shift) Sprite_Play(S.player, "Run", "Walk"); else Sprite_Play(S.player, "Walk", "Idle"); }
        else if (crouch)          Sprite_Play(S.player, "Crouch", "Idle");
        else                      Sprite_Play(S.player, "Idle", nullptr);

        float animSpeed = 1.0f;
        if (g_playerModel)
        {
            if (PP.modelStrideSpeed > 0.0f && g_grounded && !g_skidding && planar > 0.4f)
                animSpeed = fminf(fmaxf(planar / PP.modelStrideSpeed, 0.6f), 2.0f);
        }
        else if (g_grounded && !g_skidding && planar > 0.4f && Bind_Down(ACT_SPRINT)) animSpeed = PP.runAnimSpeed;
        G->SpriteSetSpeed(S.player, animSpeed);
        if (S.hurt > 0.0f) S.hurt -= dt;
      //  else if (g_attackAnim > 0.0f && Sprite_Play(S.player, "Attack", nullptr)) {}   // 3D: swing in place (and in the air)
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