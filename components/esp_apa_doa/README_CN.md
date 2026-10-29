# ESP APA DOA

[English](README.md)

**esp_apa_doa** 是乐鑫推出的轻量级多麦克风声源到达方向（DOA）算法组件，可准确估计声源方向。该算法支持 2～4 路麦克风阵列以及交错、平面（planar）数据输入，可与 ESP-SR、AFE 等多通道音频前端灵活集成。该组件可广泛应用于智能音箱、语音家电、会议终端、云台摄像机、服务机器人、车载语音设备和智能玩具等产品，为定向拾音、波束控制、说话人跟踪、摄像头转向及远场语音交互提供可靠的方向信息。

## 核心特性

- 支持 2/3/4 路麦克风阵列
- 音频数据支持交错或平面（planar）输入
- 多麦克风支持多种摆放形状（共线、三角形、正方形、长方形）
- 支持直接对接 ESP-SR / AFE 等模块，方便多算法集成

**支持芯片**：ESP32 · ESP32-C3 · ESP32-C5 · ESP32-C6 · ESP32-C61 · ESP32-S3 · ESP32-S31 · ESP32-P4

---

## 工作原理

典型集成按以下顺序进行：**麦克风布局 → 采集 PCM →（可选）Layout 引擎（去掉非麦克风数据）→ 配置引擎 → 处理与结果**。

### 1. 麦克风布局

DOA 需要知道每颗麦在板上的 `(x, y)` 坐标（米），且 **PCM 通道 `i` 必须对应 `mic_pos[i]` 那颗物理麦**。原点为阵列中心，**+Y = 0°（正前方）**，方位角顺时针增加。

常用宏（参数为相邻麦间距）：

| 宏                                       | 麦数 | 说明                |
| ---------------------------------------- | ---: | ------------------- |
| `ESP_APA_DOA_MIC_POS_LINEAR(dist_m)`   |    2 | 共线，输出 0..180° |
| `ESP_APA_DOA_MIC_POS_TRIANGLE(side_m)` |    3 | 等边三角形，360°   |
| `ESP_APA_DOA_MIC_POS_SQUARE(side_m)`   |    4 | 正方形，360°       |
| `ESP_APA_DOA_MIC_POS_RECT(w_m, h_m)`   |    4 | 长方形，360°       |

按逻辑顺序填写 `mic_pos[]`，使 PCM 通道 `i` 对应 `mic_pos[i]` 那颗物理麦。若交错流里夹有参考、噪声等非麦克风数据，可用 `esp_apa_data_layout` **去掉**这些无效数据（见下文）；若保留的麦在 PCM 中的先后顺序与几何逻辑不一致，须在采集路径**重排**，`esp_apa_data_layout` **不负责重排**。详见 [麦克风布局](#麦克风布局)。

### 2. 采集 PCM

从 I2S / TDM / AFE 得到多通道 16 位 PCM，按 `input_layout` 以**交错**或**平面**形式送入引擎。`sample_count` 须为 `mic_num` 的整数倍；不必每次恰好满一块，引擎内部会缓冲。

### 3. Layout 引擎（去掉非麦克风数据）

ESP-SR / AFE 常输出固定 slot 排列（如 **MRMN**：麦、参考、麦、噪声）。DOA 只需要其中的 **M** 数据，不需要参考/噪声等 slot。

`esp_apa_data_layout` 用布局字符串描述整条交错流：每个字符对应一个 slot，`M` 表示麦克风，其它字母（`R`、`N` 等）丢弃，输出仍为交错的纯麦 PCM。保留的麦**按原流中的先后顺序输出，不重排**。

```c
esp_apa_data_layout_cfg_t ecfg = ESP_APA_DATA_LAYOUT_CFG_DEFAULT("MRMN");
esp_apa_data_layout_handle_t layout = NULL;
esp_apa_data_layout_create(&ecfg, &layout);

const int16_t *doa_pcm = NULL;
size_t doa_bytes = 0;
esp_apa_data_layout_process(layout, raw_pcm, raw_bytes, &doa_pcm, &doa_bytes);
/* doa_pcm：仅含 slot 0、2 两路麦，交错 */
```

若驱动已直接输出与 `mic_pos[]` 一致的麦通道，可跳过此步。

### 4. 配置引擎

在 `esp_apa_doa_create()` 前填写 `esp_apa_doa_cfg_t`：采样率、麦数、`mic_pos`、输入布局及分析参数。以 `ESP_APA_DOA_CFG_DEFAULT()` 为基准，板级门限等见 [配置](#配置) 与 [FAQ](#faq)。

### 5. 处理与结果

反复调用 `esp_apa_doa_process()` 喂入 PCM。窗满且通过质量门限后，`esp_apa_doa_result_t` 中 `valid == true`，可读 `azimuth_deg`（度）与 `quality`（0..10）。`valid == false` 时勿使用方位字段。配合外部 VAD 时，语句起止可调用 `esp_apa_doa_reset()`，详见 [FAQ](#faq)。

### 6. 示例流程

```c
#include "esp_apa_doa.h"
#include "esp_apa_data_layout.h"  /* 混合 slot 流时需要 */

#define USE_DATA_LAYOUT  0  /* 1：从混合 slot 流提取麦（如 AFE MRMN） */

/* --- 1. 布局：2 麦线性，间距 65 mm --- */
esp_apa_doa_cfg_t cfg = ESP_APA_DOA_CFG_DEFAULT();
cfg.mic_num = 2;
const float mic_pos[2][2] = ESP_APA_DOA_MIC_POS_LINEAR(0.065f);
memcpy(cfg.mic_pos, mic_pos, sizeof(mic_pos));

/* --- 2.（可选）从 AFE 的 MRMN 流提取两路麦；mic_num 须与 'M' 个数一致 --- */
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
while (/* 采集 raw_pcm / raw_bytes */) {
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

## 配置

所有运行时选项在 `esp_apa_doa_cfg_t`（`include/esp_apa_doa.h`）中。以`ESP_APA_DOA_CFG_DEFAULT()` 为基准，在 `esp_apa_doa_create()` 前按板级需求覆盖字段。

| 字段                   | 必填 | 默认值（填 0 时）         | 说明                                                                |
| ---------------------- | ---- | ------------------------- | ------------------------------------------------------------------- |
| `sample_rate_hz`     | 是   | —                        | 输入 PCM 采样率（Hz）                                               |
| `mic_num`           | 是   | —                        | 麦克风数量（2..4）                                                  |
| `mic_pos[][2]`       | 是   | —                        | 各麦 (x, y)（米）；下标`i` 对应 PCM 通道 `i`                    |
| `input_layout`       | 是   | 交错                      | `ESP_APA_DOA_INPUT_LAYOUT_INTERLEAVED` 或 `_PLANAR`             |
| `window_samples`     | 否   | 自动（约 32 ms）          | 每麦分析窗长；2 的幂且 >= 32；16 kHz 下约 512                       |
| `analysis_stride`    | 否   | 4                         | 每 N 个内部分块做一次完整 DOA；越大越省 CPU、延迟越高               |
| `fmax_hz`            | 否   | 2500                      | 分析频带上限（Hz）                                                  |
| `level_min_db`       | 否   | -55（2 麦）/ -42（3+ 麦） | 最小信号电平门限（dBFS）                                            |
| `fit_residual_max`   | 否   | 0.35                      | 3+ 麦几何拟合残差上限（归一化 0~1），越小越严；板级调法见[FAQ](#faq) |
| `confidence_min`     | 否   | 1.5                       | 互相关峰锐度（峰值/RMS）下限；板级调法见[FAQ](#faq)                  |
| `azimuth_offset_deg` | 否   | 0                         | 从原始方位角减去的校准偏移（度）                                    |

创建后可调用 `esp_apa_doa_get_frame_samples()` 查询建议的每次每麦 sample 数。不必每次严格按该长度喂数据 — 引擎内部有输入缓冲，`sample_count` 只需为 `mic_num` 的正整数倍，不足部分会留到下次调用。每次按建议长度喂入可获得最低延迟和最均匀的 CPU 负载。

`analysis_stride` 控制完整 DOA 分析的执行间隔：取 1 表示每个分块都算，跟踪最快、CPU 最高；增大 stride 可明显降低 CPU，但方位刷新变慢（约 `stride × 16 ms` @16 kHz / chunk=256）。未执行完整分析的分块复用上次结果。默认 4（约 64 ms 刷新，CPU/延迟较均衡）。

### 处理预设

在 `esp_apa_doa_create()` 前应用 CPU / 跟踪速度权衡：

| 预设宏                                 | `analysis_stride` | `fmax_hz` | 适用场景           |
| -------------------------------------- | ------------------- | ----------- | ------------------ |
| `ESP_APA_DOA_PRESET_ECONOMY(cfg)`    | 4                   | 2000        | 最低 CPU，跟踪较慢 |
| `ESP_APA_DOA_PRESET_BALANCED(cfg)`   | 2                   | 2500        | 默认平衡           |
| `ESP_APA_DOA_PRESET_RESPONSIVE(cfg)` | 1                   | 2500        | 最快跟踪，CPU 最高 |

```c
esp_apa_doa_cfg_t cfg = ESP_APA_DOA_CFG_DEFAULT();
ESP_APA_DOA_PRESET_RESPONSIVE(&cfg);
cfg.fmax_hz = 4000.0f;  // 预设后可再覆盖
```

### 性能与内存（ESP32-S3）

条件：CPU **240 MHz**，Flash **QIO**；16 kHz，window=512，chunk=256（≈16 ms）。方位刷新延迟仅与 `analysis_stride` 有关：≈ `stride × 16 ms`（2→32 ms，4→64 ms，10→160 ms）。

| 麦数 | create 内存 | CPU% stride=2 | CPU% stride=4 | CPU% stride=10 |
| ---: | ----------: | ------------: | ------------: | -------------: |
|    2 |      ~23 KB |          6.4% |          3.8% |           2.3% |
|    3 |      ~26 KB |         10.7% |          6.3% |           3.7% |
|    4 |      ~30 KB |         18.1% |         10.3% |           5.7% |

| 麦数 |                        MAE |            max 误差 |
| ---: | -------------------------: | ------------------: |
|    2 | ~0.3°（stride10 ~0.36°） | ~3.0–3.8°（端射） |
|    3 |                    ~0.10° |             ~0.21° |
|    4 |                    ~0.12° |             ~0.25° |

理想平面波 5° 扫描；stride 对 3/4 麦精度几乎无影响。`CFG_DEFAULT` 为 stride=4。

### 窗长与输入长度

- `window_samples = 0` 自动推导约 32 ms（16 kHz 下 512，8 kHz 下 256）。
- 窗越长，稳定性和抗噪越好，但延迟和 CPU 增加。
- 每次喂多少 PCM 见上文 `esp_apa_doa_get_frame_samples()`；不足整步的样本会在内部缓冲，凑够后再进入分析。

---

## 麦克风布局

### 坐标系

- 原点 = 阵列中心（质心）。
- **+Y = 0°**（正前方），方位角**顺时针**增加（90° = 右，180° = 后，270° = 左）。
- 各麦坐标为 `(x, y)`，单位米。

俯视图（从上往下看阵列，方位角沿顺时针增加）：

```
              0° (+Y, 正前方)
                   ↑
                   │
    270° (左) ←────·────→ 90° (右)
                   │
                   ↓
              180° (后)

         +X → 右      · = 原点（阵列中心）
```

下方便利宏的参数（`dist_m` / `side_m`）均为**相邻麦克风间距**（边长），不是麦到中心的距离。

### PCM 通道顺序必须与 `mic_pos[]` 一致

引擎约定：PCM 通道下标 `i` 的样本来自 `mic_pos[i]` 所对应的麦克风：

| PCM 通道 | 配置字段       | 必须对应         |
| -------- | -------------- | ---------------- |
| 0        | `mic_pos[0]` | 同一颗物理麦克风 |
| 1        | `mic_pos[1]` | 同一颗物理麦克风 |
| …       | …             | …               |

**交错**输入时，一帧为 `mic0, mic1, …` 按此顺序；**平面**输入时，先放通道 0 的全部样本，再放通道 1，仍分别对应 `mic_pos[0]`、`mic_pos[1]`、…

内置布局宏（`ESP_APA_DOA_MIC_POS_LINEAR/TRIANGLE/SQUARE/RECT`）同时定义了几何坐标和**逻辑通道顺序**。

- **麦顺序与 `mic_pos[]` 不一致**：在采集路径重排通道，再按该逻辑顺序填写 `mic_pos[]`。`esp_apa_data_layout` 不做重排。
- **流中夹有非麦克风数据**（如 ESP-SR 的 MRMN）：用 [esp_apa_data_layout](#3-layout-引擎去掉非麦克风数据) 去掉参考/噪声等，只保留 `M` 对应的数据。

**不要**仅 permute `mic_pos[]` 去迎合原始 wire 顺序而 PCM 不改，否则方位角会错。

### 内置布局宏

**2 麦线性** — `ESP_APA_DOA_MIC_POS_LINEAR(dist_m)`

```
mic0 ────────── mic1        （X 轴上，间距 dist_m）
 0°              180°       （共线 → 仅输出 0..180°）
```

**3 麦等边三角形** — `ESP_APA_DOA_MIC_POS_TRIANGLE(side_m)`

```
            mic0 (+Y)
           /    \
          /  ·   \           · = 原点
    mic1 ─────── mic2
```

mic0 在前顶点；mic1/mic2 在后底角。三边均为 `side_m`。

**4 麦正方形** — `ESP_APA_DOA_MIC_POS_SQUARE(side_m)`

```
  mic0 ───── mic1
   │          │
   │    ·     │             · = 原点
   │          │
  mic3 ───── mic2
```

mic0..mic3 从左上顺时针编号。相邻间距 = `side_m`；对角线间距 = `side_m × √2`。

**4 麦长方形** — `ESP_APA_DOA_MIC_POS_RECT(w_m, h_m)`

宽 `w_m`、高 `h_m`（米）；顺序为左上、右上、右下、左下。异形板请按 PCM 通道顺序填写 `mic_pos`，勿直接套用默认顺序。

### 自定义布局

对非规则板型直接填写 `mic_pos`。引擎自动构建麦克风对几何并检测共线（共线阵列仅输出 0..180°）。

宏参数用相邻麦孔中心距测量值，或逐麦测量 `(x, y)`。间距越大，角分辨率一般越好。

---

## API 参考

头文件：`include/esp_apa_doa.h`、`include/esp_apa_data_layout.h`。

### 生命周期

| 函数                                            | 说明                                   |
| ----------------------------------------------- | -------------------------------------- |
| `esp_apa_doa_create(cfg, &handle)`            | 分配引擎、校验配置、构建几何           |
| `esp_apa_doa_process(h, pcm, count, &result)` | 喂 PCM；窗满时更新估计                 |
| `esp_apa_doa_reset(h)`                        | 语句边界清除跟踪状态；保留历史缓冲     |
| `esp_apa_doa_destroy(h)`                      | 释放全部资源                           |
| `esp_apa_doa_get_frame_samples(h, &chunk)`    | 建议的每次每麦 sample 数（见「配置」） |

同一 handle 上所有 API **非 ISR 安全**且**非线程安全**。

### 输入 PCM 布局

**交错**（默认）— `mic0[0], mic1[0], mic2[0], mic0[1], ...`

**平面（planar）** — `mic0[0..N-1], mic1[0..N-1], mic2[0..N-1], ...`

通过 `cfg.input_layout` 设置。仅支持 16 位有符号 PCM。

此处的 `mic0`、`mic1` 为**逻辑通道**：`mic_pos[i]` 描述的是向通道 `i` 供电的那颗麦。详见[PCM 通道顺序必须与 `mic_pos[]` 一致](#pcm-通道顺序必须与-mic_pos-一致)。

### 结果：`esp_apa_doa_result_t`

| 字段            | 类型      | 含义                                          |
| --------------- | --------- | --------------------------------------------- |
| `valid`       | `bool`  | 本帧产生并接受估计时为`true`                |
| `azimuth_deg` | `float` | 平滑方位角（度）（**仅 valid 时有效**） |
| `quality`     | `float` | 可靠性 0..10（**仅 valid 时有效**）     |

**务必先判断 `valid`。** 当 `valid == false` 时忽略 `azimuth_deg` 和 `quality`（保留上次值）。

`quality` 表示已接受估计的可信程度，不决定是否 valid：

| quality | 含义                         |
| ------- | ---------------------------- |
| 0       | 刚达内部接受门限 — 谨慎使用 |
| 0 – 3  | 余量低；噪声或混响           |
| 3 – 6  | 中等；正常可用               |
| 6 – 10 | 强；相关峰尖锐               |

可在 `valid` 之上再加更严门限（如仅接受 `quality >= 3`）。

何时 `valid` 为 false、何时调用 `esp_apa_doa_reset()` 见 [FAQ](#faq)。

通道提取 API 见 `include/esp_apa_data_layout.h`；去掉 ESP-SR 等非麦克风数据的用法见 [工作原理 · Layout 引擎](#3-layout-引擎去掉非麦克风数据)。

---

## 示例

推荐从 [`examples/live_example`](examples/live_example/) 开始：在 ESP32-S31-Korvo-1 上演示`esp_codec_dev_read()` + `esp_apa_doa_process()` 的最小实时采集集成流程。

---

## FAQ

### 何时 `valid == false`？

`esp_apa_doa_process()` 在 `valid == false` 时仍返回 `ESP_OK`。`false` 表示该次调用中**最后一个完成的内部分块**未产生新的可接受估计。常见情况：

| 情况                                         | `valid` | 说明                                                                |
| -------------------------------------------- | --------- | ------------------------------------------------------------------- |
| 输入仍在缓冲（不足一个内部分块）             | `false` | `sample_count` 须为 `mic_num` 倍数，可小于一块；样本保留至凑满 |
| 分析窗预热（`hist_fill < window_samples`） | `false` | create/reset 后每麦约前 `window_samples` 个样本                    |
| 信号过弱或不可靠（门限未过）                 | `false` | 置信度、电平或（2 麦）通道平衡 /（3+ 麦）残差检查失败               |
| `out == NULL`                              | 不变      | 仅推进历史；不写结果（静音时常用）                                  |

**不必每次恰好喂满建议长度** — 引擎会在内部缓冲输入，凑够一次分析所需样本后再处理；窗未满且门限未过前 `valid` 保持 `false`。

若一次 `process()` 完成多个内部分块，`valid` 仅反映**最后一次**分块。

### 何时调用 `esp_apa_doa_reset`？

`esp_apa_doa_reset()` 清除瞬态跟踪状态（缓存时延、TDOA 滤波、待处理输入片段），但**保留 PCM 历史缓冲**，可在静音期继续喂音并在语音开始时快速出角。

**应调用 reset：**

- 外部 VAD 检测到**语音开始** — 为新语句重新估计方向。
- 外部 VAD 检测到**语音结束** — 丢弃已结束语句的跟踪状态。
- 会话变化（换说话人、长时间 idle、用户触发重新定位）。

**不应调用 reset：**

- **语音仍在进行** — 会打断平滑与 TDOA 跟踪，引起抖动、收敛变慢。
- **每次** `esp_apa_doa_process()` 都 reset。
- **无 VAD 的连续跟踪**场景。
- 同一句内 VAD 短暂掉线 — 应加 hangover，仅持续静音才 reset。

销毁引擎直接 `esp_apa_doa_destroy()`，无需先 reset。

### 如何根据自己的场景调参？

字段填 `0` 沿用库默认。换板或现场环境与实验室不同时，在 `esp_apa_doa_create()` 前于应用层覆盖，不要改库内默认值。

#### 1. 开 debug 日志

menuconfig → `Maximum log verbosity` = **Debug**，运行时：

```c
esp_log_level_set("ESP_APA_DOA", ESP_LOG_DEBUG);
```

标定 `fit_residual_max` / `confidence_min` 时，先把两者设为 `0`，固定角度重复说话10~20 次，收集 **`gate` 行**（仅 **3+ 麦** 会打印；2 麦无此行）。

三门都过（本帧会出 `valid`）：

```
gate az=127.8 res=0.037/0.35 conf=1.65/1.50 lvl=-33/-42
```

某项未过 —— 库在 **`res` / `conf` / `lvl` 那一项的数值后面**追加 ` REJ`（注意前有空格）：

```
gate az=119.9 res=0.080/0.35 conf=1.50/1.50 REJ lvl=-30/-42
gate az=20.9 res=0.974/0.35 REJ conf=1.44/1.50 REJ lvl=-50/-42
```

| 日志片段               | 调参时怎么用                                                                   |
| ---------------------- | ------------------------------------------------------------------------------ |
| `res=0.037/0.35`     | 斜杠前是实测值、后是门限；好帧`res` 通常 < 0.1，乱跳帧通常 > 0.2             |
| `res=... REJ`        | 收紧`fit_residual_max`，或该帧本身不可信（对照 `az` 是否乱跳）             |
| `conf=1.50/1.50 REJ` | 实测 conf 低于门限 → 降低`confidence_min`                                   |
| `lvl=-55/-42 REJ`    | 电平不够 → 降低`level_min_db`（例 -42 → -58）                              |
| `az=127.8`           | 同角度好帧`az` 应接近；`az` 乱跳且 `res` 大 → 收紧 `fit_residual_max` |

若日志里**始终没有 `REJ`**（三门一直过但方位仍跳），直接按 `res` 数值分簇定`fit_residual_max`，不必等 `REJ`。

#### 2. 调 `fit_residual_max`（3+ 麦，优先）

1. 从日志里找出**方位正确**的帧，记下 `res`（多在 0.02~0.10）。
2. 找出**方位乱跳**的帧，记下 `res`（多在 0.20 以上）。
3. 设 `fit_residual_max` = 两簇之间的值（例：好帧 0.04、坏帧 0.21 → 设 **0.15**）。
4. 复测：仍乱跳 → 再收紧；整段说话常无 `valid` → 略放松。

#### 3. 调 `confidence_min`（3+ 麦，其次）

1. 好帧日志里 `conf=1.50/1.50 REJ`（conf 后面有 ` REJ`）→ 把 `confidence_min` 降到**好帧最低 conf 再减 0.2~0.3**（例：最低 1.50 → 设 **1.2**）。
2. 若从未出现 `conf ... REJ`，保持库默认 1.5 即可。

#### 4. 其他参数（看现象直接改）

| 现象                                   | 改什么                           | 建议                                              |
| -------------------------------------- | -------------------------------- | ------------------------------------------------- |
| 轻声/短音无输出，日志里`lvl=... REJ` | `level_min_db`                 | 放松（例 -42 → -58）                             |
| 方位整体偏一个固定角度                 | `azimuth_offset_deg`           | 实测偏移后填入                                    |
| CPU 过高                               | `analysis_stride`、`fmax_hz` | `ESP_APA_DOA_PRESET_ECONOMY` 或增大 stride      |
| 跟踪太慢                               | `analysis_stride`              | 减小 stride 或`ESP_APA_DOA_PRESET_RESPONSIVE`   |
| 2 麦                                   | —                               | 不设`fit_residual_max`；主要调 `level_min_db` |

#### 5. 写入配置

```c
esp_apa_doa_cfg_t cfg = ESP_APA_DOA_CFG_DEFAULT();
cfg.level_min_db     = -58.0f;   /* 按日志 lvl REJ 调整；0 = 自动 */
cfg.fit_residual_max = 0.15f;    /* 3+ 麦，按 res 空档；0 = 自动 0.35 */
cfg.confidence_min   = 1.2f;     /* 按 conf REJ 调整；0 = 自动 1.5 */
```
