#include "../Game.h"


// Bindings.cpp

static const char* kLabel[ACT_COUNT] = { "Move Left", "Move Right", "Move Forward", "Move Back", "Sprint / Hold Shell", "Jump", "Attack" };
static const int   kDefault[ACT_COUNT] = { 'A', 'D', 'W', 'S', VK_SHIFT, VK_SPACE, VK_LBUTTON };
static int s_key[ACT_COUNT];

static int    s_listen = -1;          // action being rebound, -1 = not listening
static char   s_msg[64] = "";
static double s_msgUntil = 0.0;

static void Say(const char* m) { strcpy_s(s_msg, m); s_msgUntil = G->GetTime() + 2.0; }

// Arrow keys always move too (Left/Right only, as before: Up/Down stay fireball aim / crouch)
static int Alias(int act) { return act == ACT_LEFT ? VK_LEFT : (act == ACT_RIGHT ? VK_RIGHT : 0); }

// Keys that can never be bound: they have fixed jobs elsewhere
static bool Reserved(int vk)
{
    switch (vk)
    {
    case VK_ESCAPE: case VK_F10: case VK_BACK:
    case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN:
    case 'Z': case 'F': case 'Q': case 'E':
        return true;
    }
    return false;
}

// Which key codes the "press a key" scan looks at
static bool Scannable(int vk)
{
    if (vk >= 0xA0 && vk <= 0xA5) return false;   // left/right Shift/Ctrl/Alt: we store the generic VK_SHIFT etc.
    if (vk >= 0x5B && vk <= 0x5D) return false;   // Windows keys, Apps
    if (vk >= 0xC1 && vk <= 0xDA) return false;   // gamepad key codes
    return true;
}

static void Defaults() { for (int i = 0; i < ACT_COUNT; i++) s_key[i] = kDefault[i]; }

// ---- save / load: %APPDATA%\AdventuresOfCube\keybinds.txt ----
static bool CfgPath(char* out, size_t n)
{
    char base[MAX_PATH];
    if (!GetEnvironmentVariableA("APPDATA", base, sizeof base)) return false;
    snprintf(out, n, "%s\\AdventuresOfCube", base);
    CreateDirectoryA(out, nullptr);
    strcat_s(out, n, "\\keybinds.txt");
    return true;
}

static void Save()
{
    char p[MAX_PATH];
    if (!CfgPath(p, sizeof p)) return;
    FILE* f = nullptr;
    if (fopen_s(&f, p, "w") != 0 || !f) return;
    for (int i = 0; i < ACT_COUNT; i++) fprintf(f, "%d\n", s_key[i]);
    fclose(f);
}

static void Load()
{
    char p[MAX_PATH];
    if (!CfgPath(p, sizeof p)) return;
    FILE* f = nullptr;
    if (fopen_s(&f, p, "r") != 0 || !f) return;
    int t[ACT_COUNT];
    bool ok = true;
    for (int i = 0; i < ACT_COUNT && ok; i++)
    {
        if (fscanf_s(f, "%d", &t[i]) != 1) { ok = false; break; }
        if (!(t[i] == VK_LBUTTON || (t[i] >= 8 && t[i] < 255 && !Reserved(t[i])))) ok = false;
        for (int j = 0; j < i && ok; j++) if (t[j] == t[i]) ok = false;   // no duplicates
    }
    fclose(f);
    if (ok) for (int i = 0; i < ACT_COUNT; i++) s_key[i] = t[i];
}

// =====================  GAMEPAD BINDINGS  =====================
static const char* kPadBtn[GHOST_PAD_BUTTON_COUNT] =
{ "A", "B", "X", "Y", "L1", "R1", "Select", "Start", "L3", "R3", "D-Pad Up", "D-Pad Down", "D-Pad Left", "D-Pad Right" };
// order: Left, Right, Forward, Back, Sprint, Jump, Attack
static const int kPadDefault[ACT_COUNT] = { GHOST_PAD_LEFT, GHOST_PAD_RIGHT, GHOST_PAD_UP, GHOST_PAD_DOWN, GHOST_PAD_X, GHOST_PAD_A, GHOST_PAD_B };
static int s_pad[ACT_COUNT];
static int s_padListen = -1;

// L1 = pipe, R1 = view switch, Start = pause: the game reads their synthesized keys (F, Z, F10)
static bool PadReserved(int b) { return b == GHOST_PAD_LB || b == GHOST_PAD_RB || b == GHOST_PAD_START; }
static bool PadValid(int b) { return b >= 0 && b < GHOST_PAD_BUTTON_COUNT && !PadReserved(b); }

static void PadDefaults() { for (int i = 0; i < ACT_COUNT; i++) s_pad[i] = kPadDefault[i]; }

static bool PadPath(char* out, size_t n)
{
    char base[MAX_PATH];
    if (!GetEnvironmentVariableA("APPDATA", base, sizeof base)) return false;
    snprintf(out, n, "%s\\AdventuresOfCube", base);
    CreateDirectoryA(out, nullptr);
    strcat_s(out, n, "\\padbinds.txt");
    return true;
}

static void PadSave()
{
    char p[MAX_PATH];
    if (!PadPath(p, sizeof p)) return;
    FILE* f = nullptr;
    if (fopen_s(&f, p, "w") != 0 || !f) return;
    for (int i = 0; i < ACT_COUNT; i++) fprintf(f, "%d\n", s_pad[i]);
    fclose(f);
}

static void PadLoad()
{
    char p[MAX_PATH];
    if (!PadPath(p, sizeof p)) return;
    FILE* f = nullptr;
    if (fopen_s(&f, p, "r") != 0 || !f) return;
    int t[ACT_COUNT];
    bool ok = true;
    for (int i = 0; i < ACT_COUNT && ok; i++)
    {
        if (fscanf_s(f, "%d", &t[i]) != 1 || !PadValid(t[i])) { ok = false; break; }
        for (int j = 0; j < i && ok; j++) if (t[j] == t[i]) ok = false;
    }
    fclose(f);
    if (ok) for (int i = 0; i < ACT_COUNT; i++) s_pad[i] = t[i];
}

// Pad buttons / sticks currently held. While true, any key the engine "presses" is the pad talking.
static bool PadBusy()
{
    if (!G->PadConnected()) return false;
    for (int b = 0; b < GHOST_PAD_BUTTON_COUNT; b++) if (G->PadButtonDown(b)) return true;
    return fabsf(G->PadAxis(GHOST_PAD_AXIS_LX)) > 0.4f || fabsf(G->PadAxis(GHOST_PAD_AXIS_LY)) > 0.4f ||
        fabsf(G->PadAxis(GHOST_PAD_AXIS_RX)) > 0.4f;
}

static bool PadMode() { return G->PadConnected() && G->PadActive(); }

static int PadAssign(int act, int b)
{
    int swapped = -1;
    for (int i = 0; i < ACT_COUNT; i++)
        if (i != act && s_pad[i] == b) { s_pad[i] = s_pad[act]; swapped = i; break; }
    s_pad[act] = b;
    PadSave();
    return swapped;
}

void Bind_Init() { Defaults(); Load(); PadDefaults(); PadLoad(); s_listen = -1; s_padListen = -1; }
void Bind_Reset() { Defaults(); Save(); }

bool Bind_Down(int a)
{
    if (PadMode()) return G->PadButtonDown(s_pad[a]) != 0;
    return G->KeyDown(s_key[a]) || (Alias(a) && G->KeyDown(Alias(a)));
}
bool Bind_Pressed(int a)
{
    if (PadMode()) return G->PadButtonPressed(s_pad[a]) != 0;
    return G->KeyPressed(s_key[a]) || (Alias(a) && G->KeyPressed(Alias(a)));
}
bool Bind_Released(int a)
{
    if (PadMode()) return G->PadButtonReleased(s_pad[a]) != 0;
    return G->KeyReleased(s_key[a]) || (Alias(a) && G->KeyReleased(Alias(a)));
}

// Gives vk to act. If another action already had it, that action gets this one's old key (swap). Returns the swapped action or -1.
static int Assign(int act, int vk)
{
    int swapped = -1;
    for (int i = 0; i < ACT_COUNT; i++)
        if (i != act && s_key[i] == vk) { s_key[i] = s_key[act]; swapped = i; break; }
    s_key[act] = vk;
    Save();
    return swapped;
}

// ---- display names ----
static void KeyName(int vk, char* out, int n)
{
    if (vk == VK_LBUTTON) { strcpy_s(out, n, "Left Mouse"); return; }
    LONG sc = (LONG)MapVirtualKeyA((UINT)vk, MAPVK_VK_TO_VSC) << 16;
    switch (vk)
    {
    case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN: case VK_INSERT: case VK_DELETE:
    case VK_HOME: case VK_END: case VK_PRIOR: case VK_NEXT: case VK_DIVIDE: case VK_NUMLOCK:
        sc |= (1 << 24); break;                    // extended-key bit
    }
    if (!GetKeyNameTextA(sc, out, n) || !out[0]) snprintf(out, n, "Key %d", vk);
}
void Bind_Name(int act, char* out, int n) { KeyName(s_key[act], out, n); }

// ---- the Keyboard tab ----
bool Keys_Listening() { return s_listen >= 0; }
void Keys_CancelListen() { s_listen = -1; }

void Keys_Update(int ui)
{
    char row[16], name[32], line[96];

    // 1) If we were already listening (from an earlier frame), look for the key
    if (s_listen >= 0)
    {
        if (G->KeyPressed(VK_ESCAPE)) s_listen = -1;
        else if (!PadBusy())
        {
            for (int vk = 8; vk < 255; vk++)       // starts at 8: skips the mouse buttons so the row click can't bind itself
            {
                if (!Scannable(vk) || !G->KeyPressed(vk)) continue;
                if (Reserved(vk)) { Say("That key is reserved"); break; }   // keep listening
                const int sw = Assign(s_listen, vk);
                if (sw >= 0) { snprintf(line, sizeof line, "Swapped with %s", kLabel[sw]); Say(line); }
                s_listen = -1;
                break;
            }
        }
    }

    // 2) Row clicks start listening (the next frame's scan picks up the key)
    for (int i = 0; i < ACT_COUNT; i++)
    {
        snprintf(row, sizeof row, "KeyBind%d", i);
        if (G->UIButtonClicked(ui, row)) { s_listen = i; s_msg[0] = 0; }
    }
    if (G->UIButtonClicked(ui, "KeyReset")) { Bind_Reset(); s_listen = -1; Say("Keys reset to default"); }

    // 3) Labels
    for (int i = 0; i < ACT_COUNT; i++)
    {
        snprintf(row, sizeof row, "KeyBind%d", i);
        if (i == s_listen) snprintf(line, sizeof line, "%s:  press a key...", kLabel[i]);
        else { Bind_Name(i, name, sizeof name); snprintf(line, sizeof line, "%s:  %s", kLabel[i], name); }
        G->UISetText(ui, row, line);
    }

    // 4) Hint line
    if (s_listen >= 0)
    {
        snprintf(line, sizeof line, "Press a key for %s.  Esc cancels.", kLabel[s_listen]);
        G->UISetText(ui, "KeyHint", line);
    }
    else if (s_msg[0] && G->GetTime() < s_msgUntil) G->UISetText(ui, "KeyHint", s_msg);
    else G->UISetText(ui, "KeyHint", "Click a row, then press the new key. Esc cancels. Arrow keys always move too.");
}


// ---- the Gamepad tab ----
void Pad_Name(int act, char* out, int n) { strcpy_s(out, n, kPadBtn[s_pad[act]]); }
bool Pad_Listening() { return s_padListen >= 0; }
void Pad_CancelListen() { s_padListen = -1; }

void Pad_Update(int ui)
{
    char row[16], name[32], line[96];

    if (s_padListen >= 0)
    {
        if (G->KeyPressed(VK_ESCAPE)) s_padListen = -1;
        else
        {
            for (int b = 0; b < GHOST_PAD_BUTTON_COUNT; b++)
            {
                if (!G->PadButtonPressed(b)) continue;
                if (PadReserved(b)) { Say("That button is reserved"); break; }   // keep listening
                const int sw = PadAssign(s_padListen, b);
                if (sw >= 0) { snprintf(line, sizeof line, "Swapped with %s", kLabel[sw]); Say(line); }
                s_padListen = -1;
                break;
            }
        }
    }

    for (int i = 0; i < ACT_COUNT; i++)
    {
        snprintf(row, sizeof row, "PadBind%d", i);
        if (!G->UIButtonClicked(ui, row)) continue;
        if (!G->PadConnected()) { Say("No gamepad connected"); continue; }
        s_padListen = i; s_msg[0] = 0;
    }
    if (G->UIButtonClicked(ui, "PadReset")) { PadDefaults(); PadSave(); s_padListen = -1; Say("Pad reset to default"); }

    for (int i = 0; i < ACT_COUNT; i++)
    {
        snprintf(row, sizeof row, "PadBind%d", i);
        if (i == s_padListen) snprintf(line, sizeof line, "%s:  press a button...", kLabel[i]);
        else { Pad_Name(i, name, sizeof name); snprintf(line, sizeof line, "%s:  %s", kLabel[i], name); }
        G->UISetText(ui, row, line);
    }

    if (s_padListen >= 0)
    {
        snprintf(line, sizeof line, "Press a button for %s.  Esc cancels.", kLabel[s_padListen]);
        G->UISetText(ui, "PadStatus", line);
    }
    else if (s_msg[0] && G->GetTime() < s_msgUntil) G->UISetText(ui, "PadStatus", s_msg);
    else G->UISetText(ui, "PadStatus", "Click a row, then press a pad button. Esc cancels. Left stick always moves too.");
}