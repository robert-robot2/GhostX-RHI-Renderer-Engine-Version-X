#include "Game.h"
//Collision.cpp

static const float SKIN = 0.001f;      // pushes land a hair outside the surface, so resting contact never counts as overlap

bool IsSolid(int id)
{
    char tag[32];
    if (!G->GetTag(id, tag, sizeof tag)) return false;
    if (strcmp(tag, "Boss") == 0 || strcmp(tag, "BossAxe") == 0 || strcmp(tag, "BossPoint") == 0 || strcmp(tag, "Pathway") == 0) return false;
    return !(strcmp(tag, "Player") == 0 || strcmp(tag, "Enemy") == 0 || strcmp(tag, "Coin") == 0 ||
        strcmp(tag, "Goal") == 0 || strcmp(tag, "Mushroom") == 0 || strcmp(tag, "FireFlower") == 0 ||
        strcmp(tag, "Star") == 0 || strcmp(tag, "OneUp") == 0 || strcmp(tag, "Defeated") == 0 ||
        strcmp(tag, "Projectile") == 0 || strcmp(tag, "Broken") == 0 ||
        strcmp(tag, "Shell") == 0 || strcmp(tag, "HeldShell") == 0 ||      // ADD THIS LINE
        strcmp(tag, "SlidingShell") == 0);                                 
}

bool LoadBody(int id, Body& b)
{
    float mn[3], mx[3];
    if (!G->GetBounds(id, mn, mx)) return false;
    float p[3] = { 0, 0, 0 };
    G->GetPosition(id, p);
    b.id = id;
    for (int i = 0; i < 3; i++)
    {
        b.pos[i] = (mn[i] + mx[i]) * 0.5f;
        b.half[i] = (mx[i] - mn[i]) * 0.5f;
        b.off[i] = b.pos[i] - p[i];
    }
    return true;
}

void StoreBody(const Body& b)
{
    const float p[3] = { b.pos[0] - b.off[0], b.pos[1] - b.off[1], b.pos[2] - b.off[2] };
    G->SetPosition(b.id, p);
}

int QueryBody(const Body& b, int* ids, int maxCount)
{
    float mn[3], mx[3];
    for (int i = 0; i < 3; i++) { mn[i] = b.pos[i] - b.half[i]; mx[i] = b.pos[i] + b.half[i]; }

    int tmp[64];
    int n = G->QueryBox(mn, mx, tmp, 64);
    int c = 0;
    for (int i = 0; i < n && c < maxCount; i++)
        if (tmp[i] != b.id) ids[c++] = tmp[i];
    return c;
}
// ============================================================
//  Terrain (floor Tile Maps): solid below the surface, not as one box up to the highest vertex
// ============================================================
static const float kStepUp = 0.06f;   // ledge walked up without a slope check
static const float kMaxSlope = 1.2f;    // rise / run you can walk up (1.2 = about 50 degrees)
static const float kSnapDown = 0.5f;    // walking downhill stays glued to the surface within this drop

bool Terrain_Is(int id) { return G->IsTerrain && G->IsTerrain(id) != 0; }

// Highest terrain surface under the body's centre. false = no terrain here.
static bool TerrainGround(const Body& b, float& outY)
{
    if (!G->IsTerrain || !G->GetTerrainHeight) return false;
    const float mn[3] = { b.pos[0] - b.half[0], b.pos[1] - b.half[1] - 1.0f, b.pos[2] - b.half[2] };
    const float mx[3] = { b.pos[0] + b.half[0], b.pos[1] + b.half[1],        b.pos[2] + b.half[2] };
    int ids[64];
    const int n = G->QueryBox(mn, mx, ids, 64);
    bool any = false;
    float best = -1e30f;
    for (int i = 0; i < n; i++)
    {
        if (ids[i] == b.id || !Terrain_Is(ids[i])) continue;
        float y;
        if (G->GetTerrainHeight(ids[i], b.pos[0], b.pos[2], &y) && y > best) { best = y; any = true; }
    }
    if (any) outY = best;
    return any;
}

// After a horizontal move: is the terrain ahead a wall (too steep / too high)? Undoes the move if so.
static bool TerrainBlocks(Body& b, int axis, float before)
{
    float gy;
    if (!TerrainGround(b, gy)) return false;
    const float rise = gy - (b.pos[1] - b.half[1]);
    if (rise <= kStepUp) return false;                               // at or below the surface: fine
    const float run = fabsf(b.pos[axis] - before);
    if (rise <= kStepUp + run * kMaxSlope) return false;             // gentle slope: the Y pass lifts the body onto it
    b.pos[axis] = before;
    return true;
}

// Moves along one axis and pushes the body out of any solid it ends up inside
static bool ResolveAxis(Body& b, int axis, float delta, int* hitId)
{
    if (delta == 0.0f) return false;
    b.pos[axis] += delta;

    float mn[3], mx[3];
    for (int i = 0; i < 3; i++) { mn[i] = b.pos[i] - b.half[i]; mx[i] = b.pos[i] + b.half[i]; }

    int ids[32];
    int n = G->QueryBox(mn, mx, ids, 32);
    bool hit = false;

    for (int i = 0; i < n; i++)
    {
        if (ids[i] == b.id || !IsSolid(ids[i]) || Terrain_Is(ids[i])) continue;   // terrain is handled separately
        float omn[3], omx[3];
        if (!G->GetBounds(ids[i], omn, omx)) continue;

        bool overlap = true;                      // earlier pushes may have already cleared this one
        for (int k = 0; k < 3; k++)
            if (!(b.pos[k] - b.half[k] < omx[k] && b.pos[k] + b.half[k] > omn[k])) overlap = false;
        if (!overlap) continue;

        if (delta > 0.0f) b.pos[axis] = omn[axis] - b.half[axis] - SKIN;
        else              b.pos[axis] = omx[axis] + b.half[axis] + SKIN;
        hit = true;
        if (hitId) *hitId = ids[i];
    }
    return hit;
}

MoveResult MoveBody(Body& b, float dt)
{
    MoveResult r;

    float g0 = 0.0f;                                                      // was the body standing on terrain?
    const bool onTerrain = TerrainGround(b, g0) && fabsf((b.pos[1] - b.half[1]) - g0) < 0.06f;

    for (int axis = 0; axis < 3; axis += 2)                               // X, then Z
    {
        const float before = b.pos[axis];
        bool hit = ResolveAxis(b, axis, b.vel[axis] * dt, nullptr);
        if (!hit && b.vel[axis] != 0.0f) hit = TerrainBlocks(b, axis, before);
        if (hit) { (axis == 0 ? r.hitX : r.hitZ) = true; b.vel[axis] = 0.0f; }
    }

    const float vy0 = b.vel[1];
    const float dy = vy0 * dt;
    int hid = 0;
    bool solidFloor = false;
    if (ResolveAxis(b, 1, dy, &hid))
    {
        if (dy < 0.0f) { r.hitFloor = true; solidFloor = true; }
        else { r.hitCeiling = true; r.ceilingId = hid; }
        b.vel[1] = 0.0f;
    }

    float gy;
    if (!solidFloor && TerrainGround(b, gy))
    {
        const float feet = b.pos[1] - b.half[1];
        if (feet < gy)                                        // sunk into the surface: stand on it
        {
            b.pos[1] += gy - feet;
            if (vy0 <= 0.0f) { r.hitFloor = true; b.vel[1] = 0.0f; }
        }
        else if (onTerrain && vy0 <= 0.0f && feet - gy < kSnapDown)   // walking downhill: don't go airborne
        {
            b.pos[1] -= feet - gy;
            r.hitFloor = true; b.vel[1] = 0.0f;
        }
    }
    return r;
}