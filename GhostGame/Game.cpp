#include "Game.h"

const GhostGameAPI* G = nullptr;
GameState S;
static int g_ui = 0;                // handle of Content/UI/HUD.ghostui (0 = failed to load)

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

static void Label(float x, float y, float size, const char* s, const float* c)
{
    const float shadow[4] = { 0.0f, 0.0f, 0.0f, 0.65f };
    G->UIText(x + size * 0.05f, y + size * 0.05f, size, s, shadow);
    G->UIText(x, y, size, s, c);
}

static void Hud()
{
    float scr[2] = { 1920.0f, 1080.0f };
    G->UIGetScreenSize(scr);
    const float W = scr[0], H = scr[1];
    const float red[4] = { 1.0f, 0.35f, 0.3f, 1.0f };

    if (!g_ui)
    {
        if (S.error) { Label(40.0f, 40.0f, 40.0f, S.error, red); return; }
        Label(40.0f, 40.0f, 36.0f, "Content/UI/HUD.ghostui could not be loaded (see the Output window)", red);
        return;
    }

    if (S.error)
    {
        G->UIShow(g_ui, 0);
        const float panel[4] = { 0.0f, 0.0f, 0.0f, 0.45f };
        G->UIRect(0.0f, H * 0.5f - 60.0f, W, 120.0f, panel);
        float tw = G->UIMeasureText(S.error, 44.0f);
        Label((W - tw) * 0.5f, H * 0.5f - 24.0f, 44.0f, S.error, red);
        return;
    }
    G->UIShow(g_ui, 1);

    const float green[4] = { 0.4f, 1.0f, 0.5f, 1.0f }, grey[4] = { 0.8f, 0.8f, 0.8f, 1.0f };
    char t[160];

    snprintf(t, sizeof t, "Coins %d / %d", S.coins, S.totalCoins);           G->UISetText(g_ui, "CoinText", t);
    snprintf(t, sizeof t, "Enemies %d / %d", S.enemiesDown, S.totalEnemies); G->UISetText(g_ui, "EnemyText", t);
    snprintf(t, sizeof t, "Deaths %d", S.deaths);                            G->UISetText(g_ui, "DeathText", t);
    G->UISetProgress(g_ui, "CoinBar", S.totalCoins > 0 ? (float)S.coins / (float)S.totalCoins : 0.0f);

    G->UISetText(g_ui, "SizeText", S.big ? "BIG" : "small");
    G->UISetColor(g_ui, "SizeText", S.big ? green : grey);

    G->UISetText(g_ui, "HintText", S.orbit
        ? "WASD move    Shift run    Space jump    Right-drag orbit    Wheel zoom    R restart"
        : "WASD move    Shift run    Space jump    Q/E camera    R restart");

    G->UISetVisible(g_ui, "CompletePanel", S.complete ? 1 : 0);
    if (S.complete)
    {
        snprintf(t, sizeof t, "Coins %d / %d      Enemies %d / %d      Deaths %d",
            S.coins, S.totalCoins, S.enemiesDown, S.totalEnemies, S.deaths);
        G->UISetText(g_ui, "CompleteStats", t);
    }
}

GHOST_GAME_EXPORT int GhostGame_Init(const GhostGameAPI* api)
{
    if (!api || api->version != GHOST_GAME_API_VERSION || api->size < (int)sizeof(GhostGameAPI)) return 0;
    G = api;
    S = GameState();
    g_ui = G->UILoad("Content/UI/HUD.ghostui");
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
    if (!S.player) { S.error = "No object with Role = Player in this scene"; Hud(); return; }

    if (G->KeyPressed('R')) Game_Restart(false);
    if (g_ui && S.complete && G->UIButtonClicked(g_ui, "AgainButton")) Game_Restart(false);
    if (!S.complete) Player_Update(dt);
    Entities_Update(dt);
    Camera_Update(dt);
    Hud();
}

GHOST_GAME_EXPORT void GhostGame_Shutdown(void)
{
    if (G) G->Log("Adventures of Cube: shutdown");
}