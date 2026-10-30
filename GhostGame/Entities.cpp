#include "Game.h"
#include <vector>

//Entities.cpp

struct Saved { int id; float pos[3], scale[3], color[3]; char tag[32]; int visible; };
struct Enemy { int id; float a[3], b[3]; float speed, pause, wait; int mode; int dir; float vy; bool alive, done, grounded; float dieTimer; };

static std::vector<Saved> g_saved;
static std::vector<int>   g_coins, g_enemyIds, g_blocks;
static std::vector<Enemy> g_enemies;
struct Pickup { int id; int type; float dir, vy, riseTo; bool rising, moves; };
struct BlockMem { int id; int coinsLeft; };
static std::vector<Pickup>   g_pickups;
static std::vector<BlockMem> g_blockMem;
static std::vector<int>      g_hidden;
static const char* kPickupTag[5] = { "", "Mushroom", "FireFlower", "Star", "OneUp" };
static const char* kPickupAnim[5] = { "", "Content/Animations/Mushroom.ghostanim", "Content/Animations/FireFlower.ghostanim",
                                      "Content/Animations/Star.ghostanim", "Content/Animations/OneUp.ghostanim" };
static const float kPickupColor[5][3] = { { 1,1,1 },{ 1,0.3f,0.3f },{ 1,0.6f,0.1f },{ 1,0.9f,0.2f },{ 0.3f,1,0.4f } };   // used if the .ghostanim is missing
static float g_spin = 0.0f;
struct Mover { int id; bool platform; float a[3], b[3]; float speed, pause; int mode; int dir; float wait; bool done; float delta[3]; };
struct Breaking { int id; float timer; };
static std::vector<Mover>    g_movers;
static std::vector<Breaking> g_breaking;
static std::vector<int>      g_moverIds, g_projIds;

struct Debris { int id; float vel[3]; float life; };
static std::vector<Debris> g_debris;

static float g_skyBase = 12.0f, g_skyCur = 12.0f;   // the level's own hour, and the hour we are showing


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
        e.id = id; e.dir = 1; e.alive = true;

        Body bd;                                              // same coordinate space the update loop uses
        if (LoadBody(id, bd)) memcpy(e.a, bd.pos, sizeof e.a);
        else G->GetPosition(id, e.a);

        GhostObjectProps pr = {};
        const bool has = G->GetObjectProps(id, &pr) != 0;
        float off[3] = { 0, 0, 0 };
        if (has) memcpy(off, pr.pathOffset, sizeof off);
        off[1] = 0.0f;                                        // gravity owns Y
        if (off[0] * off[0] + off[2] * off[2] < 0.0001f) off[2] = Cfg::EnemyPatrol;   // no offset set: old default patrol

        for (int k = 0; k < 3; k++) e.b[k] = e.a[k] + off[k];
        e.speed = (has && pr.moveSpeed > 0.01f) ? pr.moveSpeed : Cfg::EnemySpeed;
        e.pause = has ? pr.movePause : 0.0f;
        e.mode = has ? pr.enemyPathMode : 0;
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
    g_saved.clear(); g_coins.clear(); g_enemyIds.clear(); g_blocks.clear(); g_enemies.clear(); g_pickups.clear(); g_blockMem.clear(); g_hidden.clear();
    g_breaking.clear(); g_moverIds.clear(); g_projIds.clear(); g_movers.clear(); g_debris.clear();
    Snapshot("Player", nullptr);
    Snapshot("Coin", &g_coins);
    Snapshot("Enemy", &g_enemyIds);
    Snapshot("Block", &g_blocks);
    Snapshot("Breakable", nullptr);
    Snapshot("Mover", &g_moverIds);
    Snapshot("Projectile", &g_projIds);
    Snapshot("HiddenBlock", &g_hidden);
    for (int id : g_hidden) G->SetVisible(id, 0);          // hidden blocks are invisible while playing

    S.totalCoins = (int)g_coins.size();
    for (int pass = 0; pass < 2; pass++)                    // coins stored inside ? blocks count toward the total too
        for (int id : (pass == 0 ? g_blocks : g_hidden))
        {
            GhostObjectProps pr = {};
            if (G->GetObjectProps(id, &pr) && pr.contents == 2)
                S.totalCoins += pr.coinCount < 1 ? 1 : pr.coinCount;
        }
    S.totalEnemies = (int)g_enemyIds.size();
    RebuildEnemies();
    RebuildMovers();
    g_skyBase = g_skyCur = G->SkyGetTime();         // remember the authored time of day
}

void Level_Reset()
{
    for (const Debris& d : g_debris) G->Destroy(d.id);
    g_debris.clear();
    g_breaking.clear();
    for (const Pickup& p : g_pickups) G->Destroy(p.id);
    g_pickups.clear(); g_blockMem.clear();

    for (const Saved& s : g_saved)
    {
        G->SetPosition(s.id, s.pos);
        G->SetScale(s.id, s.scale);
        G->SetColor(s.id, s.color);
        G->SetTag(s.id, s.tag);
        G->SetVisible(s.id, s.visible);
        G->SpriteSetClip(s.id, "");
    }
    for (int id : g_hidden) G->SetVisible(id, 0);
    RebuildEnemies();
    RebuildMovers();
    g_skyCur = g_skyBase;                           // respawn: back to the level's own sky
    G->SkySetTime(g_skyBase);
}

void Coin_Collect(int id)
{
    if (!G->GetVisible(id)) return;   // never count the same coin twice
    G->SetVisible(id, 0);
    Coins_Add(1);
    Score_Add(200);
}

void Enemy_Kill(int id)
{
    for (Enemy& e : g_enemies)
        if (e.id == id && e.alive)
        {
            e.alive = false;
            S.enemiesDown++;
            Score_Enemy(S.starTime > 0.0f);
            G->SetTag(id, "Defeated");                       // no longer hurts you or blocks you
            if (Sprite_Play(id, "Die", "Hurt")) e.dieTimer = 0.5f;   // a sprite plays its Die clip for a moment
            else G->SetVisible(id, 0);                       // a plain cube just vanishes
        }
}

static void SpawnDebris(int brickId)
{
    float p[3], s[3];
    if (!G->GetPosition(brickId, p) || !G->GetScale(brickId, s)) return;
    const float piece[3] = { s[0] * 0.3f, s[1] * 0.3f, s[2] * 0.3f };
    const float brown[3] = { 0.55f, 0.30f, 0.18f };
    for (int k = 0; k < 4; k++)
    {
        const float sx = (k & 1) ? 1.0f : -1.0f, sz = (k & 2) ? 1.0f : -1.0f;
        const float pos[3] = { p[0] + sx * s[0] * 0.25f, p[1] + s[1] * 0.25f, p[2] + sz * s[2] * 0.25f };
        int id = G->Spawn(GHOST_API_MESH_CUBE, pos);
        if (!id) return;
        G->SetScale(id, piece);
        G->SetColor(id, brown);
        G->SetTag(id, "Broken");                 // not solid, and nothing in the player code reacts to it
        Debris d = {};
        d.id = id; d.life = 1.2f;
        d.vel[0] = sx * 2.0f; d.vel[1] = (k < 2) ? 8.5f : 7.0f; d.vel[2] = sz * 2.0f;
        g_debris.push_back(d);
    }
}

static void BreakBrick(int id, int effect)
{
    Score_Add(50);
    G->SetTag(id, "Broken");                               // no longer solid
    if (effect == 0) { G->SetVisible(id, 0); return; }
    if (Sprite_Play(id, "Break", nullptr))                 // a sprite plays its Break clip
    {
        if (effect == 1) g_breaking.push_back({ id, 0.6f });
        return;                                            // effect 2: it stays on its last frame as rubble
    }
    if (effect == 1) { SpawnDebris(id); G->SetVisible(id, 0); return; }
    float s[3], p[3];                                      // ...or squashes into rubble
    G->GetScale(id, s);
    G->GetPosition(id, p);
    p[1] -= s[1] * 0.5f * 0.8f;
    s[1] *= 0.2f;
    const float ash[3] = { 0.2f, 0.17f, 0.15f };
    G->SetScale(id, s); G->SetPosition(id, p); G->SetColor(id, ash);
}

void Coins_Add(int n) { S.coins += n; while (S.coins >= S.playerMaxCoins) { S.coins -= S.playerMaxCoins; S.lives++; } }
void Life_Add() { S.lives++; }

void Score_Add(int pts) { S.score += pts; }

// SMB1 chain: 100,200,400,500,800,1000,2000,4000,5000,8000, then a 1UP per kill
void Score_Enemy(bool star)
{
    static const int kChain[10] = { 100, 200, 400, 500, 800, 1000, 2000, 4000, 5000, 8000 };
    int& c = star ? S.starChain : S.stompChain;
    if (c < 10) Score_Add(kChain[c]); else Life_Add();
    c++;
}

void Goal_Reached()
{
    S.complete = true;
    S.timeBonus = (int)S.timeLeft * 50;
    Score_Add(S.timeBonus);
}


bool Pickup_TypeFromTag(const char* tag, int& type)
{
    for (int t = 1; t <= 4; t++) if (strcmp(tag, kPickupTag[t]) == 0) { type = t; return true; }
    return false;
}

void PowerUp_Remove(int id)
{
    G->Destroy(id);
    for (size_t i = 0; i < g_pickups.size(); i++)
        if (g_pickups[i].id == id) { g_pickups.erase(g_pickups.begin() + i); break; }
}

static void SpawnPickup(int type, const float* mn, const float* mx, bool moves)
{
    const float sz = Cfg::PickupSize;
    float pos[3] = { (mn[0] + mx[0]) * 0.5f, mx[1] - sz * 0.5f, (mn[2] + mx[2]) * 0.5f };   // starts inside the block
    int id = G->Spawn(GHOST_API_MESH_QUAD, pos);                                             // a sprite, not a cube
    if (!id) return;
    const float size[3] = { sz, sz, sz };
    G->SetScale(id, size);
    G->SetTag(id, kPickupTag[type]);
    if (!G->SpriteSetAnim(id, kPickupAnim[type])) G->SetColor(id, kPickupColor[type]);
    Pickup p = {};
    p.id = id; p.type = type; p.dir = 1.0f; p.rising = true; p.moves = moves;
    p.riseTo = mx[1] + sz * 0.5f + 0.01f;
    g_pickups.push_back(p);
}

static int& CoinsLeft(int id, int count)
{
    for (BlockMem& m : g_blockMem) if (m.id == id) return m.coinsLeft;
    g_blockMem.push_back({ id, count < 1 ? 1 : count });
    return g_blockMem.back().coinsLeft;
}

static void SetUsed(int id)
{
    G->SetTag(id, "UsedBlock");                            // still solid, can't be bumped again
    if (!Sprite_Play(id, "Used", nullptr))
    {
        const float brown[3] = { 0.45f, 0.30f, 0.15f };
        G->SetColor(id, brown);
    }
}

static void Pay(int id, const GhostObjectProps& pr, int give, bool isBlock, bool hidden)
{
    float mn[3], mx[3];
    if (!G->GetBounds(id, mn, mx)) return;
    if (give == 2)                                         // coins: one per hit until empty
    {
        Coins_Add(1);
        Score_Add(200);
        if (isBlock) { if (--CoinsLeft(id, pr.coinCount) <= 0) SetUsed(id); }
        return;
    }
    if (isBlock) SetUsed(id);
    if (give != 3) return;
    int type = pr.powerType;
    if (type == 0) type = hidden ? PU_ONEUP : (PowerLevel() == 0 ? PU_MUSHROOM : PU_FLOWER);
    const bool moves = pr.itemMoves == 1 || (pr.itemMoves == 0 && type != PU_FLOWER);
    SpawnPickup(type, mn, mx, moves);
}

// The player's head hit this object from below
void Blocks_Bump(int id)
{
    char tag[32];
    if (!G->GetTag(id, tag, sizeof tag)) return;
    const bool hidden = strcmp(tag, "HiddenBlock") == 0;
    const bool block = hidden || strcmp(tag, "Block") == 0, brick = strcmp(tag, "Breakable") == 0;
    if (!block && !brick) return;

    GhostObjectProps pr = {};
    G->GetObjectProps(id, &pr);
    int give = pr.contents;                                // 0 role default, 1 nothing, 2 coin, 3 power-up
    if (give == 0) give = block ? 3 : 1;

    if (brick && give == 1)                                // plain brick: only big Mario breaks it
    {
        if (S.big || Cfg::SmallBreaksBricks) BreakBrick(id, pr.breakEffect);
        return;
    }
    if (brick) BreakBrick(id, pr.breakEffect);             // brick with contents breaks and pays out
    Pay(id, pr, give, block, hidden);
}

// Hidden blocks: only trigger when Mario moves UP into them. Call before MoveBody.
bool Hidden_Try(Body& b, float vy, float dt)
{
    if (vy <= 0.0f) return false;
    const float headNow = b.pos[1] + b.half[1], headNext = headNow + vy * dt;
    for (int id : g_hidden)
    {
        if (G->GetVisible(id)) continue;                   // already revealed
        float mn[3], mx[3];
        if (!G->GetBounds(id, mn, mx)) continue;
        if (!(b.pos[0] - b.half[0] < mx[0] && b.pos[0] + b.half[0] > mn[0] &&
            b.pos[2] - b.half[2] < mx[2] && b.pos[2] + b.half[2] > mn[2])) continue;
        if (headNow > mn[1] + 0.02f || headNext < mn[1]) continue;
        b.pos[1] = mn[1] - b.half[1] - 0.001f;
        G->SetVisible(id, 1);
        Blocks_Bump(id);
        return true;
    }
    return false;
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
// Is there something solid just beyond the enemy's leading edge, a little below its feet?
static bool GroundAhead(const Body& b, float dx, float dz)
{
    const float len = sqrtf(dx * dx + dz * dz);
    if (len < 0.0001f) return true;
    const float lead = fmaxf(b.half[0], b.half[2]) + 0.15f;
    const float cx = b.pos[0] + dx / len * lead, cz = b.pos[2] + dz / len * lead;
    const float feet = b.pos[1] - b.half[1];
    const float mn[3] = { cx - 0.05f, feet - 0.4f,  cz - 0.05f };
    const float mx[3] = { cx + 0.05f, feet - 0.05f, cz + 0.05f };
    int ids[16];
    const int n = G->QueryBox(mn, mx, ids, 16);
    for (int i = 0; i < n; i++) if (ids[i] != b.id && IsSolid(ids[i])) return true;
    return false;
}

// Below Cfg::NightDepth the sky eases to Cfg::NightHour; above it, back to the level's own hour.
void Sky_Update(float dt)
{
    if (!S.player) return;
    float p[3];
    if (!G->GetPosition(S.player, p)) return;       // object position, not the collider center: fine for a 48-unit spacer

    const bool  night = p[1] < Cfg::NightDepth;
    const float target = night ? Cfg::NightHour : g_skyBase;
    if (!night && g_skyCur == g_skyBase) return;    // normal play: leave the sky alone

    float d = fmodf(target - g_skyCur, 24.0f);      // shortest way round the 24 h clock
    if (d > 12.0f) d -= 24.0f;
    if (d < -12.0f) d += 24.0f;

    const float step = Cfg::NightFadeSpeed * dt;
    if (fabsf(d) <= step) g_skyCur = target;
    else g_skyCur = fmodf(g_skyCur + (d > 0.0f ? step : -step) + 24.0f, 24.0f);

    G->SkySetTime(g_skyCur);                        // set every frame while night, so it holds even if Cycle Speed is not 0
}


void Entities_Update(float dt)
{
    // ---- Spinning coins and power-ups ----
    g_spin += 180.0f * dt;
    if (g_spin > 360.0f) g_spin -= 360.0f;
    const float rot[3] = { 0.0f, g_spin, 0.0f };
    Body pbody;
    const bool havePlayerBody = S.player && !S.complete && LoadBody(S.player, pbody);
    for (int id : g_coins)
    {
        if (!G->GetVisible(id)) continue;
        G->SetRotation(id, rot);
        if (!havePlayerBody) continue;
        float cp[3];                                         // fallback pickup: works even if the coin has no collider
        if (!G->GetPosition(id, cp)) continue;
        const float m = 0.5f;
        if (fabsf(cp[0] - pbody.pos[0]) < pbody.half[0] + m &&
            fabsf(cp[1] - pbody.pos[1]) < pbody.half[1] + m &&
            fabsf(cp[2] - pbody.pos[2]) < pbody.half[2] + m)
            Coin_Collect(id);
    }

    for (size_t i = 0; i < g_pickups.size(); )
    {
        Pickup& p = g_pickups[i];
        float pos[3];
        if (!G->GetPosition(p.id, pos)) { g_pickups.erase(g_pickups.begin() + i); continue; }
        if (p.rising)
        {
            pos[1] += Cfg::PickupRise * dt;
            if (pos[1] >= p.riseTo) { pos[1] = p.riseTo; p.rising = false; }
            G->SetPosition(p.id, pos);
        }
        else if (p.moves)
        {
            Body b;
            if (LoadBody(p.id, b))
            {
                p.vy -= Cfg::Gravity * dt;
                if (p.vy < -Cfg::MaxFall) p.vy = -Cfg::MaxFall;
                b.vel[0] = 0.0f; b.vel[1] = p.vy; b.vel[2] = p.dir * Cfg::PickupSpeed;
                MoveResult r = MoveBody(b, dt);
                p.vy = b.vel[1];
                if (r.hitZ) p.dir = -p.dir;
                if (p.type == PU_STAR && r.hitFloor) p.vy = Cfg::StarBounce;
                StoreBody(b);
                pos[1] = b.pos[1];
            }
        }
        if (pos[1] < Cfg::FallLimit) { G->Destroy(p.id); g_pickups.erase(g_pickups.begin() + i); continue; }
        i++;
    }
    for (size_t i = 0; i < g_debris.size(); )
    {
        Debris& d = g_debris[i];
        float pos[3];
        if (!G->GetPosition(d.id, pos)) { g_debris.erase(g_debris.begin() + i); continue; }
        d.vel[1] -= Cfg::Gravity * dt;
        for (int k = 0; k < 3; k++) pos[k] += d.vel[k] * dt;
        G->SetPosition(d.id, pos);
        d.life -= dt;
        if (d.life <= 0.0f) { G->Destroy(d.id); g_debris.erase(g_debris.begin() + i); continue; }
        i++;
    }
    // ---- Enemies: walk A <-> B from their Behavior settings (Enemy Path Mode) ----
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

        if (e.wait > 0.0f) e.wait -= dt;
        const bool walking = e.mode != 3 && !e.done && e.wait <= 0.0f;     // mode 3 = stand still

        float vx = 0.0f, vz = 0.0f;
        bool arrive = false;
        if (walking)
        {
            const float* to = (e.dir > 0) ? e.b : e.a;
            const float dx = to[0] - b.pos[0], dz = to[2] - b.pos[2];
            const float dist = sqrtf(dx * dx + dz * dz);
            arrive = dist <= e.speed * dt;
            if (dist > 0.0001f)
            {
                const float sp = arrive ? dist / dt : e.speed;             // land exactly on the end point
                vx = dx / dist * sp; vz = dz / dist * sp;
            }
            if (e.mode == 1 && e.grounded && !arrive && !GroundAhead(b, vx, vz))
            {
                e.dir = -e.dir;                                            // ledge ahead: turn around
                vx = vz = 0.0f;
            }
        }
        b.vel[0] = vx; b.vel[1] = e.vy; b.vel[2] = vz;

        MoveResult r = MoveBody(b, dt);
        e.vy = b.vel[1];
        e.grounded = r.hitFloor;

        if (walking && (arrive || r.hitX || r.hitZ))                       // reached the end, or a wall stopped it
        {
            e.wait = e.pause;
            if (e.mode == 2) e.done = true;                                // once
            else e.dir = -e.dir;                                           // ping-pong (both kinds)
        }

        if (b.pos[1] < Cfg::FallLimit)                                     // fell out of the world
        {
            e.alive = false;
            G->SetTag(e.id, "Defeated");
            G->SetVisible(e.id, 0);
            continue;
        }

        const float yaw = S.camYaw * 0.0174532925f;
        Sprite_FaceScreen(e.id, vx * cosf(yaw) - vz * sinf(yaw));          // same formula the Player uses
        Sprite_Play(e.id, walking ? "Walk" : "Idle", "Idle");
        StoreBody(b);
    }
}