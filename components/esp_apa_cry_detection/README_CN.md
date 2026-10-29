# ESP APA Cry Detection

[English](README.md)

**esp_apa_cry_detection** 是乐鑫推出的轻量级婴儿哭声检测算法组件，可从连续 PCM 流中稳定识别哭声起止。检测器维护滑动音频窗口，按 RMS 能量门控静音帧，运行轻量 int8 CNN 分类，再经指数滑动平均（EMA）平滑和双阈值有限状态机（FSM）输出 START / END 事件。该组件可广泛应用于婴儿监护、智能音箱、看护摄像头、服务机器人及其他 AIoT 看护产品。

## 核心特性

- 16 kHz 单声道 16 位 PCM；窗 1000 ms、hop 150 ms
- 能量门控跳过静音窗，降低 CPU
- 轻量 int8 CNN 输出哭声概率
- EMA + 双阈值 FSM 输出稳定的 START / END，抑制短误触发
- 算法不含降噪、重采样或下混

**支持芯片**：ESP32 · ESP32-C3 · ESP32-C5 · ESP32-C6 · ESP32-C61 · ESP32-S3 · ESP32-S31 · ESP32-P4

---

## 配置

运行时选项在 `esp_apa_cry_detection_config_t`（`include/esp_apa_cry_detection.h`）中。以 `ESP_APA_CRY_DETECTION_DEFAULT_CONFIG()` 为基准，在 `esp_apa_cry_detection_open()` 前按板级需求覆盖。

对外只暴露灵敏度相关项：`gate` 用于静音窗跳过 CNN 以省 CPU；`fsm` 控制 START / END 触发难易。

### 能量门控 `gate`

CNN 前的预滤波：判断当前 1000 ms 窗口是否够响，**不负责**判定是否为哭声。

| 字段 | 默认值 | 说明 |
| ---- | ------ | ---- |
| `enable` | `true` | `true`：RMS 低于门限则跳过 CNN，向 FSM 送入概率 0.0；`false`：每个 hop 都跑 CNN |
| `energy_threshold` | `0.001` | RMS 门限，线性幅度（PCM 归一化到 `[-1, 1]`） |

提高门限可滤掉更多安静噪声；过低哭声被跳过时再降低。默认 `0.001` 可放过安静 / 远场哭声（`0.01` 偏严，容易漏检）。

### FSM / EMA `fsm`

CNN 原始概率先经 EMA 平滑，再经双阈值 FSM 确认，以抑制短误触发，并容忍哭声过程中的短暂停顿。

- **START**：EMA 连续 `start_confirm_count` 个 hop 高于 `start_threshold`
- **END**：EMA 连续 `end_confirm_count` 个 hop 低于 `end_threshold`
- 须保持 `start_threshold` > `end_threshold`（滞回）
- `ema_alpha` 越大越平滑、越慢；越小越跟手、越易抖

hop = 150 ms 时，默认 4 / 6 hop 约对应 600 ms / 900 ms。

| 字段 | 默认值 | 说明 |
| ---- | ------ | ---- |
| `start_threshold` | `0.85` | 进入哭声的 EMA 概率门限，典型 0.7～0.9 |
| `end_threshold` | `0.35` | 退出哭声的 EMA 概率门限，典型 0.2～0.4，须小于 `start_threshold` |
| `start_confirm_count` | `4` | 连续高于进入门限的 hop 数后发出 START |
| `end_confirm_count` | `6` | 连续低于退出门限的 hop 数后发出 END |
| `ema_alpha` | `0.77` | EMA 历史权重，范围 `(0, 1]` |

### 固定参数

| 项 | 取值 |
| -- | ---- |
| 采样率 / 声道 / 位深 | 16 kHz / 单声道 / int16 |
| CNN 窗 | 1000 ms（16000 点） |
| 推理 hop | 150 ms（2400 点） |
| 环形缓冲 | 2000 ms（32000 点） |

### 性能与内存（ESP32-P4）

条件：Function EV，CPU **400 MHz**。CPU-loading 按单次 hop 墙钟时间相对 **1000 ms 分析窗** 计算。

| 项 | 占用 |
| -- | ---- |
| CPU-loading | 5.72% |
| Stack | 3 KiB |
| PSRAM | 158.5 KiB |
| InnRAM | 0 KiB |
| Flash | 64.4 KiB |

本组件依赖 `espressif/gmf_fft` 做 Log-Mel 的 STFT。有 PSRAM 的芯片上，算法堆内存申请在 PSRAM。

---

## API 参考

头文件：`include/esp_apa_cry_detection.h`。

输入须为 **16 kHz 单声道 int16**。组件不做重采样、下混或位深转换。

### 生命周期

| 函数 | 说明 |
| ---- | ---- |
| `esp_apa_cry_detection_open(cfg, &handle)` | 分配检测器；失败时 `*handle = NULL`。不创建内部任务 |
| `esp_apa_cry_detection_process(h, pcm, samples, &status)` | 喂入 PCM；凑满 hop 时推理并更新 `status` |
| `esp_apa_cry_detection_close(h)` | 释放全部内部资源 |

`samples` 为 `data` 中的采样点数。同一 handle **非 ISR 安全、非线程安全**。

### 检测器状态：`esp_apa_cry_detection_state_t`

| 值 | 含义 |
| -- | ---- |
| `ESP_APA_CRY_DETECTION_STATE_IDLE` | 空闲，无哭声活动 |
| `ESP_APA_CRY_DETECTION_STATE_DETECTING` | EMA 接近进入门限，尚未确认 |
| `ESP_APA_CRY_DETECTION_STATE_CRYING` | 已确认处于哭声状态 |

### 一次性事件：`esp_apa_cry_detection_event_t`

| 值 | 含义 |
| -- | ---- |
| `ESP_APA_CRY_DETECTION_EVENT_NONE` | 本次 `process()` 无状态跳变 |
| `ESP_APA_CRY_DETECTION_EVENT_START` | 进入 `CRYING` |
| `ESP_APA_CRY_DETECTION_EVENT_END` | 退出 `CRYING` |

产品逻辑应订阅 **event**，而不是每 hop 轮询 `cry_prob`。

### 结果：`esp_apa_cry_detection_status_t`

| 字段 | 类型 | 含义 |
| ---- | ---- | ---- |
| `valid` | `bool` | 字段是否有意义 |
| `audio_active` | `bool` | 能量门控是否打开（即将跑 / 已跑 CNN） |
| `state` | `esp_apa_cry_detection_state_t` | 当前检测器状态 |
| `cry_prob` | `float` | 最近一次原始 P(cry)，范围 `[0, 1]` |
| `ema_prob` | `float` | EMA 平滑后的 P(cry)，范围 `[0, 1]` |
| `event` | `esp_apa_cry_detection_event_t` | 本次调用中最后一次 START / END；无 hop 则为 `NONE` |
| `inference_count` | `uint32_t` | 已完成的推理 hop 次数 |

无 hop 时 `event` 为 `NONE`，`cry_prob` / `ema_prob` / `state` 保留上一拍快照。一次 `process()` 若跑完多个 hop，`event` 取其中最后一次 START / END。

---

## 示例

实时麦克风演示：[`examples/infant_cry_detection`](examples/infant_cry_detection/)。例程采集 PCM，经独立 `esp_ns`（非 AFE）后按 50 ms 调用 `esp_apa_cry_detection_process()`，在串口打印哭声 START / END。换板请先执行 `idf.py bmgr`（见 [例程 README](examples/infant_cry_detection/README_CN.md)）。

```c
#include "esp_apa_cry_detection.h"

esp_apa_cry_detection_config_t cfg = ESP_APA_CRY_DETECTION_DEFAULT_CONFIG();
esp_apa_cry_detection_handle_t cry = NULL;
ESP_ERROR_CHECK(esp_apa_cry_detection_open(&cfg, &cry));

int16_t pcm[800];
esp_apa_cry_detection_status_t st;

while (/* 送入 16 kHz 单声道 PCM */) {
    ESP_ERROR_CHECK(esp_apa_cry_detection_process(cry, pcm, 800, &st));
    if (st.event == ESP_APA_CRY_DETECTION_EVENT_START) {
        /* 哭声开始 */
    } else if (st.event == ESP_APA_CRY_DETECTION_EVENT_END) {
        /* 哭声结束 */
    }
}

ESP_ERROR_CHECK(esp_apa_cry_detection_close(cry));
```

---

## FAQ

### 为何一直没有 START？

按下面顺序排查：

| 情况 | 说明 |
| ---- | ---- |
| 窗尚未填满 | 需先积累约 1000 ms，之后每 150 ms 才有一次推理 |
| `audio_active == false` | 能量门过严，CNN 被跳过、概率被置 0；降低 `gate.energy_threshold` 或设 `gate.enable = false` 对照 |
| `ema_prob` 已高但无 START | 确认计数不够；减小 `start_confirm_count` 或略降 `start_threshold` |
| 输入不是 16 kHz 单声道 int16 | 组件不做重采样 / 下混，格式不对会几乎检不到 |

### 为何误触发太多？

提高 `start_threshold`、增大 `start_confirm_count`，或略提高 `gate.energy_threshold` 滤掉环境噪声。`ema_alpha` 略增大可使概率更稳、更不容易被短噪声拉过门限。

### 哭声中间短暂停顿就 END 了？

这是退出滞回偏紧。增大 `end_confirm_count`（默认 6 hop ≈ 900 ms）或略降 `end_threshold`，让短暂停顿仍保持 `CRYING`。

### 一次 `process()` 喂很大一块会怎样？

可以。内部按 hop 边界切窗：每个 hop 使用以该 hop 边界为终点的 1000 ms 窗口。`status->event` 只保留本调用中**最后一次** START / END；`inference_count` 会累加本次完成的 hop 数。

### 需要自己建任务吗？

需要。`open()` 不创建 FreeRTOS 任务。在采集任务里按块调用 `process()` 即可，注意 API 非线程安全，不要多任务同时操作同一 handle。

### 如何按自己的场景调参？

字段以 `ESP_APA_CRY_DETECTION_DEFAULT_CONFIG()` 为起点，在 `open()` 前覆盖，不要改库内默认值。

| 现象 | 改什么 | 建议 |
| ---- | ------ | ---- |
| 远场 / 小声哭漏检，且 `audio_active` 常为 false | `gate.energy_threshold` | 降低（默认 0.001；勿轻易升回 0.01） |
| 环境噪声频繁 START | `start_threshold`、`start_confirm_count`、`gate.energy_threshold` | 提高进入门限或确认 hop；必要时略提高能量门 |
| START 太慢 | `start_confirm_count`、`ema_alpha` | 减少确认 hop（延迟约 `count × 150 ms`）；略降 `ema_alpha` |
| 一句哭声被切成多段 | `end_confirm_count`、`end_threshold` | 增大退出确认；略降退出门限 |
| CPU 偏高 | `gate.enable` | 保持门控开启，让静音 hop 跳过 CNN |

```c
esp_apa_cry_detection_config_t cfg = ESP_APA_CRY_DETECTION_DEFAULT_CONFIG();
cfg.gate.energy_threshold    = 0.001f;  /* 远场漏检再降低；噪声误触发再升高 */
cfg.fsm.start_threshold      = 0.85f;
cfg.fsm.end_threshold        = 0.35f;
cfg.fsm.start_confirm_count  = 4;       /* ≈ 600 ms */
cfg.fsm.end_confirm_count    = 6;       /* ≈ 900 ms */
cfg.fsm.ema_alpha            = 0.77f;
ESP_ERROR_CHECK(esp_apa_cry_detection_open(&cfg, &cry));
```
