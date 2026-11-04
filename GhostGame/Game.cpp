#include "Game.h"

//Game.cpp

const GhostGameAPI* G = nullptr;
GameState S;

static const char* kLevels[] = { "Level1.ghostscene", "Level2.ghostscene" };   // add more file names here for more levels
static const int   kLevelCount = (int)(sizeof(kLevels) / sizeof(kLevels[0]));

static int FindLevelIndex()
{
    char name[160];
    if (!G->GetSceneName(name, sizeof name)) return -1;
    for (int i = 0; i < kLevelCount; i++) if (_stricmp(name, kLevels[i]) == 0) return i;
    return -1;
}

int Sprite_Play(int id, const char* clip, const char* fallback)
{
    if (G->SpriteSetState(id, clip)) return 1;
    return (fallback && G->SpriteSetState(id, fallback)) ? 1 : 0;
}

void Sprite_FaceScreen(int id, float v)
{
    if (v > 0.2f) G->SpriteSetFlip(id, 0);
    else if (v < -0.2f) G->SpriteSetFlip(id, 1);
}

static int g_musicState = -1;       // what Music_Update last asked for: 0 scene track, 1 star, 2 underground, 3 silent

static void Music_Update()
{
    int want = 0;
    if (S.mode == MODE_OVER || S.complete || S.onPole || S.bossCut) want = 3;
    else if (S.starTime > 0.0f) want = 1;
    else
    {
        float p[3];
        if (S.player && G->GetPosition(S.player, p) && p[1] < Cfg::NightDepth) want = 2;   // same depth the sky goes dark at
    }
    if (want == g_musicState) return;
    g_musicState = want;
    switch (want)
    {
    case 0: G->PlaySceneAudio(0.4f); break;
    case 1: G->PlayMusic("music_star", 0.2f); break;
    case 2: G->PlayMusic("music_underground", 0.4f); break;
    case 3: G->StopMusic(0.3f); break;
    }
}

// Buttons named MusicButton / SfxButton / AmbienceButton on a .ghostui toggle their bus. A missing widget is ignored.
static void Sound_Toggles(int ui)
{
    static const struct { const char* btn; const char* label; int bus; } t[3] =
    { { "MusicButton", "Music", 1 },{ "SfxButton", "SFX", 2 },{ "AmbienceButton", "Ambience", 3 } };
    for (const auto& x : t)
    {
        if (G->UIButtonClicked(ui, x.btn)) G->SetBusEnabled(x.bus, !G->GetBusEnabled(x.bus));
        char s[48];
        snprintf(s, sizeof s, "%s: %s", x.label, G->GetBusEnabled(x.bus) ? "ON" : "OFF");
        G->UISetText(ui, x.btn, s);
    }
}

static int g_ui = 0;                // handle of Content/UI/HUD.ghostui (0 = failed to load)
static int  g_menuUi = 0, g_overUi = 0;
static bool g_started = false;     // lives outside S, so a level change doesn't bring the menu back

static int  g_selUi = 0, g_optUi = 0;
static bool g_menuOptions = false;

static void Carry_Save(bool started, int lives, int score, int coins)
{
    char t[96];
    snprintf(t, sizeof t, "%d %d %d %d %d %d", started ? 1 : 0, lives, score, coins, S.character, S.twoPlayer ? 1 : 0);
    SetEnvironmentVariableA("GHOSTGAME_CARRY", t);
}
static void Carry_Clear() { SetEnvironmentVariableA("GHOSTGAME_CARRY", nullptr); }

static void Carry_Load()
{
    char t[96];
    if (!GetEnvironmentVariableA("GHOSTGAME_CARRY", t, sizeof t)) return;
    Carry_Clear();
    int started = 0, lives = 3, score = 0, coins = 0, ch = 0, tp = 0;
    if (sscanf_s(t, "%d %d %d %d %d %d", &started, &lives, &score, &coins, &ch, &tp) != 6) return;
    g_started = (started != 0);
    S.lives = lives; S.score = score; S.coins = coins;
    S.character = ch; S.twoPlayer = (tp != 0);
}

// Restart the whole game from World 1-1 (keeps the character choice)
static void Game_FullRestart()
{
    char p[200];
    snprintf(p, sizeof p, "Scenes/%s", kLevels[0]);
    Carry_Save(true, 3, 0, 0);
    if (S.level <= 0 || !G->LoadScene(p))
    {
        Carry_Clear();
        if (S.twoPlayer) S.character = 0;
        S.lives = 3; S.score = 0; S.deaths = 0;
        S.mode = MODE_PLAY;
        Game_Restart(false);
    }
}

static void Game_ToMenu()
{
    char p[200];
    snprintf(p, sizeof p, "Scenes/%s", kLevels[0]);
    g_started = false;
    Carry_Clear();
    if (S.level <= 0 || !G->LoadScene(p))
    {
        S.twoPlayer = false; S.character = 0;
        S.lives = 3; S.score = 0; S.deaths = 0;
        Game_Restart(false);
        S.mode = MODE_MENU;
    }
}

// ---- Menu navigation: W/S/A/D or arrows or pad move a focus marker, Space / Enter / A presses, B / Backspace goes back ----
struct NavItem { const char* widget; const char* label; };
static const NavItem kNavMain[] = { { "StartButton", "Start" },{ "TwoPlayerButton", "2 Player" },{ "OptionsButton", "Options" },{ "QuitButton", "Quit" } };
static const NavItem kNavOptBack[] = { { "OptionsBackButton", "Back" } };
static const NavItem kNavSel[] = { { "MarioButton", "Mario" },{ "LuigiButton", "Luigi" },{ "StartButton", "Start" },{ "BackButton", "Back to Main Menu" } };
static const NavItem kNavPause[] = { { "ResumeButton", "Resume" },{ "RestartButton", "Restart Game" },{ "MenuButton", "Main Menu" } };
static const NavItem kNavOver[] = { { "RestartButton", "Restart" },{ "MenuButton", "Main Menu" } };
static const NavItem kNavDone[] = { { "AgainButton", "Play Again" },{ "QuitButton", "Quit" } };

static const NavItem* g_navItems = nullptr;
static int  g_navUi = 0, g_navIdx = 0;
static bool g_navPress = false, g_navBack = false, g_navShow = false;

static void Nav_Idle() { g_navItems = nullptr; g_navPress = g_navBack = false; }

static void Nav_Update(int ui, const NavItem* items, int count)
{
    if (!ui || count <= 0) { Nav_Idle(); return; }
    if (g_navItems != items) { g_navItems = items; g_navIdx = 0; g_navShow = G->PadConnected() != 0; }   // new screen: first button
    g_navUi = ui;

    const bool next = G->KeyPressed('S') || G->KeyPressed(VK_DOWN) || G->KeyPressed('D') || G->KeyPressed(VK_RIGHT);
    const bool prev = G->KeyPressed('W') || G->KeyPressed(VK_UP) || G->KeyPressed('A') || G->KeyPressed(VK_LEFT);
    if (next || prev)
    {
        if (g_navShow) g_navIdx = next ? (g_navIdx + 1) % count : (g_navIdx + count - 1) % count;   // the first press only shows the marker
        g_navShow = true;
    }
    g_navPress = g_navShow && (G->KeyPressed(VK_SPACE) || G->KeyPressed(VK_RETURN));
    g_navBack = G->PadButtonPressed(GHOST_PAD_B) || G->KeyPressed(VK_BACK);

    for (int i = 0; i < count; i++)
    {
        char t[96];
        if (g_navShow && i == g_navIdx) snprintf(t, sizeof t, "> %s <", items[i].label);
        else snprintf(t, sizeof t, "%s", items[i].label);
        G->UISetText(ui, items[i].widget, t);
    }
}

// A mouse click, or the focused button pressed from the keyboard / pad
static bool Clicked(int ui, const char* widget)
{
    if (G->UIButtonClicked(ui, widget)) return true;
    return g_navPress && g_navUi == ui && g_navItems && strcmp(g_navItems[g_navIdx].widget, widget) == 0;
}

void Game_Restart(bool died)
{
    if (died) { S.deaths++; if (--S.lives <= 0) { S.lives = 0; Sfx("game_over"); S.mode = MODE_OVER; return; } Sfx("death"); }
    S.coins = 0; S.enemiesDown = 0;
    S.big = false; S.fire = false; S.starTime = 0.0f; S.attackCool = 0.0f; S.heldShell = 0; S.shellSliding = false; S.advance = 0.0f; S.invuln = 0.0f; S.hurt = 0.0f; S.complete = false;
    S.big = false; S.advance = 0.0f; S.invuln = 0.0f; S.hurt = 0.0f; S.complete = false;
    S.onPole = false; S.bossCut = false; S.poleSlide = 0.0f; S.poleId = 0;
    S.portalCool = 0.0f;
    S.timeLeft = Cfg::TimeStart; S.stompChain = S.starChain = 0; S.timeBonus = 0;
    Level_Reset();
    Player_Reset();
    S.camSnap = true;

}

static bool  g_camFocus = false;
static float g_camFocusYaw = 90.0f;

static void Camera_Focus(float dt, float& yaw)            // eases the yaw toward the target by the short way round
{
    float d = fmodf(g_camFocusYaw - yaw, 360.0f);
    if (d > 180.0f) d -= 360.0f;
    if (d < -180.0f) d += 360.0f;
    const float step = 600.0f * dt;
    if (fabsf(d) <= step) { yaw = g_camFocusYaw; g_camFocus = false; }
    else yaw += d > 0.0f ? step : -step;
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

        if (G->GetCamera(S.camId, nullptr, yp, nullptr))
        {
            S.camYaw = yp[0];

            // Only recenter when explicitly requested
            if (g_camFocus)
            {
                Camera_Focus(dt, yp[0]);
                G->SetCamera(S.camId, nullptr, yp, nullptr);
                S.camYaw = yp[0];
            }
        }

        return;
    }

    // Game-driven camera
    if (g_camFocus)
        Camera_Focus(dt, S.camYaw);


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

    snprintf(t, sizeof t, "Coins %d / %d", S.coins, S.playerMaxCoins);         G->UISetText(g_ui, "CoinText", t);
    snprintf(t, sizeof t, "Enemies %d / %d", S.enemiesDown, S.totalEnemies); G->UISetText(g_ui, "EnemyText", t);
    snprintf(t, sizeof t, "Deaths %d", S.deaths);                            G->UISetText(g_ui, "DeathText", t);
    const float white[4] = { 1, 1, 1, 1 };
    snprintf(t, sizeof t, "Points %06d", S.score);          G->UISetText(g_ui, "Points", t);
    snprintf(t, sizeof t, "Lives: %d", S.lives);            G->UISetText(g_ui, "Lives", t);
    const int lv = S.level < 0 ? 0 : S.level;               // 4 stages per world, like SMB
    snprintf(t, sizeof t, "World %d-%d", lv / 4 + 1, lv % 4 + 1);   G->UISetText(g_ui, "World", t);
    snprintf(t, sizeof t, "Timer %d", (int)ceilf(S.timeLeft));      G->UISetText(g_ui, "Timer", t);
    G->UISetColor(g_ui, "Timer", S.timeLeft < 100.0f ? red : white);   // turns red under 100

    G->UISetProgress(g_ui, "CoinBar", (float)S.coins / (float)S.playerMaxCoins);

    const int pl = PowerLevel();
    G->UISetText(g_ui, "SizeText", pl == 2 ? "FIRE" : (pl == 1 ? "BIG" : "small"));
    G->UISetColor(g_ui, "SizeText", pl == 2 ? red : (pl == 1 ? green : grey));

    const char* hint = G->PadActive()
        ? "Left stick move    X run    A jump    B attack    RB interact    Right stick camera    Start options"
        : (S.orbit
            ? "WASD move    Shift run    Space jump    Click attack    Right-drag orbit    Wheel zoom    F10 options"
            : "WASD move    Shift run    Space jump    Click attack    Q/E camera    F10 options");
    G->UISetText(g_ui, "HintText", hint);
    G->UISetText(g_ui, "Character", S.character == 1 ? "Luigi" : "Mario");

    G->UISetVisible(g_ui, "CompletePanel", S.complete ? 1 : 0);
    if (S.complete)
    {
        snprintf(t, sizeof t, "Score %06d    Time Bonus %d    Coins %d    Deaths %d",
            S.score, S.timeBonus, S.coins, S.deaths);
        G->UISetText(g_ui, "CompleteStats", t);
        // Order of this could be off
        G->UISetText(g_ui, "CompleteTitle", S.lastLevel ? "YOU WIN!" : "LEVEL COMPLETE!");
        G->UISetVisible(g_ui, "AgainButton", S.lastLevel ? 1 : 0);
        G->UISetVisible(g_ui, "QuitButton", S.lastLevel ? 1 : 0);
    }
}

GHOST_GAME_EXPORT int GhostGame_Init(const GhostGameAPI* api)
{
    if (!api || api->version != GHOST_GAME_API_VERSION || api->size < (int)sizeof(GhostGameAPI)) return 0;
    G = api;
    S = GameState();
    g_musicState = -1;
    Carry_Load();
    S.level = FindLevelIndex();
    S.lastLevel = (S.level < 0 || S.level >= kLevelCount - 1);
    if (S.twoPlayer) S.character = S.level < 0 ? 0 : (S.level & 1);   // Mario on even levels, Luigi on odd
    g_ui = G->UILoad("Content/UI/HUD.ghostui");
    g_menuUi = G->UILoad("Content/UI/MainMenu.ghostui");
    g_overUi = G->UILoad("Content/UI/GameOver.ghostui");
    g_selUi = G->UILoad("Content/UI/CharSelect.ghostui");
    g_optUi = G->UILoad("Content/UI/PlayerOptions.ghostui");
    S.mode = g_started ? MODE_PLAY : MODE_MENU;
    if (g_menuUi) G->UIShow(g_menuUi, g_started ? 0 : 1);
    if (g_overUi) G->UIShow(g_overUi, 0);
    if (g_ui)     G->UIShow(g_ui, g_started ? 1 : 0);
    if (g_selUi) G->UIShow(g_selUi, 0);
    if (g_optUi) G->UIShow(g_optUi, 0);
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

static void Timer_Update(float dt)
{
    if (S.complete || S.onPole || S.bossCut || !S.player || S.error) return;
    S.timeLeft -= dt / Cfg::TimeTick;
    if (S.timeLeft <= 0.0f) { S.timeLeft = 0.0f; Game_Restart(true); }   // time up = lose a life
}

// Returns true while a menu screen owns the frame (the game is frozen)
static bool Screens_Update()
{
    const bool menu = (S.mode == MODE_MENU), over = (S.mode == MODE_OVER);
    const bool sel = (S.mode == MODE_SELECT), opt = (S.mode == MODE_OPTIONS);
    if (g_menuUi) G->UIShow(g_menuUi, menu ? 1 : 0);
    if (g_overUi) G->UIShow(g_overUi, over ? 1 : 0);
    if (g_selUi)  G->UIShow(g_selUi, sel ? 1 : 0);
    if (g_optUi)  G->UIShow(g_optUi, opt ? 1 : 0);

    if (menu)
    {
        if (g_ui) G->UIShow(g_ui, 0);
        if (g_menuUi)
        {
            G->UISetVisible(g_menuUi, "OptionsPanel", g_menuOptions ? 1 : 0);
            if (g_menuOptions)
            {
                Nav_Update(g_menuUi, kNavOptBack, 1);
                if (Clicked(g_menuUi, "OptionsBackButton") || g_navBack) g_menuOptions = false;
                return true;                                  // the buttons behind the panel stay dead
            }
            Nav_Update(g_menuUi, kNavMain, 4);
            if (Clicked(g_menuUi, "StartButton")) { S.twoPlayer = false; S.mode = MODE_SELECT; }
            else if (Clicked(g_menuUi, "TwoPlayerButton"))
            {
                S.twoPlayer = true; S.character = 0;          // 1-1 is Mario; later levels pick in Init
                g_started = true;
                S.mode = MODE_PLAY;
                Game_Restart(false);
            }
            else if (Clicked(g_menuUi, "OptionsButton")) g_menuOptions = true;
            else if (Clicked(g_menuUi, "QuitButton")) G->Quit();
        }
        return true;
    }

    if (sel)
    {
        if (g_ui) G->UIShow(g_ui, 0);
        if (g_selUi)
        {
            Nav_Update(g_selUi, kNavSel, 4);
            if (Clicked(g_selUi, "MarioButton")) S.character = 0;
            if (Clicked(g_selUi, "LuigiButton")) S.character = 1;
            const float lit[4] = { 1, 1, 1, 1 }, dim[4] = { 0.45f, 0.45f, 0.45f, 1 };
            G->UISetColor(g_selUi, "MarioButton", S.character == 0 ? lit : dim);
            G->UISetColor(g_selUi, "LuigiButton", S.character == 1 ? lit : dim);
            G->UISetText(g_selUi, "SelectedText", S.character == 1 ? "LUIGI" : "MARIO");
            if (Clicked(g_selUi, "StartButton"))
            {
                g_started = true;
                S.mode = MODE_PLAY;
                Game_Restart(false);
            }
            else if (Clicked(g_selUi, "BackButton") || g_navBack) S.mode = MODE_MENU;
        }
        return true;
    }

    if (opt)                                                  // the HUD stays visible behind the panel
    {
        if (g_optUi)
        {
            Sound_Toggles(g_optUi);
            Nav_Update(g_optUi, kNavPause, 3);
            if (Clicked(g_optUi, "ResumeButton") || g_navBack) S.mode = MODE_PLAY;
            else if (Clicked(g_optUi, "RestartButton")) { S.mode = MODE_PLAY; Game_FullRestart(); }
            else if (Clicked(g_optUi, "MenuButton")) Game_ToMenu();
        }
        return true;
    }

    if (over)
    {
        if (g_ui) G->UIShow(g_ui, 0);
        if (g_overUi)
        {
            Nav_Update(g_overUi, kNavOver, 2);
            char t[128];
            snprintf(t, sizeof t, "Score %06d      Coins %d      Deaths %d", S.score, S.coins, S.deaths);
            G->UISetText(g_overUi, "GameOverStats", t);

            char p[200];
            snprintf(p, sizeof p, "Scenes/%s", kLevels[0]);

            if (Clicked(g_overUi, "RestartButton"))
            {
                if (S.level <= 0 || !G->LoadScene(p))
                {
                    S.lives = 3; S.score = 0; S.deaths = 0;
                    S.mode = MODE_PLAY;
                    Game_Restart(false);
                }
            }
            else if (Clicked(g_overUi, "MenuButton"))
            {
                g_started = false;                         // Init will show the menu after the scene reloads
                if (S.level <= 0 || !G->LoadScene(p))
                {
                    S.lives = 3; S.score = 0; S.deaths = 0;
                    Game_Restart(false);
                    S.mode = MODE_MENU;
                }
            }
        }
        return true;
    }
    return false;
}

GHOST_GAME_EXPORT void GhostGame_Update(float dt)
{
    if (!S.player) { S.error = "No object with Role = Player in this scene"; Hud(); return; }
    Music_Update();
    if (G->KeyPressed(VK_F10))                             // before Screens_Update, so it can also CLOSE the options panel
    {
        if (S.mode == MODE_PLAY && !S.complete && !S.onPole) S.mode = MODE_OPTIONS;
        else if (S.mode == MODE_OPTIONS) S.mode = MODE_PLAY;
    }
    if (Screens_Update()) { Camera_Update(dt); return; }
    if (S.mode == MODE_PLAY && !S.complete && !S.onPole && !S.bossCut && G->KeyPressed('Z'))
    {
        g_camFocus = true;
        g_camFocusYaw = g_haveMoveYaw ? g_moveYawDeg : S.camYaw;   // no movement yet: nothing to turn to
    }
    if (g_ui && S.complete && S.lastLevel) Nav_Update(g_ui, kNavDone, 2);
    else Nav_Idle();
    if (g_ui && S.complete)
    {
        if (Clicked(g_ui, "AgainButton"))
        {
            char p[200];
            snprintf(p, sizeof p, "Scenes/%s", kLevels[0]);
            if (S.level <= 0 || !G->LoadScene(p)) Game_Restart(false);       // the last level starts over from Level 1
        }
        if (Clicked(g_ui, "QuitButton")) G->Quit();
    }
    Movers_Update(dt);
    Timer_Update(dt);
    if (!S.complete) Player_Update(dt);
    else Sprite_Play(S.player, "Idle", nullptr);
    if (S.complete && !S.lastLevel)                      // go to the next level after 2.5 seconds
    {
        S.advance += dt;
        if (S.advance >= 2.5f)
        {
            char p[200];
            snprintf(p, sizeof p, "Scenes/%s", kLevels[S.level + 1]);
            G->LoadScene(p);
            Carry_Save(true, S.lives, S.score, S.coins);
            S.advance = -1000.0f;
        }
    }
    Entities_Update(dt);
    Sky_Update(dt);
    Camera_Update(dt);
    Hud();
}

GHOST_GAME_EXPORT void GhostGame_Shutdown(void)
{
    if (G) G->Log("Adventures of Cube: shutdown");
}