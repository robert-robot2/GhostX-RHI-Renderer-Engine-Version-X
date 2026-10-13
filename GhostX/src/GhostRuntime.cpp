#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../include/GhostRuntime.h"
#include <Windows.h>
#include <math.h>

void GhostInput::Sample(bool enabled)
{
    for (int k = 1; k < 256; k++)
    {
        bool now = enabled && (GetAsyncKeyState(k) & 0x8000) != 0;
        if (now && !prev[k]) pressed[k] = true;
        if (!now && prev[k]) released[k] = true;
        down[k] = now;
        prev[k] = now;
    }
}

void GhostInput::EndStep()
{
    memset(pressed, 0, sizeof pressed);
    memset(released, 0, sizeof released);
}

void GhostRuntime_TestBegin(const GhostScene& s, GhostTestState& t)
{
    t = GhostTestState();
    if (!s.objects.empty())
    {
        t.objectId = s.objects.front().id;
        t.groundY = s.objects.front().position.y;
    }
}

void GhostRuntime_TestStep(GhostScene& s, const GhostInput& in, GhostTestState& t, float dt)
{
    GhostObject* o = nullptr;
    for (GhostObject& ob : s.objects) if (ob.id == t.objectId) o = &ob;
    if (!o) return;

    float dx = 0.0f, dz = 0.0f;
    if (in.down['W']) dz += 1.0f;
    if (in.down['S']) dz -= 1.0f;
    if (in.down['D']) dx += 1.0f;
    if (in.down['A']) dx -= 1.0f;
    float len = sqrtf(dx * dx + dz * dz);
    if (len > 0.0f) { dx /= len; dz /= len; }

    const float speed = in.down[VK_SHIFT] ? 6.0f : 3.0f;
    o->position.x += dx * speed * dt;
    o->position.z += dz * speed * dt;

    if (t.grounded && in.pressed[VK_SPACE]) { t.vy = 6.0f; t.grounded = false; }
    if (!t.grounded)
    {
        t.vy -= 18.0f * dt;
        o->position.y += t.vy * dt;
        if (o->position.y <= t.groundY) { o->position.y = t.groundY; t.vy = 0.0f; t.grounded = true; }
    }
}