#ifndef PSYX_TOUCH_H
#define PSYX_TOUCH_H


// On-screen gamepad for touch devices (Android)

// Returns PSX pad buttons state, active low (0xFFFF = nothing pressed)
unsigned short PsyX_Touch_GetButtons();

// Draws the overlay over the current frame, call right before swapping buffers
void PsyX_Touch_Draw();

#endif // PSYX_TOUCH_H
