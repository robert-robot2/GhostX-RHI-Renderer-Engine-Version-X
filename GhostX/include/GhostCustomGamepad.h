#pragma once
// GhostCustomGamepad.h: generic (non-XInput) USB pads through the Windows joystick API.
// Works for Logitech Dual Action and most HID pads. GhostPad.cpp calls it when no XInput pad is found.
#include <Windows.h>
#include "GhostGameAPI.h"

struct GhostRawPad
{
    bool  btn[GHOST_PAD_BUTTON_COUNT];   // already mapped to GHOST_PAD_* (D-pad comes from the hat)
    float axis[GHOST_PAD_AXIS_COUNT];    // -1..1, up = +, NO deadzone (GhostPad applies it)
    int   rawButtons;                    // bit n = physical button n + 1 (as shown in joy.cpl)
    int   id;                            // winmm joystick id
    char  name[64];
};

bool GhostCustomPad_Read(GhostRawPad& out, ULONGLONG nowMs);   // false = no pad (rescans once a second)
void GhostCustomPad_Reset();                                   // forget the pad and look again