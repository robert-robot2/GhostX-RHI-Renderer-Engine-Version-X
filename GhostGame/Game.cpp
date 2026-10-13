#include <Windows.h>
#include <math.h>
#include <stdio.h>
#include "GhostGameAPI.h"

static const GhostGameAPI* g = nullptr;
static int   g_player = 0;
static int   g_spawned = 0;
static float g_groundY = 0.0f;
static float g_vy = 0.0f;

GHOST_GAME_EXPORT int GhostGame_Init(const GhostGameAPI* api)
{
    if (!api || api->version != GHOST_GAME_API_VERSION || api->size < (int)sizeof(GhostGameAPI)) return 0;
    g = api;
    g_spawned = 0;
    g_vy = 0.0f;

    g_player = g->FindByTag("Player");
    if (g_player)
    {
        float p[3];
        g->GetPosition(g_player, p);
        g_groundY = p[1];
        g->Log("GhostGame: found the Player");
    }
    else g->Log("GhostGame: no object with Role = Player");
    return 1;
}

GHOST_GAME_EXPORT void GhostGame_Update(float dt)
{
    if (!g_player) { g->SetHudText("No object with Role = Player in this scene"); return; }

    float p[3];
    if (!g->GetPosition(g_player, p)) { g_player = 0; return; }

    float dx = 0.0f, dz = 0.0f;
    if (g->KeyDown('W')) dz += 1.0f;
    if (g->KeyDown('S')) dz -= 1.0f;
    if (g->KeyDown('D')) dx += 1.0f;
    if (g->KeyDown('A')) dx -= 1.0f;
    float len = sqrtf(dx * dx + dz * dz);
    if (len > 0.0f) { dx /= len; dz /= len; }

    const float speed = g->KeyDown(VK_SHIFT) ? 6.0f : 3.0f;
    p[0] += dx * speed * dt;
    p[2] += dz * speed * dt;

    const bool grounded = p[1] <= g_groundY + 0.001f;
    if (grounded && g->KeyPressed(VK_SPACE)) g_vy = 6.0f;
    if (!grounded || g_vy > 0.0f)
    {
        g_vy -= 18.0f * dt;
        p[1] += g_vy * dt;
        if (p[1] <= g_groundY) { p[1] = g_groundY; g_vy = 0.0f; }
    }
    g->SetPosition(g_player, p);

    // E spawns an orange cube above the player, Q destroys it
    if (g->KeyPressed('E') && !g_spawned)
    {
        float sp[3] = { p[0], p[1] + 2.5f, p[2] };
        g_spawned = g->Spawn(GHOST_API_MESH_CUBE, sp);
        const float orange[3] = { 1.0f, 0.55f, 0.1f };
        if (g_spawned) g->SetColor(g_spawned, orange);
    }
    if (g->KeyPressed('Q') && g_spawned) { g->Destroy(g_spawned); g_spawned = 0; }

    // How many other colliders overlap the player?
    int touching = 0;
    float bmin[3], bmax[3];
    if (g->GetBounds(g_player, bmin, bmax))
    {
        int ids[32];
        int n = g->QueryBox(bmin, bmax, ids, 32);
        for (int i = 0; i < n; i++) if (ids[i] != g_player) touching++;
    }

    char hud[200];
    snprintf(hud, sizeof hud, "Pos %.1f %.1f %.1f | touching %d | t=%.1fs | E spawn, Q destroy",
        p[0], p[1], p[2], touching, g->GetTime());
    g->SetHudText(hud);
}

GHOST_GAME_EXPORT void GhostGame_Shutdown(void)
{
    if (g) g->Log("GhostGame: Shutdown");
}