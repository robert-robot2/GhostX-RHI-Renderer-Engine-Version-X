#include "Game.h"

static const float SKIN = 0.001f;      // pushes land a hair outside the surface, so resting contact never counts as overlap

bool IsSolid(int id)
{
    char tag[32];
    if (!G->GetTag(id, tag, sizeof tag)) return false;
    return !(strcmp(tag, "Player") == 0 || strcmp(tag, "Enemy") == 0 || strcmp(tag, "Coin") == 0 ||
        strcmp(tag, "Goal") == 0 || strcmp(tag, "PowerUp") == 0 || strcmp(tag, "Defeated") == 0);
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
        if (ids[i] == b.id || !IsSolid(ids[i])) continue;
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
    if (ResolveAxis(b, 0, b.vel[0] * dt, nullptr)) { r.hitX = true; b.vel[0] = 0.0f; }
    if (ResolveAxis(b, 2, b.vel[2] * dt, nullptr)) { r.hitZ = true; b.vel[2] = 0.0f; }

    float dy = b.vel[1] * dt;
    int hid = 0;
    if (ResolveAxis(b, 1, dy, &hid))
    {
        if (dy < 0.0f) r.hitFloor = true;
        else { r.hitCeiling = true; r.ceilingId = hid; }
        b.vel[1] = 0.0f;
    }
    return r;
}