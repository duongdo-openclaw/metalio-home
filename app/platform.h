// Platform seam between the portable screens (app/) and the host.
//   Simulator: sim/platform_sim.cc (libcurl worker thread, local ./sdcard folder).
//   Firmware:  to be implemented with the firmware's Http class + a FreeRTOS task + sd_access.
// Every callback is delivered on the LVGL/UI thread, never from the worker.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace settings { struct Values; }

namespace platform {

struct HttpResponse {
    int status = 0;          // 0 = transport failure (DNS, TCP, TLS, timeout)
    std::string body;
    std::string error;       // human readable when status == 0
    bool ok() const { return status >= 200 && status < 300; }
};

using HttpDone = std::function<void(const HttpResponse&)>;
using Progress = std::function<void(uint64_t done, uint64_t total)>;  // total 0 = unknown

struct HttpRequest {
    std::string method = "GET";
    std::string url;
    std::string body;
    std::string content_type;
    std::string token;       // sent as "X-Kiosk-Token" when not empty
    std::vector<std::pair<std::string, std::string>> headers;   // extra headers (AI API keys)
    int timeout_ms = 8000;
    size_t max_body = 512 * 1024;
};

// Returns a request id (>0). Cancel is best effort; a cancelled request never calls `done`.
uint32_t Http(const HttpRequest& req, HttpDone done);
// Streams the body to `path` (written to path + ".part", renamed on success).
uint32_t Download(const std::string& url, const std::string& path, Progress progress, HttpDone done);
void Cancel(uint32_t id);

// Settings and paths.
struct Config {
    std::string kiosk_base_url;   // e.g. http://192.168.1.50:8124
    std::string kiosk_token;      // optional shared secret for the bridge
    std::string drive_folder;     // pasted public Drive folder link
    std::string books_dir;        // where downloaded books go (SD: /sdcard/books)
    bool allow_control = false;   // false = read-only: never sends commands
};
const Config& GetConfig();

bool FileExists(const std::string& path);

// Last touch sample for diagnostics. raw_* = what the FT6336U reported, logical_* = after the
// firmware mapping. finger_* is only known in the simulator (where the mouse really was).
struct TouchSample {
    bool valid = false;
    bool finger_known = false;
    int finger_x = 0, finger_y = 0;
    int raw_x = 0, raw_y = 0;
    int logical_x = 0, logical_y = 0;
};
TouchSample LastTouch();

// ---- files ----
std::vector<std::string> ListFiles(const std::string& dir, const std::vector<std::string>& exts);  // names, sorted
bool ReadText(const std::string& path, std::string& out);
// Replaces the file as atomically as the file system allows. Firmware note: FatFs refuses to
// rename onto an existing file, so the ESP-IDF version must remove the old file first and recover
// from a leftover ".tmp" on read (docs/firmware-port.md, risk R4).
bool WriteText(const std::string& path, const std::string& data);
bool RemoveFile(const std::string& path);
uint64_t FileSize(const std::string& path);
std::string MusicDir();        // <sd>/music
std::string RecordingsDir();   // <sd>/recordings
std::string StateDir();        // <sd>/.metalio  (reading progress, settings)
std::string FontsDir();        // <sd>/fonts  (extra TTF/OTF reading fonts)
std::string SdRoot();          // card root (/sdcard on the device)
std::string DictDir();         // <sd>/dict   StarDict dictionaries (.ifo + .idx + .dict[.dz])
std::string VocabDir();        // <sd>/vocab  word lists (TSV) for "Học từ vựng"
std::string NotesDir();        // <sd>/notes  notes exported as Markdown, one file per book
struct DirEntry { std::string name; bool dir = false; uint64_t size = 0; };
std::vector<DirEntry> ListDir(const std::string& dir);   // no hidden entries, sorted by name
bool MakeDir(const std::string& path);                    // one level; true if it exists afterwards
// Random access read (dictionary data files are tens of MB: never read them whole).
bool ReadRange(const std::string& path, uint64_t offset, size_t len, std::string& out);

// ---- settings hooks (firmware: screen_frontlight, epd black depth, haptic motor, epd refresh) ----
void ApplySettings(const settings::Values& v);   // frontlight, black depth, radios…
void Vibrate(int strength);                      // 0 off .. 3 strong (short tap feedback)
void FullRefresh();                              // one flashing full-panel refresh (clears ghosting)
void PowerOff();                                 // power key held 3 s (firmware: P13 shutdown pulse)
// Rotates the UI (firmware: DisplayUiSetOrient). Changes the LVGL resolution synchronously:
// 0/3 = 680x1212 portrait (3 upside down), 1/2 = 1212x680 landscape.
void SetDisplayOrientation(int orient);
int DisplayOrientation();

struct StorageInfo { bool present = false; uint64_t total = 0, free = 0; };
StorageInfo Storage();                           // SD card

// Erase and re-create the SD card file system (FAT32; firmware: esp_vfs_fat_sdcard_format).
// Runs in the background; `progress` and `done` are called on the UI thread.
// keep_settings: owner info + device settings (in <sd>/.metalio) survive the format.
using FormatProgress = std::function<void(int percent, const std::string& stage)>;
using FormatDone = std::function<void(bool ok, const std::string& error)>;
void StorageFormat(bool keep_settings, FormatProgress progress, FormatDone done);
bool StorageFormatting();

struct WifiNetwork { std::string ssid; int rssi = 0; bool connected = false, secured = true; };
std::vector<WifiNetwork> WifiScan();
std::vector<std::string> BluetoothScan();        // nearby BLE page turners (names)
bool UsbDiskSupported();                         // firmware: usb_virtual_disk (MSC)

struct DeviceInfo { std::string model, chip, panel, mac, firmware; };
DeviceInfo GetDeviceInfo();

// ---- file transfer over Wi-Fi (firmware: esp_http_server, like cloud_screen/ap/ap_transfer) ----
struct TransferEvent {
    std::string action;   // "upload", "delete", "error"
    std::string dir, name;
    uint64_t bytes = 0;
    bool ok = true;
    std::string detail;
};
// Wifi: serve on the home network. Hotspot: the device opens its own access point (firmware:
// esp_wifi AP mode, 192.168.4.1) so a phone can connect without any router.
enum class TransferMode { Wifi, Hotspot };
struct TransferInfo {
    std::string url;
    std::string ssid, password;   // hotspot credentials (empty in Wi-Fi mode)
};
bool TransferStart(TransferMode mode, std::function<void(const TransferEvent&)> on_event, TransferInfo& info, std::string& err);
void TransferStop();
// Read-only sharing on the same server: a page at info.url (random token in the path) lists
// `items` for download; nothing can be uploaded or deleted. Also serves <url>list.json so another
// Metalio can receive the books ("Nhận sách"). Events: action "share_download". Stop: TransferStop.
struct ShareItem { std::string path, name; uint64_t size = 0; };
bool ShareStart(TransferMode mode, const std::string& title, const std::vector<ShareItem>& items,
                std::function<void(const TransferEvent&)> on_event, TransferInfo& info, std::string& err);
bool TransferRunning();

// ---- device status (firmware: BQ27220 fuel gauge, Wi-Fi driver) ----
int BatteryPercent();          // -1 unknown
// Battery details (firmware: BQ27220 gauge) and charge current (CX25601N, stock options
// 500 / 1000 / 1500 mA, kept in NVS "charger/current_ma" by MetalioPower_SetChargeCurrentMa).
struct BatteryInfo { int percent = -1; bool charging = false; int voltage_mv = 0; int current_ma = 0; };
BatteryInfo Battery();
int ChargeCurrentMa();
bool SetChargeCurrentMa(int ma);   // false when the charger refuses (value kept unchanged)
bool Charging();
std::string WifiName();        // "" when not connected

// ---- audio playback (firmware: app_audio_player; MP3 needs a decoder added on device) ----
enum class PlayState { Stopped, Loading, Playing, Paused, Ended, Error };
struct PlayStatus {
    PlayState state = PlayState::Stopped;
    std::string path;
    uint64_t position_ms = 0, duration_ms = 0;
    int volume = 70;           // 0..100
    std::string error;
};
bool AudioPlay(const std::string& path);   // replaces whatever is playing
void AudioPause(bool paused);
void AudioStop();
void AudioSeek(uint64_t ms);
void AudioSetVolume(int volume);
PlayStatus AudioStatus();

// ---- recording (firmware: app_audio_recorder, 16 kHz mono 16-bit WAV) ----
enum class RecState { Idle, Recording, Paused, Saved, Error };
struct RecStatus {
    RecState state = RecState::Idle;
    uint64_t duration_ms = 0;
    int level = 0;             // 0..100 peak of the last ~100 ms
    std::string path, error;
};
bool RecStart(const std::string& path);
void RecPause(bool paused);
bool RecStop();                // writes the WAV file
RecStatus RecGetStatus();
const char* TouchDriverInfo();   // e.g. "FT6336 fw=0x.. ok" or simulator profile name
void Log(const char* fmt, ...);

}  // namespace platform
