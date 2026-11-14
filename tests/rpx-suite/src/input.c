#include "input.h"

#include <vpad/input.h>

uint32_t input_pressed(void)
{
    VPADStatus status;
    VPADReadError error = VPAD_READ_SUCCESS;
    if (VPADRead(VPAD_CHAN_0, &status, 1, &error) < 1 || error != VPAD_READ_SUCCESS)
        return 0;
    return status.trigger;
}
