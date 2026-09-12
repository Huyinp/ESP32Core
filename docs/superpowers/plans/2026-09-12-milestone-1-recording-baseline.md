# Milestone 1 Recording Baseline Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build and verify a V2-board firmware that starts/stops 16 kHz mono WAV recording with the PWR button, writes safely to microSD, shows recording status, and recovers an interrupted file after reboot.

**Architecture:** A small ESP-IDF application uses Waveshare's managed BSP for the V2 board and keeps recorder logic independent from hardware. Pure-C state, WAV, naming, and recovery units are host-testable; thin ESP-IDF adapters own ES8311/I2S, SD, button, and LVGL calls. The audio task is the only writer to an active recording and has higher priority than UI work.

**Tech Stack:** ESP-IDF 5.5.4, C17, FreeRTOS, ESP-IDF Unity tests, Waveshare BSP `waveshare/esp32_s3_touch_amoled_1_8` 2.0.3, `esp_codec_dev`, FATFS/SDMMC, LVGL 9.

**Spec:** `docs/superpowers/specs/2026-09-12-esp32-s3-ai-recorder-design.md`

## Global Constraints

- Target only ESP32-S3-Touch-AMOLED-1.8 V2: CO5300 display and CST820/CST816S-family touch path; never import SH8601/FT3168 V1 definitions.
- Use `D:\DevTool\Espressif\frameworks\esp-idf-v5.5.4`; target is `esp32s3`.
- Pin the managed BSP to `waveshare/esp32_s3_touch_amoled_1_8: 2.0.3`.
- Capture format for this milestone is signed PCM, 16 kHz, 16-bit, mono, stored as WAV.
- SD write and file integrity take priority over BLE (not implemented here) and UI animation.
- No Opus, BLE, Wi-Fi, cloud, ASR, summary, IMU feature, OTA, or generalized hardware abstraction beyond the interfaces in this plan.
- Hardware acceptance requires the user's V2 board; host or emulator results alone cannot mark a hardware criterion complete.

## File Map

```text
CMakeLists.txt                         ESP-IDF project entry
sdkconfig.defaults                    S3/PSRAM/LVGL defaults
partitions.csv                        App partition sized for BSP/LVGL
main/CMakeLists.txt                    Application component registration
main/idf_component.yml                Managed BSP 2.0.3 dependency
main/app_main.c                        Composition root and event loop
components/recorder/                   State machine and recording coordinator
components/wav/                        WAV header/finalization logic
components/storage/                    Recording names, temp commit, recovery
components/board/                      V2 BSP adapters for SD/audio/button/display
components/ui/                         One recording-status screen
components/*/test/                     ESP-IDF Unity unit tests
test_app/                              On-device Unity test runner
scripts/export-idf.ps1                 Repository-local environment launcher
docs/hardware/m1-test-log.md           Reproducible real-board evidence template
```

---

### Task 1: Bootable V2 Project and Board Capability Check

**Files:**
- Create: `CMakeLists.txt`
- Create: `sdkconfig.defaults`
- Create: `partitions.csv`
- Create: `main/CMakeLists.txt`
- Create: `main/idf_component.yml`
- Create: `main/app_main.c`
- Create: `scripts/export-idf.ps1`
- Create: `docs/hardware/m1-test-log.md`

**Interfaces:**
- Consumes: managed component `waveshare/esp32_s3_touch_amoled_1_8` version `2.0.3`.
- Produces: `app_main(void)` that prints `RECORDER_BOARD_READY`, detected hardware generation, flash/PSRAM sizes, and BSP capabilities.

- [ ] **Step 1: Add the project skeleton and pin the V2-capable BSP**

`main/idf_component.yml`:

```yaml
version: "1.0.0"
dependencies:
  idf: ">=5.5,<5.6"
  waveshare/esp32_s3_touch_amoled_1_8:
    version: "2.0.3"
    public: true
```

Use the official `00_board_check` example as the source for capability names. `app_main.c` must fail with a logged `ESP_ERROR_CHECK` result if PSRAM or required V2 BSP capabilities are absent; it must not silently select a V1 driver.

- [ ] **Step 2: Add deterministic build configuration**

Set `CONFIG_IDF_TARGET="esp32s3"`, 16 MB flash size, octal PSRAM, custom partition CSV, LVGL 9-compatible BSP defaults, and USB Serial/JTAG console in `sdkconfig.defaults`. Define a factory app partition large enough for the managed BSP and LVGL, leaving no OTA slots in this milestone.

- [ ] **Step 3: Add the environment helper**

`scripts/export-idf.ps1` must dot-source `D:\DevTool\Espressif\frameworks\esp-idf-v5.5.4\export.ps1` and then run the arguments passed to it. It must not edit machine-wide environment variables.

- [ ] **Step 4: Build to verify the pinned dependency and target**

Run:

```powershell
.\scripts\export-idf.ps1 idf.py set-target esp32s3
.\scripts\export-idf.ps1 idf.py build
```

Expected: exit code 0; component lock resolves BSP 2.0.3, CO5300, CST816S-family touch, LVGL, and `esp_codec_dev`; no SH8601 component appears in `dependencies.lock`.

- [ ] **Step 5: Flash the board capability check**

First run `Get-CimInstance Win32_SerialPort | Select-Object DeviceID,Name`, then set `$recorderPort = Read-Host 'V2 board COM port'` and run `.\scripts\export-idf.ps1 idf.py -p $recorderPort flash monitor`.

Expected serial evidence: `RECORDER_BOARD_READY`, ESP32-S3 target, 16 MB flash, 8 MB PSRAM, display 368×448, SD support, and audio support. Record the exact port, commit, board label, and log excerpt in `docs/hardware/m1-test-log.md`.

- [ ] **Step 6: Commit**

```powershell
git add CMakeLists.txt sdkconfig.defaults partitions.csv main scripts/export-idf.ps1 docs/hardware/m1-test-log.md dependencies.lock
git commit -m "build: bootstrap ESP32-S3 V2 recorder firmware"
```

---

### Task 2: Recorder State Machine

**Files:**
- Create: `components/recorder/include/recorder_state.h`
- Create: `components/recorder/recorder_state.c`
- Create: `components/recorder/CMakeLists.txt`
- Create: `components/recorder/test/CMakeLists.txt`
- Create: `components/recorder/test/test_recorder_state.c`
- Create: `test_app/CMakeLists.txt`
- Create: `test_app/main/CMakeLists.txt`
- Create: `test_app/main/test_main.c`

**Interfaces:**
- Consumes: `recorder_event_t` values `START`, `CAPTURE_READY`, `STOP`, `FINALIZED`, `FAIL`, `RECOVERED`.
- Produces: `recorder_state_t recorder_state_next(recorder_state_t current, recorder_event_t event)` and `bool recorder_state_is_recording(recorder_state_t state)`.

- [ ] **Step 1: Write failing transition tests**

```c
TEST_CASE("idle start reaches preparing", "[recorder]") {
    TEST_ASSERT_EQUAL(RECORDER_PREPARING,
        recorder_state_next(RECORDER_IDLE, RECORDER_EVENT_START));
}

TEST_CASE("recording stop reaches finalizing", "[recorder]") {
    TEST_ASSERT_EQUAL(RECORDER_FINALIZING,
        recorder_state_next(RECORDER_RECORDING, RECORDER_EVENT_STOP));
}

TEST_CASE("invalid duplicate start is rejected", "[recorder]") {
    TEST_ASSERT_EQUAL(RECORDER_INVALID,
        recorder_state_next(RECORDER_RECORDING, RECORDER_EVENT_START));
}
```

Add coverage for the complete approved path: `IDLE → PREPARING → RECORDING → FINALIZING → STORED`, failure to `RECOVERY_REQUIRED`, and `RECOVERY_REQUIRED → STORED`.

- [ ] **Step 2: Add the shared on-device Unity runner, then observe the expected compile failure**

Configure `test_app` as an ESP-IDF project whose `main` component requires `unity` and calls `unity_run_menu()` from `app_main()`. Add the repository `components` directory through `EXTRA_COMPONENT_DIRS`, then run:

```powershell
.\scripts\export-idf.ps1 idf.py -C test_app set-target esp32s3
.\scripts\export-idf.ps1 idf.py -C test_app build
```

Expected: FAIL because `recorder_state.h` and its symbols do not exist.

- [ ] **Step 3: Implement the explicit transition table**

Use an array of `{from, event, to}` records. Return `RECORDER_INVALID` for every unspecified pair; do not add implicit or wildcard transitions.

- [ ] **Step 4: Run all recorder tests on the V2 board**

Run `.\scripts\export-idf.ps1 idf.py -C test_app -p $recorderPort flash monitor`, select the `[recorder]` tests from the Unity menu, and capture the summary.

Expected: all transition and `recorder_state_is_recording()` assertions pass with `0 Failures`.

- [ ] **Step 5: Commit**

```powershell
git add components/recorder test_app
git commit -m "feat: add recorder state machine"
```

---

### Task 3: Recoverable WAV Writer

**Files:**
- Create: `components/wav/include/wav_writer.h`
- Create: `components/wav/wav_writer.c`
- Create: `components/wav/CMakeLists.txt`
- Create: `components/wav/test/test_wav_writer.c`

**Interfaces:**
- Produces:
  - `esp_err_t wav_write_placeholder(FILE *file, const wav_format_t *format)`
  - `esp_err_t wav_finalize(FILE *file, uint32_t pcm_bytes, const wav_format_t *format)`
  - `esp_err_t wav_repair(FILE *file, uint32_t actual_file_bytes, const wav_format_t *format)`
- `wav_format_t` fields are `sample_rate_hz`, `bits_per_sample`, and `channels` as unsigned integers.

- [ ] **Step 1: Write failing byte-level WAV tests**

Tests must assert `RIFF`, `WAVE`, `fmt `, PCM format 1, mono, 16000 Hz, 16 bits, byte rate 32000, block alignment 2, `data`, and correct RIFF/data sizes after finalization. A repair test must generate 32000 zero-valued PCM bytes in chunks, append them after the placeholder, and verify a one-second playable header after `wav_repair()`.

- [ ] **Step 2: Run and verify failure before implementation**

Expected: build fails because `wav_writer.h` is absent.

- [ ] **Step 3: Implement only canonical 44-byte PCM WAV headers**

Write each little-endian field explicitly; do not serialize a packed C struct. Reject null files, zero rates, channel counts other than 1, bits other than 16, `actual_file_bytes < 44`, and sizes exceeding the 32-bit RIFF limit.

- [ ] **Step 4: Run WAV tests and inspect the generated fixture with `ffprobe` when available**

Expected: Unity tests pass. `ffprobe` reports `pcm_s16le`, 16000 Hz, mono, duration 1.000 s. Absence of `ffprobe` does not fail the firmware tests; record it as unavailable in the test log.

- [ ] **Step 5: Commit**

```powershell
git add components/wav
git commit -m "feat: add recoverable PCM WAV writer"
```

---

### Task 4: Recording Storage and Crash Recovery

**Files:**
- Create: `components/storage/include/recording_store.h`
- Create: `components/storage/recording_store.c`
- Create: `components/storage/CMakeLists.txt`
- Create: `components/storage/test/test_recording_store.c`

**Interfaces:**
- Consumes: mounted root path and `recording_clock_t { bool valid; int64_t unix_seconds; uint32_t boot_count; uint32_t sequence; }`.
- Produces:
  - `esp_err_t recording_store_begin(recording_store_t *, const char *root, const recording_clock_t *, wav_format_t)`
  - `esp_err_t recording_store_append(recording_store_t *, const void *pcm, size_t bytes)`
  - `esp_err_t recording_store_commit(recording_store_t *, recording_info_t *out)`
  - `esp_err_t recording_store_recover_all(const char *root, recovery_report_t *out)`
- Temporary suffix is `.wav.part`; committed suffix is `.wav`.

- [ ] **Step 1: Write failing filesystem tests**

Cover valid RTC name `20260912T093000Z-0001.wav.part`, fallback name `boot-00000007-0001.wav.part`, append byte accounting, commit rename, recovery of a 44-byte header plus PCM, preservation of files shorter than 44 bytes as `.corrupt`, and idempotent second recovery.

- [ ] **Step 2: Run and verify failure before implementation**

Expected: compile failure for missing `recording_store.h`.

- [ ] **Step 3: Implement begin, append, commit, and recovery**

Create files exclusively (`"wbx"` where supported; otherwise pre-check and fail on collision), call `fflush()` after the placeholder and at a configurable 1-second PCM boundary, finalize before `fclose()`, and rename only after close succeeds. Recovery derives PCM length from actual file size, calls `wav_repair()`, flushes/closes, then renames.

- [ ] **Step 4: Run storage and WAV tests together**

Expected: all tests pass and a second recovery reports zero newly repaired files.

- [ ] **Step 5: Commit**

```powershell
git add components/storage
git commit -m "feat: add crash-safe recording storage"
```

---

### Task 5: V2 Board Audio, SD, and PWR Button Adapters

**Files:**
- Create: `components/board/include/recorder_board.h`
- Create: `components/board/recorder_board.c`
- Create: `components/board/CMakeLists.txt`
- Create: `components/board/test/test_button_classifier.c`

**Interfaces:**
- Produces:
  - `esp_err_t recorder_board_init(void)`
  - `esp_err_t recorder_board_mount_sd(const char **mount_path)`
  - `esp_err_t recorder_board_open_mic(recorder_audio_source_t *out, uint32_t rate_hz)`
  - `esp_err_t recorder_audio_read(recorder_audio_source_t *, int16_t *samples, size_t capacity, size_t *read)`
  - `esp_err_t recorder_board_register_button(recorder_button_cb_t cb, void *ctx)`
  - `button_action_t button_classify(uint32_t held_ms)` where `<50` is ignored, `50..999` is short press, and `>=1000` is long press.

- [ ] **Step 1: Write failing boundary tests for button classification**

Assert 49 ms ignored, 50 and 999 ms short, and 1000 ms long. These thresholds are constants in `recorder_board.h` so UI copy and tests share them.

- [ ] **Step 2: Implement the pure button classifier and run its tests**

Expected: all four boundary assertions pass.

- [ ] **Step 3: Implement SD and V2 audio adapters from official examples**

Use the managed BSP's definitions and the official `09_sdmmc` and `12_i2s_codec` initialization order: configure I2S first, initialize the ES8311 control path second, then enable capture. Configure 16000 Hz, mono, 16-bit input. Return errors instead of asserting or reboot-looping.

Do not copy V1 pin constants. Confirm the resolved BSP path contains CO5300 and CST816S support before flashing. If BSP 2.0.3 lacks a microphone capture helper, instantiate `esp_codec_dev` input using only pin and I2C definitions exported by that BSP; document the exact exported definitions in `recorder_board.c` comments.

- [ ] **Step 4: Add a diagnostic capture command**

On boot, when BOOT is held, capture exactly five seconds into `/sdcard/diagnostic.wav`, finalize it, print peak absolute sample, clipped sample count, and zero sample count, then return to idle. This path reuses `recording_store` and is not a second recorder implementation.

- [ ] **Step 5: Build, flash, and verify the diagnostic file**

Expected: build exit 0; serial shows SD mounted and 160000 samples captured; the copied file is 160044 bytes, plays for five seconds, and contains audible speech. Log peak/clipping/zero counts and listening notes.

- [ ] **Step 6: Commit**

```powershell
git add components/board main/app_main.c docs/hardware/m1-test-log.md
git commit -m "feat: add V2 board audio and storage adapters"
```

---

### Task 6: Recording Coordinator and Physical Controls

**Files:**
- Create: `components/recorder/include/recorder.h`
- Create: `components/recorder/recorder.c`
- Create: `components/recorder/test/test_recorder.c`
- Modify: `components/recorder/CMakeLists.txt`
- Modify: `main/app_main.c`

**Interfaces:**
- Consumes: `recorder_audio_source_t`, `recording_store_t`, and button actions.
- Produces:
  - `esp_err_t recorder_init(const recorder_config_t *)`
  - `esp_err_t recorder_request_start(recording_mode_t mode)`
  - `esp_err_t recorder_request_stop(void)`
  - `recorder_snapshot_t recorder_get_snapshot(void)`
  - observer callback `void (*recorder_observer_t)(const recorder_snapshot_t *, void *)`.
- `recorder_snapshot_t` contains state, mode, elapsed milliseconds, PCM bytes, dropped buffers, last error, and active recording ID.

- [ ] **Step 1: Write failing coordinator tests with fake audio and storage ports**

Verify: start opens storage before audio reads; duplicate start returns `ESP_ERR_INVALID_STATE`; short reads append only returned samples; stop drains queued PCM before commit; an append error transitions to recovery-required and closes capture; observers see ordered state changes.

- [ ] **Step 2: Run and verify the expected missing-interface failure**

Expected: compile failure for missing `recorder.h`.

- [ ] **Step 3: Implement the minimum coordinator**

Create one FreeRTOS capture task with a fixed pool of DMA-sized buffers. No heap allocation is permitted inside the steady-state capture loop. Set capture priority above UI. Increment `dropped_buffers` on queue exhaustion and log a rate-limited error.

- [ ] **Step 4: Bind physical button actions in `app_main.c`**

Short press in `IDLE` or `STORED` requests a meeting-mode start; short press in `RECORDING` requests stop. Long press requests shutdown only when not preparing/finalizing; during finalization it sets a deferred shutdown flag executed after commit. Touch mode selection is Task 7.

- [ ] **Step 5: Run component tests and a 10-minute real-board recording**

Expected: all unit tests pass; serial reports zero dropped buffers; WAV plays for the measured duration; header size equals actual PCM bytes; start/stop works without a phone.

- [ ] **Step 6: Commit**

```powershell
git add components/recorder main/app_main.c docs/hardware/m1-test-log.md
git commit -m "feat: record WAV files from the PWR button"
```

---

### Task 7: Minimal AMOLED Recording UI

**Files:**
- Create: `components/ui/include/recorder_ui.h`
- Create: `components/ui/recorder_ui.c`
- Create: `components/ui/CMakeLists.txt`
- Modify: `main/app_main.c`

**Interfaces:**
- Consumes: immutable `recorder_snapshot_t` updates and touch events from the managed BSP LVGL port.
- Produces: `esp_err_t recorder_ui_init(void)`, `void recorder_ui_update(const recorder_snapshot_t *)`, and selected `recording_mode_t recorder_ui_mode(void)`.

- [ ] **Step 1: Add a host-testable view-model formatter and failing tests**

Define `recorder_ui_model_t` with fixed buffers for mode text, `HH:MM:SS`, status text, and error text. Test `0 → 00:00:00`, `3,678,000 ms → 01:01:18`, meeting/scratch-note labels, and SD-full error mapping.

- [ ] **Step 2: Implement the formatter and run tests**

Expected: all formatter tests pass with no dynamic allocation.

- [ ] **Step 3: Build the single LVGL screen**

Initialize the CO5300 display and CST816S-family touch through BSP 2.0.3. Show clock/status bar, explicit meeting/scratch-note selector while idle, large elapsed time, recording indicator, storage remaining, and error text. Disable mode switching while preparing, recording, or finalizing. UI callbacks enqueue commands; they never call recorder or filesystem code while holding the LVGL lock.

- [ ] **Step 4: Verify V2 display and touch on hardware**

Expected: 368×448 screen renders without offset; both modes can be selected while idle; short PWR starts the selected mode; elapsed display updates once per second; touch remains responsive; no SH8601/FT3168 log or dependency is present.

- [ ] **Step 5: Commit**

```powershell
git add components/ui main/app_main.c docs/hardware/m1-test-log.md
git commit -m "feat: show recording state on the V2 AMOLED"
```

---

### Task 8: Power-Loss Recovery and Two-Hour Acceptance

**Files:**
- Create: `scripts/verify-wav.ps1`
- Modify: `main/app_main.c`
- Modify: `docs/hardware/m1-test-log.md`
- Create: `docs/hardware/m1-acceptance.md`

**Interfaces:**
- Consumes: completed firmware and WAV files copied from microSD.
- Produces: a reproducible acceptance report with build hash, board version, media, commands, results, and known limitations.

- [ ] **Step 1: Add an automated WAV integrity checker**

`scripts/verify-wav.ps1 -Path recording.wav` must read the 44-byte header, verify all fixed fields, assert `data_size == file_length - 44`, compute duration from byte rate, and print SHA-256. It exits nonzero on any mismatch.

- [ ] **Step 2: Verify the checker rejects corruption before relying on it**

Run it against a valid fixture and a copy truncated by one byte.

Expected: valid fixture exits 0; truncated fixture exits nonzero with `data size does not match file length`.

- [ ] **Step 3: Perform forced power-loss tests**

Start recording, speak for at least 60 seconds, remove power without stopping, reboot, and copy the recovered WAV. Repeat at three interruption points: within 5 seconds, after 1 minute, and during a UI update.

Expected for each: device boots without a loop, reports one recovered file, checker exits 0, recovered audio contains all data through the last periodic flush, and a second reboot does not create another file.

- [ ] **Step 4: Perform the two-hour soak test**

Record continuously for at least 2:05:00 while playing a spoken-word source near the board. Log heap, minimum free heap, buffer high-water mark, dropped buffers, and SD write errors every minute.

Expected: zero dropped buffers and SD errors; no monotonic heap loss; final WAV passes the checker and plays at the expected duration. Because canonical WAV uses 32-bit sizes, this duration remains below the format limit.

- [ ] **Step 5: Measure microphone suitability**

Record three five-minute samples: 20–30 cm near speech, device worn near the collar, and device centered on a meeting table with speakers at approximately 1 m and 3 m. Preserve unprocessed files and record intelligibility, clipping, noise, and whether the 3 m case is acceptable for later ASR testing. Do not claim a five-meter pickup radius from this single-microphone board.

- [ ] **Step 6: Run the final verification set**

```powershell
.\scripts\export-idf.ps1 idf.py fullclean
.\scripts\export-idf.ps1 idf.py build
git status --short
```

Expected: clean build from scratch, all component tests recorded as passing, every hardware acceptance row has evidence, and only intentional test-log changes remain before commit.

- [ ] **Step 7: Commit acceptance evidence**

```powershell
git add scripts/verify-wav.ps1 docs/hardware/m1-test-log.md docs/hardware/m1-acceptance.md
git commit -m "test: verify recording baseline on V2 hardware"
```

## Milestone Exit Gate

Milestone 1 is complete only when all eight tasks are committed and the real V2 board evidence proves: physical one-button recording without a phone, two-hour uninterrupted capture with zero dropped buffers, playable finalized WAV files, and recoverable recordings at all three forced power-loss points. If microphone tests show inadequate meeting intelligibility, record that as a hardware finding before proceeding; do not conceal it with software post-processing.

After this gate, write separate plans in dependency order for: (1) BLE index and resumable transfer, (2) WeChat mini-program and backend upload loop, (3) ASR/summary processing, (4) real-time scratch-note streaming, and (5) Opus optimization.
