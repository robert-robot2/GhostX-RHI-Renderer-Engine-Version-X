#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../include/GhostPad.h"
#include <Windows.h>
#include <Xinput.h>
#include <math.h>
#include <string.h>

#pragma comment(lib, "xinput.lib")                    // so no project setting is needed

static float s_dead = 0.22f, s_trigger = 0.35f;
static bool  s_rumbleOn = true;
static int   s_cfgSlot = -1, s_active = -1, s_probeSlot = -1;
static ULONGLONG s_nextScan = 0, s_probeScan = 0, s_rumbleUntil = 0;
static bool  s_rumbling = false, s_padLast = false;

static bool  s_down[GHOST_PAD_BUTTON_COUNT], s_pressed[GHOST_PAD_BUTTON_COUNT], s_released[GHOST_PAD_BUTTON_COUNT];
static float s_axis[GHOST_PAD_AXIS_COUNT];
static bool  s_key[256];

// Radial deadzone: the stick starts at 0 right after the deadzone and still reaches 1
static void Stick(SHORT x, SHORT y, float dz, float& ox, float& oy)
{
    const float fx = x / 32767.0f, fy = y / 32767.0f;
    const float m = sqrtf(fx * fx + fy * fy);
    if (m <= dz) { ox = oy = 0.0f; return; }
    const float k = (fminf(m, 1.0f) - dz) / (1.0f - dz) / m;
    ox = fx * k; oy = fy * k;
}

static void Decode(const XINPUT_GAMEPAD& g, bool* dn, float* ax)
{
    const WORD b = g.wButtons;
    dn[GHOST_PAD_A] = (b & XINPUT_GAMEPAD_A) != 0;
    dn[GHOST_PAD_B] = (b & XINPUT_GAMEPAD_B) != 0;
    dn[GHOST_PAD_X] = (b & XINPUT_GAMEPAD_X) != 0;
    dn[GHOST_PAD_Y] = (b & XINPUT_GAMEPAD_Y) != 0;
    dn[GHOST_PAD_LB] = (b & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0;
    dn[GHOST_PAD_RB] = (b & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0;
    dn[GHOST_PAD_BACK] = (b & XINPUT_GAMEPAD_BACK) != 0;
    dn[GHOST_PAD_START] = (b & XINPUT_GAMEPAD_START) != 0;
    dn[GHOST_PAD_LSTICK] = (b & XINPUT_GAMEPAD_LEFT_THUMB) != 0;
    dn[GHOST_PAD_RSTICK] = (b & XINPUT_GAMEPAD_RIGHT_THUMB) != 0;
    dn[GHOST_PAD_UP] = (b & XINPUT_GAMEPAD_DPAD_UP) != 0;
    dn[GHOST_PAD_DOWN] = (b & XINPUT_GAMEPAD_DPAD_DOWN) != 0;
    dn[GHOST_PAD_LEFT] = (b & XINPUT_GAMEPAD_DPAD_LEFT) != 0;
    dn[GHOST_PAD_RIGHT] = (b & XINPUT_GAMEPAD_DPAD_RIGHT) != 0;

    Stick(g.sThumbLX, g.sThumbLY, s_dead, ax[GHOST_PAD_AXIS_LX], ax[GHOST_PAD_AXIS_LY]);
    Stick(g.sThumbRX, g.sThumbRY, s_dead, ax[GHOST_PAD_AXIS_RX], ax[GHOST_PAD_AXIS_RY]);
    const float lt = g.bLeftTrigger / 255.0f, rt = g.bRightTrigger / 255.0f;
    ax[GHOST_PAD_AXIS_LT] = lt < 0.05f ? 0.0f : lt;
    ax[GHOST_PAD_AXIS_RT] = rt < 0.05f ? 0.0f : rt;
}

static void SetMotors(int slot, float strong, float weak)
{
    XINPUT_VIBRATION v;
    v.wLeftMotorSpeed = (WORD)(fminf(fmaxf(strong, 0.0f), 1.0f) * 65535.0f);
    v.wRightMotorSpeed = (WORD)(fminf(fmaxf(weak, 0.0f), 1.0f) * 65535.0f);
    XInputSetState((DWORD)slot, &v);
}

static void StopRumble()
{
    if (s_rumbling && s_active >= 0) SetMotors(s_active, 0.0f, 0.0f);
    s_rumbling = false;
}

// Reads the active pad. Disconnected slots are only re-scanned once a second (polling them every frame is slow).
static bool Acquire(XINPUT_STATE& st, ULONGLONG now)
{
    if (s_active >= 0)
    {
        if (XInputGetState((DWORD)s_active, &st) == ERROR_SUCCESS) return true;
        s_active = -1; s_rumbling = false; s_nextScan = now + 1000;
        return false;
    }
    if (now < s_nextScan) return false;
    s_nextScan = now + 1000;
    const int from = s_cfgSlot >= 0 ? s_cfgSlot : 0, to = s_cfgSlot >= 0 ? s_cfgSlot : 3;
    for (int i = from; i <= to; i++)
        if (XInputGetState((DWORD)i, &st) == ERROR_SUCCESS) { s_active = i; return true; }
    return false;
}

void GhostPad_Poll(bool enabled)
{
    const ULONGLONG now = GetTickCount64();
    memset(s_key, 0, sizeof s_key);

    if (!enabled)                                      // no focus, or not playing: the pad is silent
    {
        StopRumble();
        memset(s_down, 0, sizeof s_down); memset(s_pressed, 0, sizeof s_pressed);
        memset(s_released, 0, sizeof s_released); memset(s_axis, 0, sizeof s_axis);
        return;
    }
    if (s_rumbling && now >= s_rumbleUntil) StopRumble();

    bool dn[GHOST_PAD_BUTTON_COUNT] = {};
    float ax[GHOST_PAD_AXIS_COUNT] = {};
    XINPUT_STATE st = {};
    if (Acquire(st, now)) Decode(st.Gamepad, dn, ax);

    for (int i = 0; i < GHOST_PAD_BUTTON_COUNT; i++)
    {
        if (dn[i] && !s_down[i]) { s_pressed[i] = true; s_padLast = true; }
        if (!dn[i] && s_down[i]) s_released[i] = true;
        s_down[i] = dn[i];
    }
    memcpy(s_axis, ax, sizeof s_axis);
    if (ax[0] * ax[0] + ax[1] * ax[1] > 0.25f || ax[2] * ax[2] + ax[3] * ax[3] > 0.25f) s_padLast = true;

    // ---- The button layout: change it here and nowhere else ----
    const float th = 0.45f;                            // stick -> digital key threshold
    auto K = [&](int vk, bool v) { if (v) s_key[vk] = true; };
    K('W', ax[GHOST_PAD_AXIS_LY] > th || dn[GHOST_PAD_UP]);
    K('S', ax[GHOST_PAD_AXIS_LY] < -th || dn[GHOST_PAD_DOWN]);
    K('A', ax[GHOST_PAD_AXIS_LX] < -th || dn[GHOST_PAD_LEFT]);
    K('D', ax[GHOST_PAD_AXIS_LX] > th || dn[GHOST_PAD_RIGHT]);
    K(VK_SPACE, dn[GHOST_PAD_A]);                                                   // jump
    K(VK_SHIFT, dn[GHOST_PAD_X] || ax[GHOST_PAD_AXIS_LT] > s_trigger);              // run / grab shell
    K(VK_LBUTTON, dn[GHOST_PAD_B] || dn[GHOST_PAD_Y] || ax[GHOST_PAD_AXIS_RT] > s_trigger);   // attack
    K('F', dn[GHOST_PAD_RB]);                                                       // interact / portal
    K(VK_F10, dn[GHOST_PAD_START]);                                                 // options menu
    K('Q', ax[GHOST_PAD_AXIS_RX] < -th);                                            // free camera turn
    K('E', ax[GHOST_PAD_AXIS_RX] > th);
}

void GhostPad_EndStep() { memset(s_pressed, 0, sizeof s_pressed); memset(s_released, 0, sizeof s_released); }
void GhostPad_Shutdown() { StopRumble(); }

bool  GhostPad_Connected() { return s_active >= 0; }
bool  GhostPad_ButtonDown(int b) { return b >= 0 && b < GHOST_PAD_BUTTON_COUNT && s_down[b]; }
bool  GhostPad_ButtonPressed(int b) { return b >= 0 && b < GHOST_PAD_BUTTON_COUNT && s_pressed[b]; }
bool  GhostPad_ButtonReleased(int b) { return b >= 0 && b < GHOST_PAD_BUTTON_COUNT && s_released[b]; }
float GhostPad_Axis(int a) { return (a >= 0 && a < GHOST_PAD_AXIS_COUNT) ? s_axis[a] : 0.0f; }
bool  GhostPad_AsKey(int vk) { return vk > 0 && vk < 256 && s_key[vk]; }
void  GhostPad_NoteKeyboard() { s_padLast = false; }
bool  GhostPad_PadActive() { return s_padLast; }

void GhostPad_Rumble(float strong, float weak, float seconds)
{
    if (!s_rumbleOn || s_active < 0 || seconds <= 0.0f) return;
    SetMotors(s_active, strong, weak);
    s_rumbling = true;
    s_rumbleUntil = GetTickCount64() + (ULONGLONG)(seconds * 1000.0f);
}

void GhostPad_SetSettings(float deadzone, bool rumble, int slot)
{
    s_dead = deadzone != deadzone ? 0.22f : (deadzone < 0.05f ? 0.05f : (deadzone > 0.6f ? 0.6f : deadzone));
    s_rumbleOn = rumble;
    if (!rumble) StopRumble();
    slot = slot < -1 ? -1 : (slot > 3 ? 3 : slot);
    if (slot != s_cfgSlot)                             // switch pads: rescan right away
    {
        StopRumble();
        s_cfgSlot = slot; s_active = -1; s_nextScan = 0; s_probeSlot = -1; s_probeScan = 0;
    }
}

bool GhostPad_Probe(int& slot, int& buttons, float* axes6)
{
    XINPUT_STATE st = {};
    const ULONGLONG now = GetTickCount64();
    bool ok = false;
    if (s_probeSlot >= 0)
    {
        ok = XInputGetState((DWORD)s_probeSlot, &st) == ERROR_SUCCESS;
        if (!ok) { s_probeSlot = -1; s_probeScan = now + 500; }
    }
    else if (now >= s_probeScan)
    {
        s_probeScan = now + 500;
        const int from = s_cfgSlot >= 0 ? s_cfgSlot : 0, to = s_cfgSlot >= 0 ? s_cfgSlot : 3;
        for (int i = from; i <= to && !ok; i++)
            if (XInputGetState((DWORD)i, &st) == ERROR_SUCCESS) { ok = true; s_probeSlot = i; }
    }
    if (!ok) return false;

    bool dn[GHOST_PAD_BUTTON_COUNT] = {};
    float ax[GHOST_PAD_AXIS_COUNT] = {};
    Decode(st.Gamepad, dn, ax);
    slot = s_probeSlot;
    buttons = 0;
    for (int i = 0; i < GHOST_PAD_BUTTON_COUNT; i++) if (dn[i]) buttons |= (1 << i);
    if (axes6) memcpy(axes6, ax, sizeof ax);
    return true;
}

void GhostPad_TestRumble(bool on)
{
    if (s_probeSlot < 0) return;
    SetMotors(s_probeSlot, on ? 0.8f : 0.0f, on ? 0.8f : 0.0f);
}