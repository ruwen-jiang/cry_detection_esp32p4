# Infant Cry Detection

- [中文版本](./README_CN.md)
- Regular Example: ⭐

## Example Brief

This example shows the smallest live-capture integration of `esp_apa_cry_detection` on a board with `audio_adc`. Recommended hardware: [**ESP32-S3-Korvo-2**](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-korvo-2/user_guide.html) or [**ESP32-P4 Function EV**](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32p4/esp32-p4-function-ev-board/user_guide.html).

- **Function**: After boot, capture microphone PCM continuously, detect infant-cry start and end, and print START / END on UART.
- **Highlights**: Confirmed START / END events (not raw probability); energy gate skips silence; no internal FreeRTOS task — call `process()` from your capture path.
- **Technical**: `esp_board_manager` brings up the board and `audio_adc`. The example downmixes to mono, runs standalone `esp_ns.h` (WebRTC, 10 ms), packs 50 ms chunks, then calls `esp_apa_cry_detection_process()`.

The **algorithm itself does not** resample, downmix, or denoise. NS, downmix, and packing live only in this example. The detector takes 16 kHz mono int16. `esp_apa_cry_detection_open()` does not create a task.

### Typical Scenarios

- Baby monitor or smart crib: alert on cry START and clear the alert on END
- Camera, doorbell, or speaker dock: start recording or a notification when a baby starts crying

### Runtime Flow

```
Boot → esp_board_manager_init()
     → init audio_adc → esp_codec_dev_read()
     → downmix to mono → ns_process() (10 ms)
     → pack 50 ms → esp_apa_cry_detection_process()
     → print when event is START / END
```

`capture_task` captures, denoises, and calls `esp_apa_cry_detection_process()`. Multi-mic boards can define `MIC_CHANNELS`; the example downmixes to mono before NS.

## Environment Setup

### Hardware Requirements

A board with `audio_adc` in `esp_board_manager`, plus a USB cable (power and serial flash). Verified boards:

| Board | Target | `idf.py bmgr` |
|-------|--------|----------------|
| [ESP32-S3-Korvo-2](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-korvo-2/user_guide.html) | esp32s3 | `-b esp32_s3_korvo_2_3` |
| [ESP32-P4 Function EV](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32p4/esp32-p4-function-ev-board/user_guide.html) | esp32p4 | `-b esp32_p4_function_ev_board` |

### Supported IDF Branch

This example is developed and verified on IDF **master**.

### Software Requirements

- ESP-IDF environment configured
- `esp_apa_cry_detection`, `espressif/esp_board_manager`, and `espressif/esp-sr` in `main/idf_component.yml`

## Build and Flash

### Build Preparation

Configure ESP-IDF first if needed:

```
./install.sh
. ./export.sh
```

Enter the example directory:

```
cd components/esp_apa_cry_detection/examples/infant_cry_detection
```

This example uses [ESP Board Manager](https://github.com/espressif/esp-board-manager). Installing [`esp-bmgr-assist`](https://pypi.org/project/esp-bmgr-assist/) is recommended.

In the active ESP-IDF Python environment (once per environment):

```bash
pip install esp-bmgr-assist
pip install --upgrade esp-bmgr-assist  # when an update is prompted
```

List available boards:

```bash
idf.py bmgr -l
```

Select the board (recommended: `esp32_s3_korvo_2_3`):

```bash
idf.py set-target esp32s3
idf.py bmgr -b esp32_s3_korvo_2_3
```

> After changing boards, run `idf.py bmgr` once so `esp_board_manager_includes.h` is generated.
> For another `esp_board_manager` board, replace the name and re-run `idf.py set-target` and `idf.py bmgr -b <board_name>`.
> See the [ESP Board Manager guide](https://github.com/espressif/esp-board-manager/blob/main/esp_board_manager/README.md).

### Project Configuration

No extra menuconfig items beyond board selection. Default board is in `board_manager.defaults`.

### Build and Flash

ESP32-S3-Korvo-2:

```
idf.py set-target esp32s3
idf.py bmgr -b esp32_s3_korvo_2_3
idf.py build
idf.py -p PORT flash monitor
```

ESP32-P4 Function EV:

```
idf.py set-target esp32p4
idf.py bmgr -b esp32_p4_function_ev_board
idf.py build
idf.py -p PORT flash monitor
```

Exit the monitor with `Ctrl-]`

## How to Use the Example

### Features and Usage

1. After flash, capture and cry detection start automatically
2. Play or simulate infant cry toward the on-board mic
3. UART prints START / END when the detector confirms enter / leave crying

Subscribe to **event** (`ESP_APA_CRY_DETECTION_EVENT_START` / `END`), not per-hop `cry_prob`. The detector needs about 1000 ms to fill the window, then infers every 150 ms. Short bursts may not emit START.

## Capture preprocessing

The example runs **preprocessing in the capture path**, not inside `esp_apa_cry_detection`. The detector does not resample, downmix, or denoise.

The 10 ms WebRTC NS (`esp_ns.h`) is there to **reduce stationary environmental noise** (fan, AC, hiss, speaker leftover after playback). That keeps the energy gate able to close on quiet periods and avoids the CNN treating room noise as cry. Infant cry is non-stationary and is not the target of this NS.

Multi-mic PCM is downmixed to **16 kHz mono int16** in `pcm_to_mono()` before NS. If the product already has AFE or another denoiser, drop `ns_create()` / `ns_process()` and feed 16 kHz mono PCM to `esp_apa_cry_detection_process()`.

```c
esp_apa_cry_detection_config_t cfg = ESP_APA_CRY_DETECTION_DEFAULT_CONFIG();
esp_apa_cry_detection_handle_t cry = NULL;
ESP_ERROR_CHECK(esp_apa_cry_detection_open(&cfg, &cry));

esp_apa_cry_detection_status_t st;
ESP_ERROR_CHECK(esp_apa_cry_detection_process(cry, pcm, samples, &st));
if (st.event == ESP_APA_CRY_DETECTION_EVENT_START) {
    /* cry started */
} else if (st.event == ESP_APA_CRY_DETECTION_EVENT_END) {
    /* cry ended */
}
```

Tuning: [component README · Configuration](../../README.md#configuration).

## Adapt to Your Own Board

When changing the board or mic count, update `app_main.c` and board-manager selection:

### 1. Mic channel count

Default `MIC_CHANNELS` is 1. On a multi-mic board, set it to the capture channel count; the example downmixes to mono before NS:

```c
#ifndef MIC_CHANNELS
#define MIC_CHANNELS  1
#endif
```

Confirm `esp_codec_dev_open()` `channel` matches board `audio_adc`.

### 2. Sample rate, gain and packing

- Working sample rate: **16 kHz only** (fixed). The detector is trained and windowed at 16 kHz (`SAMPLE_RATE` in `app_main.c`); it does not resample. Open `esp_codec_dev` at 16 kHz.
- Input gain: `MIC_IN_GAIN_DB` (default 36 dB)
- NS frame: 10 ms (`esp_ns` requirement)
- Chunk to the detector: `PROCESS_MS` (default 50 ms / 800 samples). Need not equal the 150 ms hop; the ring buffer accumulates.

### 3. Board and detector tuning

- New board: update `board_manager.defaults`, run `idf.py set-target <target>` and `idf.py bmgr -b <board_name>`
- Sensitivity: override `gate` / `fsm` in `ESP_APA_CRY_DETECTION_DEFAULT_CONFIG()` before `open()` (see component README)

## Troubleshooting

- **`idf.py bmgr` fails or `gen_bmgr_codes` missing**: Ensure `espressif/esp_board_manager` is in `main/idf_component.yml`; run `idf.py bmgr -l`. If needed, `idf.py bmgr -x` then `idf.py bmgr -b <board_name>`.
- **`codec read failed` or capture task delays**: Check USB power, `audio_adc` init, and that `bmgr` board selection completed.
- **No START for a long time**: Wait ~1 s for the window; speak toward the mic; check `capture_task` is running. See [component README · FAQ](../../README.md#faq) if the energy gate or confirm hops are too strict.
- **Too many false START events**: Raise `fsm.start_threshold` / `start_confirm_count`, or slightly raise `gate.energy_threshold`.
- **Almost no detections**: Input must be 16 kHz mono int16. Multi-mic must be downmixed first.

## Technical Support

Get technical support through the following channels:

- Technical support: [esp32.com](https://esp32.com/viewforum.php?f=20) forum
- Feedback: create an [ESP-APA issue](https://github.com/espressif/esp-apa/issues)

We will reply as soon as possible.
