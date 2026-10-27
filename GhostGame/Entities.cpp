#include "Game.h"
#include <vector>

struct Saved { int id; float pos[3], scale[3], color[3]; char tag[32]; int visible; };
struct Enemy { int id; float homeX; float dir; float vy; bool alive; float dieTimer; };

static std::vector<Saved> g_saved;
static std::vector<int>   g_coins, g_enemyIds, g_blocks;
static std::vector<Enemy> g_enemies;
static std::vector<int>   g_powerups;
static float g_spin = 0.0f;
struct Mover { int id; bool platform; float a[3], b[3]; float speed, pause; int mode; int dir; float wait; bool done; float delta[3]; };
struct Breaking { int id; float timer; };
static std::vector<Mover>    g_movers;
static std::vector<Breaking> g_breaking;
static std::vector<int>      g_moverIds, g_projIds;



static void Snapshot(const char* tag, std::vector<int>* idsOut)
{
    int ids[512];
    int n = G->FindAllByTag(tag, ids, 512);
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

static void RebuildMovers()
{
    g_movers.clear();
    for (int pass = 0; pass < 2; pass++)
        for (int id : (pass == 0 ? g_moverIds : g_projIds))
        {
            GhostObjectProps pr = {};
            if (!G->GetObjectProps(id, &pr)) continue;
            Mover m = {};
            m.id = id; m.platform = (pass == 0); m.dir = 1;
            G->GetPosition(id, m.a);
            for (int i = 0; i < 3; i++) m.b[i] = m.a[i] + pr.pathOffset[i];
            m.speed = pr.moveSpeed > 0.01f ? pr.moveSpeed : 0.01f;
            m.pause = pr.movePause;
            m.mode = pr.pathMode;
            g_movers.push_back(m);
        }
}


// Records the level exactly as authored, so a restart can put everything back
void Level_Init()
{
    g_saved.clear(); g_coins.clear(); g_enemyIds.clear(); g_blocks.clear(); g_enemies.clear(); g_powerups.clear();
    g_breaking.clear(); g_moverIds.clear(); g_projIds.clear(); g_movers.clear();
    Snapshot("Player", nullptr);
    Snapshot("Coin", &g_coins);
    Snapshot("Enemy", &g_enemyIds);
    Snapshot("Block", &g_blocks);
    Snapshot("Breakable", nullptr);
    Snapshot("Mover", &g_moverIds);
    Snapshot("Projectile", &g_projIds);
    S.totalCoins = (int)g_coins.size();
    S.totalEnemies = (int)g_enemyIds.size();
    RebuildEnemies();
    RebuildMovers();
}

void Level_Reset()
{
    g_breaking.clear();
    for (int id : g_powerups) G->Destroy(id);
    g_powerups.clear();

    for (const Saved& s : g_saved)
    {
        G->SetPosition(s.id, s.pos);
        G->SetScale(s.id, s.scale);
        G->SetColor(s.id, s.color);
        G->SetTag(s.id, s.tag);
        G->SetVisible(s.id, s.visible);
        G->SpriteSetClip(s.id, "");
    }
    RebuildEnemies();
    RebuildMovers();
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
static void SpawnPowerUp(const float* mn, const float* mx)
{
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

static void BreakBrick(int id, int effect)
{
    G->SetTag(id, "Broken");                               // no longer solid
    if (effect == 0) { G->SetVisible(id, 0); return; }
    if (Sprite_Play(id, "Break", nullptr))                 // a sprite plays its Break clip
    {
        if (effect == 1) g_breaking.push_back({ id, 0.6f });
        return;                                            // effect 2: it stays on its last frame as rubble
    }
    if (effect == 1) { G->SetVisible(id, 0); return; }     // a plain cube just vanishes...
    float s[3], p[3];                                      // ...or squashes into rubble
    G->GetScale(id, s);
    G->GetPosition(id, p);
    p[1] -= s[1] * 0.5f * 0.8f;
    s[1] *= 0.2f;
    const float ash[3] = { 0.2f, 0.17f, 0.15f };
    G->SetScale(id, s); G->SetPosition(id, p); G->SetColor(id, ash);
}

// The player's head hit this object from below
void Blocks_Bump(int id)
{
    char tag[32];
    if (!G->GetTag(id, tag, sizeof tag)) return;
    const bool block = strcmp(tag, "Block") == 0, brick = strcmp(tag, "Breakable") == 0;
    if (!block && !brick) return;

    GhostObjectProps pr = {};
    G->GetObjectProps(id, &pr);
    float mn[3], mx[3];
    if (!G->GetBounds(id, mn, mx)) return;

    int give = pr.contents;                                // 0 role default, 1 nothing, 2 coin, 3 power-up
    if (give == 0) give = block ? 3 : 1;                   // a ? block gives a power-up, a brick gives nothing

    if (brick && give == 1)                                // a plain brick: big Mario breaks it
    {
        if (S.big || Cfg::SmallBreaksBricks) BreakBrick(id, pr.breakEffect);
        return;
    }

    if (brick) BreakBrick(id, pr.breakEffect);             // a brick with contents breaks and pays out
    else
    {
        G->SetTag(id, "UsedBlock");                        // still solid, can't be bumped again
        if (!Sprite_Play(id, "Used", nullptr))
        {
            const float brown[3] = { 0.45f, 0.30f, 0.15f };
            G->SetColor(id, brown);
        }
    }

    if (give == 2 || (give == 3 && S.big)) { S.coins++; return; }    // a coin, or a coin instead of a power-up when already big
    if (give == 3) SpawnPowerUp(mn, mx);
}

void Movers_Update(float dt)
{
    for (size_t i = 0; i < g_breaking.size(); )            // bricks hide after their Break clip has played
    {
        g_breaking[i].timer -= dt;
        if (g_breaking[i].timer <= 0.0f) { G->SetVisible(g_breaking[i].id, 0); g_breaking.erase(g_breaking.begin() + i); }
        else i++;
    }

    Body pb;
    float pp[3] = { 0, 0, 0 };
    const bool havePlayer = S.player && !S.complete && LoadBody(S.player, pb) && G->GetPosition(S.player, pp);

    for (Mover& m : g_movers)
    {
        if (m.done) continue;
        if (m.wait > 0.0f) { m.wait -= dt; continue; }

        float cur[3];
        if (!G->GetPosition(m.id, cur)) continue;
        const float* to = (m.dir > 0) ? m.b : m.a;
        const float d[3] = { to[0] - cur[0], to[1] - cur[1], to[2] - cur[2] };
        const float dist = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        const float step = m.speed * dt;

        float next[3];
        const bool arrived = step >= dist;
        bool jumped = false;
        if (arrived) memcpy(next, to, sizeof next);
        else for (int k = 0; k < 3; k++) next[k] = cur[k] + d[k] / dist * step;

        if (arrived)
        {
            m.wait = m.pause;
            if (m.mode == 0) m.dir = -m.dir;                                    // ping-pong
            else if (m.mode == 1) { memcpy(next, m.a, sizeof next); jumped = true; }   // restart at A
            else m.done = true;                                                 // once
        }

        if (m.platform && havePlayer && !jumped)                                // carry whoever is standing on top
        {
            float mn[3], mx[3];
            if (G->GetBounds(m.id, mn, mx))
            {
                const float feet = pb.pos[1] - pb.half[1];
                const bool onTop = fabsf(feet - mx[1]) < 0.12f &&
                    pb.pos[0] + pb.half[0] > mn[0] && pb.pos[0] - pb.half[0] < mx[0] &&
                    pb.pos[2] + pb.half[2] > mn[2] && pb.pos[2] - pb.half[2] < mx[2];
                if (onTop)
                {
                    for (int k = 0; k < 3; k++) { pp[k] += next[k] - cur[k]; pb.pos[k] += next[k] - cur[k]; }
                    G->SetPosition(S.player, pp);
                }
            }
        }
        G->SetPosition(m.id, next);
        if (!m.platform) Sprite_Play(m.id, "Walk", "Idle");                     // fireballs and the like
    }
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