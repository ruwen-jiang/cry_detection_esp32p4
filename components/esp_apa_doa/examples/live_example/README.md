# Live DOA

- [中文版本](./README_CN.md)
- Regular Example: ⭐⭐

## Example Brief

This example demonstrates minimal live-capture integration of `esp_apa_doa` on the [**ESP32-S31-Korvo-1**](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s31/esp32-s31-korvo-1/user_guide.html#hardware-reference) board with an on-board **2-microphone** linear array. See the [User Guide · Hardware Reference](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s31/esp32-s31-korvo-1/user_guide.html#hardware-reference) for board details.

- **Function**: Continuously capture mic PCM after boot, estimate source direction, and print results on UART.
- **Technical**: `esp_board_manager` brings up board and `audio_adc`; `esp_codec_dev_read()` feeds interleaved 2-mic PCM into `esp_apa_doa_process()`. This is a **pure ADC path** with no VAD.

The `esp_apa_doa` component supports 2/3/4-mic layouts; this example only shows 2-mic wiring on S31-Korvo-1.

### Typical Scenarios

- Verify the DOA capture pipeline on S31-Korvo-1
- Reference template for wiring `esp_codec_dev` to DOA in a product

### Runtime Flow

```
Boot → esp_board_manager_init()
     → init audio_adc → esp_codec_dev_read()
     → pack 2-mic PCM → esp_apa_doa_process()
     → log azimuth / quality when valid (loop, no VAD gating)
```

For voice-triggered products, add **VAD** or AFE voice-activity detection before DOA. See `4_mic_example`.

## Environment Setup

### Hardware Requirements

- [**ESP32-S31-Korvo-1**](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s31/esp32-s31-korvo-1/user_guide.html#hardware-reference) board (on-board 2-mic array)
- USB cable (power and serial flash)

### Supported IDF Branch

This example is developed and verified on IDF **master**.

### Software Requirements

- ESP-IDF environment configured
- `esp_apa_doa` and `espressif/esp_board_manager` declared in `main/idf_component.yml`

## Build and Flash

### Build Preparation

Configure ESP-IDF first if needed:

```
./install.sh
. ./export.sh
```

Enter the example directory:

```
cd components/esp_apa_doa/examples/live_example
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

Select the board (default: `esp32_s31_korvo_1`):

```bash
idf.py --preview bmgr -b esp32_s31_korvo_1
```

> ESP32-S31 is a preview target; add `--preview` to `build`, `flash`, and `monitor`.
> To use another board, replace the board name and run `idf.py bmgr -b <board_name>` again.
> See the [ESP Board Manager guide](https://github.com/espressif/esp-board-manager/blob/main/esp_board_manager/README.md).

### Project Configuration

No extra menuconfig items beyond board selection. Default board is in `board_manager.defaults` (`esp32_s31_korvo_1`).

### Build and Flash

```
idf.py --preview build
idf.py --preview -p PORT flash monitor
```

Exit the monitor with `Ctrl-]`

## How to Use the Example

### Features and Usage

1. After flash, capture and DOA start automatically (continuous, no VAD gating)
2. Speak toward the on-board mics; UART prints azimuth when `valid == true`
3. The 2-mic linear array outputs **0..180°** (0 = left mic, 90 = front, 180 = right mic)

`azimuth_deg` (degrees) and `quality` (0..10) are meaningful only when `valid == true`.

### Log Output

Representative continuous log (not stitched from different runs):

```text
I (xxx) LIVE_DOA: Live DOA example start
I (xxx) LIVE_DOA: ADC ready: 16000 Hz, 2 stream ch
I (xxx) LIVE_DOA: 2-mic DOA running: mics=2 chunk=256
I (xxx) LIVE_DOA: az=  92.3 bin= 6 front   q=5.2
```

## Using with ESP-SR (data layout)

This example uses a **pure ADC 2-mic path**; `esp_codec_dev_read()` output goes directly to `esp_apa_doa_process()`.

With **ESP-SR / AFE**, PCM is often a multi-slot interleaved stream (e.g. `MRMN`). DOA needs only `M` slots — use `esp_apa_data_layout` to drop reference/noise slots first:

```c
#include "esp_apa_data_layout.h"

esp_apa_data_layout_handle_t layout = NULL;
esp_apa_data_layout_cfg_t ecfg = ESP_APA_DATA_LAYOUT_CFG_DEFAULT("MRMN");
esp_apa_data_layout_create(&ecfg, &layout);

const int16_t *doa_pcm = NULL;
size_t doa_bytes = 0;
esp_apa_data_layout_process(layout, raw_pcm, raw_bytes, &doa_pcm, &doa_bytes);
esp_apa_doa_process(doa, doa_pcm, doa_bytes / sizeof(int16_t), &res);

esp_apa_data_layout_destroy(layout);
```

- Layout string must match the AFE output; `'M'` keeps a mic slot
- `esp_apa_data_layout` **extracts only — does not reorder**; output order must match `mic_pos[]`
- Do not `free` `doa_pcm` (internal buffer)

See `include/esp_apa_data_layout.h` and the component README.

## Adapt to Your Own Board

When changing the board or array (3-mic, 4-mic, 2-mic with different spacing, etc.), update `app_main.c`:

### 1. Mic count and geometry

Set `cfg.mic_num` and `cfg.mic_pos` (see [component README · Built-in layout macros](../../README.md#built-in-layout-macros)):

| Array | Macro / usage |
|-------|----------------|
| 2-mic linear (spacing `d` meters) | `ESP_APA_DOA_MIC_POS_LINEAR(d)` |
| 3-mic equilateral triangle (side `d`) | `ESP_APA_DOA_MIC_POS_TRIANGLE(d)` |
| 4-mic square (side `d`) | `ESP_APA_DOA_MIC_POS_SQUARE(d)` |
| 4-mic rectangle (width `w`, height `h`) | `ESP_APA_DOA_MIC_POS_RECT(w, h)` |
| Custom layout | Fill `mic_pos[][]` in PCM logical channel order |

**PCM mic channel order must match `mic_pos[]`.** Reorder channels in the capture path if wiring differs — do not only permute `mic_pos`.

### 2. Capture channels and slot mapping

Update `adc_read_packed()` settings: `channel` / `channel_mask` in `esp_codec_dev_open()`, `MIC_SLOTS[]`, and `STREAM_CHANNELS`.

### 3. Board and DOA tuning

- New board: update `board_manager.defaults`, run `idf.py bmgr -b <board_name>`
- Adjust sample rate, gain, and DOA preset (e.g. `ESP_APA_DOA_PRESET_RESPONSIVE`, `fmax_hz`)

See also `4_mic_example` and the component README.

## Troubleshooting

- **`idf.py bmgr` fails or `gen_bmgr_codes` missing**: Ensure `espressif/esp_board_manager` is in `main/idf_component.yml`; run `idf.py bmgr -l`. If needed, `idf.py bmgr -x` then `idf.py --preview bmgr -b esp32_s31_korvo_1`.
- **esp32s31 build errors**: Add `--preview` (S31 is a preview target).
- **`codec read failed` in log**: Check USB power, `audio_adc` init, and that `bmgr` board selection completed.
- **No `az=` lines for a long time**: Speak toward the mics; weak or silent input keeps `valid` false — adjust gain or DOA gates (component README FAQ).

## Technical Support

Get technical support through the following channels:

- Technical support: [esp32.com](https://esp32.com/viewforum.php?f=20) forum
- Feedback: create an [ESP-APA issue](https://github.com/espressif/esp-apa/issues)

We will reply as soon as possible.
