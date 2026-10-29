# 实时 DOA（Live DOA）

- [English Version](./README.md)
- 例程难度：⭐⭐

## 例程简介

本例程在 [**ESP32-S31-Korvo-1**](https://docs.espressif.com/projects/esp-dev-kits/zh_CN/latest/esp32s31/esp32-s31-korvo-1/user_guide.html#hardware-reference) 上演示 `esp_apa_doa` 的最小实时采集集成。该开发板板载**双麦克风**线性阵列；硬件说明见 [用户指南 · 硬件参考](https://docs.espressif.com/projects/esp-dev-kits/zh_CN/latest/esp32s31/esp32-s31-korvo-1/user_guide.html#hardware-reference)。

- **功能**：上电后持续采集麦克风 PCM，估计声源方位角，并在串口打印结果。
- **技术**：`esp_board_manager` 初始化板级与 `audio_adc`，经 `esp_codec_dev_read()` 读取双麦交织 PCM，再调用 `esp_apa_doa_process()` 完成 DOA；本例为**纯 ADC 路径**，无 VAD。

`esp_apa_doa` 组件本身支持 2/3/4 麦等多种阵列；本例仅演示 S31-Korvo-1 上的双麦接入方式。

### 典型场景

- 验证 S31-Korvo-1 上 DOA 采集链路是否打通
- 作为产品侧将 `esp_codec_dev` 与 DOA 对接的参考模板

### 运行机制

```
上电 → esp_board_manager_init()
     → 初始化 audio_adc → esp_codec_dev_read()
     → 按 slot 打包双麦 PCM → esp_apa_doa_process()
     → valid 时串口输出 azimuth / quality（循环，无 VAD 门控）
```

人声触发类产品（唤醒后定位、仅在有说话时更新方向）建议在 DOA 前增加 **VAD** 或 AFE 语音活动检测，可参考 `4_mic_example`。

## 环境配置

### 硬件要求

- [**ESP32-S31-Korvo-1**](https://docs.espressif.com/projects/esp-dev-kits/zh_CN/latest/esp32s31/esp32-s31-korvo-1/user_guide.html#hardware-reference) 开发板（板载双麦）
- USB 数据线（供电与串口烧录）

### 默认 IDF 分支

本例程基于 IDF **master** 分支开发与验证。

### 软件要求

- 已配置 ESP-IDF 环境
- `main/idf_component.yml` 声明 `esp_apa_doa` 与 `espressif/esp_board_manager` 依赖

## 编译和下载

### 编译准备

编译本例程前需先确保已配置 ESP-IDF 环境；若已配置可跳过本段。若未配置，请在 ESP-IDF 根目录执行：

```
./install.sh
. ./export.sh
```

进入本例程工程目录：

```
cd components/esp_apa_doa/examples/live_example
```

本例程使用 [ESP Board Manager](https://github.com/espressif/esp-board-manager) 管理板级外设。推荐安装辅助工具 [`esp-bmgr-assist`](https://pypi.org/project/esp-bmgr-assist/) 作为默认入口。

在已激活的 ESP-IDF Python 环境下安装（同一环境只需安装一次）：

```bash
pip install esp-bmgr-assist
pip install --upgrade esp-bmgr-assist  # 当提示需要更新时执行此命令
```

列出当前可见的开发板：

```bash
idf.py bmgr -l
```

选择开发板（本例默认 `esp32_s31_korvo_1`）：

```bash
idf.py --preview bmgr -b esp32_s31_korvo_1
```

> ESP32-S31 为 preview target，后续 `build` / `flash` / `monitor` 均需加 `--preview`。
> 切换其他 `esp_board_manager` 支持的开发板时，请替换板型名称并重新执行 `idf.py bmgr -b <board_name>`。
> 更多信息见 [ESP Board Manager 入门指南](https://github.com/espressif/esp-board-manager/blob/main/esp_board_manager/README_CN.md)。

### 项目配置

本例程除 board manager 选板外无额外 menuconfig 项。默认板型见 `board_manager.defaults`（`esp32_s31_korvo_1`）。

### 编译与烧录

```
idf.py --preview build
idf.py --preview -p PORT flash monitor
```

退出 monitor：`Ctrl-]`

## 如何使用例程

### 功能和用法

1. 烧录上电后自动开始采集与 DOA 估计（持续运行，无 VAD 门控）
2. 对着板载麦克风说话；`valid == true` 时串口打印方位角
3. 双麦线性阵列输出 **0..180°**（0 = 左麦，90 = 正前，180 = 右麦）

仅当 `valid == true` 时，`azimuth_deg`（方位角，度）与 `quality`（可靠度 0..10）有效。

### 日志输出

关键日志如下（连续片段，未拼接）：

```text
I (xxx) LIVE_DOA: Live DOA example start
I (xxx) LIVE_DOA: ADC ready: 16000 Hz, 2 stream ch
I (xxx) LIVE_DOA: 2-mic DOA running: mics=2 chunk=256
I (xxx) LIVE_DOA: az=  92.3 bin= 6 front   q=5.2
```

## 与 ESP-SR 配合（data layout）

本例走**纯 ADC 双麦路径**，`esp_codec_dev_read()` 输出可直接送入 `esp_apa_doa_process()`。

与 **ESP-SR / AFE** 配合时，PCM 常为多 slot 交错流（如 `MRMN`）。DOA 只需 `M` 槽，先用 `esp_apa_data_layout` 去掉参考/噪声等非麦 slot：

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

- 布局字符串须与 AFE 实际输出一致；`'M'` 保留麦 slot，其他字符丢弃
- `esp_apa_data_layout` **只抽取、不重排**；输出麦顺序须与 `mic_pos[]` 对应
- `doa_pcm` 为内部缓冲区，调用方不要 `free`

详见 `include/esp_apa_data_layout.h` 与组件 README。

## 适配自己的板子

换板或改阵列（三麦、四麦、双麦但间距不同等）时，主要修改 `app_main.c`：

### 1. 麦克风数量与几何

设置 `cfg.mic_num` 与 `cfg.mic_pos`（详见 [组件 README · 内置布局宏](../../README_CN.md#内置布局宏)）：

| 阵列 | 宏 / 写法 |
|------|-----------|
| 2 麦线性（间距 `d` 米） | `ESP_APA_DOA_MIC_POS_LINEAR(d)` |
| 3 麦等边三角形（边长 `d`） | `ESP_APA_DOA_MIC_POS_TRIANGLE(d)` |
| 4 麦正方形（边长 `d`） | `ESP_APA_DOA_MIC_POS_SQUARE(d)` |
| 4 麦长方形（宽 `w`、高 `h`） | `ESP_APA_DOA_MIC_POS_RECT(w, h)` |
| 异形布局 | 按 PCM 逻辑通道顺序填写 `mic_pos[][]` |

**PCM 麦通道顺序必须与 `mic_pos[]` 一一对应**；接线顺序与几何不一致时应在采集路径重排通道，而不是只改 `mic_pos`。

### 2. 采集通道与 slot 映射

修改 `adc_read_packed()` 相关配置：`esp_codec_dev_open()` 的 `channel` / `channel_mask`、`MIC_SLOTS[]`、`STREAM_CHANNELS`。

### 3. 板级与 DOA 参数

- 换开发板：更新 `board_manager.defaults`，执行 `idf.py bmgr -b <board_name>`
- 按新板调整采样率、增益及 DOA 预设（如 `ESP_APA_DOA_PRESET_RESPONSIVE`、`fmax_hz`）

更多示例见 `4_mic_example` 与组件 README。

## 故障排除

- **`idf.py bmgr` 失败或找不到 `gen_bmgr_codes`**：确认 `main/idf_component.yml` 已声明 `espressif/esp_board_manager`，执行 `idf.py bmgr -l` 查看可用板型；仍失败可 `idf.py bmgr -x` 清理后重新 `idf.py --preview bmgr -b esp32_s31_korvo_1`。
- **编译提示 esp32s31 相关错误**：确认命令带 `--preview`（S31 为 preview target）。
- **日志出现 `codec read failed`**：检查 USB 供电、板级 `audio_adc` 是否初始化成功，以及是否已执行 `bmgr` 选板。
- **长时间无 `az=` 输出**：对着麦克风说话并提高音量；环境过静或信号过弱时 `valid` 可能长期为 false，可适当调整增益或 DOA 门限（见组件 README FAQ）。

## 技术支持

请通过以下方式获取技术支持：

- 技术支持参见 [esp32.com](https://esp32.com/viewforum.php?f=20) 论坛
- 问题反馈请创建 [ESP-APA issues](https://github.com/espressif/esp-apa/issues)

我们会尽快回复。
