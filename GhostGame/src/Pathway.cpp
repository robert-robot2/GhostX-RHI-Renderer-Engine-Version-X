#include "../Game.h"
#include <algorithm>
// Pathway.cpp

static float Dist2D(const float* a, const float* b)
{
    const float dx = b[0] - a[0], dz = b[2] - a[2];
    return sqrtf(dx * dx + dz * dz);
}

void Path_Reset(PathFollower& f)
{
    f.cur = 0; f.tgt = 1; f.dir = 1; f.phase = 0; f.act = 0;
    f.hold = f.pts.empty() ? 0.0f : f.pts[0].hold;
    f.t = 0.0f; f.stuck = 0.0f; f.lastDist = 1e9f;
    f.started = false;
}

bool Path_Load(PathFollower& f, int group)
{
    f = PathFollower();
    int ids[128];
    const int n = G->FindAllByTag("Pathway", ids, 128);
    for (int i = 0; i < n; i++)
    {
        GhostObjectProps pr = {};
        if (!G->GetObjectProps(ids[i], &pr) || pr.path.pathGroup != group) continue;
        if (!pr.link[0]) { G->Log("A Pathway marker has no Link ID and was ignored"); continue; }
        PathPoint pt = {};
        G->GetPosition(ids[i], pt.p);
        strncpy_s(pt.key, pr.link, _TRUNCATE);
        pt.action = pr.path.action;
        pt.hold = pr.path.holdTime;
        pt.jumpH = pr.path.jumpHeight;
        f.pts.push_back(pt);
    }
    std::sort(f.pts.begin(), f.pts.end(), [](const PathPoint& a, const PathPoint& b)
        {
            const size_t la = strlen(a.key), lb = strlen(b.key);
            return la != lb ? la < lb : strcmp(a.key, b.key) < 0;     // A..Z, AA..; 9 before 10
        });
    f.active = f.pts.size() >= 2;
    Path_Reset(f);
    return f.active;
}

static void PathArrive(PathFollower& f)
{
    f.cur = f.tgt;
    f.hold = f.pts[(size_t)f.cur].hold;
    f.phase = 0;
    f.stuck = 0.0f;
}

void Path_Update(PathFollower& f, const Body& b, float dt, const PathCfg& c, PathOut& o)
{
    o = PathOut();
    if (!f.active) return;
    const int n = (int)f.pts.size();

    if (!f.started)                                   // first frame: stand on the first marker
    {
        f.started = true;
        o.setPos = true;
        o.pos[0] = f.pts[0].p[0]; o.pos[1] = b.pos[1]; o.pos[2] = f.pts[0].p[2];
        return;
    }
    if (f.phase == 3) return;                         // finished: idle

    // ---- hold at a marker, then pick the next one ----
    if (f.phase == 0)
    {
        f.hold -= dt;
        if (f.hold > 0.0f) return;

        int next = f.cur + f.dir, segment = f.cur;     // segment = the marker whose Action describes this leg
        if (next >= n)
        {
            if (c.loopMode == 1) { f.phase = 3; return; }                     // stop at the end
            if (c.loopMode == 2) { f.dir = -1; next = n - 2; segment = next; } // ping-pong
            else if (c.loopMode == 3)                                          // teleport back to the first marker
            {
                f.cur = 0; f.hold = f.pts[0].hold; f.stuck = 0.0f;
                o.setPos = true;
                o.pos[0] = f.pts[0].p[0]; o.pos[1] = b.pos[1]; o.pos[2] = f.pts[0].p[2];
                return;
            }
            else { next = 0; segment = n - 1; }                                // travel back using the last marker's action
        }
        else if (next < 0) { f.dir = 1; next = 1; segment = 0; }
        else if (f.dir < 0) segment = next;

        f.tgt = next;
        f.act = f.pts[(size_t)segment].action;
        f.stuck = 0.0f; f.lastDist = 1e9f;

        if (f.act == 2 || f.act == 3)                  // jump arcs
        {
            const bool sp = (f.act == 3);
            const float speed = fmaxf(c.walkSpeed * (sp ? c.sprintMul : 1.0f), 0.01f);
            const PathPoint& A = f.pts[(size_t)f.cur];
            const PathPoint& B = f.pts[(size_t)next];
            memcpy(f.from, b.pos, sizeof f.from);
            f.to[0] = B.p[0]; f.to[2] = B.p[2];
            f.to[1] = b.pos[1] + (B.p[1] - A.p[1]);     // marker height difference = landing height difference
            f.dur = fmaxf(Dist2D(f.from, f.to) / speed, 0.25f);
            const float own = f.pts[(size_t)segment].jumpH;
            f.jumpH = own > 0.0f ? own : (sp ? c.sprintJumpH : c.jumpH);
            f.t = 0.0f;
            f.phase = 2;
        }
        else f.phase = 1;                              // walk, sprint, idle-then-walk
    }

    // ---- walk / sprint: the normal physics step moves the body ----
    if (f.phase == 1)
    {
        const PathPoint& B = f.pts[(size_t)f.tgt];
        const bool sp = (f.act == 1);
        const float speed = fmaxf(c.walkSpeed * (sp ? c.sprintMul : 1.0f), 0.01f);
        const float dx = B.p[0] - b.pos[0], dz = B.p[2] - b.pos[2];
        const float dist = sqrtf(dx * dx + dz * dz);
        const bool arrive = dist <= speed * dt + 0.02f;
        if (dist > 0.0001f)
        {
            const float s = arrive ? dist / dt : speed;     // land exactly on the marker
            o.vx = dx / dist * s; o.vz = dz / dist * s;
        }
        o.anim = sp ? 2 : 1;
        if (f.lastDist - dist < speed * dt * 0.2f) f.stuck += dt; else f.stuck = 0.0f;   // blocked: give up after 1.5 s
        f.lastDist = dist;
        if (arrive || f.stuck > 1.5f) PathArrive(f);
        return;
    }

    // ---- jump: a timed arc, no collisions, so the height must clear whatever is in the way ----
    if (f.phase == 2)
    {
        f.t += dt;
        const float k = fminf(f.t / f.dur, 1.0f);
        o.setPos = true; o.jumping = true; o.anim = 3;
        o.pos[0] = f.from[0] + (f.to[0] - f.from[0]) * k;
        o.pos[2] = f.from[2] + (f.to[2] - f.from[2]) * k;
        o.pos[1] = f.from[1] + (f.to[1] - f.from[1]) * k + 4.0f * f.jumpH * k * (1.0f - k);
        const float dx = f.to[0] - f.from[0], dz = f.to[2] - f.from[2];
        const float l = sqrtf(dx * dx + dz * dz);
        if (l > 0.0001f) { const float s = l / f.dur; o.vx = dx / l * s; o.vz = dz / l * s; }   // facing only
        if (k >= 1.0f) PathArrive(f);
    }
}

void Path_PlayAnim(int id, int anim)
{
    switch (anim)
    {
    case 1:  Sprite_Play(id, "Walk", "Idle"); break;
    case 2:  Sprite_Play(id, "Run", "Walk");  break;
    case 3:  Sprite_Play(id, "Jump", "Walk"); break;
    default: Sprite_Play(id, "Idle", nullptr); break;
    }
}