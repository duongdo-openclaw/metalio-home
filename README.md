# Metalio Home

Giao diện và ứng dụng mới cho máy đọc sách **Metalio E-Ink4-Plus**: ESP32-S31, màn e-ink ED047TC2 4,7", 684 × 1216 dọc, 297 ppi. Phát triển và kiểm thử trên **bản giả lập chạy trên PC** trước khi nạp xuống máy.

> **Trạng thái (11/2026):** giao diện và logic ứng dụng đã hoàn chỉnh và chạy được trên giả lập. **Chưa có firmware chạy trên máy thật.** Lớp nền tảng cho ESP-IDF chưa được viết: xem [docs/firmware-port.md](docs/firmware-port.md). Ai tiếp tục dự án (người hay AI) đọc [AGENTS.md](AGENTS.md) trước khi sửa code.

---

## Mục lục

1. [Tính năng](#1-tính-năng)
2. [Bắt đầu nhanh](#2-bắt-đầu-nhanh)
3. [Kiến trúc](#3-kiến-trúc)
4. [Cấu trúc thư mục](#4-cấu-trúc-thư-mục)
5. [Mô hình hiển thị e-ink](#5-mô-hình-hiển-thị-e-ink)
6. [Dữ liệu trên thẻ nhớ](#6-dữ-liệu-trên-thẻ-nhớ)
7. [Phím cứng](#7-phím-cứng)
8. [Bản giả lập](#8-bản-giả-lập)
9. [Kiểm thử](#9-kiểm-thử)
10. [Lộ trình và rủi ro đã biết](#10-lộ-trình-và-rủi-ro-đã-biết)
11. [Giấy phép bên thứ ba](#11-giấy-phép-bên-thứ-ba)

---

## 1. Tính năng

| Nhóm | Tính năng | Mã nguồn chính |
|---|---|---|
| Đọc sách | EPUB/TXT, dàn trang riêng (căn đều, thụt đầu dòng, giãn dòng, lề, phông TTF), mục lục, dấu trang, tìm không dấu, tự lật, màn ngang, thời gian còn lại | `reader_screen.cc`, `paginate.cc`, `book.cc` |
| Khi đọc | Giữ một từ để chọn, chạm từ khác để chọn cả đoạn: Tra từ, Trích dẫn (gạch chân), Ghi chú, Thêm từ vựng, Hỏi AI | `reader_screen.cc` |
| Thư viện | Quét EPUB/TXT ở **mọi thư mục** trên thẻ; kệ lớn 3×3; tab Sách / Tác giả / Thư mục; tìm, lọc, sắp xếp | `library.cc`, `library_screen.cc` |
| Trang chủ | Sách đang đọc, mục tiêu đọc, kệ 4 bìa, thanh điều hướng | `launcher_screen.cc` |
| Sách online | Nhiều thư mục Google Drive công khai (không cần API key) | `drive_folder.cc`, `online_library.cc`, `drive_screen.cc` |
| Chia sẻ | Trang tải về chỉ-đọc kèm mã QR; máy Metalio khác nhận trực tiếp; QR của thư viện Drive | `share_screen.cc`, `sim/transfer_sim.cc` |
| Chuyển file | Qua Wi-Fi nhà hoặc máy tự phát Wi-Fi: sách, nhạc, phông, từ điển, bộ từ, ghi chú, link Drive, khoá API | `transfer_screen.cc`, `sim/transfer_sim.cc` |
| Từ điển | StarDict (`.ifo/.idx/.dict[.dz]`, đọc dictzip theo chunk) và TSV; nhận dạng biến tố tiếng Anh | `dictionary.cc`, `dictionary_screen.cc` |
| Học từ vựng | Bộ từ `.tsv`, lặp lại ngắt quãng kiểu SM-2 (Quên/Khó/Nhớ/Dễ), trắc nghiệm | `vocab.cc`, `vocab_screen.cc` |
| Ghi chú | Trích dẫn và ghi chú riêng; tự xuất Markdown theo từng sách | `notes.cc`, `notes_screen.cc` |
| Trợ lý AI | ChatGPT, Gemini, Claude, DeepSeek, Grok, Mistral, Groq, OpenRouter, máy chủ riêng; giữ phím AI để nói; chế độ Ghi chú; bật/tắt được | `ai_client.cc`, `assistant_screen.cc` |
| Mục tiêu đọc | Số phút mỗi ngày, số cuốn mỗi năm, biểu đồ 7 ngày, chuỗi ngày | `goals_screen.cc`, `reading_state.cc` |
| Hệ thống | Cài đặt nhanh (vuốt xuống), 4 hướng màn, màn chờ có thông tin chủ máy, phông hệ thống, pin và dòng sạc, phím cứng tuỳ biến | `quick_settings.cc`, `system.cc`, `settings*.cc` |
| Khác | Nghe nhạc MP3/WAV, ghi âm WAV 16 kHz, nhà thông minh (ZBOX, mặc định chỉ đọc), kiểm tra cảm ứng | `music_screen.cc`, `recorder_screen.cc`, `smarthome_screen.cc`, `touch_test_screen.cc` |

Giao diện tiếng Việt. Mọi thao tác chính đều làm được bằng phím cứng, vì một số máy bị lỗi cảm ứng.

## 2. Bắt đầu nhanh

### Yêu cầu

Linux (đã thử trên Ubuntu 24.04), CMake ≥ 3.16, GCC/Clang hỗ trợ C++17, cùng các gói:

```bash
sudo apt install build-essential cmake pkg-config libsdl2-dev libcjson-dev libcurl4-openssl-dev zlib1g-dev
```

### Lấy mã và build

```bash
git clone --recurse-submodules <repo-url> metalio-home
cd metalio-home
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
ctest --test-dir build          # 8 bộ test
./build/metalio_sim             # cửa sổ giả lập, 1 điểm ảnh máy = 1 điểm ảnh màn hình
```

Nếu đã clone mà chưa có submodule: `git submodule update --init`.

### Build để dò lỗi bộ nhớ (khuyến nghị trước mỗi lần commit lớn)

```bash
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
      -DCMAKE_C_FLAGS="-fsanitize=address,undefined"
cmake --build build-asan -j
ASAN_OPTIONS=detect_leaks=0 ./build-asan/metalio_sim --headless --script "open home; wait 500; quit"
```

### Thẻ nhớ giả lập

Mặc định giả lập dùng thư mục `./sdcard`, tạo ra ở lần chạy đầu. Thư mục này **không được commit** vì chứa dữ liệu cá nhân và sách có bản quyền. Để có dữ liệu thử:

```bash
python3 tools/make_sample_dict.py sdcard/dict   # từ điển mẫu Anh-Việt (StarDict) và Việt-Việt (TSV)
mkdir -p sdcard/books && cp ~/some-books/*.epub sdcard/books/
```

Bộ từ vựng mẫu được tự tạo trong `sdcard/vocab/` ở lần mở Học từ vựng đầu tiên.

## 3. Kiến trúc

```
┌──────────────────────────────────────────────────────────────────────┐
│ app_ui  (LVGL screens, portable)                                     │
│  launcher · library · reader · settings · assistant · notes · vocab… │
│  ui_common: fonts, page chrome, buttons, prompt/Telex, battery icon  │
├──────────────────────────────────────────────────────────────────────┤
│ app_core  (pure C++17 + cJSON + zlib; no LVGL; unit-tested)          │
│  book · paginate* · reading_state* · settings · library* · notes     │
│  vocab · dictionary · telex · text_search · ai_client · drive_folder │
│  online_library · kiosk_model · owner · media_info · home_gate       │
├──────────────────────────────────────────────────────────────────────┤
│ platform.h  — the ONLY seam to the host (HTTP, files, audio, keys…)  │
├───────────────────────────────┬──────────────────────────────────────┤
│ sim/  (SDL2, libcurl, POSIX)  │ firmware/  (ESP-IDF) — CHƯA CÓ        │
└───────────────────────────────┴──────────────────────────────────────┘
 * paginate/reading_state/library are built with app_ui because they use LVGL
   text metrics or the reader settings; keep them free of widgets.
```

### Nguyên tắc

1. **Một ranh giới nền tảng.** Code trong `app/` chỉ gọi phần cứng qua `platform.h`. Không include SDL, curl hay ESP-IDF trong `app/`. Lớp firmware chỉ cần cài đặt lại đúng các hàm trong `platform.h`.
2. **Callback về luồng UI.** Mọi callback bất đồng bộ (`Http`, `Download`, `TransferStart`, `StorageFormat`) đều được gọi trên luồng LVGL. Luồng nền không được chạm vào đối tượng LVGL.
3. **Màn hình tự sở hữu trạng thái.** Mỗi màn hình tạo `State` bằng `new`, giải phóng trong `LV_EVENT_DELETE`. Callback đến muộn phải kiểm tra `ui::Alive(screen)` (một `shared_ptr<bool>`) trước khi dùng `State`.
4. **Hành động của nút chạy trên bản sao.** `ui::Button` lưu `std::function` và gọi một **bản sao** của nó, vì hành động thường dựng lại màn hình và xoá chính nút đang chạy. Lỗi use-after-free này đã từng xảy ra, đừng gỡ bỏ cơ chế này.
5. **Đổi màn bất đồng bộ.** `ui::Show(screen)` nạp màn mới qua `lv_async_call` rồi xoá màn cũ. Đặt focus vào đối tượng đầu tiên và cuộn về đầu trang.
6. **Không cuộn quán tính, ưu tiên chia trang.** Danh sách dài chia trang và lật bằng phím lật trang (`ui::SetPageKeyHandler`).

### Luồng điều hướng

`sim/main.cpp` gắn `ui::SetNavHandler` cho thanh dưới gồm Kệ sách, Khám phá, Trợ lý AI, Ứng dụng. Chạm "Kệ sách" khi đang ở trang chủ thì mở kệ lớn. Nút Back và phím Trở về gọi `ui::InvokeBack()`, mỗi màn tự đặt hành động qua `ui::SetBackHandler`. Lớp phủ (cài đặt nhanh, bảng nhập chữ, bộ lọc) lưu handler cũ và chỉ khôi phục khi màn hình bên dưới chưa đổi.

## 4. Cấu trúc thư mục

```
app/                 Mã ứng dụng dùng được cho cả giả lập lẫn firmware
  platform.h         Hợp đồng với nền tảng (đọc kỹ trước khi port)
  screens.h          Điểm vào của mọi màn hình + bộ điều phối phím cứng
  ui_common.*        Phông, khung trang, nút, toast, bảng nhập Telex, biểu tượng pin, cử chỉ vuốt
  icons.h            Mã UTF-8 của 103 icon Lucide (sinh tự động, xem tools/)
  *_screen.cc        Từng màn hình
  book.* paginate.*  Đọc EPUB/TXT và dàn trang
  settings.*         Cài đặt máy (.metalio/settings.json)
  reading_state.*    Tiến độ, dấu trang, thống kê, cài đặt trình đọc (.metalio/reading.json)
  library.*          Quét sách trên thẻ, cache metadata
  dictionary.*       StarDict + dictzip + TSV
  vocab.*            Bộ từ và lịch ôn SM-2
  notes.*            Ghi chú + xuất Markdown
  ai_client.*        Tạo request và đọc response cho 9 nhà cung cấp AI
  telex.*            Bộ gõ Telex
sim/                 Lớp nền tảng cho PC
  main.cpp           Cửa sổ SDL, xoay màn, bàn phím → phím máy, ngôn ngữ script, chụp màn
  platform_sim.cc    HTTP (curl, 2 worker), tệp, pin, dòng sạc
  device_sim.cc      Đèn nền, rung, làm mới màn, bộ nhớ thẻ, định dạng thẻ, tắt máy
  transfer_sim.cc    Máy chủ HTTP chuyển file, chia sẻ, API khoá AI
  audio_sim.cc       Phát MP3/WAV (minimp3 + SDL audio), ghi âm WAV
  touch_map.c        map_to_logical() chép nguyên từ firmware, tái hiện lỗi cảm ứng
tests/               Unit test (không cần màn hình), fixtures tự tạo
tools/               make_sample_dict.py (từ điển mẫu), bộ cắt icon Lucide
assets/              Phông Noto (OFL), phông icon Lucide (ISC)
docs/firmware-port.md  Kế hoạch đưa xuống máy thật, thông số phần cứng, rủi ro
third_party/         lvgl (submodule, v9.5.0 giống firmware), minimp3 (CC0)
lv_conf.h            Cấu hình LVGL của giả lập
```

## 5. Mô hình hiển thị e-ink

- **Độ phân giải:** tấm nền 1216×684, dùng dọc 684×1216. LVGL vẽ trong vùng **680×1212** (chừa 2 px mỗi cạnh như firmware gốc), lấy qua `ui::ScreenW()/ScreenH()`. Khi đọc ở màn ngang thì là 1212×680.
- **Hai chế độ vẽ** (giống `lv_adapter_epdiy.cc` của firmware):
  - **I1 (đen trắng 1-bit)** là mặc định. Màu được cắt ngưỡng, nên **màu xám nhạt thành trắng**.
  - **L8 (16 mức xám)** chỉ bật khi màn đang hiện có đối tượng đánh dấu `ui::MarkGray(obj, role)`: ảnh bìa, chữ sách, kệ sách.
  - Quy tắc: đường kẻ hay chữ phụ trên màn I1 phải dùng **đen**, không dùng xám (xem mục rủi ro R7).
- **Phông chữ:** tiny_ttf nạp Noto Sans/Serif. Mọi phông dự phòng sang phông icon Lucide, nên một nhãn có thể chứa cả chữ lẫn `icon::X`. Mã icon thuộc vùng Private Use và được chuyển thẳng sang phông icon. Phông dưới 24 px sẽ mất dấu tiếng Việt ở chế độ 1-bit: không dùng.
- **Không có hoạt ảnh.** Mỗi lần đổi trạng thái là vẽ lại; định kỳ làm mới toàn màn để xoá bóng mờ (`full_refresh_pages`).

## 6. Dữ liệu trên thẻ nhớ

```
<sd>/
  books/  (hoặc bất kỳ thư mục nào)   sách EPUB/TXT, quét đệ quy tối đa 6 cấp
  music/  recordings/  fonts/  dict/  vocab/  notes/
  .metalio/
    settings.json   cài đặt máy (đèn, phím, pin, phông, AI bật/tắt, mục tiêu)
    reading.json    tiến độ, dấu trang, thống kê ngày, cài đặt trình đọc
    notes.json      ghi chú (bản Markdown nằm ở notes/<sách>.md)
    vocab.json      lịch ôn từng thẻ
    owner.json      thông tin chủ máy (hiện ở màn chờ)
    libraries.json  thư viện Google Drive
    ai.json         nhà cung cấp, khoá API (dạng chữ thường), model
    sim-sdcard      dấu hiệu "thẻ do giả lập tạo"; thiếu tệp này thì giả lập từ chối định dạng thẻ
```

Mọi tệp JSON đều đọc ghi qua `platform::ReadText/WriteText`. Khoá lạ thì bỏ qua, khoá thiếu thì lấy mặc định, nên đổi phiên bản không làm hỏng dữ liệu cũ. Enum được lưu bằng số: **chỉ thêm vào cuối**, không đổi thứ tự.

## 7. Phím cứng

Lấy từ `metalio_keys.cc` và `vk_key_handler.cc` của firmware gốc. Bộ điều phối là `screens::OnKey(HwKey, Press)` trong `system.cc`.

| Phím | Phần cứng | Cố định | Tuỳ chỉnh (`settings::KeySlot`) |
|---|---|---|---|
| Nguồn | TCA P17 | Bấm: màn chờ / thức. Giữ 3 giây: tắt máy | — |
| Âm lượng + / – | TCA P10 / P11 | Ngoài trang đọc: ±10, giữ = tối đa / tắt tiếng | Khi đọc: Tự động / Lật trang / Âm lượng |
| AI (BOOT) | GPIO61 | — | Bấm, bấm đúp, giữ (mặc định giữ = Trợ lý AI, nhả = gửi) |
| Home | Phím cảm ứng viền, x=100 | — | Bấm, giữ |
| Trở về | Phím cảm ứng viền, x=200 | Bấm: trở về; khi đọc: trang trước | Giữ (ngoài trang đọc) |
| Trang sau | Phím cảm ứng viền, x=300 | Khi đọc: trang sau | Bấm, giữ (ngoài trang đọc) |

Có 23 hành động gán được, liệt kê trong `settings::KeyAction`. Khi trợ lý AI tắt (`settings.ai_enabled = false`), hành động "Trợ lý AI" chỉ hiện thông báo.

## 8. Bản giả lập

```
./build/metalio_sim [--sd DIR] [--scale pixel|real|N] [--headless] [--fake-mic]
                    [--touch portrait|landscape|dead] [--server URL] [--token T]
                    [--drive LINK | --drive-file FILE] [--allow-control]
                    [--script "lệnh; lệnh; …"] [--clean-shots]
```

**Bàn phím máy tính ↔ phím máy** (thêm Shift = giữ):

| Phím PC | Phím máy |
|---|---|
| F2 | AI (Ctrl+F2 = bấm đúp; giữ F3 = nói, nhả = gửi) |
| F4 | Nguồn |
| F6 | Home |
| F7 | Trở về |
| F8 | Trang sau |
| F9 / F10 | Âm lượng + / – |
| PgUp / PgDn | Lật trang |
| F5 | Cài đặt nhanh |
| F1 | Đổi kiểu chip cảm ứng |
| F12 | Chụp màn |

**Ngôn ngữ script** (`--script`, các lệnh cách nhau bằng `;`, toạ độ theo cửa sổ):

| Lệnh | Ý nghĩa |
|---|---|
| `wait MS` / `idle MS` | chờ; chờ hết request HTTP |
| `tap X Y` / `hold X Y [MS]` / `swipe X1 Y1 X2 Y2` | cảm ứng |
| `key NAME` | `esc pgup pgdn enter quick ai aidouble aihold aiup home homehold vkback vknext volup voldown power powerhold …` |
| `type TEXT` | gõ từng ký tự (bộ gõ Telex chạy như khi bấm phím thật) |
| `open SCREEN[:ARG]` | mở thẳng một màn: `home library apps settings[:N] key:N reader[:path] assistant dict[:word] vocab notes goals music recorder smarthome touch owner transfer online share sharelink receive quick standby` |
| `shot FILE.bmp` / `profile P` / `quit` | chụp màn; đổi chip cảm ứng; thoát |

Ví dụ chụp toàn bộ màn để rà giao diện:

```bash
./build/metalio_sim --headless --clean-shots \
  --script "open settings; wait 500; shot a.bmp; open settings:3; wait 500; shot b.bmp; quit"
```

**Biến môi trường:** `METALIO_SIM_BATTERY=0..100` đặt mức pin, `METALIO_SIM_CHARGING=1` giả lập đang sạc.

**An toàn:** giả lập chỉ định dạng thẻ khi thư mục có `.metalio/sim-sdcard`, và không bao giờ đụng vào thư mục có sẵn.

## 9. Kiểm thử

| Test | Phạm vi |
|---|---|
| `touch_map_test` | Ánh xạ toạ độ FT6336U: dọc, ngang, không phản hồi |
| `kiosk_model_test` | Đọc `/api/kiosk/summary`, kiểm tra địa chỉ mạng nhà |
| `drive_folder_test` | Đọc link và trang thư mục Drive |
| `book_test` | EPUB/TXT: metadata, chương, ảnh bìa, entity |
| `search_paginate_test` | Bỏ dấu, tìm, dàn trang |
| `telex_test` | Bộ gõ Telex (28 trường hợp) |
| `study_test` | Từ điển (StarDict, dictzip, TSV, biến tố), ghi chú, SM-2 |
| `ai_client_test` | Request và response cho OpenAI, Gemini, Claude, các bên tương thích, giọng nói |

Quy trình trước khi coi một thay đổi là xong:

1. Chạy `ctest`.
2. Build ASan và chạy script mở mọi màn (mẫu trong AGENTS.md).
3. Chụp các màn vừa sửa, xem ảnh để kiểm tra chữ chạm viền, xuống dòng lạ, chữ bị cắt.

## 10. Lộ trình và rủi ro đã biết

Chi tiết nằm trong [docs/firmware-port.md](docs/firmware-port.md). Tóm tắt:

| ID | Mức | Vấn đề |
|---|---|---|
| R1 | Chặn | Chưa có lớp nền tảng ESP-IDF (`platform.h` cho firmware) |
| R2 | Chặn | Phân vùng ứng dụng `ota_0` 5 MB đã dùng 4,97 MB; `font_data` đã dùng 4,75/5 MB |
| R3 | Chặn | Firmware đang tắt `LV_USE_TINY_TTF` và `LV_USE_TJPGD`, và vẽ ở `LV_COLOR_DEPTH=1` |
| R4 | Chặn | `WriteText` ghi tệp tạm rồi `rename()` đè; trên FATFS đổi tên đè **thất bại** → phải xoá tệp cũ trước, giữ bản `.bak` |
| R5 | Cao | Quét thẻ, mở EPUB khi sắp xếp, nạp từ điển đều chạy trên luồng UI; task watchdog 20 giây |
| R6 | Cao | RAM: cache ảnh bìa không giới hạn, tệp `.idx` nạp nguyên, mỗi cỡ chữ một bộ đệm 1024 glyph |
| R7 | Cao | Đường kẻ xám (`0xb9b6ae`) trên màn I1 bị cắt ngưỡng thành trắng (thấy rõ ở Từ điển, nhóm trong Ứng dụng) |
| R8 | Cao | Máy chủ chuyển file đọc `online::Sources()` ngay trên luồng của nó |
| R9 | Cao | Lỗi cảm ứng ở một số máy chưa được xác nhận bằng log máy thật |
| R10 | TB | Trang chuyển file không có xác thực; khoá API lưu dạng chữ thường |
| R11 | TB | Chưa gọi thử API AI thật; Claude Opus có thể trả lời rất chậm |

## 11. Giấy phép bên thứ ba

- LVGL — MIT (`third_party/lvgl`)
- minimp3 — CC0 (`third_party/minimp3`)
- Noto Sans / Noto Serif — SIL OFL 1.1 (`assets/LICENSE-noto-OFL.txt`)
- Lucide icons — ISC (`assets/LICENSE-lucide.txt`)
- Firmware gốc Metalio (tham chiếu, không nằm trong repo) — MIT

Từ điển mẫu, truyện mẫu và test fixtures do dự án tự viết.
