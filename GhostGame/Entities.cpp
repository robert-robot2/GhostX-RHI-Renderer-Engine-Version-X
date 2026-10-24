#include "Game.h"
#include <vector>

struct Saved { int id; float pos[3], scale[3], color[3]; char tag[32]; int visible; };
struct Enemy { int id; float homeX; float dir; float vy; bool alive; float dieTimer; };

static std::vector<Saved> g_saved;
static std::vector<int>   g_coins, g_enemyIds, g_blocks;
static std::vector<Enemy> g_enemies;
static std::vector<int>   g_powerups;
static float g_spin = 0.0f;

static void Snapshot(const char* tag, std::vector<int>* idsOut)
{
    int ids[256];
    int n = G->FindAllByTag(tag, ids, 256);
    for (int i = 0; i < n; i++)
    {
        Saved s = {};
        s.id = ids[i];
        G->GetPosition(s.id, s.pos);
        G->GetScale(s.id, s.scale);
        G->GetColor(s.id, s.color);
        strncpy_s(s.tag, tag, _TRUNCATE);
        s.visible = G->GetVisible(s.id);
        g_saved.push_back(s);
        if (idsOut) idsOut->push_back(s.id);
    }
}

static void RebuildEnemies()
{
    g_enemies.clear();
    for (int id : g_enemyIds)
    {
        Enemy e = {};
        e.id = id; e.dir = 1.0f; e.alive = true;
        float p[3];
        if (G->GetPosition(id, p)) e.homeX = p[0];
        g_enemies.push_back(e);
    }
}

// Records the level exactly as authored, so a restart can put everything back
void Level_Init()
{
    g_saved.clear(); g_coins.clear(); g_enemyIds.clear(); g_blocks.clear(); g_enemies.clear(); g_powerups.clear();
    Snapshot("Player", nullptr);
    Snapshot("Coin", &g_coins);
    Snapshot("Enemy", &g_enemyIds);
    Snapshot("Block", &g_blocks);
    S.totalCoins = (int)g_coins.size();
    S.totalEnemies = (int)g_enemyIds.size();
    RebuildEnemies();
}

void Level_Reset()
{
    for (int id : g_powerups) G->Destroy(id);
    g_powerups.clear();

    for (const Saved& s : g_saved)
    {
        G->SetPosition(s.id, s.pos);
        G->SetScale(s.id, s.scale);
        G->SetColor(s.id, s.color);
        G->SetTag(s.id, s.tag);
        G->SetVisible(s.id, s.visible);
    }
    RebuildEnemies();
}

void Coin_Collect(int id)
{
    G->SetVisible(id, 0);          // hidden objects are skipped by QueryBox
    S.coins++;
}

void Enemy_Kill(int id)
{
    for (Enemy& e : g_enemies)
        if (e.id == id && e.alive)
        {
            e.alive = false;
            S.enemiesDown++;
            G->SetTag(id, "Defeated");                       // no longer hurts you or blocks you
            if (Sprite_Play(id, "Die", "Hurt")) e.dieTimer = 0.5f;   // a sprite plays its Die clip for a moment
            else G->SetVisible(id, 0);                       // a plain cube just vanishes
        }
}

void PowerUp_Remove(int id)
{
    G->Destroy(id);
    for (size_t i = 0; i < g_powerups.size(); i++)
        if (g_powerups[i] == id) { g_powerups.erase(g_powerups.begin() + i); break; }
}

// The player's head hit this object from below
void Blocks_Bump(int id)
{
    char tag[32];
    if (!G->GetTag(id, tag, sizeof tag) || strcmp(tag, "Block") != 0) return;

    float mn[3], mx[3];
    if (!G->GetBounds(id, mn, mx)) return;

    G->SetTag(id, "UsedBlock");                    // still solid, but it can't be bumped again
    const float brown[3] = { 0.45f, 0.30f, 0.15f };
    G->SetColor(id, brown);

    if (S.big) { S.coins++; return; }              // already big: the block pays out a coin

    float pos[3] = { (mn[0] + mx[0]) * 0.5f, mx[1] + 0.26f, (mn[2] + mx[2]) * 0.5f };
    int pu = G->Spawn(GHOST_API_MESH_CUBE, pos);
    if (!pu) return;
    const float size[3] = { 0.5f, 0.5f, 0.5f };
    const float green[3] = { 0.2f, 1.0f, 0.4f };
    G->SetScale(pu, size);
    G->SetColor(pu, green);
    G->SetTag(pu, "PowerUp");
    g_powerups.push_back(pu);
}

void Entities_Update(float dt)
{
    // ---- Spinning coins and power-ups ----
    g_spin += 180.0f * dt;
    if (g_spin > 360.0f) g_spin -= 360.0f;
    const float rot[3] = { 0.0f, g_spin, 0.0f };
    for (int id : g_coins) if (G->GetVisible(id)) G->SetRotation(id, rot);
    for (int id : g_powerups) G->SetRotation(id, rot);

    // ---- Enemies: patrol along X, turn at walls and at the patrol limit ----
    for (Enemy& e : g_enemies)
    {
        if (!e.alive)
        {
            if (e.dieTimer > 0.0f) { e.dieTimer -= dt; if (e.dieTimer <= 0.0f) G->SetVisible(e.id, 0); }
            continue;
        }
        Body b;
        if (!LoadBody(e.id, b)) continue;

        e.vy -= Cfg::Gravity * dt;
        if (e.vy < -Cfg::MaxFall) e.vy = -Cfg::MaxFall;
        b.vel[0] = e.dir * Cfg::EnemySpeed;
        b.vel[1] = e.vy;

        MoveResult r = MoveBody(b, dt);
        e.vy = b.vel[1];
        if (r.hitX) e.dir = -e.dir;
        if (b.pos[0] > e.homeX + Cfg::EnemyPatrol) e.dir = -1.0f;
        else if (b.pos[0] < e.homeX - Cfg::EnemyPatrol) e.dir = 1.0f;
        {
            const float yaw = S.camYaw * 0.0174532925f;
            Sprite_FaceScreen(e.id, e.dir * cosf(yaw));      // enemies walk along X, so the screen direction follows the camera
            Sprite_Play(e.id, "Walk", "Idle");
        }
        StoreBody(b);
    }
}