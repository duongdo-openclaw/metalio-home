// Characterises the firmware's map_to_logical() against two controller orientations.
#include "check.h"
#include "touch_map.h"

int main() {
    const int W = 684, H = 1216;
    long total = 0, portrait_ok = 0, land_ok = 0, land_bad_top = 0, land_bad_bottom = 0;
    for (int y = 0; y < H; y += 4) {
        for (int x = 0; x < W; x += 4) {
            int rx, ry, lx, ly;
            ++total;
            touch_profile_raw(TOUCH_PROFILE_PORTRAIT, x, y, &rx, &ry);
            fw_map_to_logical(rx, ry, &lx, &ly);
            if (lx == x && ly == y) ++portrait_ok;
            touch_profile_raw(TOUCH_PROFILE_LANDSCAPE, x, y, &rx, &ry);
            fw_map_to_logical(rx, ry, &lx, &ly);
            if (lx == x && ly == y) ++land_ok;
            else if (y < W) ++land_bad_top;
            else ++land_bad_bottom;
        }
    }
    std::printf("portrait controller : %ld/%ld points correct (%.1f%%)\n", portrait_ok, total, 100.0 * portrait_ok / total);
    std::printf("landscape controller: %ld/%ld points correct (%.1f%%), wrong in top area: %ld, bottom: %ld\n",
                land_ok, total, 100.0 * land_ok / total, land_bad_top, land_bad_bottom);

    // Portrait controllers are mapped perfectly.
    CHECK(portrait_ok == total);
    // Landscape controllers: the firmware only rotates when raw_x >= 684, i.e. logical y >= 684.
    // Everything above y = 684 (56% of the screen) passes through unrotated and lands elsewhere.
    CHECK(land_bad_bottom == 0);
    CHECK(land_bad_top > 0);
    {   // concrete example: finger on the top-left "Back" button
        int rx, ry, lx, ly;
        touch_profile_raw(TOUCH_PROFILE_LANDSCAPE, 80, 66, &rx, &ry);
        fw_map_to_logical(rx, ry, &lx, &ly);
        std::printf("landscape: finger (80,66) -> raw (%d,%d) -> firmware puts it at (%d,%d)\n", rx, ry, lx, ly);
        CHECK(!(lx == 80 && ly == 66));
    }
    // Dead controller produces no sample at all.
    int rx, ry;
    CHECK(touch_profile_raw(TOUCH_PROFILE_DEAD, 10, 10, &rx, &ry) == 0);
    DONE();
}
