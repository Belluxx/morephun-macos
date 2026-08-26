#include "../mophun_os.h"
#include "../registers.h"
#include "input_facilities.h"

void MophunOS::vGetButtonData()
{
    uint32_t pressedKeys = 0;
    const Uint8* keystate = SDL_GetKeyboardState(NULL);

    if (keystate[input->keys[KEYUP]] || keystate[SDL_SCANCODE_W])
        pressedKeys += KEY_UP;
    if (keystate[input->keys[KEYDOWN]] || keystate[SDL_SCANCODE_S])
        pressedKeys += KEY_DOWN;
    if (keystate[input->keys[KEYLEFT]] || keystate[SDL_SCANCODE_A])
        pressedKeys += KEY_LEFT;
    if (keystate[input->keys[KEYRIGHT]] || keystate[SDL_SCANCODE_D])
        pressedKeys += KEY_RIGHT;
    if (keystate[input->keys[KEYFIRE]])         pressedKeys += KEY_FIRE;
    if (keystate[input->keys[KEYFIRE2]])        pressedKeys += KEY_FIRE2;
    // V-Rally treats the phone's center/5 action as Fire. The Sony Ericsson
    // implementation code 0xD6 is not a bit flag and would also set Down.
    if (keystate[input->keys[KEYSELECT]])       pressedKeys |= KEY_FIRE;
    if (keystate[SDL_SCANCODE_ESCAPE])           pressedKeys |= KEY_SELECT;
    if (keystate[input->keys[POINTERDOWN]])     pressedKeys += POINTER_DOWN;
    if (keystate[input->keys[POINTERALTDOWN]])  pressedKeys += POINTER_ALTDOWN;

    if (devtools.enabled())
    {
        pressedKeys |= devtools.scriptedKeys();
        devtools.applyPokes(*mophunVM);
    }

    if (turbo->enabled())
        pressedKeys = turbo->filterInput(pressedKeys);

    mophunVM->writeReg(r0, pressedKeys);
}
