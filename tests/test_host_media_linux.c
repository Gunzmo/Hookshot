/* Exercise the production SDL device boundary with virtual pads and dummy audio. */
#include "hos.h"
#include <assert.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../host/pad_sdl.c"
#include "../host/nn_audio.c"

int hos_trace;
static int64_t mock_ns = 1;
void hos_log(const char *fmt, ...) { (void)fmt; }
int64_t hos_now_ns(void) { return mock_ns; }
void hos_sdl_identity(void) { SDL_SetAppMetadata("Hookshot host test", NULL, "io.github.Hookshot.test"); }
static uint16_t rumble_seen[2];
static bool rumble(void *arg, uint16_t low, uint16_t high) {
    (void)arg; rumble_seen[0] = low; rumble_seen[1] = high; return true;
}
static SDL_JoystickID attach(void) {
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = 6; desc.nbuttons = 15;
    desc.axis_mask = (1u << 6) - 1; desc.button_mask = (1u << 15) - 1;
    desc.name = "Hookshot virtual controller"; desc.Rumble = rumble;
    SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc); assert(id && SDL_IsGamepad(id));
    return id;
}
static void controllers(void) {
    uint64_t buttons = 0; int32_t sticks[4] = {0};
    hos_sdl_pads(&buttons, sticks);   /* initialization is deferred to the window/event thread */
    hos_sdl_pads_pump();
    SDL_JoystickID first = attach(), second = attach();
    SDL_Joystick *a = SDL_OpenJoystick(first), *b = SDL_OpenJoystick(second); assert(a && b);
    assert(SDL_SetJoystickVirtualButton(a, SDL_GAMEPAD_BUTTON_SOUTH, true));
    assert(SDL_SetJoystickVirtualButton(b, SDL_GAMEPAD_BUTTON_EAST, true));
    assert(SDL_SetJoystickVirtualAxis(a, SDL_GAMEPAD_AXIS_LEFTX, 20000));
    assert(SDL_SetJoystickVirtualAxis(b, SDL_GAMEPAD_AXIS_LEFTX, -25000));
    assert(SDL_SetJoystickVirtualAxis(a, SDL_GAMEPAD_AXIS_LEFTY, -32768));
    assert(SDL_SetJoystickVirtualAxis(a, SDL_GAMEPAD_AXIS_RIGHTX, 100));
    mock_ns += 1000000001;
    hos_sdl_pads_pump(); buttons = 0; memset(sticks, 0, sizeof sticks);
    assert(hos_sdl_pads(&buttons, sticks) == 2);
    assert(buttons & (1ull << HOS_PAD_SOUTH)); assert(buttons & (1ull << HOS_PAD_EAST));
    assert(sticks[0] == -25000 && sticks[1] == 32767 && !sticks[2]);
    hos_sdl_rumble(2.0f, -1.0f); hos_sdl_pads_pump();
    assert(rumble_seen[0] == 65535 && !rumble_seen[1]);
    SDL_CloseJoystick(a); SDL_CloseJoystick(b);
    assert(SDL_DetachVirtualJoystick(first) && SDL_DetachVirtualJoystick(second));
    mock_ns += 1000000001; hos_sdl_pads_pump();
    buttons = 0; memset(sticks, 0, sizeof sticks);
    assert(hos_sdl_pads(&buttons, sticks) == 0 && !buttons && !sticks[0] && !sticks[1]);
}
static void audio(void) {
    char filename[] = "/tmp/hookshot-audio-XXXXXX";
    int fd = mkstemp(filename); assert(fd >= 0); close(fd);
    assert(!setenv("HOS_AUDIO_DUMP", filename, 1));
    output_open(); assert(wout); assert(SDL_PauseAudioStreamDevice(wout));
    int16_t pcm[FRAME * 2]; for (int i = 0; i < FRAME * 2; i++) pcm[i] = (int16_t)(i * 13 - 3000);
    output_frame(pcm); assert(!SDL_GetAudioStreamQueued(wout));
    output_frame(pcm); assert(SDL_GetAudioStreamQueued(wout) == (int)sizeof blocks[0]);
    int16_t played[FRAME * 4];
    assert(SDL_GetAudioStreamData(wout, played, sizeof played) == sizeof played);
    assert(!memcmp(played, pcm, sizeof pcm) && !memcmp(played + FRAME * 2, pcm, sizeof pcm));
    for (int i = 0; i < NBLOCKS * 8; i++) output_frame(pcm);
    assert(SDL_GetAudioStreamQueued(wout) <= NBLOCKS * (int)sizeof blocks[0]);
    assert(SDL_ResumeAudioStreamDevice(wout)); SDL_Delay(50);
    assert(SDL_GetAudioStreamQueued(wout) < NBLOCKS * (int)sizeof blocks[0]);
    dump_finish(); fclose(dump); dump = NULL;
    FILE *f = fopen(filename, "rb"); assert(f);
    uint8_t header[44]; assert(fread(header, 1, 44, f) == 44);
    assert(!memcmp(header, "RIFF", 4) && !memcmp(header + 8, "WAVEfmt ", 8));
    uint32_t size; memcpy(&size, header + 40, 4); assert(size == (2 + NBLOCKS * 8) * FRAME * 4);
    int16_t saved[FRAME * 2]; assert(fread(saved, 1, sizeof saved, f) == sizeof saved);
    assert(!memcmp(saved, pcm, sizeof pcm)); fclose(f); unlink(filename);
    SDL_DestroyAudioStream(wout); wout = NULL;
    unsetenv("HOS_AUDIO_DUMP"); setenv("HOS_AUDIO", "0", 1);
    output_open(); assert(!wout);
}
int main(void) {
    controllers(); audio(); SDL_Quit();
    puts("native SDL virtual controllers, rumble, bounded audio and PCM dump passed");
    return 0;
}
