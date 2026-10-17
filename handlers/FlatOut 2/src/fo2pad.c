/*
 * fo2pad - dinput8.dll replacement for FlatOut 2 split-screen (Nucleus Co-op).
 *
 * FlatOut 2 only reads controllers through DirectInput 8. Windows only delivers DirectInput data from
 * Xbox One / Series pads to the window that is really in front, so with several game windows only the
 * focused one gets input. XInput has no such restriction.
 *
 * This DLL sits in one game instance's folder and makes that instance see exactly ONE gamepad, fed from
 * a single XInput slot (fo2pad.ini: [Pad] Slot=1..4, 0 = no pad). The pad mimics how an Xbox controller
 * looks to DirectInput (5 axes X/Y/Z/Rx/Ry with both triggers on Z, 10 buttons, 1 POV), so FlatOut's
 * existing Xbox pad bindings keep working. Its product GUID is taken from Savegame\options.cfg
 * (the pad chosen in FlatOut's controller options), so FlatOut selects it automatically.
 * Keyboard/mouse requests are passed to the real dinput8.dll; real game controllers are hidden.
 *
 * It also patches a few of FlatOut2.exe's own imports (nothing outside this game process is touched):
 *  - LAN between instances on one PC ([Net] Address=127.0.0.N): FlatOut uses fixed ports, so each
 *    instance binds its sockets to its own loopback address, reports that address as its host address,
 *    and reports a slightly different MAC address (the game ignores packets from its own MAC).
 *  - Menus with the pad ([Menu] Keys=1): FlatOut's menus only read the keyboard, through a WH_KEYBOARD
 *    hook it installs before loading dinput8.dll. The pad is fed to that hook procedure as keys
 *    (D-pad = arrows, Start = Escape; A = Enter, B = Escape and left stick = arrows only while no
 *    trigger was pressed recently, i.e. not while racing), so every window works without focus.
 *
 * Build (32-bit, Tiny C Compiler 0.9.27):  python build.py <tcc.exe>
 * (tcc decorates the stdcall export, build.py renames it to "DirectInput8Create").
 */
#include <windows.h>

/* ---------------------------------------------------------------- DirectInput definitions */

#define DI_OK                 0
#define DI_NOEFFECT           1
#define DIERR_INVALIDPARAM    ((HRESULT)0x80070057L)
#define DIERR_UNSUPPORTED     ((HRESULT)0x80004001L)
#define DIERR_OBJECTNOTFOUND  ((HRESULT)0x80070002L)
#define DIERR_DEVICENOTREG    ((HRESULT)0x80040154L)
#define DIERR_NOTBUFFERED     ((HRESULT)0x80040207L)
#define DIERR_OUTOFMEMORY     ((HRESULT)0x8007000EL)
#define E_NOINTERFACE_        ((HRESULT)0x80004002L)

#define DIDFT_RELAXIS     0x00000001
#define DIDFT_ABSAXIS     0x00000002
#define DIDFT_AXIS        0x00000003
#define DIDFT_PSHBUTTON   0x00000004
#define DIDFT_TGLBUTTON   0x00000008
#define DIDFT_BUTTON      0x0000000C
#define DIDFT_POV         0x00000010
#define DIDFT_ANYINSTANCE 0x00FFFF00
#define DIDFT_FFACTUATOR      0x01000000
#define DIDFT_FFEFFECTTRIGGER 0x02000000
#define DIDFT_OUTPUT          0x10000000
#define DIDFT_MAKEINSTANCE(n) ((DWORD)(n) << 8)
#define DIDFT_GETTYPE(n)      ((n) & 0xFF)
#define DIDFT_GETINSTANCE(n)  (((n) >> 8) & 0xFFFF)
#define DIDOI_ASPECTPOSITION  0x00000100
#define DIDOI_ASPECTMASK      0x00000F00

#define DIPH_DEVICE   0
#define DIPH_BYOFFSET 1
#define DIPH_BYID     2
#define DIPH_BYUSAGE  3

#define DI8DEVCLASS_ALL     0
#define DI8DEVCLASS_GAMECTRL 4
#define DI8DEVTYPE_JOYSTICK 0x14
#define DI8DEVTYPE_GAMEPAD  0x15
#define DIEDFL_FORCEFEEDBACK 0x00000100
#define DIDC_ATTACHED 0x00000001
#define DIGDD_PEEK 0x00000001

typedef struct { DWORD dwSize, dwHeaderSize, dwObj, dwHow; } DIPROPHEADER;
typedef struct { DIPROPHEADER diph; LONG lMin, lMax; } DIPROPRANGE;
typedef struct { DIPROPHEADER diph; DWORD dwData; } DIPROPDWORD;
typedef struct { DIPROPHEADER diph; WCHAR wsz[MAX_PATH]; } DIPROPSTRING;
typedef struct { DIPROPHEADER diph; GUID guidClass; WCHAR wszPath[MAX_PATH]; } DIPROPGUIDANDPATH;
typedef struct { const GUID *pguid; DWORD dwOfs, dwType, dwFlags; } DIOBJECTDATAFORMAT;
typedef struct { DWORD dwSize, dwObjSize, dwFlags, dwDataSize, dwNumObjs; DIOBJECTDATAFORMAT *rgodf; } DIDATAFORMAT;
typedef struct { DWORD dwOfs, dwData, dwTimeStamp, dwSequence; UINT_PTR uAppData; } DIDEVICEOBJECTDATA;

typedef struct {
    DWORD dwSize; GUID guidInstance; GUID guidProduct; DWORD dwDevType;
    CHAR tszInstanceName[MAX_PATH]; CHAR tszProductName[MAX_PATH];
    GUID guidFFDriver; WORD wUsagePage; WORD wUsage;
} DIDEVICEINSTANCEA;

typedef struct {
    DWORD dwSize; GUID guidType; DWORD dwOfs; DWORD dwType; DWORD dwFlags; CHAR tszName[MAX_PATH];
    DWORD dwFFMaxForce; DWORD dwFFForceResolution; WORD wCollectionNumber; WORD wDesignatorIndex;
    WORD wUsagePage; WORD wUsage; DWORD dwDimension; WORD wExponent; WORD wReportId;
} DIDEVICEOBJECTINSTANCEA;

typedef struct {
    DWORD dwSize, dwFlags, dwDevType, dwAxes, dwButtons, dwPOVs;
    DWORD dwFFSamplePeriod, dwFFMinTimeResolution, dwFirmwareRevision, dwHardwareRevision, dwFFDriverVersion;
} DIDEVCAPS;

typedef BOOL (CALLBACK *ENUMDEVCB)(const DIDEVICEINSTANCEA *, LPVOID);
typedef BOOL (CALLBACK *ENUMOBJCB)(const DIDEVICEOBJECTINSTANCEA *, LPVOID);

static const GUID IID_IUnknown_          = {0x00000000,0x0000,0x0000,{0xC0,0,0,0,0,0,0,0x46}};
static const GUID IID_IDirectInput8A_    = {0xBF798030,0x483A,0x4DA2,{0xAA,0x99,0x5D,0x64,0xED,0x36,0x97,0x00}};
static const GUID IID_IDirectInputDevice8A_ = {0x54D41080,0xDC15,0x4833,{0xA4,0x1B,0x74,0x8F,0x73,0xA3,0x81,0x79}};
static const GUID GUID_Joystick_ = {0x6F1D2B70,0xD5A0,0x11CF,{0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00}};
static const GUID GUID_XAxis_  = {0xA36D02E0,0xC9F3,0x11CF,{0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00}};
static const GUID GUID_YAxis_  = {0xA36D02E1,0xC9F3,0x11CF,{0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00}};
static const GUID GUID_ZAxis_  = {0xA36D02E2,0xC9F3,0x11CF,{0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00}};
static const GUID GUID_RxAxis_ = {0xA36D02F4,0xC9F3,0x11CF,{0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00}};
static const GUID GUID_RyAxis_ = {0xA36D02F5,0xC9F3,0x11CF,{0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00}};
static const GUID GUID_Button_ = {0xA36D02F0,0xC9F3,0x11CF,{0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00}};
static const GUID GUID_POV_    = {0xA36D02F2,0xC9F3,0x11CF,{0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00}};
/* Xbox 360 controller as DirectInput reports it: VID 045E / PID 028E */
static const GUID GUID_X360Product = {0x028E045E,0x0000,0x0000,{0x00,0x00,0x50,0x49,0x44,0x56,0x49,0x44}};

/* ---------------------------------------------------------------- XInput */

typedef struct { WORD wButtons; BYTE bLeftTrigger, bRightTrigger; SHORT sThumbLX, sThumbLY, sThumbRX, sThumbRY; } XINPUT_GAMEPAD_;
typedef struct { DWORD dwPacketNumber; XINPUT_GAMEPAD_ Gamepad; } XINPUT_STATE_;
typedef DWORD (WINAPI *XInputGetState_t)(DWORD, XINPUT_STATE_ *);

/* ---------------------------------------------------------------- globals */

static HINSTANCE g_hinst;
static char g_dir[MAX_PATH];           /* folder this DLL lives in, with trailing backslash */
static int g_slot = 1;                 /* XInput slot 1..4, 0 = no pad */
static int g_log = 0;
static int g_logLines = 0;
static GUID g_product;
static GUID g_instance;
static XInputGetState_t g_XInputGetState;
static HRESULT (WINAPI *g_realCreate)(HINSTANCE, DWORD, const GUID *, void **, void *);
static int g_menuKeys = 1;             /* pad -> keyboard in menus */
static int g_driveHoldMs = 1500;       /* A/B/stick stop acting as keys for this long after a trigger press */
static int g_startIsEscape = 1;        /* Start sends Escape (pause in races, back in menus) */
static int g_selfTest = 0;             /* debug: press Enter every few seconds */
static int g_remote = 0;               /* debug: press keys listed in fo2pad_remote.txt */
static int g_logPackets = 0;           /* debug: log every UDP packet the game sends */
static DWORD g_netAddr;                /* instance loopback address, network byte order, 0 = off */
static int g_netIndex;                 /* last octet of g_netAddr */
static char g_netAddrStr[16];
static volatile DWORD g_lastPadRead;   /* GetTickCount() of the game's last pad read */

static void Log(const char *fmt, ...)
{
    char buf[512], path[MAX_PATH];
    HANDLE f;
    DWORD written;
    va_list ap;
    if (!g_log || g_logLines > 3000) return;
    g_logLines++;
    va_start(ap, fmt);
    wvsprintfA(buf, fmt, ap);
    va_end(ap);
    lstrcatA(buf, "\r\n");
    lstrcpyA(path, g_dir);
    lstrcatA(path, "fo2pad.log");
    f = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, 0, NULL);
    if (f != INVALID_HANDLE_VALUE) { WriteFile(f, buf, lstrlenA(buf), &written, NULL); CloseHandle(f); }
}

static int HexVal(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* FlatOut stores the selected pad as its product GUID's 16 bytes in reverse order, as hex */
static BOOL ProductFromOptions(GUID *out)
{
    char path[MAX_PATH], buf[8192];
    const char *key = "Settings.Control.ControllerGuid";
    HANDLE f;
    DWORD n = 0;
    char *p, *q;
    BYTE bytes[16];
    int i, nonzero = 0;

    lstrcpyA(path, g_dir);
    lstrcatA(path, "Savegame\\options.cfg");
    f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return FALSE;
    ReadFile(f, buf, sizeof(buf) - 1, &n, NULL);
    CloseHandle(f);
    buf[n] = 0;
    for (p = buf; *p; p++) {
        for (i = 0; key[i] && p[i] == key[i]; i++) {}
        if (!key[i]) break;
    }
    if (!*p) return FALSE;
    for (q = p; *q && *q != '"' && *q != '\n'; q++) {}
    if (*q != '"') return FALSE;
    q++;
    for (i = 0; i < 16; i++) {
        int hi = HexVal(q[2 * i]), lo = HexVal(q[2 * i + 1]);
        if (hi < 0 || lo < 0) return FALSE;
        bytes[15 - i] = (BYTE)(hi * 16 + lo);
        if (bytes[15 - i]) nonzero = 1;
    }
    if (!nonzero) return FALSE;
    memcpy(out, bytes, 16);
    return TRUE;
}

/* Runs from DllMain: only kernel32 calls, no LoadLibrary */
static void ReadConfig(void)
{
    char path[MAX_PATH], addr[32], *slash;
    int i, a, b, c, d;

    GetModuleFileNameA(g_hinst, g_dir, MAX_PATH);
    slash = g_dir;
    for (i = 0; g_dir[i]; i++) if (g_dir[i] == '\\') slash = g_dir + i + 1;
    *slash = 0;
    lstrcpyA(path, g_dir);
    lstrcatA(path, "fo2pad.ini");
    g_slot = GetPrivateProfileIntA("Pad", "Slot", 1, path);
    g_log = GetPrivateProfileIntA("Pad", "Log", 0, path);
    g_menuKeys = GetPrivateProfileIntA("Menu", "Keys", 1, path);
    g_driveHoldMs = GetPrivateProfileIntA("Menu", "DriveHoldMs", 1500, path);
    g_startIsEscape = GetPrivateProfileIntA("Menu", "StartIsEscape", 1, path);
    g_selfTest = GetPrivateProfileIntA("Menu", "SelfTest", 0, path);
    g_remote = GetPrivateProfileIntA("Menu", "Remote", 0, path);
    g_logPackets = GetPrivateProfileIntA("Net", "LogPackets", 0, path);
    if (g_slot < 0 || g_slot > 4) g_slot = 0;

    /* [Net] Address=127.0.0.N */
    GetPrivateProfileStringA("Net", "Address", "", addr, sizeof(addr), path);
    a = b = c = d = -1;
    for (i = 0, a = 0; addr[i] >= '0' && addr[i] <= '9'; i++) a = a * 10 + addr[i] - '0';
    if (addr[0] && addr[i] == '.') {
        for (i++, b = 0; addr[i] >= '0' && addr[i] <= '9'; i++) b = b * 10 + addr[i] - '0';
        if (addr[i] == '.') for (i++, c = 0; addr[i] >= '0' && addr[i] <= '9'; i++) c = c * 10 + addr[i] - '0';
        if (addr[i] == '.') for (i++, d = 0; addr[i] >= '0' && addr[i] <= '9'; i++) d = d * 10 + addr[i] - '0';
    }
    if (a == 127 && b >= 0 && b < 256 && c >= 0 && c < 256 && d > 0 && d < 255) {
        g_netAddr = (DWORD)a | ((DWORD)b << 8) | ((DWORD)c << 16) | ((DWORD)d << 24);
        g_netIndex = d;
        wsprintfA(g_netAddrStr, "%d.%d.%d.%d", a, b, c, d);
    }

    if (!ProductFromOptions(&g_product)) g_product = GUID_X360Product;
    g_instance.Data1 = 0xF02BAD00 + g_slot;
    g_instance.Data2 = 0x464F; g_instance.Data3 = 0x3250;
    memcpy(g_instance.Data4, "XINPUTPD", 8);
}

static void Init(void)
{
    static int done = 0;
    char path[MAX_PATH], sys[MAX_PATH];
    HMODULE real, xi;
    const char *xinputs[] = { "xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll" };
    int i;

    if (done) return;
    done = 1;

    GetSystemDirectoryA(sys, MAX_PATH);
    lstrcpyA(path, sys);
    lstrcatA(path, "\\dinput8.dll");
    real = LoadLibraryA(path);
    if (real) *(FARPROC *)&g_realCreate = GetProcAddress(real, "DirectInput8Create");

    for (i = 0; i < 3 && !g_XInputGetState; i++) {
        lstrcpyA(path, sys);
        lstrcatA(path, "\\");
        lstrcatA(path, xinputs[i]);
        xi = LoadLibraryA(path);
        if (xi) *(FARPROC *)&g_XInputGetState = GetProcAddress(xi, "XInputGetState");
    }
    Log("fo2pad: slot=%d product=%08lX real dinput8=%s xinput=%s net=%s menuKeys=%d", g_slot, g_product.Data1,
        g_realCreate ? "ok" : "MISSING", g_XInputGetState ? "ok" : "MISSING", g_netAddr ? g_netAddrStr : "off", g_menuKeys);
}

/* ---------------------------------------------------------------- virtual pad objects */

#define NUM_AXES    5
#define NUM_BUTTONS 10
#define NUM_OBJS    (NUM_AXES + 1 + NUM_BUTTONS)
#define OBJ_POV     NUM_AXES

typedef struct { const GUID *guid; DWORD type; WORD usagePage, usage; } ObjDef;

static ObjDef g_objs[NUM_OBJS];

static void InitObjs(void)
{
    static const GUID *axes[NUM_AXES] = { &GUID_XAxis_, &GUID_YAxis_, &GUID_ZAxis_, &GUID_RxAxis_, &GUID_RyAxis_ };
    static const WORD usages[NUM_AXES] = { 0x30, 0x31, 0x32, 0x33, 0x34 };
    int i;
    for (i = 0; i < NUM_AXES; i++) {
        g_objs[i].guid = axes[i]; g_objs[i].type = DIDFT_ABSAXIS | DIDFT_MAKEINSTANCE(i);
        g_objs[i].usagePage = 1; g_objs[i].usage = usages[i];
    }
    g_objs[OBJ_POV].guid = &GUID_POV_; g_objs[OBJ_POV].type = DIDFT_POV | DIDFT_MAKEINSTANCE(0);
    g_objs[OBJ_POV].usagePage = 1; g_objs[OBJ_POV].usage = 0x39;
    for (i = 0; i < NUM_BUTTONS; i++) {
        ObjDef *o = &g_objs[NUM_AXES + 1 + i];
        o->guid = &GUID_Button_; o->type = DIDFT_PSHBUTTON | DIDFT_MAKEINSTANCE(i);
        o->usagePage = 9; o->usage = (WORD)(i + 1);
    }
}

static void ObjName(int i, char *out)
{
    static const char *axisNames[NUM_AXES] = { "X Axis", "Y Axis", "Z Axis", "X Rotation", "Y Rotation" };
    if (i < NUM_AXES) lstrcpyA(out, axisNames[i]);
    else if (i == OBJ_POV) lstrcpyA(out, "Hat Switch");
    else wsprintfA(out, "Button %d", i - NUM_AXES);
}

/* ---------------------------------------------------------------- device */

typedef struct Device Device;
struct Device {
    void **vtbl;
    LONG refs;
    LONG rangeMin[NUM_AXES], rangeMax[NUM_AXES];
    DWORD deadzone[NUM_AXES], saturation[NUM_AXES];
    DWORD dataSize;
    int objOfs[NUM_OBJS];              /* offset of each object in the game's data format, -1 = unmapped */
    int fmtCount;
    DWORD fmtOfs[256]; DWORD fmtType[256];   /* unmatched format entries (to fill POVs with "centered") */
    DWORD bufferSize;
    DIDEVICEOBJECTDATA events[256];
    int evCount;
    DWORD seq;
    DWORD lastVal[NUM_OBJS];
    int haveLast;
};

static int g_wroteFirstState = 0;

/* Menus don't read the pad, races do: remember when the game last read it */
static void NotePadRead(void)
{
    static DWORD windowStart, reads;
    DWORD now = GetTickCount();
    g_lastPadRead = now;
    reads++;
    if (now - windowStart >= 2000) {
        static int reports;
        if (windowStart && reports++ < 3) Log("pad reads in last %lu ms: %lu", now - windowStart, reads);
        windowStart = now;
        reads = 0;
    }
}

/* Current pad values, in the game's units, for every object */
static void Sample(Device *d, DWORD vals[NUM_OBJS])
{
    XINPUT_STATE_ st;
    double v[NUM_AXES];
    WORD b;
    int i, connected = 0;
    static const WORD btnMask[NUM_BUTTONS] = { 0x1000, 0x2000, 0x4000, 0x8000, 0x0100, 0x0200, 0x0020, 0x0010, 0x0040, 0x0080 };

    memset(&st, 0, sizeof(st));
    if (g_slot > 0 && g_XInputGetState && g_XInputGetState(g_slot - 1, &st) == 0) connected = 1;

    v[0] = st.Gamepad.sThumbLX / 32767.0;
    v[1] = -st.Gamepad.sThumbLY / 32767.0;
    v[2] = (st.Gamepad.bLeftTrigger - st.Gamepad.bRightTrigger) / 255.0;  /* both triggers share Z, like a real Xbox pad */
    v[3] = st.Gamepad.sThumbRX / 32767.0;
    v[4] = -st.Gamepad.sThumbRY / 32767.0;

    for (i = 0; i < NUM_AXES; i++) {
        double a = v[i] < 0 ? -v[i] : v[i], dz = d->deadzone[i] / 10000.0, sat = d->saturation[i] / 10000.0, out;
        if (a > 1.0) a = 1.0;
        if (sat <= dz) sat = 1.0, dz = 0.0;
        if (a <= dz) a = 0.0;
        else if (a >= sat) a = 1.0;
        else a = (a - dz) / (sat - dz);
        out = v[i] < 0 ? -a : a;
        vals[i] = (DWORD)(LONG)(d->rangeMin[i] + (out + 1.0) * 0.5 * ((double)d->rangeMax[i] - d->rangeMin[i]) + 0.5);
    }

    b = st.Gamepad.wButtons;
    {
        int up = (b & 1) != 0, down = (b & 2) != 0, left = (b & 4) != 0, right = (b & 8) != 0;
        DWORD pov = 0xFFFFFFFF;
        if (up && right) pov = 4500; else if (right && down) pov = 13500;
        else if (down && left) pov = 22500; else if (left && up) pov = 31500;
        else if (up) pov = 0; else if (right) pov = 9000; else if (down) pov = 18000; else if (left) pov = 27000;
        vals[OBJ_POV] = pov;
    }
    for (i = 0; i < NUM_BUTTONS; i++) vals[NUM_AXES + 1 + i] = (b & btnMask[i]) ? 0x80 : 0;

    if (!g_wroteFirstState) {
        g_wroteFirstState = 1;
        Log("first sample: slot %d %s, LX=%d LY=%d LT=%d RT=%d buttons=%04X", g_slot, connected ? "connected" : "NOT connected",
            st.Gamepad.sThumbLX, st.Gamepad.sThumbLY, st.Gamepad.bLeftTrigger, st.Gamepad.bRightTrigger, b);
    }
}

static void QueueChanges(Device *d, const DWORD vals[NUM_OBJS])
{
    int i;
    if (d->haveLast && d->bufferSize > 0) {
        for (i = 0; i < NUM_OBJS; i++) {
            if (vals[i] == d->lastVal[i] || d->objOfs[i] < 0) continue;
            if (d->evCount >= (int)d->bufferSize || d->evCount >= 256) {   /* overflow: drop oldest */
                memmove(&d->events[0], &d->events[1], sizeof(d->events[0]) * (d->evCount - 1));
                d->evCount--;
            }
            d->events[d->evCount].dwOfs = (DWORD)d->objOfs[i];
            d->events[d->evCount].dwData = vals[i];
            d->events[d->evCount].dwTimeStamp = GetTickCount();
            d->events[d->evCount].dwSequence = ++d->seq;
            d->events[d->evCount].uAppData = 0;
            d->evCount++;
        }
    }
    memcpy(d->lastVal, vals, sizeof(d->lastVal));
    d->haveLast = 1;
}

static int FindByHow(Device *d, DWORD obj, DWORD how)
{
    int i;
    for (i = 0; i < NUM_OBJS; i++) {
        if (how == DIPH_BYOFFSET && d->objOfs[i] == (int)obj) return i;
        if (how == DIPH_BYID && DIDFT_GETINSTANCE(obj) == DIDFT_GETINSTANCE(g_objs[i].type)
            && (DIDFT_GETTYPE(obj) & DIDFT_GETTYPE(g_objs[i].type))) return i;
        if (how == DIPH_BYUSAGE && LOWORD(obj) == g_objs[i].usagePage && HIWORD(obj) == g_objs[i].usage) return i;
    }
    return -1;
}

static void FillObjectInstance(Device *d, int i, DIDEVICEOBJECTINSTANCEA *o)
{
    DWORD size = o->dwSize ? o->dwSize : sizeof(*o);
    if (size > sizeof(*o)) size = sizeof(*o);
    memset(o, 0, size);
    o->dwSize = size;
    o->guidType = *g_objs[i].guid;
    o->dwOfs = d->objOfs[i] >= 0 ? (DWORD)d->objOfs[i] : 0;
    o->dwType = g_objs[i].type;
    o->dwFlags = i < NUM_AXES ? DIDOI_ASPECTPOSITION : 0;
    ObjName(i, o->tszName);
    if (size >= sizeof(*o)) { o->wUsagePage = g_objs[i].usagePage; o->wUsage = g_objs[i].usage; }
}

static void FillDeviceInstance(DIDEVICEINSTANCEA *di)
{
    DWORD size = di->dwSize;
    if (size == 0 || size > sizeof(*di)) size = sizeof(*di);
    memset(di, 0, size);
    di->dwSize = size;
    di->guidInstance = g_instance;
    di->guidProduct = g_product;
    di->dwDevType = 0x00010000 | (2 << 8) | DI8DEVTYPE_GAMEPAD;   /* HID, standard gamepad */
    wsprintfA(di->tszInstanceName, "Controller (XInput pad %d)", g_slot);
    lstrcpyA(di->tszProductName, "Controller (XBOX 360 For Windows)");
    if (size >= sizeof(*di)) { di->wUsagePage = 1; di->wUsage = 5; }
}

#define DEV(p) ((Device *)(p))

static HRESULT WINAPI Dev_QueryInterface(void *self, const GUID *riid, void **out)
{
    if (!out) return DIERR_INVALIDPARAM;
    if (IsEqualGUID(riid, &IID_IUnknown_) || IsEqualGUID(riid, &IID_IDirectInputDevice8A_)) {
        *out = self; InterlockedIncrement(&DEV(self)->refs); return DI_OK;
    }
    *out = NULL;
    return E_NOINTERFACE_;
}
static ULONG WINAPI Dev_AddRef(void *self) { return InterlockedIncrement(&DEV(self)->refs); }
static ULONG WINAPI Dev_Release(void *self)
{
    LONG r = InterlockedDecrement(&DEV(self)->refs);
    if (r == 0) HeapFree(GetProcessHeap(), 0, self);
    return r;
}
static HRESULT WINAPI Dev_GetCapabilities(void *self, DIDEVCAPS *caps)
{
    DWORD size;
    if (!caps) return DIERR_INVALIDPARAM;
    size = caps->dwSize ? caps->dwSize : sizeof(*caps);
    if (size > sizeof(*caps)) size = sizeof(*caps);
    memset(caps, 0, size);
    caps->dwSize = size;
    caps->dwFlags = DIDC_ATTACHED;
    caps->dwDevType = 0x00010000 | (2 << 8) | DI8DEVTYPE_GAMEPAD;
    caps->dwAxes = NUM_AXES; caps->dwButtons = NUM_BUTTONS; caps->dwPOVs = 1;
    return DI_OK;
}
static HRESULT WINAPI Dev_EnumObjects(void *self, ENUMOBJCB cb, void *ref, DWORD flags)
{
    DIDEVICEOBJECTINSTANCEA o;
    DWORD type = DIDFT_GETTYPE(flags);
    int i;
    if (!cb) return DIERR_INVALIDPARAM;
    if (flags & (DIDFT_FFACTUATOR | DIDFT_FFEFFECTTRIGGER | DIDFT_OUTPUT)) return DI_OK;   /* no force feedback */
    for (i = 0; i < NUM_OBJS; i++) {
        if (type && !(DIDFT_GETTYPE(g_objs[i].type) & type)) continue;
        o.dwSize = sizeof(o);
        FillObjectInstance(DEV(self), i, &o);
        if (!cb(&o, ref)) break;
    }
    return DI_OK;
}
static HRESULT WINAPI Dev_GetProperty(void *self, const GUID *prop, DIPROPHEADER *h)
{
    Device *d = DEV(self);
    UINT_PTR id = (UINT_PTR)prop;
    int i;
    if (!h) return DIERR_INVALIDPARAM;
    if (id > 0xFFFF) return DIERR_UNSUPPORTED;
    i = h->dwHow == DIPH_DEVICE ? 0 : FindByHow(d, h->dwObj, h->dwHow);
    switch (id) {
    case 1: ((DIPROPDWORD *)h)->dwData = d->bufferSize; return DI_OK;                 /* BUFFERSIZE */
    case 2: ((DIPROPDWORD *)h)->dwData = 0; return DI_OK;                             /* AXISMODE: absolute */
    case 3: ((DIPROPDWORD *)h)->dwData = 1; return DI_OK;                             /* GRANULARITY */
    case 4: case 18: case 19:                                                         /* RANGE / PHYSICAL / LOGICAL */
        if (i < 0 || i >= NUM_AXES) return DIERR_OBJECTNOTFOUND;
        if (id == 4) { ((DIPROPRANGE *)h)->lMin = d->rangeMin[i]; ((DIPROPRANGE *)h)->lMax = d->rangeMax[i]; }
        else { ((DIPROPRANGE *)h)->lMin = 0; ((DIPROPRANGE *)h)->lMax = 65535; }
        return DI_OK;
    case 5: if (i < 0 || i >= NUM_AXES) return DIERR_OBJECTNOTFOUND; ((DIPROPDWORD *)h)->dwData = d->deadzone[i]; return DI_OK;
    case 6: if (i < 0 || i >= NUM_AXES) return DIERR_OBJECTNOTFOUND; ((DIPROPDWORD *)h)->dwData = d->saturation[i]; return DI_OK;
    case 9: ((DIPROPDWORD *)h)->dwData = 0; return DI_OK;                             /* AUTOCENTER off */
    case 12:                                                                          /* GUIDANDPATH */
        memset(&((DIPROPGUIDANDPATH *)h)->guidClass, 0, sizeof(GUID));
        lstrcpyW(((DIPROPGUIDANDPATH *)h)->wszPath, L"\\\\?\\fo2pad#xinput");
        return DI_OK;
    case 13: case 14:                                                                 /* INSTANCENAME / PRODUCTNAME */
        lstrcpyW(((DIPROPSTRING *)h)->wsz, L"Controller (XBOX 360 For Windows)");
        return DI_OK;
    case 15: ((DIPROPDWORD *)h)->dwData = g_slot; return DI_OK;                       /* JOYSTICKID */
    case 24: ((DIPROPDWORD *)h)->dwData = MAKELONG(LOWORD(g_product.Data1), HIWORD(g_product.Data1)); return DI_OK; /* VIDPID */
    }
    return DIERR_UNSUPPORTED;
}
static HRESULT WINAPI Dev_SetProperty(void *self, const GUID *prop, const DIPROPHEADER *h)
{
    Device *d = DEV(self);
    UINT_PTR id = (UINT_PTR)prop;
    int i, first, last;
    if (!h) return DIERR_INVALIDPARAM;
    if (id > 0xFFFF) return DIERR_UNSUPPORTED;
    if (h->dwHow == DIPH_DEVICE) { first = 0; last = NUM_AXES - 1; }
    else { first = last = FindByHow(d, h->dwObj, h->dwHow); }
    Log("SetProperty %d how=%d obj=%lu -> obj %d", (int)id, (int)h->dwHow, h->dwObj, first);
    switch (id) {
    case 1:
        d->bufferSize = ((const DIPROPDWORD *)h)->dwData;
        if (d->bufferSize > 256) d->bufferSize = 256;
        return DI_OK;
    case 4:
        if (first < 0 || first >= NUM_AXES) return DIERR_OBJECTNOTFOUND;
        for (i = first; i <= last; i++) { d->rangeMin[i] = ((const DIPROPRANGE *)h)->lMin; d->rangeMax[i] = ((const DIPROPRANGE *)h)->lMax; }
        return DI_OK;
    case 5: case 6:
        if (first < 0 || first >= NUM_AXES) return DIERR_OBJECTNOTFOUND;
        for (i = first; i <= last; i++) {
            DWORD v = ((const DIPROPDWORD *)h)->dwData;
            if (v > 10000) v = 10000;
            if (id == 5) d->deadzone[i] = v; else d->saturation[i] = v;
        }
        return DI_OK;
    case 2: case 7: case 9: case 10: case 22:     /* axis mode, FF gain, autocenter, calibration mode, app data */
        return DI_OK;
    }
    return DIERR_UNSUPPORTED;
}
static HRESULT WINAPI Dev_Acquire(void *self) { return DI_OK; }      /* always "acquired": XInput works in the background */
static HRESULT WINAPI Dev_Unacquire(void *self) { return DI_OK; }
static HRESULT WINAPI Dev_GetDeviceState(void *self, DWORD cb, void *data)
{
    Device *d = DEV(self);
    DWORD vals[NUM_OBJS];
    int i;
    if (!data || !d->dataSize) return DIERR_INVALIDPARAM;
    if (cb != d->dataSize) { Log("GetDeviceState size %lu != format %lu", cb, d->dataSize); return DIERR_INVALIDPARAM; }
    NotePadRead();
    Sample(d, vals);
    QueueChanges(d, vals);
    memset(data, 0, cb);
    for (i = 0; i < d->fmtCount; i++)          /* POV entries with no matching object read as centered */
        if (DIDFT_GETTYPE(d->fmtType[i]) & DIDFT_POV && d->fmtOfs[i] + 4 <= cb) *(DWORD *)((BYTE *)data + d->fmtOfs[i]) = 0xFFFFFFFF;
    for (i = 0; i < NUM_OBJS; i++) {
        int ofs = d->objOfs[i];
        if (ofs < 0) continue;
        if (i > OBJ_POV) { if ((DWORD)ofs < cb) ((BYTE *)data)[ofs] = (BYTE)vals[i]; }
        else if ((DWORD)ofs + 4 <= cb) *(DWORD *)((BYTE *)data + ofs) = vals[i];
    }
    return DI_OK;
}
static HRESULT WINAPI Dev_GetDeviceData(void *self, DWORD cbObj, DIDEVICEOBJECTDATA *rg, DWORD *inout, DWORD flags)
{
    Device *d = DEV(self);
    DWORD vals[NUM_OBJS], n, i;
    if (!inout) return DIERR_INVALIDPARAM;
    if (!d->bufferSize) return DIERR_NOTBUFFERED;
    if (cbObj != 16 && cbObj != sizeof(DIDEVICEOBJECTDATA)) return DIERR_INVALIDPARAM;
    NotePadRead();
    Sample(d, vals);
    QueueChanges(d, vals);
    n = *inout;
    if (n > (DWORD)d->evCount) n = d->evCount;
    if (rg) for (i = 0; i < n; i++) memcpy((BYTE *)rg + i * cbObj, &d->events[i], cbObj);
    if (!(flags & DIGDD_PEEK)) {
        memmove(&d->events[0], &d->events[n], sizeof(d->events[0]) * (d->evCount - n));
        d->evCount -= n;
    }
    *inout = n;
    return DI_OK;
}
static HRESULT WINAPI Dev_SetDataFormat(void *self, const DIDATAFORMAT *f)
{
    Device *d = DEV(self);
    BOOL claimed[NUM_OBJS];
    DWORD k;
    int j;
    if (!f || !f->rgodf) return DIERR_INVALIDPARAM;
    memset(claimed, 0, sizeof(claimed));
    for (j = 0; j < NUM_OBJS; j++) d->objOfs[j] = -1;
    d->dataSize = f->dwDataSize;
    d->fmtCount = 0;
    d->haveLast = 0;
    d->evCount = 0;
    /* same matching rules as DirectInput: type class, optional GUID, instance (or any), position aspect only */
    for (k = 0; k < f->dwNumObjs; k++) {
        const DIOBJECTDATAFORMAT *o = (const DIOBJECTDATAFORMAT *)((const BYTE *)f->rgodf + k * f->dwObjSize);
        DWORD type = DIDFT_GETTYPE(o->dwType), inst = DIDFT_GETINSTANCE(o->dwType), aspect = o->dwFlags & DIDOI_ASPECTMASK;
        int matched = 0;
        if (aspect == 0 || aspect == DIDOI_ASPECTPOSITION) {
            for (j = 0; j < NUM_OBJS; j++) {
                if (claimed[j]) continue;
                if (type && !(DIDFT_GETTYPE(g_objs[j].type) & type)) continue;
                if (o->pguid && !IsEqualGUID(o->pguid, g_objs[j].guid)) continue;
                if (inst != 0xFFFF && inst != DIDFT_GETINSTANCE(g_objs[j].type)) continue;
                claimed[j] = TRUE; d->objOfs[j] = (int)o->dwOfs; matched = 1;
                break;
            }
        }
        if (!matched && d->fmtCount < 256) { d->fmtOfs[d->fmtCount] = o->dwOfs; d->fmtType[d->fmtCount] = o->dwType; d->fmtCount++; }
    }
    Log("SetDataFormat: size=%lu objects=%lu -> X@%d Y@%d Z@%d Rx@%d Ry@%d POV@%d A@%d", f->dwDataSize, f->dwNumObjs,
        d->objOfs[0], d->objOfs[1], d->objOfs[2], d->objOfs[3], d->objOfs[4], d->objOfs[5], d->objOfs[6]);
    return DI_OK;
}
static HRESULT WINAPI Dev_SetEventNotification(void *self, HANDLE e) { return DI_OK; }
static HRESULT WINAPI Dev_SetCooperativeLevel(void *self, HWND w, DWORD fl) { Log("SetCooperativeLevel flags=%lX", fl); return DI_OK; }
static HRESULT WINAPI Dev_GetObjectInfo(void *self, DIDEVICEOBJECTINSTANCEA *o, DWORD obj, DWORD how)
{
    int i = FindByHow(DEV(self), obj, how);
    if (!o) return DIERR_INVALIDPARAM;
    if (i < 0) return DIERR_OBJECTNOTFOUND;
    FillObjectInstance(DEV(self), i, o);
    return DI_OK;
}
static HRESULT WINAPI Dev_GetDeviceInfo(void *self, DIDEVICEINSTANCEA *di) { if (!di) return DIERR_INVALIDPARAM; FillDeviceInstance(di); return DI_OK; }
static HRESULT WINAPI Dev_RunControlPanel(void *self, HWND w, DWORD f) { return DI_OK; }
static HRESULT WINAPI Dev_Initialize(void *self, HINSTANCE h, DWORD v, const GUID *g) { return DI_OK; }
static HRESULT WINAPI Dev_CreateEffect(void *self, const GUID *g, void *e, void **out, void *outer) { if (out) *out = NULL; return DIERR_UNSUPPORTED; }
static HRESULT WINAPI Dev_EnumEffects(void *self, void *cb, void *ref, DWORD t) { return DI_OK; }
static HRESULT WINAPI Dev_GetEffectInfo(void *self, void *ei, const GUID *g) { return DIERR_UNSUPPORTED; }
static HRESULT WINAPI Dev_GetForceFeedbackState(void *self, DWORD *out) { return DIERR_UNSUPPORTED; }
static HRESULT WINAPI Dev_SendForceFeedbackCommand(void *self, DWORD c) { return DIERR_UNSUPPORTED; }
static HRESULT WINAPI Dev_EnumCreatedEffectObjects(void *self, void *cb, void *ref, DWORD f) { return DI_OK; }
static HRESULT WINAPI Dev_Escape(void *self, void *esc) { return DIERR_UNSUPPORTED; }
static HRESULT WINAPI Dev_Poll(void *self) { return DI_NOEFFECT; }
static HRESULT WINAPI Dev_SendDeviceData(void *self, DWORD cb, const void *rg, DWORD *inout, DWORD f) { return DIERR_UNSUPPORTED; }
static HRESULT WINAPI Dev_EnumEffectsInFile(void *self, const char *file, void *cb, void *ref, DWORD f) { return DIERR_UNSUPPORTED; }
static HRESULT WINAPI Dev_WriteEffectToFile(void *self, const char *file, DWORD n, void *rg, DWORD f) { return DIERR_UNSUPPORTED; }
static HRESULT WINAPI Dev_BuildActionMap(void *self, void *af, const char *user, DWORD f) { return DIERR_UNSUPPORTED; }
static HRESULT WINAPI Dev_SetActionMap(void *self, void *af, const char *user, DWORD f) { return DIERR_UNSUPPORTED; }
static HRESULT WINAPI Dev_GetImageInfo(void *self, void *info) { return DIERR_UNSUPPORTED; }

static void *g_devVtbl[32] = {
    Dev_QueryInterface, Dev_AddRef, Dev_Release, Dev_GetCapabilities, Dev_EnumObjects, Dev_GetProperty,
    Dev_SetProperty, Dev_Acquire, Dev_Unacquire, Dev_GetDeviceState, Dev_GetDeviceData, Dev_SetDataFormat,
    Dev_SetEventNotification, Dev_SetCooperativeLevel, Dev_GetObjectInfo, Dev_GetDeviceInfo, Dev_RunControlPanel,
    Dev_Initialize, Dev_CreateEffect, Dev_EnumEffects, Dev_GetEffectInfo, Dev_GetForceFeedbackState,
    Dev_SendForceFeedbackCommand, Dev_EnumCreatedEffectObjects, Dev_Escape, Dev_Poll, Dev_SendDeviceData,
    Dev_EnumEffectsInFile, Dev_WriteEffectToFile, Dev_BuildActionMap, Dev_SetActionMap, Dev_GetImageInfo
};

static Device *NewDevice(void)
{
    Device *d = (Device *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(Device));
    int i;
    if (!d) return NULL;
    d->vtbl = g_devVtbl;
    d->refs = 1;
    for (i = 0; i < NUM_AXES; i++) { d->rangeMin[i] = 0; d->rangeMax[i] = 65535; d->saturation[i] = 10000; }
    for (i = 0; i < NUM_OBJS; i++) d->objOfs[i] = -1;
    return d;
}

/* ---------------------------------------------------------------- IDirectInput8A wrapper */

typedef struct { void **vtbl; LONG refs; void *real; } DInput;
#define DI(p) ((DInput *)(p))
#define REALV(p, i) ((*(void ***)DI(p)->real)[i])

typedef ULONG   (WINAPI *PFN_Release)(void *);
typedef HRESULT (WINAPI *PFN_CreateDevice)(void *, const GUID *, void **, void *);
typedef HRESULT (WINAPI *PFN_EnumDevices)(void *, DWORD, ENUMDEVCB, void *, DWORD);
typedef HRESULT (WINAPI *PFN_GetDeviceStatus)(void *, const GUID *);
typedef HRESULT (WINAPI *PFN_RunControlPanel)(void *, HWND, DWORD);
typedef HRESULT (WINAPI *PFN_Initialize)(void *, HINSTANCE, DWORD);
typedef HRESULT (WINAPI *PFN_FindDevice)(void *, const GUID *, const char *, GUID *);

static int IsGameController(DWORD devType)
{
    DWORD t = devType & 0xFF;
    return (t >= 0x14 && t <= 0x18) || t == 0x1C;
}

static HRESULT WINAPI DI_QueryInterface(void *self, const GUID *riid, void **out)
{
    if (!out) return DIERR_INVALIDPARAM;
    if (IsEqualGUID(riid, &IID_IUnknown_) || IsEqualGUID(riid, &IID_IDirectInput8A_)) {
        *out = self; InterlockedIncrement(&DI(self)->refs); return DI_OK;
    }
    *out = NULL;
    return E_NOINTERFACE_;
}
static ULONG WINAPI DI_AddRef(void *self) { return InterlockedIncrement(&DI(self)->refs); }
static ULONG WINAPI DI_Release(void *self)
{
    LONG r = InterlockedDecrement(&DI(self)->refs);
    if (r == 0) {
        ((PFN_Release)REALV(self, 2))(DI(self)->real);
        HeapFree(GetProcessHeap(), 0, self);
    }
    return r;
}
static HRESULT WINAPI DI_CreateDevice(void *self, const GUID *guid, void **out, void *outer)
{
    if (!guid || !out) return DIERR_INVALIDPARAM;
    if (IsEqualGUID(guid, &g_instance) || IsEqualGUID(guid, &GUID_Joystick_)) {
        Log("CreateDevice: virtual pad (slot %d)", g_slot);
        if (g_slot == 0) { *out = NULL; return DIERR_DEVICENOTREG; }
        *out = NewDevice();
        return *out ? DI_OK : DIERR_OUTOFMEMORY;
    }
    Log("CreateDevice: passing %08lX to the real dinput8", guid->Data1);
    return ((PFN_CreateDevice)REALV(self, 3))(DI(self)->real, guid, out, outer);
}

typedef struct { ENUMDEVCB cb; void *ref; BOOL stopped; } EnumCtx;
static BOOL CALLBACK FilterRealDevices(const DIDEVICEINSTANCEA *di, void *ref)
{
    EnumCtx *c = (EnumCtx *)ref;
    if (IsGameController(di->dwDevType)) return TRUE;    /* real pads are hidden */
    if (!c->cb(di, c->ref)) { c->stopped = TRUE; return FALSE; }
    return TRUE;
}
static HRESULT WINAPI DI_EnumDevices(void *self, DWORD type, ENUMDEVCB cb, void *ref, DWORD flags)
{
    EnumCtx c;
    BOOL wantPads = type == DI8DEVCLASS_ALL || type == DI8DEVCLASS_GAMECTRL || (type >= 0x11 && IsGameController(type));
    BOOL wantOthers = type != DI8DEVCLASS_GAMECTRL && !(type >= 0x11 && IsGameController(type));
    if (!cb) return DIERR_INVALIDPARAM;
    Log("EnumDevices type=%lu flags=%lX", type, flags);
    if (wantPads && g_slot > 0 && !(flags & DIEDFL_FORCEFEEDBACK)) {
        DIDEVICEINSTANCEA di;
        di.dwSize = sizeof(di);
        FillDeviceInstance(&di);
        if (!cb(&di, ref)) return DI_OK;
    }
    if (!wantOthers) return DI_OK;
    c.cb = cb; c.ref = ref; c.stopped = FALSE;
    return ((PFN_EnumDevices)REALV(self, 4))(DI(self)->real, type, FilterRealDevices, &c, flags);
}
static HRESULT WINAPI DI_GetDeviceStatus(void *self, const GUID *guid)
{
    if (guid && IsEqualGUID(guid, &g_instance)) return DI_OK;
    return ((PFN_GetDeviceStatus)REALV(self, 5))(DI(self)->real, guid);
}
static HRESULT WINAPI DI_RunControlPanel(void *self, HWND w, DWORD f)
{ return ((PFN_RunControlPanel)REALV(self, 6))(DI(self)->real, w, f); }
static HRESULT WINAPI DI_Initialize(void *self, HINSTANCE h, DWORD v)
{ return ((PFN_Initialize)REALV(self, 7))(DI(self)->real, h, v); }
static HRESULT WINAPI DI_FindDevice(void *self, const GUID *g, const char *name, GUID *out)
{ return ((PFN_FindDevice)REALV(self, 8))(DI(self)->real, g, name, out); }
static HRESULT WINAPI DI_EnumDevicesBySemantics(void *self, const char *user, void *af, void *cb, void *ref, DWORD f)
{ return DIERR_UNSUPPORTED; }
static HRESULT WINAPI DI_ConfigureDevices(void *self, void *cb, void *params, DWORD f, void *ref)
{ return DIERR_UNSUPPORTED; }

static void *g_diVtbl[11] = {
    DI_QueryInterface, DI_AddRef, DI_Release, DI_CreateDevice, DI_EnumDevices, DI_GetDeviceStatus,
    DI_RunControlPanel, DI_Initialize, DI_FindDevice, DI_EnumDevicesBySemantics, DI_ConfigureDevices
};

/* ---------------------------------------------------------------- FlatOut2.exe import hooks */

/* Replace one entry of the game executable's import table; returns the original function or NULL */
static void *PatchImport(const char *dll, const char *name, WORD ordinal, void *repl)
{
    BYTE *base = (BYTE *)GetModuleHandleA(NULL);
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_DATA_DIRECTORY *dir = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    IMAGE_IMPORT_DESCRIPTOR *desc;

    if (!dir->VirtualAddress) return NULL;
    for (desc = (IMAGE_IMPORT_DESCRIPTOR *)(base + dir->VirtualAddress); desc->Name; desc++) {
        DWORD *names, *slots;
        if (lstrcmpiA((const char *)(base + desc->Name), dll) != 0) continue;
        names = (DWORD *)(base + (desc->OriginalFirstThunk ? desc->OriginalFirstThunk : desc->FirstThunk));
        slots = (DWORD *)(base + desc->FirstThunk);
        for (; *names; names++, slots++) {
            BOOL hit;
            if (*names & 0x80000000) hit = ordinal && (WORD)(*names & 0xFFFF) == ordinal;
            else hit = name && lstrcmpA((const char *)(base + *names + 2), name) == 0;
            if (hit) {
                void *old = (void *)*slots;
                DWORD prot;
                VirtualProtect(slots, sizeof(DWORD), PAGE_READWRITE, &prot);
                *slots = (DWORD)repl;
                VirtualProtect(slots, sizeof(DWORD), prot, &prot);
                return old;
            }
        }
    }
    return NULL;
}

/* --- LAN on one PC
 * FlatOut uses fixed ports, so every instance binds its sockets to its own loopback address 127.0.0.N.
 * The game ignores 127.x peers (a join request then goes to 0.0.0.0), so it is shown a virtual LAN
 * 192.168.80.N instead: adapter info, host name lookups, getsockname and the sender address of received
 * packets are translated 127.0.0.x -> 192.168.80.x, and the destination of sent packets back
 * (192.168.80.255 -> 255.255.255.255). Each instance also reports a different MAC address, because the
 * game ignores packets that seem to come from its own MAC. */

#define VNET_BASE 0x0050A8C0u   /* 192.168.80.0 in network byte order */
#define REAL_BASE 0x0000007Fu   /* 127.0.0.0 */

typedef struct { short family; unsigned short port; DWORD addr; char zero[8]; } SockAddrIn;
typedef struct { char *h_name; char **h_aliases; short h_addrtype; short h_length; char **h_addr_list; } HostEnt;
typedef struct { ULONG len; char *buf; } WsaBuf;

static DWORD ToVirtual(DWORD a) { return (a & 0x00FFFFFF) == REAL_BASE ? (VNET_BASE | (a & 0xFF000000)) : a; }
static DWORD ToReal(DWORD a)
{
    if ((a & 0x00FFFFFF) != VNET_BASE) return a;
    if ((a >> 24) == 255) return 0xFFFFFFFF;
    return REAL_BASE | (a & 0xFF000000);
}
static unsigned PortH(unsigned short p) { return (unsigned)(((p & 0xFF) << 8) | (p >> 8)); }

static int (WINAPI *g_bind)(UINT_PTR, const void *, int);
static int (WINAPI *g_getsockname)(UINT_PTR, void *, int *);
static int (WINAPI *g_gethostname)(char *, int);
static HostEnt *(WINAPI *g_gethostbyname)(const char *);
static DWORD (WINAPI *g_GetAdaptersInfo)(void *, ULONG *);
static int (WINAPI *g_WSASendTo)(UINT_PTR, WsaBuf *, DWORD, DWORD *, DWORD, const void *, int, void *, void *);
static int (WINAPI *g_sendto)(UINT_PTR, const char *, int, int, const void *, int);
static int (WINAPI *g_WSARecvFrom)(UINT_PTR, WsaBuf *, DWORD, DWORD *, DWORD *, void *, int *, void *, void *);
static int (WINAPI *g_recvfrom)(UINT_PTR, char *, int, int, void *, int *);
static BOOL (WINAPI *g_WSAGetOverlappedResult)(UINT_PTR, void *, DWORD *, BOOL, DWORD *);
static char g_fakeHostName[32], g_realNetbios[64], g_realDns[256], g_vnetAddrStr[16];
static HostEnt g_host;
static DWORD g_hostAddr;
static char *g_hostAddrList[2];
static char *g_hostAliases[1];

static void LogPacket(const char *api, UINT_PTR s, const char *data, int len, const void *addr)
{
    char hex[3 * 40 + 1];
    int i, n = len < 40 ? len : 40;
    const SockAddrIn *a = (const SockAddrIn *)addr;
    static int count;
    if (!g_logPackets || count++ > 80 || !data) return;
    for (i = 0; i < n; i++) wsprintfA(hex + 3 * i, "%02X ", (BYTE)data[i]);
    hex[3 * n] = 0;
    if (a && a->family == 2)
        Log("%s sock=%lu %d.%d.%d.%d:%u len=%d | %s", api, (DWORD)s, a->addr & 0xFF, (a->addr >> 8) & 0xFF, (a->addr >> 16) & 0xFF,
            a->addr >> 24, PortH(a->port), len, hex);
    else
        Log("%s sock=%lu (no address) len=%d | %s", api, (DWORD)s, len, hex);
}

static void FromToVirtual(void *from)
{
    SockAddrIn *a = (SockAddrIn *)from;
    if (a && a->family == 2) a->addr = ToVirtual(a->addr);
}

static int WINAPI Hook_bind(UINT_PTR s, const void *name, int len)
{
    const SockAddrIn *in = (const SockAddrIn *)name;
    if (in && len >= (int)sizeof(SockAddrIn) && in->family == 2 && (in->addr == 0 || ToReal(in->addr) == g_netAddr)) {
        SockAddrIn a = *in;
        a.addr = g_netAddr;
        Log("bind port %u -> %s", PortH(a.port), g_netAddrStr);
        return g_bind(s, &a, sizeof(a));
    }
    return g_bind(s, name, len);
}

static int WINAPI Hook_getsockname(UINT_PTR s, void *name, int *len)
{
    int r = g_getsockname(s, name, len);
    if (r == 0) FromToVirtual(name);
    return r;
}

static int WINAPI Hook_gethostname(char *name, int len)
{
    if (!name || len <= lstrlenA(g_fakeHostName)) return g_gethostname(name, len);
    lstrcpyA(name, g_fakeHostName);
    return 0;
}

/* "FO2-PLAYER-<n>" -> instance n's virtual address */
static DWORD OtherInstanceAddr(const char *name)
{
    const char *prefix = "FO2-PLAYER-";
    int i, n = 0;
    for (i = 0; prefix[i]; i++) if ((name[i] & ~0x20) != (prefix[i] & ~0x20)) return 0;
    for (; name[i] >= '0' && name[i] <= '9'; i++) n = n * 10 + name[i] - '0';
    if (name[i] || n <= 0 || n >= 255) return 0;
    return VNET_BASE | ((DWORD)n << 24);
}

static HostEnt *WINAPI Hook_gethostbyname(const char *name)
{
    DWORD other = name ? OtherInstanceAddr(name) : 0;
    if (!name || !*name || other || !lstrcmpiA(name, g_realNetbios) || !lstrcmpiA(name, g_realDns) || !lstrcmpiA(name, "localhost")) {
        g_hostAddr = other ? other : ToVirtual(g_netAddr);
        g_hostAddrList[0] = (char *)&g_hostAddr;
        g_hostAddrList[1] = NULL;
        g_hostAliases[0] = NULL;
        g_host.h_name = g_fakeHostName;
        g_host.h_aliases = g_hostAliases;
        g_host.h_addrtype = 2;
        g_host.h_length = 4;
        g_host.h_addr_list = g_hostAddrList;
        Log("gethostbyname(%s) -> 192.168.80.%d", name ? name : "NULL", (int)(g_hostAddr >> 24));
        return &g_host;
    }
    Log("gethostbyname(%s) -> real lookup", name);
    return g_gethostbyname(name);
}

static int WINAPI Hook_WSASendTo(UINT_PTR s, WsaBuf *bufs, DWORD count, DWORD *sent, DWORD flags, const void *to, int tolen, void *ov, void *cr)
{
    const SockAddrIn *in = (const SockAddrIn *)to;
    if (in && tolen >= (int)sizeof(SockAddrIn) && in->family == 2) {
        SockAddrIn a = *in;
        a.addr = ToReal(a.addr);
        if (bufs && count) LogPacket("send to", s, bufs[0].buf, (int)bufs[0].len, &a);
        return g_WSASendTo(s, bufs, count, sent, flags, &a, sizeof(a), ov, cr);
    }
    if (bufs && count) LogPacket("send", s, bufs[0].buf, (int)bufs[0].len, to);
    return g_WSASendTo(s, bufs, count, sent, flags, to, tolen, ov, cr);
}

static int WINAPI Hook_sendto(UINT_PTR s, const char *buf, int len, int flags, const void *to, int tolen)
{
    const SockAddrIn *in = (const SockAddrIn *)to;
    if (in && tolen >= (int)sizeof(SockAddrIn) && in->family == 2) {
        SockAddrIn a = *in;
        a.addr = ToReal(a.addr);
        LogPacket("sendto", s, buf, len, &a);
        return g_sendto(s, buf, len, flags, &a, sizeof(a));
    }
    return g_sendto(s, buf, len, flags, to, tolen);
}

/* overlapped receives fill the sender address on completion: remember where it goes */
static struct { void *ov; void *from; char *buf; } g_pendingRecv[64];

static int WINAPI Hook_WSARecvFrom(UINT_PTR s, WsaBuf *bufs, DWORD count, DWORD *got, DWORD *flags, void *from, int *fromlen, void *ov, void *cr)
{
    int i, r = g_WSARecvFrom(s, bufs, count, got, flags, from, fromlen, ov, cr);
    if (r == 0 && from) {                            /* completed right away */
        FromToVirtual(from);
        if (got && bufs) LogPacket("recv from", s, bufs[0].buf, (int)*got, from);
    }
    if (ov && from) {
        int slot = -1;
        for (i = 0; i < 64; i++) {
            if (g_pendingRecv[i].ov == ov) { slot = i; break; }
            if (slot < 0 && !g_pendingRecv[i].ov) slot = i;
        }
        if (slot >= 0) { g_pendingRecv[slot].ov = ov; g_pendingRecv[slot].from = from; g_pendingRecv[slot].buf = bufs ? bufs[0].buf : NULL; }
    }
    return r;
}

static BOOL WINAPI Hook_WSAGetOverlappedResult(UINT_PTR s, void *ov, DWORD *got, BOOL wait, DWORD *flags)
{
    BOOL r = g_WSAGetOverlappedResult(s, ov, got, wait, flags);
    int i;
    if (r) for (i = 0; i < 64; i++) if (g_pendingRecv[i].ov == ov) {
        FromToVirtual(g_pendingRecv[i].from);
        if (got) LogPacket("recv from", s, g_pendingRecv[i].buf, (int)*got, g_pendingRecv[i].from);
        g_pendingRecv[i].ov = NULL;
        break;
    }
    return r;
}

static int WINAPI Hook_recvfrom(UINT_PTR s, char *buf, int len, int flags, void *from, int *fromlen)
{
    int r = g_recvfrom(s, buf, len, flags, from, fromlen);
    if (r > 0 && from) { FromToVirtual(from); LogPacket("recvfrom", s, buf, r, from); }
    return r;
}

/* IP_ADAPTER_INFO (32-bit): AddressLength @400, Address[8] @404, IpAddressList @428 (Next, IpAddress[16] @432, IpMask[16] @448) */
static DWORD WINAPI Hook_GetAdaptersInfo(void *info, ULONG *len)
{
    DWORD r = g_GetAdaptersInfo(info, len);
    if (r == 0 && info) {
        BYTE *a = (BYTE *)info;
        UINT alen = *(UINT *)(a + 400);
        *(void **)a = NULL;                          /* report only the first adapter */
        if (alen > 0 && alen <= 8) a[404 + alen - 1] = (BYTE)(a[404 + alen - 1] + g_netIndex);
        *(void **)(a + 428) = NULL;
        lstrcpyA((char *)(a + 432), g_vnetAddrStr);
        lstrcpyA((char *)(a + 448), "255.255.255.0");
        Log("GetAdaptersInfo -> %s, MAC last byte %02X", g_vnetAddrStr, alen ? a[404 + alen - 1] : 0);
    }
    return r;
}

/* --- menus: pad -> the game's keyboard hook */

typedef struct { DWORD vkCode, scanCode, flags, time; ULONG_PTR dwExtraInfo; } KbdLL;

static HHOOK (WINAPI *g_SetWindowsHookExA)(int, HOOKPROC, HINSTANCE, DWORD);
static LRESULT (WINAPI *g_CallNextHookEx)(HHOOK, int, WPARAM, LPARAM);
static BOOL (WINAPI *g_PeekMessageA)(MSG *, HWND, UINT, UINT, UINT);
static HOOKPROC g_kbProc;
static int g_kbType;
static int g_injecting;
static HWND g_gameWnd;

static HHOOK WINAPI Hook_SetWindowsHookExA(int id, HOOKPROC proc, HINSTANCE mod, DWORD tid)
{
    HHOOK h = g_SetWindowsHookExA(id, proc, mod, tid);
    Log("SetWindowsHookExA id=%d thread=%lu -> %s", id, tid, h ? "ok" : "failed");
    if (h && (id == 2 /* WH_KEYBOARD */ || id == 13 /* WH_KEYBOARD_LL */)) { g_kbProc = proc; g_kbType = id; }
    return h;
}

static LRESULT WINAPI Hook_CallNextHookEx(HHOOK h, int code, WPARAM w, LPARAM l)
{
    if (g_injecting) return 0;                       /* our synthetic key isn't part of a real hook chain */
    return g_CallNextHookEx(h, code, w, l);
}

static BOOL CALLBACK FindGameWindow(HWND w, LPARAM l)
{
    DWORD pid;
    char cls[64];
    GetWindowThreadProcessId(w, &pid);
    if (pid != GetCurrentProcessId() || !IsWindowVisible(w)) return TRUE;
    GetClassNameA(w, cls, sizeof(cls));
    if (lstrcmpA(cls, "BDX9 Render Window") != 0) return TRUE;
    g_gameWnd = w;
    return FALSE;
}

static void SendKey(int vk, BOOL down, BOOL repeat)
{
    UINT sc = MapVirtualKeyA(vk, 0);
    BOOL ext = vk >= VK_PRIOR && vk <= VK_DOWN;
    LPARAM lp = 1 | ((LPARAM)sc << 16) | ((LPARAM)ext << 24) | ((down && !repeat) ? 0 : (1 << 30)) | (down ? 0 : 0x80000000);
    if (g_kbProc) {
        g_injecting = 1;
        if (g_kbType == 13) {
            KbdLL k;
            k.vkCode = vk; k.scanCode = sc; k.flags = (ext ? 1 : 0) | (down ? 0 : 0x80); k.time = GetTickCount(); k.dwExtraInfo = 0;
            g_kbProc(0, down ? WM_KEYDOWN : WM_KEYUP, (LPARAM)&k);
        } else {
            g_kbProc(0 /* HC_ACTION */, vk, lp);
        }
        g_injecting = 0;
    } else {
        if (!g_gameWnd) EnumWindows(FindGameWindow, 0);
        if (g_gameWnd) PostMessageA(g_gameWnd, down ? WM_KEYDOWN : WM_KEYUP, vk, lp);
    }
}

enum { MK_UP, MK_DOWN, MK_LEFT, MK_RIGHT, MK_ENTER, MK_ESC, MK_COUNT };
static const int g_menuVk[MK_COUNT] = { VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT, VK_RETURN, VK_ESCAPE };

static void MenuTick(void)
{
    static DWORD lastTick, heldSince[MK_COUNT], lastRepeat[MK_COUNT];
    static BOOL held[MK_COUNT], wasMenu = -1, testStarted;
    static DWORD testStart;
    XINPUT_STATE_ st;
    BOOL want[MK_COUNT], menu;
    DWORD now = GetTickCount();
    int i;

    if (now - lastTick < 15) return;
    lastTick = now;

    if (g_selfTest) {                                /* debug: Enter every 4 s from 8 s after start, 12 times */
        if (!testStarted) { testStarted = TRUE; testStart = now; }
        if (now - testStart > 8000 && (now - testStart) / 4000 < 14) {
            static DWORD lastPress;
            if (now - lastPress > 4000) { lastPress = now; Log("self-test: Enter (hook %s)", g_kbProc ? "captured" : "not captured, posting"); SendKey(VK_RETURN, TRUE, FALSE); SendKey(VK_RETURN, FALSE, FALSE); }
        }
    }

    if (g_remote) {                                  /* debug: keys listed in fo2pad_remote.txt, e.g. "DOWN DOWN ENTER" */
        static char queue[512];
        static int qpos, qlen;
        static DWORD lastCheck, nextKey;
        if (qpos >= qlen && now - lastCheck > 200) {
            char path[MAX_PATH];
            HANDLE f;
            DWORD n = 0;
            lastCheck = now;
            lstrcpyA(path, g_dir);
            lstrcatA(path, "fo2pad_remote.txt");
            f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
            if (f != INVALID_HANDLE_VALUE) {
                ReadFile(f, queue, sizeof(queue) - 1, &n, NULL);
                CloseHandle(f);
                DeleteFileA(path);
                queue[n] = 0; qlen = n; qpos = 0;
            }
        }
        if (qpos < qlen && now >= nextKey) {
            char tok[16];
            int t = 0, vk = 0;
            while (qpos < qlen && (queue[qpos] == ' ' || queue[qpos] == '\r' || queue[qpos] == '\n' || queue[qpos] == ',')) qpos++;
            while (qpos < qlen && queue[qpos] > ' ' && queue[qpos] != ',' && t < 15) tok[t++] = queue[qpos++];
            tok[t] = 0;
            if (!lstrcmpiA(tok, "UP")) vk = VK_UP; else if (!lstrcmpiA(tok, "DOWN")) vk = VK_DOWN;
            else if (!lstrcmpiA(tok, "LEFT")) vk = VK_LEFT; else if (!lstrcmpiA(tok, "RIGHT")) vk = VK_RIGHT;
            else if (!lstrcmpiA(tok, "ENTER")) vk = VK_RETURN; else if (!lstrcmpiA(tok, "ESC")) vk = VK_ESCAPE;
            if (vk) { SendKey(vk, TRUE, FALSE); SendKey(vk, FALSE, FALSE); Log("remote key %s", tok); }
            nextKey = now + (!lstrcmpiA(tok, "WAIT") ? 1000 : 300);
        }
    }

    if (!g_menuKeys || g_slot == 0 || !g_XInputGetState) return;

    /* The game reads the pad all the time, even in menus, so "racing" is guessed from the triggers:
       A, B and the left stick only act as keys when no trigger was pressed for DriveHoldMs.
       The D-pad (unused while driving) and Start (pause / back) always do. */
    memset(want, 0, sizeof(want));
    if (g_XInputGetState(g_slot - 1, &st) == 0) {
        static DWORD lastTrigger;
        WORD b = st.Gamepad.wButtons;
        if (st.Gamepad.bLeftTrigger > 30 || st.Gamepad.bRightTrigger > 30) lastTrigger = now;
        menu = now - lastTrigger > (DWORD)g_driveHoldMs;
        if (menu != wasMenu) { Log("A/B/stick menu keys %s", menu ? "ON" : "OFF (triggers in use)"); wasMenu = menu; }
        want[MK_UP] = (b & 0x0001) || (menu && st.Gamepad.sThumbLY > 20000);
        want[MK_DOWN] = (b & 0x0002) || (menu && st.Gamepad.sThumbLY < -20000);
        want[MK_LEFT] = (b & 0x0004) || (menu && st.Gamepad.sThumbLX < -20000);
        want[MK_RIGHT] = (b & 0x0008) || (menu && st.Gamepad.sThumbLX > 20000);
        want[MK_ENTER] = menu && (b & 0x1000);                                   /* A */
        want[MK_ESC] = (menu && (b & 0x2000)) || (g_startIsEscape && (b & 0x0010)); /* B, Start */
    }
    for (i = 0; i < MK_COUNT; i++) {
        if (want[i] && !held[i]) { SendKey(g_menuVk[i], TRUE, FALSE); held[i] = TRUE; heldSince[i] = lastRepeat[i] = now; }
        else if (want[i] && i <= MK_RIGHT && now - heldSince[i] > 450 && now - lastRepeat[i] > 110) { SendKey(g_menuVk[i], TRUE, TRUE); lastRepeat[i] = now; }
        else if (!want[i] && held[i]) { SendKey(g_menuVk[i], FALSE, FALSE); held[i] = FALSE; }
    }
}

/* Menu/race detection candidates: the game shows a mouse cursor in menus */
static int (WINAPI *g_ShowCursor)(BOOL);
static HCURSOR (WINAPI *g_SetCursor)(HCURSOR);
static volatile LONG g_cursorCount;      /* ShowCursor display counter as last returned to the game */
static volatile int g_cursorNull;

static int WINAPI Hook_ShowCursor(BOOL show)
{
    int r = g_ShowCursor(show);
    if (r != g_cursorCount) Log("ShowCursor(%d) -> %d", show, r);
    g_cursorCount = r;
    return r;
}

static HCURSOR WINAPI Hook_SetCursor(HCURSOR c)
{
    if ((c == NULL) != g_cursorNull) Log("SetCursor(%s)", c ? "cursor" : "NULL");
    g_cursorNull = c == NULL;
    return g_SetCursor(c);
}

/* FlatOut 2 v1.2 installs WH_KEYBOARD with this procedure before it loads dinput8.dll (FlatOut2.exe+0x15AAD0) */
static const BYTE g_fo2KbProcStart[16] = { 0x8B, 0x54, 0x24, 0x04, 0x8B, 0x4C, 0x24, 0x08, 0x83, 0xEC, 0x20, 0x53, 0x55, 0x56, 0x57, 0x33 };

static void FindKnownKeyboardProc(void)
{
    BYTE *p = (BYTE *)GetModuleHandleA(NULL) + 0x15AAD0;
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(p, &mbi, sizeof(mbi)) && mbi.State == MEM_COMMIT && (mbi.Protect & 0xF0) && memcmp(p, g_fo2KbProcStart, 16) == 0) {
        g_kbProc = (HOOKPROC)p;
        g_kbType = 2;
        Log("keyboard hook procedure found at FlatOut2.exe+0x15AAD0");
    } else {
        Log("keyboard hook procedure not at the known address (different game version?)");
    }
}

static BOOL WINAPI Hook_PeekMessageA(MSG *m, HWND h, UINT a, UINT b, UINT r)
{
    BOOL ret = g_PeekMessageA(m, h, a, b, r);
    MenuTick();
    return ret;
}

static void InstallGameHooks(void)
{
    if (g_netAddr) {
        DWORD n = sizeof(g_realNetbios);
        BOOL (WINAPI *getNameEx)(int, char *, DWORD *);
        GetComputerNameA(g_realNetbios, &n);
        n = sizeof(g_realDns);
        *(FARPROC *)&getNameEx = GetProcAddress(GetModuleHandleA("kernel32.dll"), "GetComputerNameExA");
        if (!getNameEx || !getNameEx(1 /* ComputerNameDnsHostname */, g_realDns, &n)) g_realDns[0] = 0;
        wsprintfA(g_fakeHostName, "FO2-PLAYER-%d", g_netIndex);
        wsprintfA(g_vnetAddrStr, "192.168.80.%d", g_netIndex);
        *(void **)&g_bind = PatchImport("WS2_32.dll", NULL, 2, Hook_bind);
        *(void **)&g_getsockname = PatchImport("WS2_32.dll", NULL, 6, Hook_getsockname);
        *(void **)&g_recvfrom = PatchImport("WS2_32.dll", NULL, 17, Hook_recvfrom);
        *(void **)&g_sendto = PatchImport("WS2_32.dll", NULL, 20, Hook_sendto);
        *(void **)&g_gethostbyname = PatchImport("WS2_32.dll", NULL, 52, Hook_gethostbyname);
        *(void **)&g_gethostname = PatchImport("WS2_32.dll", NULL, 57, Hook_gethostname);
        *(void **)&g_WSASendTo = PatchImport("WS2_32.dll", "WSASendTo", 0, Hook_WSASendTo);
        *(void **)&g_WSARecvFrom = PatchImport("WS2_32.dll", "WSARecvFrom", 0, Hook_WSARecvFrom);
        *(void **)&g_WSAGetOverlappedResult = PatchImport("WS2_32.dll", "WSAGetOverlappedResult", 0, Hook_WSAGetOverlappedResult);
        *(void **)&g_GetAdaptersInfo = PatchImport("iphlpapi.dll", "GetAdaptersInfo", 0, Hook_GetAdaptersInfo);
        Log("net hooks for %s (shown to the game as %s): bind=%s getsockname=%s recvfrom=%s sendto=%s gethostbyname=%s "
            "gethostname=%s WSASendTo=%s WSARecvFrom=%s WSAGetOverlappedResult=%s GetAdaptersInfo=%s", g_netAddrStr, g_vnetAddrStr,
            g_bind ? "ok" : "-", g_getsockname ? "ok" : "-", g_recvfrom ? "ok" : "-", g_sendto ? "ok" : "-",
            g_gethostbyname ? "ok" : "-", g_gethostname ? "ok" : "-", g_WSASendTo ? "ok" : "-", g_WSARecvFrom ? "ok" : "-",
            g_WSAGetOverlappedResult ? "ok" : "-", g_GetAdaptersInfo ? "ok" : "-");
    }
    if (g_menuKeys || g_selfTest || g_remote) {
        *(void **)&g_SetWindowsHookExA = PatchImport("USER32.dll", "SetWindowsHookExA", 0, Hook_SetWindowsHookExA);
        *(void **)&g_CallNextHookEx = PatchImport("USER32.dll", "CallNextHookEx", 0, Hook_CallNextHookEx);
        *(void **)&g_PeekMessageA = PatchImport("USER32.dll", "PeekMessageA", 0, Hook_PeekMessageA);
        *(void **)&g_ShowCursor = PatchImport("USER32.dll", "ShowCursor", 0, Hook_ShowCursor);
        *(void **)&g_SetCursor = PatchImport("USER32.dll", "SetCursor", 0, Hook_SetCursor);
        Log("menu hooks: SetWindowsHookExA=%s CallNextHookEx=%s PeekMessageA=%s ShowCursor=%s SetCursor=%s",
            g_SetWindowsHookExA ? "ok" : "-", g_CallNextHookEx ? "ok" : "-", g_PeekMessageA ? "ok" : "-",
            g_ShowCursor ? "ok" : "-", g_SetCursor ? "ok" : "-");
        FindKnownKeyboardProc();
    }
}

/* ---------------------------------------------------------------- export */

__declspec(dllexport) HRESULT WINAPI DirectInput8Create(HINSTANCE hinst, DWORD ver, const GUID *riid, void **out, void *outer)
{
    void *real = NULL;
    HRESULT hr;
    DInput *w;

    Init();
    if (!g_realCreate) return DIERR_DEVICENOTREG;
    hr = g_realCreate(hinst, ver, riid, &real, outer);
    Log("DirectInput8Create ver=%lX -> %lX", ver, hr);
    if (hr < 0 || !out) { if (out) *out = real; return hr; }
    if (!IsEqualGUID(riid, &IID_IDirectInput8A_)) { *out = real; return hr; }   /* only the ANSI interface FlatOut uses is wrapped */
    w = (DInput *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(DInput));
    if (!w) { *out = real; return hr; }
    w->vtbl = g_diVtbl;
    w->refs = 1;
    w->real = real;
    *out = w;
    return hr;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        g_hinst = h;
        DisableThreadLibraryCalls(h);
        InitObjs();
        ReadConfig();
        InstallGameHooks();
    }
    return TRUE;
}
