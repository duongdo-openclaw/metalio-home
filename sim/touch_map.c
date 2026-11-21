/*
 * Touch coordinate mapping copied VERBATIM from the Metalio E-Ink4-Plus firmware
 * (main/boards/metalio-e-ink4-plus/display/metalio_touch.c, map_to_logical, v2.0.82).
 * Upstream: https://github.com/CloudZao/Metalio-E-INK4-Plus — MIT License.
 *
 * Kept byte-for-byte identical (only renamed + made non-static) so the simulator
 * reproduces exactly what the device does with raw FT6336U coordinates.
 */
#include "touch_map.h"

#define EPD_LOGICAL_W 684  /* main/display/epd_font24/epd_font24.h */
#define EPD_LOGICAL_H 1216

void fw_map_to_logical(int raw_x, int raw_y, int* lx, int* ly) {
    int x = raw_x;
    int y = raw_y;
    if (raw_y >= EPD_LOGICAL_W && raw_x < EPD_LOGICAL_W) {
        x = raw_x;
        y = raw_y;
    } else if (raw_x >= EPD_LOGICAL_W) {
        x = EPD_LOGICAL_W - 1 - raw_y;
        y = raw_x;
    }
    if (x < 0) {
        x = 0;
    } else if (x >= EPD_LOGICAL_W) {
        x = EPD_LOGICAL_W - 1;
    }
    if (y < 0) {
        y = 0;
    } else if (y >= EPD_LOGICAL_H) {
        y = EPD_LOGICAL_H - 1;
    }
    *lx = x;
    *ly = y;
}
