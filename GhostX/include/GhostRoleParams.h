// GhostRoleParams.h: per-role gameplay settings, shared by the engine, the editor and GhostGame.dll.
// One list per role. F = float, I = int (0/1 for checkboxes). Arguments: name, default, min, max.
// Defaults are the values the game code used to hard-code, so nothing changes until a value is edited.
// To add a parameter later: add a line at the END of the list, add the same field at the END of the
// C# struct (GhostEngine.cs), and bump the counts in the static_asserts and in GhostRoleParamCheck.
#pragma once

#define GHOST_PLAYER_PARAMS(F, I) \
    /* Movement */ \
    F(walk,              4.0f,    0.1f,    50.0f) \
    F(run,               7.0f,    0.1f,    80.0f) \
    F(runAnimSpeed,      1.6f,    0.1f,     5.0f) \
    F(groundAccel,      60.0f,    1.0f,  1000.0f) \
    F(airAccel,         30.0f,    1.0f,  1000.0f) \
    F(gravity,          25.0f,    0.0f,   300.0f) \
    F(maxFall,          30.0f,    1.0f,   300.0f) \
    F(fallLimit,       -50.0f, -10000.0f,  100.0f) \
    I(sideScroller,         0,        0,        1) \
    /* Jump */ \
    F(jump,             15.0f,    0.0f,    80.0f) \
    F(runJump,          20.0f,    0.0f,    80.0f) \
    F(jumpCut,           0.5f,    0.0f,     1.0f) \
    F(coyoteTime,       0.10f,    0.0f,     1.0f) \
    F(jumpBuffer,       0.12f,    0.0f,     1.0f) \
    /* Stomp and damage */ \
    F(stompBounce,       7.0f,    0.0f,    80.0f) \
    F(stompBounceHeld,  11.0f,    0.0f,    80.0f) \
    F(stompTolerance,   0.25f,    0.0f,     2.0f) \
    F(invulnerable,      1.5f,    0.0f,    30.0f) \
    F(hurtTime,         0.45f,    0.0f,     5.0f) \
    F(hurtHop,           6.0f,    0.0f,    80.0f) \
    I(smallBreaksBricks,    0,        0,        1) \
    /* Power-ups */ \
    F(bigScaleX,         1.0f,    0.1f,    10.0f) \
    F(bigScaleY,         2.0f,    0.1f,    10.0f) \
    F(fireScaleX,        1.0f,    0.1f,    10.0f) \
    F(fireScaleY,        2.0f,    0.1f,    10.0f) \
    F(modelBigScale,     1.5f,    0.1f,    10.0f) \
    F(modelFireScale,    1.5f,    0.1f,    10.0f) \
    F(growPop,           0.1f,    0.0f,     5.0f) \
    F(transformTime,     0.9f,    0.0f,    10.0f) \
    F(shrinkTime,        0.9f,    0.0f,    10.0f) \
    F(starTime,         10.0f,    0.0f,   120.0f) \
    /* Skid */ \
    F(skidMinSpeed,      4.5f,    0.0f,    50.0f) \
    F(skidExitSpeed,     0.5f,    0.0f,    50.0f) \
    F(skidFriction,     28.0f,    0.0f,   500.0f) \
    F(skidOpposeCos,    -0.6f,   -1.0f,     1.0f) \
    /* Attack and fireball */ \
    F(attackCooldown,    0.4f,    0.0f,    10.0f) \
    F(fireballSpeed,     8.0f,    0.1f,    80.0f) \
    F(fireballLife,      3.0f,    0.1f,    30.0f) \
    F(fireballBounce,    5.0f,    0.0f,    80.0f) \
    /* Death */ \
    F(deathTime,         2.5f,    0.1f,    30.0f) \
    F(deathHop,         12.0f,    0.0f,    80.0f) \
    F(deathPop,         0.65f,    0.0f,     3.0f) \
    /* 3D model */ \
    F(modelYawOffset,    0.0f, -360.0f,   360.0f) \
    F(modelTurnSpeed,  720.0f,    1.0f, 10000.0f) \
    F(modelStrideSpeed,  0.0f,    0.0f,    50.0f) \
    F(modelAttackTime,   0.9f,   0.05f,    10.0f) \
    F(modelAttackHitAt,  0.9f,    0.0f,     1.0f) \
    F(modelAttackReach,  1.4f,    0.0f,    20.0f) \
    I(modelMeleeDamage,     1,        0,        1)

#define GHOST_ENEMY_PARAMS(F, I) \
    /* Physics and patrol */ \
    F(gravity,          25.0f,    0.0f,   300.0f) \
    F(maxFall,          30.0f,    1.0f,   300.0f) \
    F(fallLimit,       -50.0f, -10000.0f,  100.0f) \
    F(patrolDefault,     3.0f,    0.0f,   100.0f) \
    F(ledgeLead,        0.15f,    0.0f,     2.0f) \
    F(ledgeDepth,        0.4f,   0.05f,     5.0f) \
    /* Attack (3D enemies) */ \
    F(attackRange,       1.6f,    0.1f,    20.0f) \
    F(attackHeight,      1.5f,    0.1f,    20.0f) \
    F(attackTime,        0.9f,   0.05f,    10.0f) \
    F(attackCooldown,    1.5f,    0.0f,    30.0f) \
    /* Death */ \
    F(modelDieTime,      1.2f,    0.0f,    30.0f) \
    F(spriteDieTime,     0.5f,    0.0f,    30.0f) \
    F(flattenTime,       0.6f,    0.0f,    30.0f) \
    F(flattenScaleY,     0.2f,   0.01f,     1.0f) \
    F(flattenDrop,       0.7f,    0.0f,     1.0f) \
    F(deathBounce,      10.5f,    0.0f,    80.0f) \
    F(shellGrace,        0.5f,    0.0f,    10.0f) \
    /* 3D model */ \
    F(modelYawOffset,    0.0f, -360.0f,   360.0f) \
    F(modelTurnSpeed,  720.0f,    1.0f, 10000.0f) \
    /* Pathway following */ \
    I(followPathway,        0,        0,        1) \
    I(pathGroup,            0,        0,       99) \
    I(pathLoop,             0,        0,        3) \
    F(pathSprintMul,     2.0f,     1.0f,     10.0f) \
    F(pathJumpHeight,    1.5f,     0.0f,     20.0f) \
    F(pathSprintJumpHeight, 2.5f,  0.0f,     20.0f)

#define GHOST_BOSS_PARAMS(F, I) \
    /* Motion */ \
    I(autoJumpY,            1,        0,        1) \
    F(hopInterval,       1.4f,     0.0f,     30.0f) \
    F(gravity,          25.0f,     0.0f,    300.0f) \
    F(maxFall,          30.0f,     1.0f,    300.0f) \
    /* Pathway following */ \
    I(followPathway,        0,        0,        1) \
    I(pathGroup,            0,        0,       99) \
    I(pathLoop,             0,        0,        3) \
    F(pathSprintMul,     2.0f,     1.0f,     10.0f) \
    F(pathJumpHeight,    1.5f,     0.0f,     20.0f) \
    F(pathSprintJumpHeight, 2.5f,  0.0f,     20.0f) \
    /* 3D model */ \
    F(modelYawOffset,    0.0f,  -360.0f,    360.0f) \
    F(modelTurnSpeed,  720.0f,     1.0f,  10000.0f)

// Settings of a Pathway marker (Role = Custom, Custom Tag = Pathway)
#define GHOST_PATH_PARAMS(F, I) \
    I(pathGroup,            0,        0,       99) \
    I(action,               0,        0,        4) \
    F(holdTime,          0.0f,     0.0f,   3600.0f) \
    F(jumpHeight,        0.0f,     0.0f,     20.0f)



// ---- generated from the lists above ----
static inline float GhostRP_ClampF(float v, float lo, float hi, float def)
{
    if (v != v) return def;
    return v < lo ? lo : (v > hi ? hi : v);
}
static inline int GhostRP_ClampI(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

#define GRP_FIELD_F(n, d, lo, hi) float n;
#define GRP_FIELD_I(n, d, lo, hi) int n;
#define GRP_SET(n, d, lo, hi)     p.n = d;
#define GRP_CLAMP_F(n, d, lo, hi) p->n = GhostRP_ClampF(p->n, lo, hi, d);
#define GRP_CLAMP_I(n, d, lo, hi) p->n = GhostRP_ClampI(p->n, lo, hi);

typedef struct GhostPlayerParams { GHOST_PLAYER_PARAMS(GRP_FIELD_F, GRP_FIELD_I) } GhostPlayerParams;
typedef struct GhostEnemyParams { GHOST_ENEMY_PARAMS(GRP_FIELD_F, GRP_FIELD_I) } GhostEnemyParams;

typedef struct GhostBossParams { GHOST_BOSS_PARAMS(GRP_FIELD_F, GRP_FIELD_I) } GhostBossParams;
typedef struct GhostPathParams { GHOST_PATH_PARAMS(GRP_FIELD_F, GRP_FIELD_I) } GhostPathParams;
static inline GhostBossParams GhostBossParams_Make(void) { GhostBossParams p; GHOST_BOSS_PARAMS(GRP_SET, GRP_SET) return p; }
static inline GhostPathParams GhostPathParams_Make(void) { GhostPathParams p; GHOST_PATH_PARAMS(GRP_SET, GRP_SET) return p; }
static inline void GhostBossParams_Clamp(GhostBossParams* p) { GHOST_BOSS_PARAMS(GRP_CLAMP_F, GRP_CLAMP_I) }
static inline void GhostPathParams_Clamp(GhostPathParams* p) { GHOST_PATH_PARAMS(GRP_CLAMP_F, GRP_CLAMP_I) }

static inline GhostPlayerParams GhostPlayerParams_Make(void)
{
    GhostPlayerParams p; GHOST_PLAYER_PARAMS(GRP_SET, GRP_SET) return p;
}
static inline GhostEnemyParams GhostEnemyParams_Make(void)
{
    GhostEnemyParams p; GHOST_ENEMY_PARAMS(GRP_SET, GRP_SET) return p;
}

static inline void GhostPlayerParams_Clamp(GhostPlayerParams* p) { GHOST_PLAYER_PARAMS(GRP_CLAMP_F, GRP_CLAMP_I) }
static inline void GhostEnemyParams_Clamp(GhostEnemyParams* p) { GHOST_ENEMY_PARAMS(GRP_CLAMP_F, GRP_CLAMP_I) }

#ifdef __cplusplus
static_assert(sizeof(GhostPlayerParams) == 49 * 4, "GhostPlayerParams: field count changed, update the C# struct");
static_assert(sizeof(GhostEnemyParams) == 25 * 4, "GhostEnemyParams: field count changed, update the C# struct");
static_assert(sizeof(GhostBossParams) == 12 * 4, "GhostBossParams: field count changed, update the C# struct");
static_assert(sizeof(GhostPathParams) == 4 * 4, "GhostPathParams: field count changed, update the C# struct");
#endif