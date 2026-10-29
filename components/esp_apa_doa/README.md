# ESP APA DOA

[中文](README_CN.md)

**esp_apa_doa** is a lightweight multi-microphone direction-of-arrival (DOA) algorithm component from Espressif that accurately estimates the sound-source direction. It supports 2–4 mic arrays with interleaved or planar data input, and integrates flexibly with multi-channel audio front ends such as ESP-SR and AFE. The component fits a wide range of products—smart speakers, voice home appliances, conferencing endpoints, PTZ cameras, service robots, in-vehicle voice devices, and smart toys—providing reliable direction information for directional pickup, beam control, speaker tracking, camera steering, and far-field voice interaction.

## Key Features

- Supports 2/3/4-mic arrays
- Audio input can be interleaved or planar
- Multi-mic arrays support multiple geometries (collinear, triangular, square, rectangular)
- Connects directly to modules such as ESP-SR / AFE for easy multi-algorithm integration

**Supported chips**: ESP32 · ESP32-C3 · ESP32-C5 · ESP32-C6 · ESP32-C61 · ESP32-S3 · ESP32-S31 · ESP32-P4

---

## How it works

Typical integration follows: **mic layout → capture PCM → (optional) Layout engine (drop non-microphone data) → configure engine → process and read results**.

### 1. Microphone layout

DOA needs each mic’s board `(x, y)` in meters, and **PCM channel `i` must be the physical mic at `mic_pos[i]`**. Origin = array center; **+Y = 0° (front)**; azimuth increases clockwise.

Common macros (arguments are adjacent mic spacing):

| Macro | Mics | Notes |
| ----- | ---: | ----- |
| `ESP_APA_DOA_MIC_POS_LINEAR(dist_m)` | 2 | Collinear; 0..180° output |
| `ESP_APA_DOA_MIC_POS_TRIANGLE(side_m)` | 3 | Equilateral triangle; 360° |
| `ESP_APA_DOA_MIC_POS_SQUARE(side_m)` | 4 | Square; 360° |
| `ESP_APA_DOA_MIC_POS_RECT(w_m, h_m)` | 4 | Rectangle; 360° |

Fill `mic_pos[]` in logical order so PCM channel `i` maps to `mic_pos[i]`. If the interleaved stream contains reference/noise slots, use `esp_apa_data_layout` to **drop** them (below); if kept mics are in a different order than your geometry, **reorder in the capture path** — `esp_apa_data_layout` does **not** reorder. See [Microphone layout](#microphone-layout).

### 2. Capture PCM

Obtain multi-channel 16-bit PCM from I2S / TDM / AFE and feed it **interleaved** or **planar** per `input_layout`. `sample_count` must be a multiple of `mic_num`; partial chunks are fine — the engine buffers internally.

### 3. Layout engine (drop non-microphone data)

ESP-SR / AFE often delivers a fixed slot layout (e.g. **MRMN**: mic, reference, mic, noise). DOA only needs **M** slots, not reference/noise.

`esp_apa_data_layout` describes the full interleaved stream: one character per slot; `M` marks a microphone, other letters (`R`, `N`, …) are dropped. Output remains interleaved mic-only PCM. Kept mics are output **in their original stream order — no reordering**.

```c
esp_apa_data_layout_cfg_t ecfg = ESP_APA_DATA_LAYOUT_CFG_DEFAULT("MRMN");
esp_apa_data_layout_handle_t layout = NULL;
esp_apa_data_layout_create(&ecfg, &layout);

const int16_t *doa_pcm = NULL;
size_t doa_bytes = 0;
esp_apa_data_layout_process(layout, raw_pcm, raw_bytes, &doa_pcm, &doa_bytes);
/* doa_pcm: slots 0 and 2 only, interleaved */
```

Skip this step if the driver already delivers mic channels aligned with `mic_pos[]`.

### 4. Configure the engine

Fill `esp_apa_doa_cfg_t` before `esp_apa_doa_create()`: sample rate, mic count, `mic_pos`, input layout, and analysis parameters. Start from `ESP_APA_DOA_CFG_DEFAULT()`; see [Configuration](#configuration) and [FAQ](#faq) for board-level gates.

### 5. Process and results

Call `esp_apa_doa_process()` repeatedly. When the window is full and quality gates pass, `esp_apa_doa_result_t.valid == true` — read `azimuth_deg` (degrees) and `quality` (0..10). When `valid == false`, ignore azimuth fields. With external VAD, call `esp_apa_doa_reset()` at utterance boundaries; see [FAQ](#faq).

### 6. Example flow

```c
#include "esp_apa_doa.h"
#include "esp_apa_data_layout.h"  /* mixed-slot streams */

#define USE_DATA_LAYOUT  0  /* 1: extract mics from mixed-slot PCM (e.g. AFE MRMN) */

/* --- 1. Layout: 2-mic linear, 65 mm spacing --- */
esp_apa_doa_cfg_t cfg = ESP_APA_DOA_CFG_DEFAULT();
cfg.mic_num = 2;
const float mic_pos[2][2] = ESP_APA_DOA_MIC_POS_LINEAR(0.065f);
memcpy(cfg.mic_pos, mic_pos, sizeof(mic_pos));

/* --- 2. (Optional) extract two mics from AFE MRMN; mic_num must match 'M' count --- */
esp_apa_data_layout_handle_t layout = NULL;
#if USE_DATA_LAYOUT
esp_apa_data_layout_cfg_t ecfg = ESP_APA_DATA_LAYOUT_CFG_DEFAULT("MRMN");
esp_apa_data_layout_create(&ecfg, &layout);
#endif

esp_apa_doa_handle_t doa = NULL;
esp_apa_doa_create(&cfg, &doa);

uint16_t chunk = 0;
esp_apa_doa_get_frame_samples(doa, &chunk);

esp_apa_doa_result_t res = {0};
while (/* capture raw_pcm / raw_bytes */) {
    const int16_t *pcm = raw_pcm;
    size_t sample_count = raw_bytes / sizeof(int16_t);

#if USE_DATA_LAYOUT
    const int16_t *doa_pcm = NULL;
    size_t doa_bytes = 0;
    esp_apa_data_layout_process(layout, raw_pcm, raw_bytes, &doa_pcm, &doa_bytes);
    pcm = doa_pcm;
    sample_count = doa_bytes / sizeof(int16_t);
#endif

    esp_apa_doa_process(doa, pcm, sample_count, &res);
    if (res.valid) {
        /* res.azimuth_deg, res.quality */
    }
}

if (layout) {
    esp_apa_data_layout_destroy(layout);
}
esp_apa_doa_destroy(doa);
```

---

## Configuration

All runtime options are in `esp_apa_doa_cfg_t` (`include/esp_apa_doa.h`). Start from `ESP_APA_DOA_CFG_DEFAULT()` and override board-specific fields before `esp_apa_doa_create()`.

| Field | Required | Default (when 0) | Description |
| ----- | -------- | ---------------- | ----------- |
| `sample_rate_hz` | yes | — | Input PCM sample rate (Hz) |
| `mic_num` | yes | — | Microphone count (2..4) |
| `mic_pos[][2]` | yes | — | Mic (x, y) in meters; index `i` = PCM channel `i` |
| `input_layout` | yes | interleaved | `ESP_APA_DOA_INPUT_LAYOUT_INTERLEAVED` or `_PLANAR` |
| `window_samples` | no | auto (~32 ms) | Analysis window per mic; power of 2, >= 32; ~512 @ 16 kHz |
| `analysis_stride` | no | 4 | Full DOA every N internal chunks; larger → lower CPU, higher latency |
| `fmax_hz` | no | 2500 | Upper analysis band edge (Hz) |
| `level_min_db` | no | -55 (2-mic) / -42 (3+ mic) | Minimum signal level gate (dBFS) |
| `fit_residual_max` | no | 0.35 | Max geometric fit residual (3+ mic, 0..1); see [FAQ](#faq) |
| `confidence_min` | no | 1.5 | Cross-correlation peak sharpness (peak/RMS); see [FAQ](#faq) |
| `azimuth_offset_deg` | no | 0 | Calibration offset subtracted from raw azimuth (degrees) |

After `esp_apa_doa_create()`, call `esp_apa_doa_get_frame_samples()` for the recommended samples per mic per call. You do not need to match that size every time — the engine buffers input; `sample_count` only needs to be a positive multiple of `mic_num`. Feeding the recommended length each time gives the lowest latency and most even CPU load.

`analysis_stride` sets how often the full DOA analysis runs: `1` = every chunk (fastest tracking, highest CPU). Larger stride lowers CPU but slows azimuth refresh (about `stride × 16 ms` @ 16 kHz / chunk=256). Skipped chunks reuse the last result. Default is 4 (~64 ms refresh, balanced CPU vs latency).

### Processing presets

Apply a CPU / tracking trade-off before `esp_apa_doa_create()`:

| Preset macro | `analysis_stride` | `fmax_hz` | Use when |
| ------------ | ----------------- | --------- | -------- |
| `ESP_APA_DOA_PRESET_ECONOMY(cfg)` | 4 | 2000 | Lowest CPU, slower tracking |
| `ESP_APA_DOA_PRESET_BALANCED(cfg)` | 2 | 2500 | Default balance |
| `ESP_APA_DOA_PRESET_RESPONSIVE(cfg)` | 1 | 2500 | Fastest tracking, highest CPU |

```c
esp_apa_doa_cfg_t cfg = ESP_APA_DOA_CFG_DEFAULT();
ESP_APA_DOA_PRESET_RESPONSIVE(&cfg);
cfg.fmax_hz = 4000.0f;  // optional override after preset
```

### Performance and memory (ESP32-S3)

Setup: CPU **240 MHz**, Flash **QIO**; 16 kHz, window=512, chunk=256 (≈16 ms). Azimuth refresh latency depends only on `analysis_stride`: ≈ `stride × 16 ms` (2→32 ms, 4→64 ms, 10→160 ms).

| mics | create heap | CPU% stride=2 | CPU% stride=4 | CPU% stride=10 |
| ---: | ----------: | ------------: | ------------: | -------------: |
| 2 | ~23 KB | 6.4% | 3.8% | 2.3% |
| 3 | ~26 KB | 10.7% | 6.3% | 3.7% |
| 4 | ~30 KB | 18.1% | 10.3% | 5.7% |

| mics | MAE | max error |
| ---: | --: | --------: |
| 2 | ~0.3° (~0.36° at stride 10) | ~3.0–3.8° (endfire) |
| 3 | ~0.10° | ~0.21° |
| 4 | ~0.12° | ~0.25° |

Ideal plane-wave 5° sweep; stride barely changes 3/4-mic accuracy. `CFG_DEFAULT` uses stride=4.

### Window size and input length

- `window_samples = 0` auto-derives ~32 ms (512 @ 16 kHz, 256 @ 8 kHz).
- Larger windows improve stability and noise robustness at the cost of latency and CPU.
- See `esp_apa_doa_get_frame_samples()` above; partial frames are buffered until a full analysis step is ready.

---

## Microphone layout

### Coordinate system

- Origin = array center (centroid).
- **+Y = 0°** (front), azimuth increases **clockwise** (90° = right, 180° = back, 270° = left).
- Each mic is `(x, y)` in meters.

Top view (looking down at the array; azimuth increases clockwise):

```
              0° (+Y, front)
                   ↑
                   │
    270° (left) ←──·──→ 90° (right)
                   │
                   ↓
              180° (back)

         +X → right     · = origin (array center)
```

In convenience macros below, `dist_m` / `side_m` are **adjacent mic spacing** (edge length), not distance from center.

### PCM channel order must match `mic_pos[]`

PCM channel index `i` must be the microphone at `mic_pos[i]`:

| PCM channel | Config field | Must agree on |
| ----------- | ------------ | ------------- |
| 0 | `mic_pos[0]` | Same physical mic |
| 1 | `mic_pos[1]` | Same physical mic |
| … | … | … |

For **interleaved** input, one frame is `mic0, mic1, …` in that order. For **planar** input, all samples of channel 0 come first, then channel 1, … — still mapped to `mic_pos[0]`, `mic_pos[1]`, …

Built-in layout macros (`ESP_APA_DOA_MIC_POS_LINEAR/TRIANGLE/SQUARE/RECT`) define geometry **and** logical channel order together.

- **Mic order does not match `mic_pos[]`**: reorder in the capture path, then fill `mic_pos[]` in that logical order. `esp_apa_data_layout` does not reorder.
- **Non-microphone data in the stream** (e.g. ESP-SR MRMN): use [esp_apa_data_layout](#3-layout-engine-drop-non-microphone-data) to drop reference/noise and keep `M` slots only.

Do **not** only permute `mic_pos[]` to match raw wire order while leaving PCM unchanged, or azimuth will be wrong.

### Built-in layout macros

**2-mic linear** — `ESP_APA_DOA_MIC_POS_LINEAR(dist_m)`

```
mic0 ────────── mic1        (on X axis, dist_m apart)
 0°              180°       (collinear → 0..180° output only)
```

**3-mic equilateral triangle** — `ESP_APA_DOA_MIC_POS_TRIANGLE(side_m)`

```
            mic0 (+Y)
           /    \
          /  ·   \           · = origin
    mic1 ─────── mic2
```

mic0 at the front vertex; mic1/mic2 at the rear corners. All three edges = `side_m`.

**4-mic square** — `ESP_APA_DOA_MIC_POS_SQUARE(side_m)`

```
  mic0 ───── mic1
   │          │
   │    ·     │             · = origin
   │          │
  mic3 ───── mic2
```

mic0..mic3 clockwise from top-left. Adjacent spacing = `side_m`; diagonal = `side_m × √2`.

**4-mic rectangle** — `ESP_APA_DOA_MIC_POS_RECT(w_m, h_m)`

Width `w_m`, height `h_m` (meters); order top-left, top-right, bottom-right, bottom-left. For irregular boards, fill `mic_pos` in PCM channel order — do not assume the default slot order.

### Custom layouts

Fill `mic_pos` for non-regular boards. The engine builds pair geometry and detects collinearity (collinear arrays output 0..180° only).

Use measured adjacent hole spacing for macro arguments, or per-mic `(x, y)`. Larger spacing generally improves angular resolution.

---

## API reference

Headers: `include/esp_apa_doa.h`, `include/esp_apa_data_layout.h`.

### Lifecycle

| Function | Description |
| -------- | ----------- |
| `esp_apa_doa_create(cfg, &handle)` | Allocate engine, validate config, build geometry |
| `esp_apa_doa_process(h, pcm, count, &result)` | Feed PCM; update estimate when window is full |
| `esp_apa_doa_reset(h)` | Clear tracking at utterance boundary; keep history buffer |
| `esp_apa_doa_destroy(h)` | Free all resources |
| `esp_apa_doa_get_frame_samples(h, &chunk)` | Recommended samples per mic per call (see Configuration) |

All APIs are **not ISR-safe** and **not thread-safe** on the same handle.

### Input PCM layout

**Interleaved** (default) — `mic0[0], mic1[0], mic2[0], mic0[1], ...`

**Planar** — `mic0[0..N-1], mic1[0..N-1], mic2[0..N-1], ...`

Set `cfg.input_layout` accordingly. Only 16-bit signed PCM is supported.

`mic0`, `mic1`, … are **logical** channels: `mic_pos[i]` describes the mic that supplies channel `i`. See [PCM channel order must match `mic_pos[]`](#pcm-channel-order-must-match-mic_pos).

### Result: `esp_apa_doa_result_t`

| Field | Type | Meaning |
| ----- | ---- | ------- |
| `valid` | `bool` | `true` when this frame produced an accepted estimate |
| `azimuth_deg` | `float` | Smoothed azimuth in degrees (**valid only**) |
| `quality` | `float` | Reliability 0..10 (**valid only**) |

**Always check `valid` first.** When `valid == false`, ignore `azimuth_deg` and `quality` (they retain previous values).

`quality` grades reliability of an accepted estimate; it does not decide validity:

| quality | Meaning |
| ------- | ------- |
| 0 | Just met internal accept threshold — use with caution |
| 0 – 3 | Low margin; noisy or reverberant |
| 3 – 6 | Moderate; normal usable |
| 6 – 10 | Strong; sharp correlation peak |

Apply a stricter cutoff on top of `valid` when needed (e.g. `quality >= 3`).

See [FAQ](#faq) for when `valid` is false and when to call `esp_apa_doa_reset()`.

Channel extract API: `include/esp_apa_data_layout.h`; dropping ESP-SR non-microphone data: [How it works · Layout engine](#3-layout-engine-drop-non-microphone-data).

---

## Examples

Start with [`examples/live_example`](examples/live_example/): minimal live capture integration on ESP32-S31-Korvo-1 using `esp_codec_dev_read()` and `esp_apa_doa_process()`.

---

## FAQ

### When is `valid` false?

`esp_apa_doa_process()` returns `ESP_OK` even when `valid == false`. `false` means the **last completed internal chunk** in that call did not produce a new accepted estimate. Common cases:

| Situation | `valid` | Notes |
| --------- | ------- | ----- |
| Input still buffering (less than one internal chunk) | `false` | `sample_count` must be a multiple of `mic_num`; can be smaller than one chunk |
| Analysis window warming up (`hist_fill < window_samples`) | `false` | First ~`window_samples` samples per mic after create/reset |
| Signal too weak or unreliable (gates not met) | `false` | Level, confidence, or (2-mic) balance / (3+ mic) residual check failed |
| `out == NULL` | unchanged | History only; no result written (typical during silence) |

**Partial input is fine** — the engine buffers until enough samples are ready; until the window is full and gates pass, `valid` stays `false`.

If one `process()` call completes multiple internal chunks, `valid` reflects only the **last** chunk.

### When to call `esp_apa_doa_reset`?

`esp_apa_doa_reset()` clears transient tracking (cached delays, TDOA filters, pending input) but **keeps the PCM history buffer**, so you can keep feeding silence/pre-roll and get a low-latency estimate when speech starts.

**Call reset when:**

- External VAD detects **speech start** — fresh estimate for the new utterance.
- External VAD detects **speech end** — discard tracking from the finished utterance.
- Session changes (new speaker, long idle, user-triggered re-localization).

**Do not call reset when:**

- Speech is **still ongoing** — causes jitter and slower convergence.
- On **every** `esp_apa_doa_process()` call.
- **Continuous tracking** without utterance segmentation.
- Brief VAD dropouts within one utterance — use hangover; reset only on sustained silence.

Call `esp_apa_doa_destroy()` directly to tear down; reset is not required first.

### How do I tune parameters for my scenario?

Field value `0` keeps the library default. Override in your application before `esp_apa_doa_create()` when hardware or environment differs — do not change library defaults.

#### 1. Enable debug logs

menuconfig → **Maximum log verbosity** = **Debug**, then at runtime:

```c
esp_log_level_set("ESP_APA_DOA", ESP_LOG_DEBUG);
```

To calibrate `fit_residual_max` / `confidence_min`, set both to `0` first, speak from a fixed angle 10–20 times, and collect **`gate` lines** (**3+ mic only**; 2-mic has no such line).

All gates pass (frame can become `valid`):

```
gate az=127.8 res=0.037/0.35 conf=1.65/1.50 lvl=-33/-42
```

A gate fails — the library appends ` REJ` **right after that field** (note the leading space):

```
gate az=119.9 res=0.080/0.35 conf=1.50/1.50 REJ lvl=-30/-42
gate az=20.9 res=0.974/0.35 REJ conf=1.44/1.50 REJ lvl=-50/-42
```

| Log fragment | How to use when tuning |
| ------------ | ---------------------- |
| `res=0.037/0.35` | Value before `/`, gate after; good frames often `res` < 0.1, bad/jumping often > 0.2 |
| `res=... REJ` | Tighten `fit_residual_max`, or discard frame (check if `az` jumps) |
| `conf=1.50/1.50 REJ` | Measured conf below gate → lower `confidence_min` |
| `lvl=-55/-42 REJ` | Level too low → lower `level_min_db` (e.g. -42 → -58) |
| `az=127.8` | Same-angle good frames cluster; wild `az` + high `res` → tighten `fit_residual_max` |

If you **never see `REJ`** (all gates pass but azimuth still jumps), set `fit_residual_max` from the `res` gap between stable and unstable frames — you do not need `REJ` for that.

#### 2. Tune `fit_residual_max` (3+ mic, first)

1. From logs, pick frames with **correct** azimuth; note their `res` (often 0.02–0.10).
2. Pick frames with **wild** azimuth; note their `res` (often 0.20+).
3. Set `fit_residual_max` between the two clusters (e.g. good 0.04, bad 0.21 → use **0.15**).
4. Re-test: still jumping → tighten; utterances often invalid → relax slightly.

#### 3. Tune `confidence_min` (3+ mic, second)

1. Log shows `conf=1.50/1.50 REJ` on good frames → set `confidence_min` **0.2–0.3 below the lowest good-frame `conf`** (e.g. min 1.50 → **1.2**).
2. If `conf ... REJ` never appears, library default 1.5 is fine.

#### 4. Other parameters (symptom → change)

| Symptom | Change | Suggestion |
| ------- | ------ | ---------- |
| Quiet/short speech, log shows `lvl=... REJ` | `level_min_db` | Relax (e.g. -42 → -58) |
| Fixed azimuth bias | `azimuth_offset_deg` | Measure and set offset |
| CPU too high | `analysis_stride`, `fmax_hz` | `ESP_APA_DOA_PRESET_ECONOMY` or larger stride |
| Tracking too slow | `analysis_stride` | Smaller stride or `ESP_APA_DOA_PRESET_RESPONSIVE` |
| 2-mic array | — | Skip `fit_residual_max`; tune `level_min_db` |

#### 5. Apply in config

```c
esp_apa_doa_cfg_t cfg = ESP_APA_DOA_CFG_DEFAULT();
cfg.level_min_db     = -58.0f;   /* from lvl REJ; 0 = auto */
cfg.fit_residual_max = 0.15f;    /* 3+ mic, from res gap; 0 = auto 0.35 */
cfg.confidence_min   = 1.2f;     /* from conf REJ; 0 = auto 1.5 */
```
