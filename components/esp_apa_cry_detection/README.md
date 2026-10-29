# ESP APA Cry Detection

[中文](README_CN.md)

**esp_apa_cry_detection** is a lightweight infant-cry detection component from Espressif. It stably detects cry start and end from a continuous PCM stream. The detector keeps a sliding audio window, gates silent frames by RMS energy, runs a compact int8 CNN classifier, then applies exponential moving average (EMA) smoothing and a dual-threshold finite state machine (FSM) to emit START / END events. Typical products: baby monitors, smart speakers, care cameras, service robots, and other AIoT care devices.

## Key Features

- 16 kHz mono 16-bit PCM; 1000 ms window, 150 ms hop
- Energy gate skips silent windows to save CPU
- Compact int8 CNN outputs cry probability
- EMA + dual-threshold FSM for stable START / END, suppressing short false triggers
- Algorithm does not resample, downmix, or denoise

**Supported chips**: ESP32 · ESP32-C3 · ESP32-C5 · ESP32-C6 · ESP32-C61 · ESP32-S3 · ESP32-S31 · ESP32-P4

---

## Configuration

Runtime options live in `esp_apa_cry_detection_config_t` (`include/esp_apa_cry_detection.h`). Start from `ESP_APA_CRY_DETECTION_DEFAULT_CONFIG()` and override board-specific fields before `esp_apa_cry_detection_open()`.

Only sensitivity-related fields are exposed: `gate` skips the CNN on silent windows to save CPU; `fsm` controls how easily START / END fire.

### Energy gate `gate`

Pre-filter before the CNN: whether the current 1000 ms window is loud enough. It does **not** decide whether the sound is a cry.

| Field | Default | Description |
| ----- | ------- | ----------- |
| `enable` | `true` | `true`: skip CNN when RMS is below the threshold and feed 0.0 to the FSM; `false`: run CNN every hop |
| `energy_threshold` | `0.001` | RMS threshold, linear amplitude (PCM normalized to `[-1, 1]`) |

Raising the threshold filters more quiet noise; lower it if quiet cries are skipped. Default `0.001` lets quiet / far-field cries through (`0.01` is strict and tends to miss).

### FSM / EMA `fsm`

Raw CNN probability is EMA-smoothed, then confirmed by a dual-threshold FSM to suppress short false triggers and tolerate brief pauses during a cry.

- **START**: EMA stays above `start_threshold` for `start_confirm_count` hops
- **END**: EMA stays below `end_threshold` for `end_confirm_count` hops
- Keep `start_threshold` > `end_threshold` (hysteresis)
- Larger `ema_alpha` is smoother and slower; smaller is more responsive and noisier

With hop = 150 ms, default 4 / 6 hops are about 600 ms / 900 ms.

| Field | Default | Description |
| ----- | ------- | ----------- |
| `start_threshold` | `0.85` | EMA probability to enter crying, typical 0.7–0.9 |
| `end_threshold` | `0.35` | EMA probability to leave crying, typical 0.2–0.4, must be less than `start_threshold` |
| `start_confirm_count` | `4` | Consecutive hops above the enter threshold before START |
| `end_confirm_count` | `6` | Consecutive hops below the exit threshold before END |
| `ema_alpha` | `0.77` | EMA history weight, range `(0, 1]` |

### Fixed parameters

| Item | Value |
| ---- | ----- |
| Sample rate / channels / bit depth | 16 kHz / mono / int16 |
| CNN window | 1000 ms (16000 samples) |
| Inference hop | 150 ms (2400 samples) |
| Ring buffer | 2000 ms (32000 samples) |

### Performance and memory (ESP32-P4)

Conditions: Function EV, CPU **400 MHz**. CPU-loading is wall-clock time of one hop relative to the **1000 ms analysis window**.

| Item | Used |
| ---- | ---- |
| CPU-loading | 5.72% |
| Stack | 3 KiB |
| PSRAM | 158.5 KiB |
| InnRAM | 0 KiB |
| Flash | 64.4 KiB |

This component uses `espressif/gmf_fft` for Log-Mel STFT. On chips with PSRAM, algorithm heap is allocated in PSRAM.

---

## API reference

Header: `include/esp_apa_cry_detection.h`.

Input must be **16 kHz mono int16**. The component does not resample, downmix, or convert bit depth.

### Lifecycle

| Function | Description |
| -------- | ----------- |
| `esp_apa_cry_detection_open(cfg, &handle)` | Allocate the detector; on failure `*handle = NULL`. Does not create an internal task |
| `esp_apa_cry_detection_process(h, pcm, samples, &status)` | Feed PCM; run inference and update `status` when a hop is complete |
| `esp_apa_cry_detection_close(h)` | Free all internal resources |

`samples` is the number of samples in `data`. The same handle is **not ISR-safe and not thread-safe**.

### Detector state: `esp_apa_cry_detection_state_t`

| Value | Meaning |
| ----- | ------- |
| `ESP_APA_CRY_DETECTION_STATE_IDLE` | Idle, no cry activity |
| `ESP_APA_CRY_DETECTION_STATE_DETECTING` | EMA near the enter threshold, not yet confirmed |
| `ESP_APA_CRY_DETECTION_STATE_CRYING` | Confirmed crying state |

### One-shot event: `esp_apa_cry_detection_event_t`

| Value | Meaning |
| ----- | ------- |
| `ESP_APA_CRY_DETECTION_EVENT_NONE` | No state transition in this `process()` call |
| `ESP_APA_CRY_DETECTION_EVENT_START` | Entered `CRYING` |
| `ESP_APA_CRY_DETECTION_EVENT_END` | Left `CRYING` |

Product logic should subscribe to **event**, not poll `cry_prob` every hop.

### Result: `esp_apa_cry_detection_status_t`

| Field | Type | Meaning |
| ----- | ---- | ------- |
| `valid` | `bool` | Whether the fields are meaningful |
| `audio_active` | `bool` | Whether the energy gate is open (CNN about to run / already ran) |
| `state` | `esp_apa_cry_detection_state_t` | Current detector state |
| `cry_prob` | `float` | Latest raw P(cry), range `[0, 1]` |
| `ema_prob` | `float` | EMA-smoothed P(cry), range `[0, 1]` |
| `event` | `esp_apa_cry_detection_event_t` | Last START / END in this call; `NONE` if no hop ran |
| `inference_count` | `uint32_t` | Number of completed inference hops |

When no hop runs, `event` is `NONE` and `cry_prob` / `ema_prob` / `state` keep the previous snapshot. If one `process()` completes multiple hops, `event` is the last START / END among them.

---

## Examples

Live microphone demo: [`examples/infant_cry_detection`](examples/infant_cry_detection/). The example captures PCM, runs standalone `esp_ns` (not AFE), then calls `esp_apa_cry_detection_process()` every 50 ms and prints cry START / END on UART. Select the board with `idf.py bmgr` first (see the [example README](examples/infant_cry_detection/README.md)).

```c
#include "esp_apa_cry_detection.h"

esp_apa_cry_detection_config_t cfg = ESP_APA_CRY_DETECTION_DEFAULT_CONFIG();
esp_apa_cry_detection_handle_t cry = NULL;
ESP_ERROR_CHECK(esp_apa_cry_detection_open(&cfg, &cry));

int16_t pcm[800];
esp_apa_cry_detection_status_t st;

while (/* feed 16 kHz mono PCM */) {
    ESP_ERROR_CHECK(esp_apa_cry_detection_process(cry, pcm, 800, &st));
    if (st.event == ESP_APA_CRY_DETECTION_EVENT_START) {
        /* crying started */
    } else if (st.event == ESP_APA_CRY_DETECTION_EVENT_END) {
        /* crying ended */
    }
}

ESP_ERROR_CHECK(esp_apa_cry_detection_close(cry));
```

---

## FAQ

### Why is there never a START?

Check in this order:

| Case | Notes |
| ---- | ----- |
| Window not full yet | About 1000 ms must accumulate first; then one inference every 150 ms |
| `audio_active == false` | Energy gate is too strict: CNN is skipped and probability is forced to 0; lower `gate.energy_threshold` or set `gate.enable = false` to compare |
| `ema_prob` is already high but no START | Confirm count is not reached; decrease `start_confirm_count` or slightly lower `start_threshold` |
| Input is not 16 kHz mono int16 | The component does not resample / downmix; wrong format almost never detects |

### Why so many false triggers?

Raise `start_threshold`, increase `start_confirm_count`, or slightly raise `gate.energy_threshold` to filter ambient noise. A slightly larger `ema_alpha` makes probability more stable and less likely to cross the threshold on short noise.

### Why does a brief pause in the cry emit END?

Exit hysteresis is too tight. Increase `end_confirm_count` (default 6 hops ≈ 900 ms) or slightly lower `end_threshold` so short pauses stay in `CRYING`.

### What if one `process()` call feeds a large chunk?

That is fine. Internally the stream is sliced on hop boundaries: each hop uses the 1000 ms window ending at that hop. `status->event` keeps only the **last** START / END in this call; `inference_count` adds the number of hops completed this time.

### Do I need to create a task myself?

Yes. `open()` does not create a FreeRTOS task. Call `process()` with chunks from the capture task. The API is not thread-safe; do not operate the same handle from multiple tasks at once.

### How do I tune for my scenario?

Start from `ESP_APA_CRY_DETECTION_DEFAULT_CONFIG()` and override fields before `open()`. Do not change library defaults.

| Symptom | What to change | Suggestion |
| ------- | -------------- | ---------- |
| Far-field / quiet cries missed, and `audio_active` is often false | `gate.energy_threshold` | Lower it (default 0.001; do not casually raise it back to 0.01) |
| Ambient noise causes frequent START | `start_threshold`, `start_confirm_count`, `gate.energy_threshold` | Raise enter threshold or confirm hops; raise the energy gate if needed |
| START is too slow | `start_confirm_count`, `ema_alpha` | Fewer confirm hops (delay ≈ `count × 150 ms`); slightly lower `ema_alpha` |
| One cry is split into several segments | `end_confirm_count`, `end_threshold` | Increase exit confirm count; slightly lower the exit threshold |
| CPU is high | `gate.enable` | Keep the gate on so silent hops skip the CNN |

```c
esp_apa_cry_detection_config_t cfg = ESP_APA_CRY_DETECTION_DEFAULT_CONFIG();
cfg.gate.energy_threshold    = 0.001f;  /* lower if far-field misses; raise if noise false-triggers */
cfg.fsm.start_threshold      = 0.85f;
cfg.fsm.end_threshold        = 0.35f;
cfg.fsm.start_confirm_count  = 4;       /* ≈ 600 ms */
cfg.fsm.end_confirm_count    = 6;       /* ≈ 900 ms */
cfg.fsm.ema_alpha            = 0.77f;
ESP_ERROR_CHECK(esp_apa_cry_detection_open(&cfg, &cry));
```
