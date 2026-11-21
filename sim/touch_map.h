#pragma once
#ifdef __cplusplus
extern "C" {
#endif

/* Firmware's raw FT6336U -> logical portrait (684x1216) mapping, unmodified. */
void fw_map_to_logical(int raw_x, int raw_y, int* lx, int* ly);

/*
 * Panel profiles: how a given touch controller reports a finger at logical (lx, ly).
 * These model hardware/firmware variants of the FT6336U config, so the simulator
 * can show what the firmware mapping does with each of them.
 */
typedef enum {
    TOUCH_PROFILE_PORTRAIT = 0,  /* controller reports portrait 684x1216 (raw == logical) */
    TOUCH_PROFILE_LANDSCAPE,     /* controller reports landscape 1216x684 (rotated 90°) */
    TOUCH_PROFILE_DEAD,          /* no I2C ACK at init: firmware disables touch entirely */
    TOUCH_PROFILE_COUNT
} touch_profile_t;

const char* touch_profile_name(touch_profile_t p);

/* Returns 0 when the controller produces no sample (dead), else fills raw coords. */
int touch_profile_raw(touch_profile_t p, int lx, int ly, int* raw_x, int* raw_y);

#ifdef __cplusplus
}
#endif
