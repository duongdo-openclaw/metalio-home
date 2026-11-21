#include "touch_map.h"

#define W 684

const char* touch_profile_name(touch_profile_t p) {
    switch (p) {
    case TOUCH_PROFILE_PORTRAIT: return "portrait (raw = logical)";
    case TOUCH_PROFILE_LANDSCAPE: return "landscape (controller rotated 90)";
    case TOUCH_PROFILE_DEAD: return "dead (no I2C ACK)";
    default: return "?";
    }
}

int touch_profile_raw(touch_profile_t p, int lx, int ly, int* raw_x, int* raw_y) {
    switch (p) {
    case TOUCH_PROFILE_PORTRAIT:
        *raw_x = lx;
        *raw_y = ly;
        return 1;
    case TOUCH_PROFILE_LANDSCAPE:
        /* Inverse of the firmware's own landscape branch:
         *   x = W-1-raw_y, y = raw_x  =>  raw_x = y, raw_y = W-1-x */
        *raw_x = ly;
        *raw_y = W - 1 - lx;
        return 1;
    default:
        return 0;
    }
}
