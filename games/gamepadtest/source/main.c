// GamePad Test: shows every Wii U GamePad input on the GamePad screen (and a summary on the TV).
// Used to check the mobile GamePad end to end: DRC image, buttons, sticks, touch and motion.
#include <coreinit/cache.h>
#include <coreinit/screen.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vpad/input.h>
#include <whb/proc.h>

#define DRC_W 854
#define DRC_H 480

static const struct {
    uint32_t bit;
    const char *name;
} kButtons[] = {
        {VPAD_BUTTON_A, "A"},         {VPAD_BUTTON_B, "B"},         {VPAD_BUTTON_X, "X"},       {VPAD_BUTTON_Y, "Y"},
        {VPAD_BUTTON_UP, "UP"},       {VPAD_BUTTON_DOWN, "DOWN"},   {VPAD_BUTTON_LEFT, "LEFT"}, {VPAD_BUTTON_RIGHT, "RIGHT"},
        {VPAD_BUTTON_L, "L"},         {VPAD_BUTTON_R, "R"},         {VPAD_BUTTON_ZL, "ZL"},     {VPAD_BUTTON_ZR, "ZR"},
        {VPAD_BUTTON_PLUS, "+"},      {VPAD_BUTTON_MINUS, "-"},     {VPAD_BUTTON_STICK_L, "L3"}, {VPAD_BUTTON_STICK_R, "R3"},
};

static void rect(int x, int y, int w, int h, uint32_t color)
{
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++)
            if (i >= 0 && j >= 0 && i < DRC_W && j < DRC_H)
                OSScreenPutPixelEx(SCREEN_DRC, i, j, color);
}

static void frame(int x, int y, int w, int h, uint32_t color)
{
    rect(x, y, w, 2, color);
    rect(x, y + h - 2, w, 2, color);
    rect(x, y, 2, h, color);
    rect(x + w - 2, y, 2, h, color);
}

// A stick: square well with the current position as a dot.
static void stick(int x, int y, float sx, float sy, const char *label, int col, int row)
{
    const int size = 120;
    frame(x, y, size, size, 0x4A5871FF);
    rect(x + size / 2 - 1, y + 4, 2, size - 8, 0x2A3548FF);
    rect(x + 4, y + size / 2 - 1, size - 8, 2, 0x2A3548FF);
    const int px = x + size / 2 + (int) (sx * (size / 2 - 10));
    const int py = y + size / 2 - (int) (sy * (size / 2 - 10));
    rect(px - 7, py - 7, 14, 14, 0x19B8F0FF);
    OSScreenPutFontEx(SCREEN_DRC, col, row, label);
}

int main(int argc, char **argv)
{
    WHBProcInit();
    OSScreenInit();
    const uint32_t tvSize = OSScreenGetBufferSizeEx(SCREEN_TV), drcSize = OSScreenGetBufferSizeEx(SCREEN_DRC);
    void *tv = memalign(0x100, tvSize), *drc = memalign(0x100, drcSize);
    OSScreenSetBufferEx(SCREEN_TV, tv);
    OSScreenSetBufferEx(SCREEN_DRC, drc);
    OSScreenEnableEx(SCREEN_TV, TRUE);
    OSScreenEnableEx(SCREEN_DRC, TRUE);

    char line[128];
    int lastTouchX = -1, lastTouchY = -1;
    uint32_t frames = 0;
    while (WHBProcIsRunning()) {
        VPADStatus pad;
        VPADReadError error;
        memset(&pad, 0, sizeof pad);
        VPADRead(VPAD_CHAN_0, &pad, 1, &error);
        VPADTouchData touch;
        VPADGetTPCalibratedPoint(VPAD_CHAN_0, &touch, &pad.tpNormal);

        OSScreenClearBufferEx(SCREEN_DRC, 0x0E1420FF);
        OSScreenClearBufferEx(SCREEN_TV, 0x101820FF);

        OSScreenPutFontEx(SCREEN_DRC, 1, 1, "WEMU GAMEPAD TEST");
        snprintf(line, sizeof line, "frame %lu", (unsigned long) frames++);
        OSScreenPutFontEx(SCREEN_DRC, 90, 1, line);

        // Buttons: a lit tile per held button.
        for (unsigned i = 0; i < sizeof kButtons / sizeof kButtons[0]; i++) {
            const int x = 16 + (int) (i % 8) * 104, y = 48 + (int) (i / 8) * 40;
            const int held = (pad.hold & kButtons[i].bit) != 0;
            if (held)
                rect(x, y, 96, 32, 0x19B8F0FF);
            else
                frame(x, y, 96, 32, 0x2A3548FF);
            OSScreenPutFontEx(SCREEN_DRC, (x + 8) / 8, (y + 8) / 16, kButtons[i].name);
        }

        stick(40, 150, pad.leftStick.x, pad.leftStick.y, "LEFT STICK", 5, 18);
        stick(200, 150, pad.rightStick.x, pad.rightStick.y, "RIGHT STICK", 25, 18);
        snprintf(line, sizeof line, "L %+.2f %+.2f   R %+.2f %+.2f", pad.leftStick.x, pad.leftStick.y, pad.rightStick.x, pad.rightStick.y);
        OSScreenPutFontEx(SCREEN_DRC, 5, 20, line);
        snprintf(line, sizeof line, "acc %+.2f %+.2f %+.2f", pad.accelorometer.acc.x, pad.accelorometer.acc.y, pad.accelorometer.acc.z);
        OSScreenPutFontEx(SCREEN_DRC, 5, 22, line);
        snprintf(line, sizeof line, "gyro %+.2f %+.2f %+.2f", pad.gyro.x, pad.gyro.y, pad.gyro.z);
        OSScreenPutFontEx(SCREEN_DRC, 5, 23, line);

        // Touch: a trail of the last point plus a crosshair where the finger is.
        if (touch.touched) {
            lastTouchX = touch.x;
            lastTouchY = touch.y;
            snprintf(line, sizeof line, "TOUCH %3d,%3d", touch.x, touch.y);
        } else
            snprintf(line, sizeof line, "TOUCH -- (tap anywhere)");
        OSScreenPutFontEx(SCREEN_DRC, 53, 8, line);
        frame(420, 150, 400, 225, 0x2A3548FF);
        if (lastTouchX >= 0) {
            const uint32_t color = touch.touched ? 0xFF5D6CFF : 0x8D9AB0FF;
            rect(lastTouchX - 12, lastTouchY - 1, 24, 3, color);
            rect(lastTouchX - 1, lastTouchY - 12, 3, 24, color);
        }

        OSScreenPutFontEx(SCREEN_TV, 2, 2, "WEMU GamePad Test - look at the GamePad screen");
        snprintf(line, sizeof line, "buttons %08lX  touch %s", (unsigned long) pad.hold, touch.touched ? "yes" : "no");
        OSScreenPutFontEx(SCREEN_TV, 2, 4, line);

        DCFlushRange(tv, tvSize);
        DCFlushRange(drc, drcSize);
        OSScreenFlipBuffersEx(SCREEN_DRC);
        OSScreenFlipBuffersEx(SCREEN_TV);
    }

    OSScreenShutdown();
    free(tv);
    free(drc);
    WHBProcShutdown();
    return 0;
}
