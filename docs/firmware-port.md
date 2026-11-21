# Đưa Metalio Home xuống máy thật (Bước 0)

Tài liệu này dành cho người làm phần firmware. Nó gồm thông số phần cứng đã đọc từ mã firmware gốc, kế hoạch triển khai theo từng nấc có điểm kiểm tra, cách cài đặt `platform.h` trên ESP-IDF, và danh sách rủi ro kèm cách xử lý.

## 1. Thông số đã xác minh

Lấy từ mã nguồn firmware gốc Metalio (MIT; nhánh của xiaozhi-esp32): `main/boards/metalio-e-ink4-plus/`, `sdkconfig`, `partitions.csv`. Đối chiếu thêm với bản sao lưu flash 16 MB của máy đang dùng.

| Hạng mục | Giá trị |
|---|---|
| SoC | ESP32-S31 (RISC-V), flash 16 MB, PSRAM octal 200 MHz (dung lượng do firmware tự dò) |
| Màn | ED047TC2 1216×684, epdiy; dùng dọc; vẽ I1, chuyển L8 khi có nội dung xám (`EpdSetContentRole`) |
| LVGL | 9.5.0, `CONFIG_LV_COLOR_DEPTH=1`; đang bật ARC, KEYBOARD, TEXTAREA, QRCODE; **đang tắt** TINY_TTF, TJPGD |
| Cảm ứng | FT6336U I2C, INT GPIO5; phím viền ảo ở Y≈1600: home x=100, back x=200, next x=300, sai số 30, giữ 500 ms |
| Phím | BOOT GPIO61; TCA9535: P10 Vol+, P11 Vol–, P17 Nguồn, P13 xung tắt máy (giữ 3 giây, 15 xung) |
| Pin / sạc | BQ27220 @0x55 (đo pin), CX25601N @0x6B (sạc); dòng sạc 500/1000/1500 mA qua `MetalioPower_SetChargeCurrentMa`, lưu NVS `charger/current_ma` |
| Đèn nền | YX6016N: GPIO6 lạnh, GPIO7 ấm |
| SD | SDMMC slot0 (GPIO20–25); FATFS LFN trên heap, **API mã UTF-8** |
| Watchdog | `CONFIG_ESP_TASK_WDT_TIMEOUT_S=20` |
| TLS | `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y` |

### Phân vùng flash (16 MB)

| Tên | Offset | Dung lượng | Đã dùng (bản sao lưu) |
|---|---|---|---|
| nvs | 0x9000 | 16 KB | |
| otadata / phy_init | 0xd000 / 0xf000 | 8 / 4 KB | |
| model (spiffs) | 0x10000 | 400 KB | |
| **ota_0** | 0x80000 | **5 MB** | **4,97 MB** |
| ota_1 | 0x580000 | 5 MB | 0 |
| resources | 0xa80000 | 400 KB | 0,29 MB |
| **font_data** | 0xae4000 | **5 MB** | **4,75 MB** |
| coredump | 0xfe4000 | 64 KB | |

## 2. Kế hoạch theo nấc

Mỗi nấc phải nạp được, khởi động được và **cảm ứng vẫn dùng được** rồi mới sang nấc sau. Luôn giữ sẵn bản sao lưu flash đầy đủ để khôi phục (`esptool write_flash 0 backup.bin`).

| Nấc | Việc | Điểm kiểm tra |
|---|---|---|
| 0.1 | Cài ESP-IDF đúng phiên bản firmware gốc dùng; build và nạp firmware gốc từ mã nguồn | Máy chạy giống bản gốc; ghi lại log `metalio_touch` (để xác nhận R9) |
| 0.2 | Thêm `app_core` thành một component (thuần C++, không LVGL) và chạy các unit test ngay trên máy (dùng `platform_stub` với VFS của SD) | 8 bộ test qua; đo RAM, thời gian |
| 0.3 | Viết `firmware/platform_esp.cc`: tệp, `Log`, pin, `ApplySettings`, `Vibrate`, `FullRefresh`, `SetDisplayOrientation` | Đọc ghi `.metalio/*.json` qua lại nhiều lần (xử lý R4) |
| 0.4 | Bật `LV_USE_TINY_TTF`, nạp phông Noto đã cắt gọn; thay **riêng** trang chủ và trình đọc | Mở và đọc một EPUB; đo thời gian lật trang và RAM |
| 0.5 | Ghép phím (`OnKey`) vào `BootKey_*`/`VkKey_*` | Mọi phím chạy đúng bảng ở README §7; nguồn và âm lượng không đổi |
| 0.6 | HTTP (`esp_http_client` + chứng chỉ có sẵn) cho `Http`/`Download` trên một task riêng, đưa kết quả về luồng LVGL | Drive, AI (dùng khoá thử), ZBOX |
| 0.7 | Chuyển file và chia sẻ (`esp_http_server`, chế độ AP), âm thanh (`app_audio_*`, minimp3), USB MSC, định dạng thẻ | Từng tính năng một |

## 3. Cài đặt `platform.h` trên ESP-IDF

| Hàm | Ánh xạ đề xuất | Lưu ý |
|---|---|---|
| `Http`, `Download`, `Cancel` | `esp_http_client` trên một task trong PSRAM; hàng đợi kết quả lấy ra trong `LvAdapterEpdiy::Tick` | Callback **phải** chạy trên luồng LVGL; `max_body` giới hạn RAM |
| `ReadText`, `ReadRange`, `ListDir`, `MakeDir`, `FileSize` | VFS stdio trên `/sdcard` | Đường dẫn UTF-8 dùng được nhờ `FATFS_API_ENCODING_UTF_8` |
| `WriteText` | ghi `x.tmp` → `fsync` → xoá `x` (nếu có) → `rename(x.tmp, x)`; nếu `x` mất mà còn `x.tmp` thì đọc từ `x.tmp` | **R4**: FatFs `f_rename` từ chối khi tệp đích đã tồn tại |
| `TransferStart`, `ShareStart` | `esp_http_server`; Hotspot: `esp_wifi` chế độ AP, 192.168.4.1 | Nhận tệp theo luồng ghi thẳng ra tệp `.part`; đưa sự kiện về luồng UI |
| `AudioPlay`…, `RecStart`… | `app_audio_player` / `app_audio_recorder` của firmware gốc; giải mã MP3 bằng minimp3 | WAV 16 kHz mono |
| `ChargeCurrentMa`, `SetChargeCurrentMa` | `MetalioPower_Get/SetChargeCurrentMa` | Chỉ nhận 500/1000/1500 mA |
| `Battery`, `BatteryPercent`, `Charging` | `Bq27220Gauge::GetCachedBatteryLevel`, `ReadVoltageMv`, `ReadCurrentMa` | Dùng bản cache, không đọc I2C trên luồng UI |
| `SetDisplayOrientation` | `DisplayUiSetOrient` | Đổi độ phân giải đồng bộ |
| `FullRefresh` | hàm làm mới GC16 của epdiy | |
| `PowerOff` | đường xung P13 có sẵn | |
| `StorageFormat` | `esp_vfs_fat_sdcard_format` trên một task | Báo tiến độ về luồng UI |

## 4. Rủi ro và cách xử lý

| ID | Rủi ro | Xử lý đề xuất |
|---|---|---|
| R2 | `ota_0` đầy | (a) bỏ các tính năng firmware gốc không dùng (trợ lý giọng nói xiaozhi, model, camera…) cho vừa 5 MB; hoặc (b) chia lại flash: một vùng ứng dụng 10 MB, không còn OTA (phải xoá trắng flash). Quyết định sau khi đo ở nấc 0.4 |
| R2 | Phông lớn | Cắt Noto chỉ giữ Latin + tiếng Việt: thử 5 phông còn **296 KB** (bản đầy đủ 3,2 MB). Phông người dùng chép vào `fonts/` thì nạp từ thẻ vào PSRAM |
| R3 | tiny_ttf trên RISC-V | Đo tốc độ vẽ và kích thước cache glyph; giảm số cỡ chữ (hiện mỗi cỡ là một phông riêng); có thể dựng sẵn bitmap cho phông giao diện |
| R4 | `rename` trên FAT | Xem bảng §3 |
| R5 | Treo vì watchdog | Lưu danh mục thư viện vào `.metalio/library.json` (đường dẫn, kích thước, mtime, tiêu đề, tác giả) và quét trên task nền; nạp từ điển `.idx` ở nền; sắp xếp chỉ dựa trên danh mục |
| R6 | RAM | Giới hạn cache ảnh bìa (LRU, chỉ giữ bản giải mã cỡ hiển thị); với `.idx` lớn thì dùng chỉ mục thưa kèm đọc theo đoạn; giới hạn cache glyph |
| R7 | Xám mất trên I1 | Hàm tiện ích `ui::Rule()` chọn đen trên màn I1, xám trên màn L8; rà các chỗ dùng `kHair` / `0xb9b6ae` |
| R8 | Hai luồng | Máy chủ chỉ đọc bản chụp danh sách do luồng UI chuẩn bị sẵn; mọi lệnh ghi chuyển qua sự kiện (đã làm cho phần ghi) |
| R9 | Cảm ứng | Log khởi động của máy lỗi; giả thuyết: `map_to_logical()` đoán hướng theo từng lần chạm (xem `sim/touch_map.c`) |
| R10 | Bảo mật | Mã PIN 4 số hiện trên máy cho phiên chuyển file; khoá API: mã hoá bằng khoá lưu trong NVS |
| R11 | AI thật | Thử với khoá thật; đặt `timeout_ms` theo từng nhà cung cấp; hiện "đang suy nghĩ"; đã bật `fallbacks: "default"` cho Claude |
