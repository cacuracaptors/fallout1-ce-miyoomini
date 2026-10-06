#include "plib/gnw/dxinput.h"

namespace fallout {

static bool dxinput_mouse_init();
static void dxinput_mouse_exit();
static bool dxinput_keyboard_init();
static void dxinput_keyboard_exit();

static int gMouseWheelDeltaX = 0;
static int gMouseWheelDeltaY = 0;

// 0x4E0400
bool dxinput_init()
{
    if (SDL_InitSubSystem(SDL_INIT_EVENTS) != 0) {
        return false;
    }

    if (!dxinput_mouse_init()) {
        goto err;
    }

    if (!dxinput_keyboard_init()) {
        goto err;
    }

    return true;

err:

    dxinput_mouse_exit();

    return false;
}

// 0x4E0478
void dxinput_exit()
{
    SDL_QuitSubSystem(SDL_INIT_EVENTS);
}

// 0x4E04E8
bool dxinput_acquire_mouse()
{
    return true;
}

// 0x4E0514
bool dxinput_unacquire_mouse()
{
    return true;
}

// 0x4E053C
bool dxinput_get_mouse_state(MouseData* mouseState)
{
    // CE: This function is sometimes called outside loops calling `get_input`
    // and subsequently `GNW95_process_message`, so mouse events might not be
    // handled by SDL yet.
    //
    // TODO: Move mouse events processing into `GNW95_process_message` and
    // update mouse position manually.
    SDL_PumpEvents();

    Uint32 buttons = SDL_GetRelativeMouseState(&(mouseState->x), &(mouseState->y));
    mouseState->buttons[0] = (buttons & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
    mouseState->buttons[1] = (buttons & SDL_BUTTON(SDL_BUTTON_RIGHT)) != 0;
    mouseState->wheelX = gMouseWheelDeltaX;
    mouseState->wheelY = gMouseWheelDeltaY;

    gMouseWheelDeltaX = 0;
    gMouseWheelDeltaY = 0;

    // BEGIN Miyoo Mini D-pad-as-mouse patch
    // The cursor speed is in pixels per second, like a real mouse, so it is
    // the same on every screen. It used to move a fixed number of pixels each
    // time the game read the mouse, so it slowed down wherever the game loop
    // runs slower (the 24 fps text entry screens, busy maps).
    static Uint32 padLastTicks = 0;
    static float padRemainderX = 0.0f;
    static float padRemainderY = 0.0f;
    Uint32 padNow = SDL_GetTicks();
    Uint32 padElapsed = padLastTicks != 0 ? padNow - padLastTicks : 0;
    padLastTicks = padNow;
    if (padElapsed > 100) {
        // Long gap (loading, a pause): do not jump.
        padElapsed = 100;
    }

    const Uint8* padState = SDL_GetKeyboardState(NULL);
    if (!padState[SDL_SCANCODE_RCTRL]) {
        // 360 and 120 pixels per second: the old 6 and 2 pixels per read at
        // 60 reads per second.
        float padSpeed = padState[SDL_SCANCODE_LSHIFT] ? 120.0f : 360.0f;
        int padDirX = (padState[SDL_SCANCODE_RIGHT] ? 1 : 0) - (padState[SDL_SCANCODE_LEFT] ? 1 : 0);
        int padDirY = (padState[SDL_SCANCODE_DOWN] ? 1 : 0) - (padState[SDL_SCANCODE_UP] ? 1 : 0);
        float padDistance = padSpeed * static_cast<float>(padElapsed) / 1000.0f;
        padRemainderX = padDirX != 0 ? padRemainderX + padDirX * padDistance : 0.0f;
        padRemainderY = padDirY != 0 ? padRemainderY + padDirY * padDistance : 0.0f;
        int padStepX = static_cast<int>(padRemainderX);
        int padStepY = static_cast<int>(padRemainderY);
        padRemainderX -= padStepX;
        padRemainderY -= padStepY;
        mouseState->x += padStepX;
        mouseState->y += padStepY;
        if (padState[SDL_SCANCODE_T]) mouseState->buttons[0] = true;
        if (padState[SDL_SCANCODE_E]) mouseState->buttons[1] = true;
    } else {
        padRemainderX = 0.0f;
        padRemainderY = 0.0f;
    }
    // END Miyoo Mini D-pad-as-mouse patch

    return true;
}

// 0x4E05A8
bool dxinput_acquire_keyboard()
{
    return true;
}

// 0x4E05D4
bool dxinput_unacquire_keyboard()
{
    return true;
}

void miyooResyncKeyState(); // input.cc

// 0x4E05FC
bool dxinput_flush_keyboard_buffer()
{
    SDL_FlushEvents(SDL_KEYDOWN, SDL_TEXTINPUT);
    miyooResyncKeyState();
    return true;
}

// 0x4E0650
bool dxinput_read_keyboard_buffer(KeyboardData* keyboardData)
{
    return true;
}

// 0x4E070C
bool dxinput_mouse_init()
{
    SDL_SetRelativeMouseMode(SDL_TRUE);
    return true;
}

// 0x4E078C
void dxinput_mouse_exit()
{
}

// 0x4E07B8
bool dxinput_keyboard_init()
{
    return true;
}

// 0x4E0874
void dxinput_keyboard_exit()
{
}

void handleMouseEvent(SDL_Event* event)
{
    // Mouse movement and buttons are accumulated in SDL itself and will be
    // processed later in `mouseDeviceGetData` via `SDL_GetRelativeMouseState`.

    if (event->type == SDL_MOUSEWHEEL) {
        gMouseWheelDeltaX += event->wheel.x;
        gMouseWheelDeltaY += event->wheel.y;
    }
}

} // namespace fallout
