#pragma once
#include <string>

#include "../app/platform.h"

void sim_platform_init(const platform::Config& cfg, const std::string& sd_root);
void sim_audio_set_fake_mic(bool fake);
void sim_audio_shutdown();
void sim_platform_shutdown();
void sim_platform_poll();   // call from the UI loop: delivers HTTP callbacks
int sim_platform_busy();    // requests still in flight
void sim_set_touch(const platform::TouchSample& s);
void sim_set_touch_info(const std::string& s);

// ---- simulated device services (sim/device_sim.cc, sim/transfer_sim.cc) ----
struct SimDevice {
    int light_mode = 0;      // settings::Light
    int brightness = 40;
    int black_depth = 0;
    bool wifi = true;
    int flash_frames = 0;    // >0: show a full-refresh flash
    bool dirty = true;
};
SimDevice& SimDeviceState();
void sim_device_set_sd(const std::string& sd);
std::string sim_lan_address();
void sim_transfer_set_sd(const std::string& sd);
void sim_transfer_poll();
void sim_device_poll();   // delivers SD-format progress on the UI thread
