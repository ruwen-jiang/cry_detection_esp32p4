/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Interleaved PCM data layout configuration.
 *
 *         Each character maps one interleaved input slot. 'M' marks a
 *         microphone channel to keep; any other letter (R, N, ...) is skipped.
 *         Example: "MRMN" on a 4-slot stream extracts slots 0 and 2 into
 *         interleaved mic-only output (M0, M1, M0, M1, ...).
 *
 *         Typical use with ESP-SR: the front-end often delivers a fixed
 *         multi-slot stream (e.g. M = mic, R = reference). DOA only needs the
 *         mic slots; reference/noise slots are dropped. Set layout to match that
 *         stream and mark 'M' positions — non-mic slots are removed, but the
 *         relative order of kept mics is unchanged (no reordering).
 *
 *         Use ESP_APA_DATA_LAYOUT_CFG_DEFAULT() as the baseline.
 */
typedef struct {
    const char *layout;           /*!< Layout string, e.g. "MRMN" (required) */
    uint8_t     bits_per_sample;  /*!< PCM width; 0 or 16 selects 16-bit (default) */
} esp_apa_data_layout_cfg_t;

/**
 * @brief  Default data layout configuration
 */
#define ESP_APA_DATA_LAYOUT_CFG_DEFAULT(layout_str) ((esp_apa_data_layout_cfg_t){ \
    .layout = (layout_str), \
    .bits_per_sample = 16, \
})

/**
 * @brief  Opaque data layout handle
 */
typedef struct esp_apa_data_layout_t *esp_apa_data_layout_handle_t;

/**
 * @brief  Create a data layout converter from a layout string
 *
 * @note   Interleaved streams only: layout maps channel positions within one
 *         interleaved frame, and both input and output are interleaved. Planar
 *         (per-channel contiguous) data is not supported.
 *
 * @note   The caller owns the returned handle and must release it with
 *         esp_apa_data_layout_destroy(). The function copies cfg contents, so the
 *         caller may release or modify cfg after this function returns.
 *
 * @note   This API allocates heap memory for the handle. It is not ISR-safe and APIs
 *         on the same handle are not thread-safe.
 *
 * @param[in]   cfg         Layout configuration; layout must contain at least two 'M' slots
 * @param[out]  out_handle  Data layout handle on success; undefined on error
 *
 * @return
 *       - ESP_OK               On success
 *       - ESP_ERR_INVALID_ARG  cfg, cfg->layout, or out_handle is NULL, or cfg is out of range
 *       - ESP_ERR_NO_MEM       Internal allocation failed
 */
esp_err_t esp_apa_data_layout_create(const esp_apa_data_layout_cfg_t *cfg,
                                     esp_apa_data_layout_handle_t *out_handle);

/**
 * @brief  Convert interleaved PCM to mic-only layout
 *
 * @note   There is no zero-copy bypass: every call copies selected mic slots into an
 *         internal output buffer, even when the layout is identity (e.g. "MM"). Skip
 *         this converter entirely when the driver already delivers mic-only PCM aligned
 *         with mic_pos[].
 *
 * @note   On success, *out_data (if not NULL) points to an internal buffer allocated
 *         and managed by the converter — the caller must not free it. The pointer
 *         remains valid only until the next esp_apa_data_layout_process() on the same
 *         handle or esp_apa_data_layout_destroy(). Copy the PCM before the next call
 *         if you need to keep it longer.
 *
 * @note   This API may grow or reallocate that internal buffer. It is not ISR-safe and
 *         must not be called concurrently on the same handle.
 *
 * @param[in]   h         Data layout handle returned by esp_apa_data_layout_create()
 * @param[in]   in_data   Interleaved input PCM (16-bit little-endian)
 * @param[in]   in_size   Input byte count; must be a non-zero multiple of frame size
 * @param[out]  out_data  Optional; receives pointer to internal mic-only output buffer
 * @param[out]  out_size  Output byte count on success
 *
 * @return
 *       - ESP_OK                On success
 *       - ESP_ERR_INVALID_ARG   h, in_data, or out_size is NULL
 *       - ESP_ERR_INVALID_SIZE  in_size is 0 or not aligned to the input frame size
 *       - ESP_ERR_NO_MEM        Internal output buffer allocation failed
 */
esp_err_t esp_apa_data_layout_process(esp_apa_data_layout_handle_t h,
                                      const void *in_data, size_t in_size,
                                      const int16_t **out_data, size_t *out_size);

/**
 * @brief  Destroy a data layout handle
 *
 * @note   After this call the handle is invalid and must not be used again.
 *         This API is not ISR-safe.
 *
 * @param[in]  h  Data layout handle returned by esp_apa_data_layout_create()
 *
 * @return
 *       - ESP_OK                     On success
 *       - ESP_ERR_INVALID_ARG        h is NULL
 */
esp_err_t esp_apa_data_layout_destroy(esp_apa_data_layout_handle_t h);

#ifdef __cplusplus
}
#endif
