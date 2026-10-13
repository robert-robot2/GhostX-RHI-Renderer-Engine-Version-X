#include "Game.h"

const GhostGameAPI* G = nullptr;
GameState S;

void Game_Restart(bool died)
{
    if (died) S.deaths++;
    S.coins = 0; S.enemiesDown = 0;
    S.big = false; S.invuln = 0.0f; S.complete = false;
    Level_Reset();
    Player_Reset();
    S.camSnap = true;
}

static void Camera_Update(float dt)
{
    if (!S.camId || !S.player) return;
    // Orbit camera: the engine follows the target and handles right-drag and wheel.
// The game only needs the camera's heading for camera-relative movement.
    S.orbit = (G->GetCameraMode(S.camId) == 1);
    if (S.orbit)
    {
        float yp[2];
        if (G->GetCamera(S.camId, nullptr, yp, nullptr)) S.camYaw = yp[0];
        return;
    }
    if (G->KeyDown('Q')) S.camYaw -= Cfg::CamTurnSpeed * dt;     // until 4D adds mouse orbit
    if (G->KeyDown('E')) S.camYaw += Cfg::CamTurnSpeed * dt;

    float p[3];
    if (!G->GetPosition(S.player, p)) return;

    const float d2r = 0.0174532925f;
    float yaw = S.camYaw * d2r, pitch = Cfg::CamPitch * d2r;
    float fx = cosf(pitch) * sinf(yaw), fy = sinf(pitch), fz = cosf(pitch) * cosf(yaw);
    float want[3] = { p[0] - fx * Cfg::CamDistance,
                      p[1] + Cfg::CamTargetHeight - fy * Cfg::CamDistance,
                      p[2] - fz * Cfg::CamDistance };

    if (S.camSnap) { memcpy(S.camPos, want, sizeof want); S.camSnap = false; }
    else
    {
        float k = 1.0f - expf(-Cfg::CamSmooth * dt);
        for (int i = 0; i < 3; i++) S.camPos[i] += (want[i] - S.camPos[i]) * k;
    }

    float pos[3] = { S.camPos[0], S.camPos[1] < 0.4f ? 0.4f : S.camPos[1], S.camPos[2] };   // never below the ground
    float yp[2] = { S.camYaw, Cfg::CamPitch };
    G->SetCamera(S.camId, pos, yp, nullptr);
}

static void Hud()
{
    char t[240];
    if (S.error)
        snprintf(t, sizeof t, "%s", S.error);
    else if (S.complete)
        snprintf(t, sizeof t, "LEVEL COMPLETE!  Coins %d/%d  Enemies %d/%d  Deaths %d  |  R: play again",
            S.coins, S.totalCoins, S.enemiesDown, S.totalEnemies, S.deaths);
    else
        snprintf(t, sizeof t, "Coins %d/%d | Enemies %d/%d | %s | Deaths %d | WASD move, Shift run, Space jump, %s, R restart",
            S.coins, S.totalCoins, S.enemiesDown, S.totalEnemies, S.big ? "BIG" : "small", S.deaths,
            S.orbit ? "right-drag orbit, wheel zoom" : "Q/E camera");
    G->SetHudText(t);
}

GHOST_GAME_EXPORT int GhostGame_Init(const GhostGameAPI* api)
{
    if (!api || api->version != GHOST_GAME_API_VERSION || api->size < (int)sizeof(GhostGameAPI)) return 0;
    G = api;
    S = GameState();

    S.player = G->FindByTag("Player");
    if (!S.player) { G->Log("Adventures of Cube: no object with Role = Player"); return 1; }

    S.camId = G->GetActiveCamera();
    float pos[3], yp[2];
    if (S.camId && G->GetCamera(S.camId, pos, yp, nullptr)) S.camYaw = yp[0];    // start behind the player, facing the way the camera did
    if (S.camId && G->GetCameraMode(S.camId) == 1 && G->GetCameraTarget(S.camId) == 0)
        G->SetCameraTarget(S.camId, S.player);       // an orbit camera with no target follows the player
    Level_Init();
    Player_Init();
    Player_Reset();
    S.camSnap = true;
    G->Log("Adventures of Cube: Level 1 ready");
    return 1;
}

GHOST_GAME_EXPORT void GhostGame_Update(float dt)
{
    if (!S.player) { G->SetHudText("No object with Role = Player in this scene"); return; }

    if (G->KeyPressed('R')) Game_Restart(false);

    if (!S.complete) Player_Update(dt);
    Entities_Update(dt);
    Camera_Update(dt);
    Hud();
}

GHOST_GAME_EXPORT void GhostGame_Shutdown(void)
{
    if (G) G->Log("Adventures of Cube: shutdown");
}