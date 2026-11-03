#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../include/GhostCustomGamepad.h"
#include <mmsystem.h>
#include <string.h>

#pragma comment(lib, "winmm.lib")

// ---- Button layout: physical button number (0-based) for each GHOST_PAD_* button, -1 = none ----
// Logitech Dual Action. If a button does the wrong thing, change its number here and nowhere else.
static const int kBtnMap[GHOST_PAD_BUTTON_COUNT] =
{
    1,   // A      (bottom)
    2,   // B      (right)
    0,   // X      (left)
    3,   // Y      (top)
    4,   // LB     (L1)
    5,   // RB     (R1)
    8,   // BACK
    9,   // START
    10,  // LSTICK (click)
    11,  // RSTICK (click)
    -1, -1, -1, -1                    // D-pad: read from the hat
};
static const int kLTButton = 6;       // L2 is a digital button on this pad
static const int kRTButton = 7;       // R2

static int       s_id = -1;
static JOYCAPSW  s_caps = {};
static ULONGLONG s_next = 0;

static bool Scan(ULONGLONG now)
{
    s_next = now + 1000;
    const UINT n = joyGetNumDevs();
    for (UINT i = 0; i < n && i < 16; i++)
    {
        JOYINFOEX ji = {};
        ji.dwSize = sizeof ji; ji.dwFlags = JOY_RETURNALL;
        if (joyGetPosEx(i, &ji) != JOYERR_NOERROR) continue;          // nothing plugged into this id
        JOYCAPSW c = {};
        if (joyGetDevCapsW(i, &c, sizeof c) != JOYERR_NOERROR) continue;
        if (c.wNumAxes < 2 || c.wNumButtons < 4) continue;            // not a gamepad
        s_id = (int)i; s_caps = c;
        return true;
    }
    return false;
}

static float Norm(DWORD v, UINT lo, UINT hi)
{
    if (hi <= lo) return 0.0f;
    const float f = ((float)v - (float)lo) / (float)(hi - lo) * 2.0f - 1.0f;
    return f < -1.0f ? -1.0f : (f > 1.0f ? 1.0f : f);
}

void GhostCustomPad_Reset() { s_id = -1; s_next = 0; }

bool GhostCustomPad_Read(GhostRawPad& o, ULONGLONG now)
{
    if (s_id < 0 && (now < s_next || !Scan(now))) return false;

    JOYINFOEX ji = {};
    ji.dwSize = sizeof ji; ji.dwFlags = JOY_RETURNALL;
    if (joyGetPosEx((UINT)s_id, &ji) != JOYERR_NOERROR)               // unplugged
    {
        s_id = -1; s_next = now + 1000;
        return false;
    }

    memset(&o, 0, sizeof o);
    o.id = s_id;
    o.rawButtons = (int)ji.dwButtons;
    WideCharToMultiByte(CP_UTF8, 0, s_caps.szPname, -1, o.name, (int)sizeof o.name, nullptr, nullptr);

    for (int i = 0; i < GHOST_PAD_BUTTON_COUNT; i++)
        if (kBtnMap[i] >= 0) o.btn[i] = ((ji.dwButtons >> kBtnMap[i]) & 1) != 0;

    if ((s_caps.wCaps & JOYCAPS_HASPOV) && ji.dwPOV < 36000)           // 0 = up, 9000 = right, 18000 = down, 27000 = left
    {
        const DWORD p = ji.dwPOV;
        o.btn[GHOST_PAD_UP] = p >= 31500 || p <= 4500;
        o.btn[GHOST_PAD_RIGHT] = p >= 4500 && p <= 13500;
        o.btn[GHOST_PAD_DOWN] = p >= 13500 && p <= 22500;
        o.btn[GHOST_PAD_LEFT] = p >= 22500 && p <= 31500;
    }

    o.axis[GHOST_PAD_AXIS_LX] = Norm(ji.dwXpos, s_caps.wXmin, s_caps.wXmax);
    o.axis[GHOST_PAD_AXIS_LY] = -Norm(ji.dwYpos, s_caps.wYmin, s_caps.wYmax);   // winmm Y grows downward
    if (s_caps.wCaps & JOYCAPS_HASZ) o.axis[GHOST_PAD_AXIS_RX] = Norm(ji.dwZpos, s_caps.wZmin, s_caps.wZmax);
    if (s_caps.wCaps & JOYCAPS_HASR) o.axis[GHOST_PAD_AXIS_RY] = -Norm(ji.dwRpos, s_caps.wRmin, s_caps.wRmax);
    if (kLTButton >= 0 && ((ji.dwButtons >> kLTButton) & 1)) o.axis[GHOST_PAD_AXIS_LT] = 1.0f;
    if (kRTButton >= 0 && ((ji.dwButtons >> kRTButton) & 1)) o.axis[GHOST_PAD_AXIS_RT] = 1.0f;
    return true;
}