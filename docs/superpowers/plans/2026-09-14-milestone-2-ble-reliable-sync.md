# Milestone 2 BLE Reliable Sync Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let an Android WeChat mini-program securely connect to the recorder, inspect device status and completed WAV files, and resume verified BLE downloads after disconnects.

**Architecture:** Pure-C protocol, catalog, and transfer-session components own all deterministic behavior and are tested independently of NimBLE. A thin NimBLE GATT adapter exposes encrypted characteristics and reads files only through the transfer component. A minimal WeChat mini-program mirrors the binary codec, persists acknowledged offsets, and writes chunks to a temporary file before acknowledging them.

**Tech Stack:** ESP-IDF 5.5.4, C17, NimBLE, FreeRTOS, FATFS, mbedTLS SHA-256, ESP-IDF Unity, WeChat mini-program JavaScript, Node.js built-in test runner.

**Spec:** `docs/superpowers/specs/2026-09-12-esp32-s3-ai-recorder-design.md`

## Global Constraints

- Target only ESP32-S3-Touch-AMOLED-1.8 V2 and ESP-IDF 5.5.4.
- Local recording and SD writes always take priority over BLE work.
- No real recording bytes may be exposed before an encrypted, authenticated BLE link is established.
- Validate BLE Secure Connections on an Android phone running WeChat before enabling file transfer. If the OS prompt cannot be triggered reliably, stop and write an application-layer encryption design; never silently allow plaintext audio.
- The mini-program transfers only while it is in the foreground; reconnecting must resume from its last durably written contiguous offset.
- Do not assume a fixed MTU. Data payload size is calculated from the current ATT MTU and the 18-byte frame overhead.
- This milestone excludes Wi-Fi, cloud upload, ASR, summaries, real-time audio streaming, Opus, deletion, and OTA.
- Repeated control requests and acknowledgements are idempotent.

## Fixed Protocol v1

All multibyte integers are unsigned little-endian. UUID base is `8d2fxxxx-7a42-4d65-a8d0-9f1c2b7e5100`:

| Value | UUID/encoding | Purpose |
|---|---|---|
| Service | `8d2f0000-7a42-4d65-a8d0-9f1c2b7e5100` | Recorder sync service |
| Control | `8d2f0001-7a42-4d65-a8d0-9f1c2b7e5100` | encrypted+authenticated write |
| Status | `8d2f0002-7a42-4d65-a8d0-9f1c2b7e5100` | encrypted+authenticated read/notify |
| Index | `8d2f0003-7a42-4d65-a8d0-9f1c2b7e5100` | encrypted+authenticated notify |
| Data | `8d2f0004-7a42-4d65-a8d0-9f1c2b7e5100` | encrypted+authenticated notify |

Control messages are `LIST=0x01`, `GET=0x02`, `ACK=0x03`, `CANCEL=0x04`. `GET` and `ACK` carry `opcode:u8, recording_id:u32, offset:u32`; `CANCEL` carries `opcode:u8, recording_id:u32`. Unknown opcodes or wrong lengths return ATT invalid-attribute-value-length and change no state.

The 16-byte status value is `version:u8, recorder_state:u8, sync_state:u8, error:u8, elapsed_ms:u32, free_kib:u32, pending_count:u16, flags:u16`. Bit 0 of `flags` means the link is authenticated.

Index fragments contain `version:u8, type=0x11:u8, recording_id:u32, file_size:u32, name_offset:u8, total_name_len:u8, chunk_len:u8, name_chunk:chunk_len`; `INDEX_END=0x12` is the two-byte version/type frame. A basename is split according to the current ATT MTU, so even the 20-byte minimum ATT payload works. An ID is CRC32 of the UTF-8 basename. A scan fails with `ESP_ERR_INVALID_STATE` if two names collide.

Data frames contain `version:u8, type=0x21:u8, recording_id:u32, offset:u32, sequence:u16, payload_len:u16, payload, crc32:u32`. CRC32 covers the 14-byte header followed by payload. The maximum payload is `min(att_mtu - 3 - 18, 512)` and transfer pauses when the result is zero.

Completion fragments contain `version:u8, type=0x22:u8, recording_id:u32, file_size:u32, digest_offset:u8, chunk_len:u8, digest_chunk:chunk_len`. The 32-byte SHA-256 is reassembled from as many fragments as the current ATT MTU requires. The mini-program acknowledges only bytes already written to its temporary file; completion is accepted only when size and SHA-256 match.

## File Map

```text
components/transfer/                  Protocol codec and resumable session
components/catalog/                   Completed-WAV enumeration/read/hash
components/ble_sync/                  NimBLE security and GATT adapter
components/ui/                        Pairing-code/status presentation
main/app_main.c                       Composition root only
miniprogram/                          Importable WeChat mini-program
miniprogram/lib/protocol.js           Binary codec mirrored from C
miniprogram/lib/sha256.js             Incremental bounded-memory SHA-256
miniprogram/lib/sync.js               Durable receive/resume state machine
test_app/main/CMakeLists.txt           On-device Unity registration
docs/hardware/m2-test-log.md           Android/WeChat acceptance evidence
```

---

### Task 1: Binary Protocol Codec

**Files:**
- Create: `components/transfer/CMakeLists.txt`
- Create: `components/transfer/include/transfer_protocol.h`
- Create: `components/transfer/transfer_protocol.c`
- Create: `components/transfer/test/test_transfer_protocol.c`
- Modify: `test_app/main/CMakeLists.txt`

**Interfaces:**
- Produces: `transfer_decode_control()`, `transfer_encode_status()`, `transfer_encode_index()`, `transfer_encode_data()`, and `transfer_payload_capacity()`.
- Consumes: byte buffers only; no BLE, filesystem, or global state.

- [ ] **Step 1: Write failing codec tests**

Add Unity cases for exact little-endian bytes, malformed control lengths, CRC corruption, and MTUs 23, 64, 247, and 517. The boundary assertion is:

```c
TEST_CASE("data payload follows negotiated mtu", "[transfer]")
{
    TEST_ASSERT_EQUAL_UINT16(2, transfer_payload_capacity(23));
    TEST_ASSERT_EQUAL_UINT16(43, transfer_payload_capacity(64));
    TEST_ASSERT_EQUAL_UINT16(226, transfer_payload_capacity(247));
    TEST_ASSERT_EQUAL_UINT16(496, transfer_payload_capacity(517));
}
```

- [ ] **Step 2: Run the test build and verify RED**

Run: `.\scripts\export-idf.ps1 -C test_app build`

Expected: compilation fails because `transfer_protocol.h` or the declared functions do not exist.

- [ ] **Step 3: Implement the codec without packed structs**

Use explicit `put_u16_le`, `put_u32_le`, `get_u32_le` helpers and length checks. Do not cast byte arrays to C structs. Implement IEEE CRC32 with reflected polynomial `0xEDB88320`, initial value `0xFFFFFFFF`, and final XOR `0xFFFFFFFF` in both C and JavaScript. Return `ESP_ERR_INVALID_SIZE` when the destination buffer is short.

- [ ] **Step 4: Verify GREEN**

Run: `.\scripts\export-idf.ps1 -C test_app build`

Then flash and run `[transfer]`; expected result is all transfer tests passing with zero ignored.

- [ ] **Step 5: Commit**

```powershell
git add components/transfer test_app/main/CMakeLists.txt
git commit -m "feat: define BLE sync protocol"
```

### Task 2: Completed Recording Catalog

**Files:**
- Create: `components/catalog/CMakeLists.txt`
- Create: `components/catalog/include/recording_catalog.h`
- Create: `components/catalog/recording_catalog.c`
- Create: `components/catalog/test/test_recording_catalog.c`
- Modify: `test_app/main/CMakeLists.txt`

**Interfaces:**
- Produces: `recording_catalog_scan(root, entries, capacity, count)`, `recording_catalog_read(root, id, offset, dst, capacity, read)`, and `recording_catalog_sha256(root, id, digest)`.
- Produces `recording_catalog_entry_t { uint32_t id; uint32_t file_size; char name[96]; }` sorted lexicographically by basename.
- Consumes: finalized `.wav` files from `recording_store`; ignores `.wav.part`, `.corrupt`, directories, and `diagnostic.wav`.

- [ ] **Step 1: Write failing filesystem tests**

Create SPIFFS fixtures `a.wav`, `b.wav`, `active.wav.part`, and `bad.txt`. Assert stable order, exact sizes, repeatable CRC32 IDs, bounded offset reads, SHA-256, and rejection of `../` names. Add a collision seam through a private hash callback and assert the scan fails rather than returning ambiguous IDs.

- [ ] **Step 2: Verify RED**

Run: `.\scripts\export-idf.ps1 -C test_app build`

Expected: missing `recording_catalog.h`.

- [ ] **Step 3: Implement the minimal catalog**

Use `opendir`, `stat`, `fseek`, and `fread`; reconstruct paths only from names returned by the opened directory. Reject files larger than `UINT32_MAX`. Hash in 4096-byte reads using `mbedtls_sha256_context`; never load a whole WAV into RAM.

- [ ] **Step 4: Verify GREEN on COM11**

Build, flash, and run `[catalog]`. Expected: all catalog tests pass and the existing storage tests remain green.

- [ ] **Step 5: Commit**

```powershell
git add components/catalog test_app/main/CMakeLists.txt
git commit -m "feat: index completed recordings"
```

### Task 3: Authenticated NimBLE Service and Status

**Files:**
- Create: `components/ble_sync/CMakeLists.txt`
- Create: `components/ble_sync/include/ble_sync.h`
- Create: `components/ble_sync/ble_sync.c`
- Create: `components/ble_sync/test/test_ble_sync_security.c`
- Modify: `sdkconfig.defaults`
- Modify: `main/CMakeLists.txt`
- Modify: `main/app_main.c`
- Modify: `components/ui/include/recorder_ui_model.h`
- Modify: `components/ui/recorder_ui_model.c`
- Modify: `components/ui/test/test_recorder_ui_model.c`

**Interfaces:**
- Produces: `ble_sync_start(const ble_sync_config_t *)`, `ble_sync_publish_status(const ble_sync_status_t *)`, and callbacks `pairing_passkey(uint32_t)` / `connection_changed(bool authenticated)`.
- Consumes: recorder snapshots and free-space/pending-count providers supplied by `app_main`; it does not call recorder or SD internals directly.

- [ ] **Step 1: Write failing security-policy and UI tests**

Extract a pure `ble_sync_access_allowed(bool encrypted, bool authenticated)` helper. Assert false for no encryption and encryption without authentication, true only for both. Add UI tests that a passkey produces `PAIR / 123456`, and clearing it returns to the recorder status.

- [ ] **Step 2: Verify RED**

Run both firmware builds. Expected: missing BLE sync symbols and pairing UI state.

- [ ] **Step 3: Add the secure GATT skeleton**

Enable NimBLE peripheral, bonding, Secure Connections, MITM, and display-only IO capability in `sdkconfig.defaults`. Advertise the fixed service UUID and device name `Recorder-97A8`. Mark Control, Status, Index, and Data with encrypted+authenticated access flags. Generate/display a six-digit passkey from NimBLE's passkey event; never log it after pairing completes. Status reads and notifications use `transfer_encode_status()`.

- [ ] **Step 4: Verify firmware tests and startup**

Run all Unity tests, then flash the product firmware. Expected serial evidence: advertising starts; existing recording remains usable while no central is connected.

- [ ] **Step 5: Commit**

```powershell
git add components/ble_sync components/ui sdkconfig.defaults main
git commit -m "feat: advertise authenticated recorder service"
```

### Task 4: Android WeChat Security Compatibility Gate

**Files:**
- Create: `miniprogram/app.js`
- Create: `miniprogram/app.json`
- Create: `miniprogram/project.config.json`
- Create: `miniprogram/pages/device/device.js`
- Create: `miniprogram/pages/device/device.json`
- Create: `miniprogram/pages/device/device.wxml`
- Create: `miniprogram/pages/device/device.wxss`
- Create: `miniprogram/lib/protocol.js`
- Create: `miniprogram/tests/protocol.test.js`
- Create: `miniprogram/package.json`
- Create: `docs/hardware/m2-test-log.md`

**Interfaces:**
- Produces: a device page that scans for the service UUID, connects, discovers characteristics, subscribes to Status, and renders connection/authentication state.
- Consumes: WeChat `wx.openBluetoothAdapter`, discovery, connection, service/characteristic, read, and notification APIs.

- [ ] **Step 1: Write failing JavaScript protocol tests**

Use `node:test` and `assert.deepEqual` to verify the same golden status/control byte arrays as the C tests. Run `npm test` from `miniprogram`; expected RED because `lib/protocol.js` does not exist.

- [ ] **Step 2: Implement the minimal device page and codec**

Keep BLE lifecycle in `pages/device/device.js`: stop discovery after matching the service, connect, discover, subscribe, then read Status to trigger the Android pairing prompt. On `onHide` or `onUnload`, stop discovery and close only this device connection. Store no audio in this task.

- [ ] **Step 3: Verify JavaScript tests**

Run: `npm test` from `miniprogram`.

Expected: all protocol golden-vector tests pass.

- [ ] **Step 4: Run the mandatory Android WeChat gate**

Import `miniprogram` into WeChat DevTools with the user's AppID, preview on an Android phone, connect, enter the six-digit code shown on the V2 display, disconnect, and reconnect. Record phone model, Android version, WeChat version, whether the OS prompt appeared, and whether the bonded reconnect exposed Status.

If any authenticated characteristic can be read without pairing, or the prompt cannot be triggered reliably across three fresh-bond attempts, stop this plan before Task 5 and create an application-layer encryption design.

- [ ] **Step 5: Commit only after PASS**

```powershell
git add miniprogram docs/hardware/m2-test-log.md
git commit -m "feat: connect WeChat mini-program securely"
```

### Task 5: File Index Over GATT

**Files:**
- Modify: `components/ble_sync/include/ble_sync.h`
- Modify: `components/ble_sync/ble_sync.c`
- Create: `components/ble_sync/test/test_ble_sync_index.c`
- Modify: `main/app_main.c`
- Modify: `miniprogram/pages/device/device.js`
- Modify: `miniprogram/pages/device/device.wxml`
- Modify: `miniprogram/lib/protocol.js`
- Modify: `miniprogram/tests/protocol.test.js`

**Interfaces:**
- Control `LIST` triggers a fresh `recording_catalog_scan()` and sends enough Index fragments to reconstruct every entry, followed by `INDEX_END`.
- The mini-program produces an in-memory file list keyed by `recording_id`; receiving the same item replaces it instead of appending a duplicate.

- [ ] **Step 1: Write failing C and JavaScript index tests**

Assert LIST is rejected before authentication, long UTF-8 names reconstruct across minimum-MTU fragments, two catalog entries end with one `INDEX_END`, a repeated LIST has no persistent device-side cursor, and duplicate fragments remain one UI row.

- [ ] **Step 2: Verify RED in both runtimes**

Run the ESP-IDF test build and `npm test`; both must fail for missing index behavior.

- [ ] **Step 3: Implement index notifications**

Scan on the BLE worker task, not in the NimBLE host callback. Copy the immutable entry list before notifying. If recording starts during a scan, the already-finalized snapshot remains valid; active `.part` files never appear.

- [ ] **Step 4: Verify GREEN and real-device listing**

Run `[ble_sync]`, all JavaScript tests, then confirm the phone shows the same finalized WAV basenames and sizes as the SD card.

- [ ] **Step 5: Commit**

```powershell
git add components/ble_sync main miniprogram
git commit -m "feat: list recordings over BLE"
```

### Task 6: Resumable Transfer Session Core

**Files:**
- Create: `components/transfer/include/transfer_session.h`
- Create: `components/transfer/transfer_session.c`
- Create: `components/transfer/test/test_transfer_session.c`
- Modify: `components/transfer/CMakeLists.txt`
- Modify: `test_app/main/CMakeLists.txt`

**Interfaces:**
- Produces: `transfer_session_begin(id, file_size, confirmed_offset, mtu)`, `transfer_session_next(read_fn, out, capacity, out_size)`, `transfer_session_ack(id, offset)`, `transfer_session_cancel(id)`, and `transfer_session_disconnect()`.
- Consumes: a bounded random-read callback matching `esp_err_t (*)(void *, uint32_t id, uint32_t offset, uint8_t *, size_t, size_t *)`.

- [ ] **Step 1: Write failing state-machine tests**

Cover MTU-derived chunk size, monotonically increasing sequence/offset, ACK lower than current offset as an idempotent no-op, ACK beyond sent bytes as invalid, restart from a durable offset, disconnect preserving no open `FILE *`, cancel, CRC, EOF, and zero-length files.

- [ ] **Step 2: Verify RED**

Run the test build; expected failure is missing `transfer_session.h`.

- [ ] **Step 3: Implement one in-flight window**

Use a single outstanding notification window for the first reliable version: do not advance the confirmed offset until ACK arrives. `next()` may resend the outstanding frame byte-for-byte after timeout or reconnect. Store only IDs and offsets; open/read/close inside the catalog callback so BLE disconnect cannot leak a file handle.

- [ ] **Step 4: Verify GREEN**

Run `[transfer]` and the complete Unity suite. Expected: all existing recorder/storage tests remain green.

- [ ] **Step 5: Commit**

```powershell
git add components/transfer test_app/main/CMakeLists.txt
git commit -m "feat: add resumable BLE transfer session"
```

### Task 7: Firmware Data Transfer Adapter

**Files:**
- Modify: `components/ble_sync/ble_sync.c`
- Modify: `components/ble_sync/test/test_ble_sync_security.c`
- Create: `components/ble_sync/test/test_ble_sync_transfer.c`
- Modify: `main/app_main.c`

**Interfaces:**
- Authenticated GET starts/resumes a `transfer_session`; ACK advances it; CANCEL ends it.
- Produces Data and Completion notifications on a BLE worker queue of depth 4.
- Consumes catalog read/hash providers passed in `ble_sync_config_t`.

- [ ] **Step 1: Write failing adapter tests with fake catalog and notifier**

Assert unauthenticated GET is rejected, missing IDs report `NOT_FOUND`, notifications never run in the GATT write callback, disconnect cancels queued sends but retains the client-owned durable offset, and recorder state transitions continue while transfer notifications are back-pressured.

- [ ] **Step 2: Verify RED**

Run the test build; expected failures identify the missing GET/ACK worker behavior.

- [ ] **Step 3: Implement the thin adapter**

The GATT callback validates and enqueues commands only. A priority-3 FreeRTOS worker reads at most one 512-byte chunk per iteration and yields after each notification. Recorder/audio tasks and SD writes retain their existing priorities. Publish sync progress in Status without modifying recorder state.

- [ ] **Step 4: Verify GREEN and recording independence**

Run all Unity tests. On COM11, start a BLE download, begin and stop a local recording with PWR, and confirm both the original transfer and new WAV remain valid.

- [ ] **Step 5: Commit**

```powershell
git add components/ble_sync main/app_main.c
git commit -m "feat: stream recording files over BLE"
```

### Task 8: Mini-Program Durable Receive and Resume

**Files:**
- Create: `miniprogram/lib/sync.js`
- Create: `miniprogram/lib/sha256.js`
- Create: `miniprogram/tests/sync.test.js`
- Modify: `miniprogram/pages/device/device.js`
- Modify: `miniprogram/pages/device/device.wxml`
- Modify: `miniprogram/pages/device/device.wxss`

**Interfaces:**
- Produces: `SyncSession` with `start(entry)`, `acceptFrame(arrayBuffer)`, `resume()`, `cancel()`, and observable progress.
- Consumes: WeChat file-system manager append/write APIs and Control characteristic writes.

- [ ] **Step 1: Write failing durable-sync tests**

Use an in-memory fake file system and BLE writer. Assert CRC failure sends no ACK, duplicate frames do not append twice, a gap sends no ACK, ACK occurs after awaited write completion, reconnect GET uses the persisted contiguous offset, fragmented completion digests reassemble in order, Completion verifies size/SHA-256, and two starts for the same recording reuse one temporary file. Verify the SHA-256 implementation with empty-string and `abc` standard vectors plus a file split at non-block-aligned offsets.

- [ ] **Step 2: Verify RED**

Run `npm test`; expected failure is missing `SyncSession`.

- [ ] **Step 3: Implement sequential durable receive**

Persist `{recordingId, tempPath, confirmedOffset, expectedSize}` with `wx.setStorageSync` only after the file write resolves. Serialize notification handling through one Promise chain. Hash the completed temporary file in bounded reads; never hold a complete recording in memory.

- [ ] **Step 4: Verify GREEN and lifecycle behavior**

Run `npm test`. On Android WeChat, background the mini-program during transfer, return to foreground, reconnect, reload the index, and verify GET resumes from the saved offset.

- [ ] **Step 5: Commit**

```powershell
git add miniprogram
git commit -m "feat: resume BLE downloads in mini-program"
```

### Task 9: Milestone 2 Acceptance

**Files:**
- Modify: `docs/hardware/m2-test-log.md`

**Interfaces:**
- Consumes: the completed firmware, Android WeChat mini-program, and SD recordings.
- Produces: reproducible evidence for every Milestone 2 acceptance criterion.

- [ ] **Step 1: Run automated verification**

Run `.\scripts\export-idf.ps1 -C test_app build`, flash and execute the complete Unity suite, run `.\scripts\export-idf.ps1 build`, and run `npm test` in `miniprogram`. Record exact test counts and firmware commit.

- [ ] **Step 2: Run random-disconnect recovery**

Transfer a WAV of at least ten minutes, force at least five disconnects at different offsets, and verify the displayed offset never exceeds the mini-program's actual temporary-file length.

- [ ] **Step 3: Verify integrity and idempotency**

Calculate SHA-256 on the SD source and downloaded phone file; require equality. Repeat LIST and GET after completion and require one logical recording and no second temporary file.

- [ ] **Step 4: Verify foreground and recording independence**

Move WeChat background/foreground three times during transfer, then record and finalize a new WAV while another file syncs. Require both final WAV and downloaded file to pass size/hash checks.

- [ ] **Step 5: Document and commit evidence**

```powershell
git add docs/hardware/m2-test-log.md
git commit -m "test: verify BLE reliable sync milestone"
```

Do not mark Milestone 2 complete if security, source/download SHA-256 equality, resume, idempotency, or concurrent local recording lacks real-device evidence.
