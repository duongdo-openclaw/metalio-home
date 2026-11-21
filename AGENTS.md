# AGENTS.md: working rules for humans and AI agents

Read `README.md` first (architecture, data layout, keys), then this file. For on-device work, also read `docs/firmware-port.md`.

## Ground rules

1. **Never flash or modify the physical device without the owner's explicit consent.** All work happens in the simulator until a device step is approved. Keep a full flash backup before any flash.
2. **No personal data in the repo.** That means no owner name or phone number, home IPs, Google Drive folder links or IDs, API keys, tokens, real smart-home entity lists, or screenshots that show any of these. `sdcard/` and `shots*/` are git-ignored for this reason. Test fixtures are synthetic. Use `192.168.1.50` and `1DemoFolder…` style placeholders.
3. **Smart home stays read-only** unless `allow_control` is set (`--allow-control`). Do not change the ZBOX server.
4. **UI language is Vietnamese.** Code, identifiers and comments are in English. User-facing strings are natural Vietnamese with diacritics; no machine-translation tone.

## Architecture invariants (do not break)

- `app/` talks to the host **only** through `app/platform.h`. Never include SDL, curl, POSIX sockets or ESP-IDF headers in `app/`. If you need a new host capability, add a function to `platform.h`, implement it in `sim/`, and document the firmware mapping in `docs/firmware-port.md` §3.
- `app_core` (see `CMakeLists.txt`) has no LVGL dependency and is unit-tested through `tests/platform_stub.cc`.
- Async callbacks (`platform::Http`, `Download`, transfer events) arrive on the LVGL thread. Guard them with `ui::Alive(screen)`; screen `State` objects are deleted in `LV_EVENT_DELETE`.
- `ui::Button` invokes a **copy** of its action, because actions routinely rebuild the screen and delete the button. Keep it that way. For raw `lv_obj_add_event_cb` callbacks that rebuild their own parent, copy what you need to locals first.
- Overlays on `lv_layer_top()` (quick settings, `ui::TextPrompt`, filter sheet) save the previous back handler and restore it only if `lv_screen_active()` is unchanged.
- Settings, reading state, notes, vocab and AI config are JSON on the SD card. **Enums are persisted as numbers: append only.** Unknown keys are ignored and missing keys fall back to defaults.
- Server threads (`sim/transfer_sim.cc`) must not mutate app state; they `Emit()` events applied on the UI thread.

## E-ink UI rules

- Logical canvas is `ui::ScreenW() × ui::ScreenH()` (680×1212 portrait). Never hard-code 684/1216.
- Minimum touch target is 64 px (88 preferred). Every action must also be reachable with hardware keys. Paginate instead of relying on scroll; wire `ui::SetPageKeyHandler`.
- Rendering is 1-bit unless a `ui::MarkGray` object is visible. On 1-bit screens use black for rules and secondary text; light greys turn white (risk R7). Text below 24 px loses Vietnamese marks in 1-bit.
- No animations. Prefer hairlines and whitespace over heavy frames. Pills (radius = h/2) for chips, with 20–24 px horizontal padding.
- Text must never touch a border, overlap another element, or wrap mid-token. Fix width with `LV_LABEL_LONG_MODE_DOTS` for one-line labels, or give a fixed-width column with `WRAP`. Always verify with a screenshot.
- Icons come from Lucide via `app/icons.h` (`icon::HOME` …). Text fonts fall back to the icon font, so `"text " + icon::X` works in any label. Do not use LV_SYMBOL glyphs except inside `lv_keyboard`. Do not use characters missing from Noto (for example U+2212 MINUS: use an en dash).

## Workflow

```bash
cmake -S . -B build && cmake --build build -j && ctest --test-dir build
```

Before calling a change done:

1. `ctest` passes. Add or extend a test in `tests/` for any logic change in `app_core`.
2. Run an ASan build across every screen:
   ```bash
   cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined" -DCMAKE_C_FLAGS="-fsanitize=address,undefined"
   cmake --build build-asan -j
   ASAN_OPTIONS=detect_leaks=0 ./build-asan/metalio_sim --headless --clean-shots --script \
     "open home; wait 400; open library; wait 400; open apps; wait 400; open settings; wait 400; open reader; wait 600; open assistant; wait 400; open vocab; wait 400; open notes; wait 400; open transfer; wait 600; quit"
   ```
   Any `AddressSanitizer` or `runtime error` line is a failure.
3. Screenshot the screens you touched (`open …; shot x.bmp`) and look at them.

### Adding things

- **New screen:** declare it in `app/screens.h`, implement `app/<name>_screen.cc` using `ui::NewPage`, add it to `app_ui` in `CMakeLists.txt`, add an entry point (Apps tile, settings row or key action), and add an `open` name in `sim/main.cpp`.
- **New setting:** add the field with its default in `settings.h`, then load and save it in `settings.cc` (clamp values), then add UI in `settings_screen.cc`.
- **New key action:** append it to `settings::KeyAction`, name it in `KeyActionName`, and implement it in `screens::RunKeyAction`.
- **New AI provider:** add a row to `kProviders` in `ai_client.cc`. Use wire `OpenAI` if the provider is OpenAI-compatible, otherwise add a wire format. Add a test in `tests/ai_client_test.cc`.
- **New icon:** add it to the list in `tools/make_icons.py` and regenerate `assets/Lucide-icons.ttf` and `app/icons.h` (source: `lucide-static` npm package, subset with `pyftsubset`).

## Claude API specifics (`ai_client.cc`)

Raw HTTPS (C++, no SDK): `POST https://api.anthropic.com/v1/messages` with these headers:

- `x-api-key`
- `anthropic-version: 2026-10-21`
- `anthropic-beta: server-side-fallback-2026-07-01`

Body:

- `model` defaults to `claude-opus-5-5`
- `max_tokens: 16000`
- `system`
- `messages`
- `fallbacks: "default"`

Response parsing: concatenate `content[].text` where `type == "text"` (thinking blocks are skipped) and treat `stop_reason == "refusal"` as an error.

## Known open issues

See README §10 (R1–R11) and `docs/firmware-port.md` §4. Highest-value next steps in the simulator: R4 (FAT-safe `WriteText` contract), R7 (1-bit-safe rules), R5/R6 (background library index, bounded caches), R10 (transfer PIN).
