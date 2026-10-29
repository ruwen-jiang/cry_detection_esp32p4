# Infant Cry Detection

- [English Version](./README.md)
- Regular Example: ⭐

## 例程简介

本例程在带 `audio_adc` 的开发板上演示 `esp_apa_cry_detection` 的最小实时采集集成。推荐使用 [**ESP32-S3-Korvo-2**](https://docs.espressif.com/projects/esp-dev-kits/zh_CN/latest/esp32s3/esp32-s3-korvo-2/user_guide.html) 或 [**ESP32-P4 Function EV**](https://docs.espressif.com/projects/esp-dev-kits/zh_CN/latest/esp32p4/esp32-p4-function-ev-board/user_guide.html)。

- **功能**：上电后持续采集麦克风 PCM，检测婴儿哭声起止，并在串口打印 START / END。
- **亮点**：确认后的 START / END 事件（不是原始概率）；能量门控跳过静音；算法不创建内部任务，在采集路径里直接 `process()`。
- **技术**：`esp_board_manager` 初始化板级与 `audio_adc`。例程先下混为单声道，再用独立 `esp_ns.h`（WebRTC，10 ms）降噪，按 50 ms 组包后调用 `esp_apa_cry_detection_process()`。

`esp_apa_cry_detection` **算法本身不含**重采样、下混或降噪。NS、下混与组包只存在于本例程。检测器输入为 16 kHz 单声道 int16。`esp_apa_cry_detection_open()` 不创建内部任务。

### 典型场景

- 婴儿监护仪 / 智能婴儿床：哭声 START 时告警，END 时解除
- 摄像头、门铃或音箱：检测到哭声开始后触发录像或通知

### 运行机制

```
Boot → esp_board_manager_init()
     → init audio_adc → esp_codec_dev_read()
     → downmix to mono → ns_process() (10 ms)
     → pack 50 ms → esp_apa_cry_detection_process()
     → print when event is START / END
```

`capture_task` 负责采集、降噪并调用 `esp_apa_cry_detection_process()`。多麦板可定义 `MIC_CHANNELS`，例程会先混成单声道再做 NS。

## 环境配置

### 硬件要求

需要 `esp_board_manager` 中带 `audio_adc` 的开发板，以及 USB 数据线（供电与串口烧录）。已验证板型：

| Board | Target | `idf.py bmgr` |
|-------|--------|----------------|
| [ESP32-S3-Korvo-2](https://docs.espressif.com/projects/esp-dev-kits/zh_CN/latest/esp32s3/esp32-s3-korvo-2/user_guide.html) | esp32s3 | `-b esp32_s3_korvo_2_3` |
| [ESP32-P4 Function EV](https://docs.espressif.com/projects/esp-dev-kits/zh_CN/latest/esp32p4/esp32-p4-function-ev-board/user_guide.html) | esp32p4 | `-b esp32_p4_function_ev_board` |

### 默认 IDF 分支

本例程基于 IDF **master** 分支开发与验证。

### 软件要求

- 已配置 ESP-IDF 环境
- `main/idf_component.yml` 声明 `esp_apa_cry_detection`、`espressif/esp_board_manager` 与 `espressif/esp-sr` 依赖

## 编译和下载

### 编译准备

编译本例程前需先配置 ESP-IDF（已配置可跳过）：

```
./install.sh
. ./export.sh
```

进入本例程工程目录：

```
cd components/esp_apa_cry_detection/examples/infant_cry_detection
```

本例程使用 [ESP Board Manager](https://github.com/espressif/esp-board-manager)。推荐安装 [`esp-bmgr-assist`](https://pypi.org/project/esp-bmgr-assist/)。

在已激活的 ESP-IDF Python 环境下安装（同一环境只需一次）：

```bash
pip install esp-bmgr-assist
pip install --upgrade esp-bmgr-assist  # when an update is prompted
```

列出当前可见的开发板：

```bash
idf.py bmgr -l
```

选择开发板（推荐 `esp32_s3_korvo_2_3`）：

```bash
idf.py set-target esp32s3
idf.py bmgr -b esp32_s3_korvo_2_3
```

> 换板后必须先执行一次 `idf.py bmgr`，以生成 `esp_board_manager_includes.h`。
> 切换其他 `esp_board_manager` 支持的开发板时，请替换板型名称并重新执行 `idf.py set-target` 与 `idf.py bmgr -b <board_name>`。
> 更多信息见 [ESP Board Manager 入门指南](https://github.com/espressif/esp-board-manager/blob/main/esp_board_manager/README_CN.md)。

### 项目配置

本例程除 board manager 选板外无额外 menuconfig 项。默认板型见 `board_manager.defaults`。

### 编译与烧录

ESP32-S3-Korvo-2：

```
idf.py set-target esp32s3
idf.py bmgr -b esp32_s3_korvo_2_3
idf.py build
idf.py -p PORT flash monitor
```

ESP32-P4 Function EV：

```
idf.py set-target esp32p4
idf.py bmgr -b esp32_p4_function_ev_board
idf.py build
idf.py -p PORT flash monitor
```

退出 monitor：`Ctrl-]`

## 如何使用例程

### 功能和用法

1. 烧录上电后自动开始采集与哭声检测
2. 对着板载麦克风播放或模拟婴儿哭声
3. 检测器确认进入 / 退出哭声时，串口分别打印 START / END

产品逻辑应订阅 **event**（`ESP_APA_CRY_DETECTION_EVENT_START` / `END`），而不是每 hop 轮询 `cry_prob`。检测器需先积累约 1000 ms 窗口，之后每 150 ms 推理一次。短促声音可能不会发出 START。

## 采集前处理

例程在**采集路径里做前处理**，不在 `esp_apa_cry_detection` 内部。检测器本身不做重采样、下混或降噪。

其中 10 ms WebRTC NS（`esp_ns.h`）用来**减弱平稳环境噪声**（风扇、空调、底噪嘶声、停播后的喇叭残响），让静音时能量门能关上，并减少 CNN 把房间噪声判成哭声。婴儿哭声是非平稳的，不是这段 NS 要抑制的对象。

多麦 PCM 在 `pcm_to_mono()` 中先下混为 **16 kHz 单声道 int16**，再进 NS。若产品已有 AFE 或其他降噪，可去掉本例的 `ns_create()` / `ns_process()`，直接把 16 kHz 单声道 PCM 喂给 `esp_apa_cry_detection_process()`。

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

调参见 [组件 README · 配置](../../README_CN.md#配置)。

## 适配自己的板子

换板或改麦数时，主要修改 `app_main.c` 与 board manager 选板：

### 1. 麦克风通道数

默认 `MIC_CHANNELS` 为 1。多麦板将其设为实际采集通道数，例程会先混成单声道再做 NS：

```c
#ifndef MIC_CHANNELS
#define MIC_CHANNELS  1
#endif
```

同时确认 `esp_codec_dev_open()` 的 `channel` 与板级 `audio_adc` 一致。

### 2. 采样率、增益与组包

- 工作采样率：**仅 16 kHz**（固定）。检测器按 16 kHz 训练与分窗（`app_main.c` 中 `SAMPLE_RATE`），组件不做重采样。`esp_codec_dev_open()` 须开成 16 kHz。
- 输入增益：`MIC_IN_GAIN_DB`（默认 36 dB）
- NS 帧长：10 ms（`esp_ns` 要求）
- 送给检测器的块长：`PROCESS_MS`（默认 50 ms / 800 点）。不必等于 150 ms hop；内部环形缓冲会自行累积。

### 3. 板级与检测参数

- 换开发板：更新 `board_manager.defaults`，执行 `idf.py set-target <target>` 与 `idf.py bmgr -b <board_name>`
- 灵敏度：在 `open()` 前覆盖 `ESP_APA_CRY_DETECTION_DEFAULT_CONFIG()` 的 `gate` / `fsm`（见组件 README）

## 故障排除

- **`idf.py bmgr` 失败或找不到 `gen_bmgr_codes`**：确认 `main/idf_component.yml` 已声明 `espressif/esp_board_manager`，执行 `idf.py bmgr -l`。仍失败可 `idf.py bmgr -x` 后重新 `idf.py bmgr -b <board_name>`。
- **`codec read failed` 或采集任务反复 delay**：检查 USB 供电、`audio_adc` 初始化，以及是否已执行 `bmgr` 选板。
- **长时间没有 START**：先等约 1 s 填满窗口；对着麦克风；确认 `capture_task` 在跑。能量门过严或确认 hop 不够时，见 [组件 README · FAQ](../../README_CN.md#faq)。
- **误触发太多**：提高 `fsm.start_threshold` / `start_confirm_count`，或略提高 `gate.energy_threshold`。
- **几乎检不到**：输入必须是 16 kHz 单声道 int16。多麦须先下混。

## 技术支持

请通过以下方式获取技术支持：

- 技术支持参见 [esp32.com](https://esp32.com/viewforum.php?f=20) 论坛
- 问题反馈请创建 [ESP-APA issues](https://github.com/espressif/esp-apa/issues)

我们会尽快回复。
