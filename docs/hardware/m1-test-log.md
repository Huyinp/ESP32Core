# Milestone 1 Hardware Test Log

## Board

- Product: ESP32-S3-Touch-AMOLED-1.8
- Hardware revision: V2 (confirmed by owner)
- ESP-IDF: 5.5.4
- BSP: waveshare/esp32_s3_touch_amoled_1_8 2.0.3

## Capability check

- Commit: this commit
- Date: 2026-09-13
- COM port: COM11
- Result: PASS
- Serial evidence: `RECORDER_BOARD_READY`; ESP32-S3, 16 MB flash, 8 MB PSRAM,
  display 368x448, `audio=1 sdcard=1 touch=1`.

## Audio diagnostic

- Date: 2026-09-13
- COM port: COM11
- Result: PASS (capture, finalize, rename, and on-device size check)
- Format: PCM signed 16-bit little-endian, 16000 Hz, mono, 5 seconds
- Samples: 80000
- PCM bytes: 160000
- WAV bytes: 160044
- Peak sample: 32767
- Clipped samples: 26
- Zero samples: 188
- Listening notes: pending manual playback after copying `diagnostic.wav` from microSD

## Board adapters and button classifier

- Microphone validation: 2 tests, 0 failures, 0 ignored
- Button boundaries: 4 tests, 0 failures, 0 ignored
- PWR input adapter: pending board-level probing; BSP 2.0.3 reports
  `BSP_CAPS_BUTTONS=0` and exports no PWR input pin, so no GPIO was guessed.

## WAV writer validation

- Board tests: 4 tests, 0 failures, 0 ignored
- Format: PCM signed 16-bit little-endian, 16000 Hz, mono
- One-second repair header: PASS (32000 PCM bytes)
- ffprobe: unavailable on development machine

## Ten-minute recording

- Result:
- Duration:
- Dropped buffers:
- File verification:

## Power-loss recovery

- Under 5 seconds:
- After 1 minute:
- During UI update:

## Two-hour soak

- Result:
- Duration:
- Dropped buffers:
- SD errors:
- Minimum free heap:
- WAV verification:
