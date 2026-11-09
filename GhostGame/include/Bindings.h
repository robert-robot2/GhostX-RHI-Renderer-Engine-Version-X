#pragma once
// Bindings.h: rebindable keyboard actions

enum Action { ACT_LEFT = 0, ACT_RIGHT, ACT_FORWARD, ACT_BACK, ACT_SPRINT, ACT_JUMP, ACT_ATTACK, ACT_COUNT };

void Bind_Init();                         // defaults, then load the saved file
void Bind_Reset();                        // back to defaults (and saves)
bool Bind_Down(int act);
bool Bind_Pressed(int act);
bool Bind_Released(int act);
void Bind_Name(int act, char* out, int n);   // "A", "Shift", "Left Mouse"...

// Options screen, Keyboard tab
void Keys_Update(int ui);                 // call every frame while the Keyboard tab is showing
bool Keys_Listening();                    // true while waiting for a key
void Keys_CancelListen();