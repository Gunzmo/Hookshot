/* Game controllers through SDL3 (linked on Linux, loaded from SDL3.dll on Windows): DualSense,
   DualShock 4, Switch Pro, Xbox and most other pads, over USB or Bluetooth. XInput alone sees only Xbox-style pads.

   A thread of its own (on Linux the window's, which runs SDL there too) opens every controller SDL knows (and ones
   plugged in later), polls them every 4 ms and keeps
   the merged state; nn_hid.c ORs it into the input with XInput, keyboard and mouse. Without SDL3.dll nothing
   changes. HOS_NO_SDL=1 leaves SDL out. The inputs go by position (HOS_PAD_*, SDL3's own order: the bottom face button
   first, cross on a DualSense, A on an Xbox pad); nn_hid.c's controls make them the Switch's buttons. */
#include "hos.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hos_sync.h"
#if defined(__linux__) && !defined(__EMSCRIPTEN__)
#include <SDL3/SDL.h>
typedef SDL_Gamepad Gamepad;
#else
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
typedef void Gamepad;
#endif
#include "../gpu/vk/vk_api.h"   /* (HOS_PAD_*) */

void hos_host_thread_register_self(const char *name);   /* hos_prof.c */

#if !defined(__linux__) || defined(__EMSCRIPTEN__)
/* the few SDL3 entry points used (SDL.h's types: SDL_Gamepad* is opaque, SDL_JoystickID a Uint32, bool C's) */
typedef bool (*InitFn)(uint32_t);
typedef bool (*SetHintFn)(const char *, const char *);
typedef uint32_t *(*GetGamepadsFn)(int *);
typedef void *(*OpenFn)(uint32_t);
typedef void (*CloseFn)(void *);
typedef bool (*ConnectedFn)(void *);
typedef void (*UpdateFn)(void);
typedef bool (*GetButtonFn)(void *, int);
typedef int16_t (*GetAxisFn)(void *, int);
typedef const char *(*NameFn)(void *);
typedef uint32_t (*IdFn)(void *);
typedef bool (*RumbleFn)(void *, uint16_t, uint16_t, uint32_t);
typedef const char *(*GetErrorFn)(void);
typedef void (*FreeFn)(void *);

static struct {
    InitFn Init;
    SetHintFn SetHint;
    GetGamepadsFn GetGamepads;
    OpenFn Open;
    CloseFn Close;
    ConnectedFn Connected;
    UpdateFn Update;
    GetButtonFn GetButton;
    GetAxisFn GetAxis;
    NameFn Name;
    IdFn Id;
    RumbleFn Rumble;
    GetErrorFn GetError;
    FreeFn Free;
} sdl;

enum { SDL_INIT_GAMEPAD = 0x2000 };
#define SDL_Init sdl.Init
#define SDL_SetHint sdl.SetHint
#define SDL_GetGamepads sdl.GetGamepads
#define SDL_OpenGamepad sdl.Open
#define SDL_CloseGamepad sdl.Close
#define SDL_GamepadConnected sdl.Connected
#define SDL_UpdateGamepads sdl.Update
#define SDL_GetGamepadButton sdl.GetButton
#define SDL_GetGamepadAxis sdl.GetAxis
#define SDL_GetGamepadName sdl.Name
#define SDL_GetGamepadID sdl.Id
#define SDL_RumbleGamepad sdl.Rumble
#define SDL_GetError sdl.GetError
#define SDL_free sdl.Free
#endif
enum { BTN_N = 15 };   /* (SDL_GAMEPAD_BUTTON_SOUTH ... _DPAD_RIGHT: HOS_PAD_SOUTH ... HOS_PAD_RIGHT) */
enum { AX_LX, AX_LY, AX_RX, AX_RY, AX_LT, AX_RT };

#define MAX_PADS 8
static struct { Gamepad *gc; uint32_t id; } pads[MAX_PADS];
static HosRwLock state_lock = HOS_RWLOCK_INIT;
static uint64_t state_buttons;
static int32_t state_stick[4];
static int state_count;
static volatile int32_t rumble_low, rumble_high;   /* 0..65535, set by the vibration HLE */

#if !defined(__linux__) || defined(__EMSCRIPTEN__)
static int load(void) {
    char path[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, path, sizeof path);
    while (n && path[n - 1] != '\\' && path[n - 1] != '/') n--;
    snprintf(path + n, sizeof path - n, "SDL3.dll");
    HMODULE m = LoadLibraryA(path);
    if (!m) m = LoadLibraryA("SDL3.dll");
    if (!m) return 0;
#define GET(field, name) if (!(*(FARPROC *)&sdl.field = GetProcAddress(m, name))) return 0
    GET(Init, "SDL_Init");
    GET(SetHint, "SDL_SetHint");
    GET(GetGamepads, "SDL_GetGamepads");
    GET(Open, "SDL_OpenGamepad");
    GET(Close, "SDL_CloseGamepad");
    GET(Connected, "SDL_GamepadConnected");
    GET(Update, "SDL_UpdateGamepads");
    GET(GetButton, "SDL_GetGamepadButton");
    GET(GetAxis, "SDL_GetGamepadAxis");
    GET(Name, "SDL_GetGamepadName");
    GET(Id, "SDL_GetGamepadID");
    GET(Rumble, "SDL_RumbleGamepad");
    GET(GetError, "SDL_GetError");
    GET(Free, "SDL_free");
#undef GET
    return 1;
}

#endif

static int32_t stick_value(int16_t v, int flip) {
    int32_t a = v < -32767 ? -32767 : v;
    if (flip) a = -a;
    return (a > 7849 || a < -7849) ? a : 0;   /* XInput's dead zone, as nn_hid.c uses */
}

/* SDL's controller support started; 0 when it could not be */
static int pads_start(void) {
    /* poll in the background, whatever window is in front (nn_hid.c decides when input counts) */
    SDL_SetHint("SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS", "1");
    /* (PlayStation pads' rumble: SDL3 turns their enhanced reports on when a rumble is first asked. Nintendo pads come
       by position too, as the map below wants: SDL2 needed telling, or a Switch Pro Controller's A acted as B) */
    if (!SDL_Init(SDL_INIT_GAMEPAD)) {
        hos_log("hid: SDL could not start its controller support: %s", SDL_GetError());
        return 0;
    }
    hos_log("hid: SDL controller support ready (DualSense, DualShock 4, Switch Pro, Xbox and others)");
    return 1;
}

/* one poll: controllers plugged in or gone (each second), then every open one's buttons, sticks and rumble */
static void pads_poll(void) {
    static int64_t next_scan = 0;
    static uint16_t last_lo = 0, last_hi = 0;
    {
        SDL_UpdateGamepads();
        const int64_t now = hos_now_ns();
        if (now >= next_scan) {   /* new controllers (and ones that went away) */
            next_scan = now + 1000000000LL;
            for (int i = 0; i < MAX_PADS; i++)
                if (pads[i].gc && !SDL_GamepadConnected(pads[i].gc)) {
                    hos_log("hid: controller \"%s\" disconnected (SDL)", SDL_GetGamepadName(pads[i].gc));
                    SDL_CloseGamepad(pads[i].gc);
                    pads[i].gc = NULL;
                }
            int nj = 0;
            uint32_t *ids = SDL_GetGamepads(&nj);
            for (int j = 0; ids && j < nj; j++) {
                const uint32_t id = ids[j];
                int have = 0, slot = -1;
                for (int i = 0; i < MAX_PADS; i++) {
                    if (pads[i].gc && pads[i].id == id) have = 1;
                    if (!pads[i].gc && slot < 0) slot = i;
                }
                if (have || slot < 0) continue;
                Gamepad *gc = SDL_OpenGamepad(id);
                if (!gc) continue;
                pads[slot].gc = gc;
                pads[slot].id = SDL_GetGamepadID(gc);
                hos_log("hid: controller \"%s\" connected (SDL)", SDL_GetGamepadName(gc));
            }
            if (ids) SDL_free(ids);
        }
        uint64_t b = 0;
        int32_t s[4] = {0};
        int count = 0;
        const uint16_t lo = (uint16_t)__atomic_load_n(&rumble_low, __ATOMIC_RELAXED),
                       hi = (uint16_t)__atomic_load_n(&rumble_high, __ATOMIC_RELAXED);
        for (int i = 0; i < MAX_PADS; i++) {
            Gamepad *gc = pads[i].gc;
            if (!gc) continue;
            count++;
            for (int k = 0; k < BTN_N; k++)
                if (SDL_GetGamepadButton(gc, k)) b |= 1ULL << k;
            if (SDL_GetGamepadAxis(gc, AX_LT) > 8000) b |= 1ULL << HOS_PAD_LTRIGGER;
            if (SDL_GetGamepadAxis(gc, AX_RT) > 8000) b |= 1ULL << HOS_PAD_RTRIGGER;
            const int32_t v[4] = {stick_value(SDL_GetGamepadAxis(gc, AX_LX), 0), stick_value(SDL_GetGamepadAxis(gc, AX_LY), 1),
                                  stick_value(SDL_GetGamepadAxis(gc, AX_RX), 0), stick_value(SDL_GetGamepadAxis(gc, AX_RY), 1)};
            for (int k = 0; k < 4; k++)
                if ((v[k] < 0 ? -v[k] : v[k]) > (s[k] < 0 ? -s[k] : s[k])) s[k] = v[k];
            if (lo != last_lo || hi != last_hi) SDL_RumbleGamepad(gc, lo, hi, lo || hi ? 1000 : 0);
        }
        last_lo = lo;
        last_hi = hi;
        hos_rwlock_write(&state_lock);
        state_buttons = b;
        memcpy(state_stick, s, sizeof s);
        state_count = count;
        hos_rwlock_write_unlock(&state_lock);
    }
}

#ifdef _WIN32
static DWORD WINAPI pad_main(void *arg) {
    (void)arg;
    hos_host_thread_register_self("gamepads (SDL)");
    if (!pads_start()) return 0;
    for (;;) {
        pads_poll();
        Sleep(4);
    }
}
#else
/* (hookshot) Linux: SDL also runs the window there (host/linux/display_sdl.c), and its event queue is not for two
   threads: polled from the window's thread, between its events, instead of a thread of its own (two threads pumping
   it corrupted the heap and crashed the game once a controller was attached) */
static int32_t pads_loaded;
void hos_sdl_pads_pump(void) {
    static int state;   /* 0 not started, 1 polling, -1 not available */
    if (!state && __atomic_load_n(&pads_loaded, __ATOMIC_ACQUIRE)) state = pads_start() ? 1 : -1;
    if (state > 0) pads_poll();
}
#endif

/* The controllers' merged state (nn_hid.c): ORs their inputs in (1 << HOS_PAD_*), keeps the larger stick deflections; returns how many
   controllers SDL has open. Starts the SDL thread on the first call. */
int hos_sdl_pads(uint64_t *buttons, int32_t stick[4]) {
    static volatile int32_t started;
    if (!__atomic_exchange_n(&started, 1, __ATOMIC_ACQ_REL)) {
        if (getenv("HOS_NO_SDL")) return 0;
#if !defined(__linux__) || defined(__EMSCRIPTEN__)
        if (!load()) {
            hos_log("hid: SDL3.dll not found next to the exe: only XInput controllers (Xbox pads) work");
            return 0;
        }
#endif
#ifdef _WIN32
        CreateThread(NULL, 0, pad_main, NULL, 0, NULL);
#else
        __atomic_store_n(&pads_loaded, 1, __ATOMIC_RELEASE);   /* (started and polled by the window's thread: hos_sdl_pads_pump) */
#endif
    }
    hos_rwlock_read(&state_lock);
    const int n = state_count;
    *buttons |= state_buttons;
    for (int k = 0; k < 4; k++)
        if ((state_stick[k] < 0 ? -state_stick[k] : state_stick[k]) > (stick[k] < 0 ? -stick[k] : stick[k]))
            stick[k] = state_stick[k];
    hos_rwlock_read_unlock(&state_lock);
    return n;
}

/* Vibration from the game (0..1 amplitudes of the low and high bands), for the pads that rumble. */
void hos_sdl_rumble(float low, float high) {
    if (low < 0) low = 0;
    if (low > 1) low = 1;
    if (high < 0) high = 0;
    if (high > 1) high = 1;
    __atomic_exchange_n(&rumble_low, (int32_t)(low * 65535), __ATOMIC_SEQ_CST);
    __atomic_exchange_n(&rumble_high, (int32_t)(high * 65535), __ATOMIC_SEQ_CST);
}
