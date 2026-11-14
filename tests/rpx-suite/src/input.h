#pragma once

#include <stdint.h>

// GamePad buttons newly pressed since the previous poll (VPAD_BUTTON_* bits).
uint32_t input_pressed(void);
