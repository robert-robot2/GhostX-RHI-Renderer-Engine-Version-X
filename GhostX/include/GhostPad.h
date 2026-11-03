#pragma once
// GhostPad.h: XInput gamepad. Polled once per frame from GhostInput::Sample.
#include "GhostGameAPI.h"

void  GhostPad_Poll(bool enabled);                    // enabled = the viewport / player has focus and the game is playing
void  GhostPad_EndStep();                             // after every fixed step: clears pressed / released
void  GhostPad_Shutdown();                            // stops rumble

bool  GhostPad_Connected();
bool  GhostPad_ButtonDown(int button);                // GHOST_PAD_*
bool  GhostPad_ButtonPressed(int button);
bool  GhostPad_ButtonReleased(int button);
float GhostPad_Axis(int axis);                        // GHOST_PAD_AXIS_*: sticks -1..1, triggers 0..1, deadzone applied
bool  GhostPad_AsKey(int vk);                         // does the pad currently act as this virtual key?
void  GhostPad_NoteKeyboard();                        // a real key was pressed: hints go back to keyboard
bool  GhostPad_PadActive();                           // true if the pad was the last device used

void  GhostPad_Rumble(float strong, float weak, float seconds);
void  GhostPad_SetSettings(float deadzone, bool rumble, int slot);   // slot -1 = first connected, 0..3 = that pad

// slot -1 = auto (XInput, then generic), 0..3 = that XInput pad, 4 = generic pad only
bool  GhostPad_Probe(int& slot, int& buttons, float* axes6, int* raw = nullptr);
void  GhostPad_TestRumble(bool on);