/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ESP_APA_DOA_MAX_MICS  4  /*!< Maximum supported microphones */

/**
 * @brief  Linear 2-mic layout coordinates for ESP_APA_DOA_MIC_POS macros
 *
 *         dist_m is the mic-to-mic spacing in meters; the pair is placed on
 *         the X axis centered at the origin.
 */
#define ESP_APA_DOA_MIC_POS_LINEAR(dist_m) {  \
    { -(dist_m) / 2.0f, 0.0f },               \
    {  (dist_m) / 2.0f, 0.0f },               \
}

/**
 * @brief  Equilateral-triangle 3-mic layout coordinates
 *
 *         side_m is the triangle side length in meters; mics are centered at
 *         the array centroid (origin), mic0 on +Y.
 */
#define ESP_APA_DOA_MIC_POS_TRIANGLE(side_m) {     \
    {  0.0f,             (side_m) / 1.73205081f }, \
    { -(side_m) / 2.0f, -(side_m) / 3.46410162f }, \
    {  (side_m) / 2.0f, -(side_m) / 3.46410162f }, \
}

/**
 * @brief  Rectangle 4-mic layout coordinates
 *
 *         w_m / h_m are the rectangle width (X) and height (Y) in meters; mics
 *         are centered at the origin, ordered top-left, top-right, bottom-right,
 *         bottom-left.
 */
#define ESP_APA_DOA_MIC_POS_RECT(w_m, h_m) { \
    { -(w_m) / 2.0f,  (h_m) / 2.0f },        \
    {  (w_m) / 2.0f,  (h_m) / 2.0f },        \
    {  (w_m) / 2.0f, -(h_m) / 2.0f },        \
    { -(w_m) / 2.0f, -(h_m) / 2.0f },        \
}

/**
 * @brief  Square 4-mic layout coordinates
 *
 *         side_m is the square side length in meters; mics are centered at
 *         the origin. Special case of ESP_APA_DOA_MIC_POS_RECT.
 */
#define ESP_APA_DOA_MIC_POS_SQUARE(side_m) ESP_APA_DOA_MIC_POS_RECT(side_m, side_m)

/**
 * @brief  PCM input buffer layout consumed by esp_apa_doa_process()
 */
typedef enum {
    ESP_APA_DOA_INPUT_LAYOUT_INTERLEAVED = 0,  /*!< Interleaved as mic0[0], mic1[0], mic2[0], mic0[1], ... */
    ESP_APA_DOA_INPUT_LAYOUT_PLANAR,           /*!< Planar as mic0[], then mic1[], then mic2[] */
} esp_apa_doa_input_layout_t;

/**
 * @brief  ESP APA DOA runtime configuration.
 *
 *         Coordinates: +Y = 0 degrees (front), azimuth increases clockwise
 *         (90 = right, 180 = back, 270 = left). The configuration is copied by
 *         esp_apa_doa_create() and is read-only inside the engine.
 *
 *         Pipeline: external VAD gate -> delay estimation -> geometric solve ->
 *         gates -> smoothing.
 *
 *         mic_num selects the array (engine sets collinear = true for a linear
 *         layout that only resolves 0..180 deg):
 *           2 -> linear pair; 180 deg user azimuth (0 = left mic, 90 = front,
 *                180 = right mic).
 *           3 -> equilateral triangle; 360 deg (default).
 *           4 -> square; 360 deg.
 *
 *         mic_pos holds the (x, y) position of each mic in meters, with the
 *         array center at the origin. Use ESP_APA_DOA_MIC_POS_LINEAR/TRIANGLE/
 *         SQUARE/RECT for common layouts, or fill custom coordinates directly.
 *
 *         Fields set to 0 use auto-derived defaults (see comments). Use
 *         ESP_APA_DOA_CFG_DEFAULT() as the baseline.
 */
typedef struct {
    int                         sample_rate_hz;                    /*!< Input PCM sample rate in Hz (required) */
    int                         window_samples;                    /*!< Analysis window per mic; power of 2 and >= 32; 0 = auto (512 @ 16kHz) */
    int                         mic_num;                           /*!< Mic count, currently 2..4 (required) */
    float                       mic_pos[ESP_APA_DOA_MAX_MICS][2];  /*!< Mic (x, y) coordinates in meters; center at origin */
    esp_apa_doa_input_layout_t  input_layout;                      /*!< PCM layout consumed by esp_apa_doa_process() */
    float                       azimuth_offset_deg;                /*!< Calibration offset subtracted from raw azimuth in degrees */
    int                         analysis_stride;                   /*!< DOA analysis run interval: 1 = every chunk, N = every Nth; 0 = auto (2) */
    float                       fmax_hz;                           /*!< Analysis band upper edge in Hz; 0 = auto (2500) */
    float                       level_min_db;                      /*!< Minimum signal level gate in dBFS; 0 = auto (-55 for 2-mic, -42 for 3+ mic) */
    float                       fit_residual_max;                  /*!< Max geometric fit residual accepted for 3+ mic solutions (normalized 0..1); 0 = auto (0.35). Lower it on small or reverberant arrays where inconsistent delay sets still land under the default */
    float                       confidence_min;                    /*!< Min cross-correlation peak sharpness (peak-to-RMS ratio); 0 = auto (1.5). On 3+ mic arrays the fit-residual gate cross-checks all pairs and separates good from bad far better, so this can be relaxed to stop it rejecting valid low-contrast peaks */
} esp_apa_doa_cfg_t;

/**
 * @brief  Default tuning for a 3-mic 65 mm triangle at 16 kHz (Balanced preset)
 *
 *         Override mic_num and mic_pos for your board geometry. All 0-valued
 *         fields are auto-derived at create time. Use ESP_APA_DOA_PRESET_ECONOMY
 *         / ESP_APA_DOA_PRESET_RESPONSIVE to switch the analysis_stride / fmax_hz
 *         tuning set.
 */
#define ESP_APA_DOA_CFG_DEFAULT() ((esp_apa_doa_cfg_t){         \
    .sample_rate_hz     = 16000.0f,                             \
    .window_samples     = 0,                                    \
    .mic_num           = 3,                                     \
    .mic_pos            = ESP_APA_DOA_MIC_POS_TRIANGLE(0.065f), \
    .input_layout       = ESP_APA_DOA_INPUT_LAYOUT_INTERLEAVED, \
    .azimuth_offset_deg = 0.0f,                                 \
    .analysis_stride    = 4,                                    \
    .fmax_hz            = 0.0f,                                 \
    .level_min_db       = 0.0f,                                 \
    .fit_residual_max   = 0.0f,                                 \
    .confidence_min     = 0.0f,                                 \
})

/**
 * @brief  Apply Economy preset: lower CPU, slower tracking
 *         analysis_stride=4, fmax=2000 Hz
 */
#define ESP_APA_DOA_PRESET_ECONOMY(cfg) do {  \
    (cfg)->analysis_stride = 10;              \
    (cfg)->fmax_hz         = 2000.0f;         \
} while (0)

/**
 * @brief  Apply Balanced preset
 *         analysis_stride=2, fmax=2500 Hz
 */
#define ESP_APA_DOA_PRESET_BALANCED(cfg) do {  \
    (cfg)->analysis_stride = 4;                \
    (cfg)->fmax_hz         = 2500.0f;          \
} while (0)

/**
 * @brief  Apply Responsive preset: fast tracking, highest CPU
 *         analysis_stride=1, fmax=2500 Hz
 */
#define ESP_APA_DOA_PRESET_RESPONSIVE(cfg) do {  \
    (cfg)->analysis_stride = 2;                  \
    (cfg)->fmax_hz         = 2500.0f;            \
} while (0)

/**
 * @brief  DOA result for one analysis frame
 *
 *         azimuth: 2-mic -> 0..180 (0 = left, 90 = front, 180 = right);
 *         3+ mics -> 0..360 (0 = front, 90 = right, clockwise).
 *
 *         azimuth and quality are only meaningful when valid == true; when valid
 *         is false they retain their previous contents and must be ignored.
 *
 *         quality grades how reliable an accepted estimate is (it does not decide
 *         validity - valid already does). It is normalized to read the same across
 *         geometries and sample rates:
 *           0        just met the internal accept threshold (use with caution)
 *           0  - 3   low margin; noisy or reverberant conditions
 *           3  - 6   moderate; normal usable detection
 *           6  - 10  strong; sharp correlation peak, most reliable
 *         Apply your own stricter cutoff (e.g. accept only quality >= 3) on top of
 *         the valid flag when you need higher precision.
 */
typedef struct {
    bool   valid;        /*!< True when this frame produced and accepted a new DOA estimate */
    float  azimuth_deg;  /*!< Smoothed azimuth in degrees (valid only) */
    float  quality;      /*!< Reliability score 0..10 (valid only): 0 = at accept threshold, 10 = ideal */
} esp_apa_doa_result_t;

/**
 * @brief  Opaque DOA engine handle
 */
typedef struct esp_apa_doa_engine_t *esp_apa_doa_handle_t;

/**
 * @brief  Create a DOA engine
 *
 * @note   The caller owns the returned handle and must release it with
 *         esp_apa_doa_destroy(). The function copies cfg, so the caller may release
 *         or modify cfg after this function returns.
 *
 * @note   This API allocates heap memory for internal analysis buffers. It is not
 *         ISR-safe and APIs on the same handle are not thread-safe.
 *
 * @param[in]   cfg         Engine configuration; use ESP_APA_DOA_CFG_DEFAULT() as baseline
 * @param[out]  out_handle  DOA handle on success; undefined on error
 *
 * @return
 *       - ESP_OK               On success
 *       - ESP_ERR_INVALID_ARG  cfg or out_handle is NULL, or cfg is out of range
 *       - ESP_ERR_NO_MEM       Internal buffer allocation failed
 *       - ESP_FAIL             Internal init failed
 */
esp_err_t esp_apa_doa_create(const esp_apa_doa_cfg_t *cfg, esp_apa_doa_handle_t *out_handle);

/**
 * @brief  Get the internal analysis frame length in samples per mic
 *
 *         Feeding this many samples per mic per call gives the lowest latency and
 *         most even CPU load, but any multiple of cfg.mic_num is accepted.
 *
 * @param[in]   h                 DOA handle returned by esp_apa_doa_create()
 * @param[out]  out_frame_samples Samples per mic per analysis frame on success
 *
 * @return
 *       - ESP_OK               On success
 *       - ESP_ERR_INVALID_ARG  h or out_frame_samples is NULL
 */
esp_err_t esp_apa_doa_get_frame_samples(esp_apa_doa_handle_t h, uint16_t *out_frame_samples);

/**
 * @brief  Feed PCM audio and update the DOA estimate
 *
 *         The caller may pass any number of complete sample groups; sample_count only
 *         needs to be a multiple of cfg.mic_num. The engine buffers input
 *         internally and processes one analysis frame at a time, maintaining the
 *         sliding analysis window. Until enough samples are accumulated,
 *         out->valid is false. If an external VAD is used, pass out == NULL for
 *         silent chunks to update only the internal history and skip DOA
 *         calculation.
 *
 * @note   out is valid only for this call. This API is not ISR-safe and must not be
 *         called concurrently on the same handle.
 *
 * @param[in]   h             DOA handle returned by esp_apa_doa_create()
 * @param[in]   pcm           PCM samples in cfg.input_layout
 * @param[in]   sample_count  Total int16 sample count; must be a multiple of cfg.mic_num
 * @param[out]  out           Result buffer populated on success; NULL updates history only
 *
 * @return
 *       - ESP_OK                Input accepted and, when the window is full, processed
 *       - ESP_ERR_INVALID_ARG   h or pcm is NULL
 *       - ESP_ERR_INVALID_SIZE  sample_count is not a positive multiple of cfg.mic_num
 */
esp_err_t esp_apa_doa_process(esp_apa_doa_handle_t h, const int16_t *pcm,
                              size_t sample_count, esp_apa_doa_result_t *out);

/**
 * @brief  Reset transient DOA tracking state
 *
 *         Clears cached delay/confidence estimates, analysis stride counters, the
 *         pending input buffer, and jump confirmation state. The internal PCM
 *         history is kept so a caller can keep feeding silent/pre-roll chunks and
 *         get a low-latency estimate when speech starts. The smoothed direction is
 *         also kept, so UI can hold the last accepted direction.
 *
 * @param[in]  h  DOA handle returned by esp_apa_doa_create()
 *
 * @return
 *       - ESP_OK               On success
 *       - ESP_ERR_INVALID_ARG  h is NULL
 */
esp_err_t esp_apa_doa_reset(esp_apa_doa_handle_t h);

/**
 * @brief  Destroy a DOA engine
 *
 * @param[in]  h  DOA handle returned by esp_apa_doa_create()
 *
 * @return
 *       - ESP_OK               On success
 *       - ESP_ERR_INVALID_ARG  h is NULL
 */
esp_err_t esp_apa_doa_destroy(esp_apa_doa_handle_t h);

#ifdef __cplusplus
}
#endif
