/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Infant cry detection (APA Cry Detection) is an audio analysis pipeline designed
 *         to detect baby crying from continuous PCM streams on Espressif chips.
 *
 *         The detector keeps a sliding audio window, gates silent frames by RMS
 *         energy, runs a CNN classifier when the gate is open, then applies
 *         Exponential Moving Average (EMA) smoothing and a confirm-count
 *         Finite State Machine (FSM) to produce stable crying start / end
 *         events.
 *
 *         Typical data flow:
 *         PCM feed -> ring buffer -> energy gate -> CNN ->
 *         Exponential Moving Average (EMA) + Finite State Machine (FSM) -> status.
 *
 *         Processing is driven by the caller. `esp_apa_cry_detection_open()` does not
 *         create an internal FreeRTOS task. Call `esp_apa_cry_detection_process()` with
 *         PCM chunks; when enough audio for one hop has been accumulated,
 *         one inference hop is executed.
 *
 *         Input audio format is fixed (not set in `esp_apa_cry_detection_config_t`):
 *         16 kHz, mono, signed 16-bit PCM (`int16_t`). The component does
 *         not resample, downmix, or convert bit depth. Multi-mic data must
 *         be downmixed by the caller before `esp_apa_cry_detection_process()`.
 *
 *         CNN window (1000 ms), hop (150 ms) and ring-buffer size (2000 ms)
 *         are also fixed internally.
 *
 *         Configurable parameters control silence gating and Finite State
 *         Machine (FSM) sensitivity.
 *         Use `ESP_APA_CRY_DETECTION_DEFAULT_CONFIG()` for recommended defaults.
 */

/**
 * @brief  Handle for infant cry detection
 */
typedef void *esp_apa_cry_detection_handle_t;

/**
 * @brief  Public detector state
 */
typedef enum {
    ESP_APA_CRY_DETECTION_STATE_IDLE      = 0,  /*!< Idle / no cry activity */
    ESP_APA_CRY_DETECTION_STATE_DETECTING = 1,  /*!< Approaching start threshold, not confirmed yet */
    ESP_APA_CRY_DETECTION_STATE_CRYING    = 2,  /*!< Confirmed crying state */
} esp_apa_cry_detection_state_t;

/**
 * @brief  One-shot event reported by a process call
 */
typedef enum {
    ESP_APA_CRY_DETECTION_EVENT_NONE  = 0,  /*!< No state transition in this call */
    ESP_APA_CRY_DETECTION_EVENT_START = 1,  /*!< Entered CRYING state */
    ESP_APA_CRY_DETECTION_EVENT_END   = 2,  /*!< Exited CRYING state */
} esp_apa_cry_detection_event_t;

/**
 * @brief  Latest detection status snapshot
 */
typedef struct {
    bool                           valid;            /*!< True if status fields are meaningful */
    bool                           audio_active;     /*!< True if energy gate is open */
    esp_apa_cry_detection_state_t  state;            /*!< Current detector state */
    float                          cry_prob;         /*!< Latest raw P(cry), range [0, 1] */
    float                          ema_prob;         /*!< Exponential Moving Average (EMA) smoothed P(cry), range [0, 1] */
    esp_apa_cry_detection_event_t  event;            /*!< START/END from this process() call; last non-NONE if several hops ran */
    uint32_t                       inference_count;  /*!< Number of completed inference hops */
} esp_apa_cry_detection_status_t;

/**
 * @brief  Energy gate configuration
 *
 *         Pre-filter before CNN. Checks whether the current window is loud
 *         enough to run inference; it does not classify crying.
 *
 *         When enabled and RMS < `energy_threshold`, CNN is skipped and
 *         probability 0.0 is fed to the Finite State Machine (FSM). When
 *         disabled, every hop runs CNN.
 *
 *         Raise the threshold to ignore more quiet noise; lower it if quiet
 *         crying is skipped. Default: enable = true, energy_threshold = 0.001.
 */
typedef struct {
    bool   enable;            /*!< Enable energy gate. false: always run CNN */
    float  energy_threshold;  /*!< RMS gate on linear scale (PCM normalized to [-1, 1]) */
} esp_apa_cry_detection_gate_config_t;

/**
 * @brief  Finite State Machine (FSM) / Exponential Moving Average (EMA)
 *         configuration for cry START / END events
 *
 *         Raw CNN probability is first smoothed by Exponential Moving Average
 *         (EMA), then confirmed by a dual-threshold Finite State Machine (FSM)
 *         to suppress short false triggers and tolerate brief pauses during
 *         crying.
 *
 *         - START: Exponential Moving Average (EMA) stays above `start_threshold`
 *           for `start_confirm_count` hops
 *         - END:   Exponential Moving Average (EMA) stays below `end_threshold`
 *           for `end_confirm_count` hops
 *         - Keep `start_threshold` > `end_threshold` (hysteresis)
 *         - Larger `ema_alpha` is smoother/slower; smaller is faster/noisier
 *
 *         With hop = 150 ms, defaults 4 / 6 hops are about 600 ms / 900 ms.
 */
typedef struct {
    float     start_threshold;      /*!< Enter threshold on Exponential Moving Average (EMA) probability, typically 0.7~0.9 */
    float     end_threshold;        /*!< Exit threshold on Exponential Moving Average (EMA) probability, typically 0.2~0.4; must be < start_threshold */
    uint32_t  start_confirm_count;  /*!< Consecutive hops above start_threshold to emit START */
    uint32_t  end_confirm_count;    /*!< Consecutive hops below end_threshold to emit END */
    float     ema_alpha;            /*!< Exponential Moving Average (EMA) history weight, range (0, 1]; default 0.75 */
} esp_apa_cry_detection_fsm_config_t;

/**
 * @brief  Configuration structure for infant cry detection
 *
 *         Only sensitivity-related options are exposed here:
 *         - `gate`: skip CNN on silent windows to save CPU
 *         - `fsm`: control how easily START/END events are triggered
 *
 * @note  Audio format is not configured here. Input must be 16 kHz mono
 *         signed 16-bit PCM. CNN window (1000 ms), hop (150 ms) and
 *         ring-buffer size (2000 ms) are fixed internally.
 */
typedef struct {
    esp_apa_cry_detection_gate_config_t  gate;  /*!< Pre-CNN energy gate */
    esp_apa_cry_detection_fsm_config_t   fsm;   /*!< Exponential Moving Average (EMA) smoothing and START/END Finite State Machine (FSM) */
} esp_apa_cry_detection_config_t;

/**
 * @brief  Default cry detector configuration
 *
 *         Suitable starting point for most products:
 *         - Gate on with RMS threshold 0.001 (lets quiet / far-field cries through;
 *           0.01 was too high and skipped many low-level cry clips)
 *         - Enter cry at Exponential Moving Average (EMA) >= 0.8 for 4 hops (~600 ms)
 *         - Exit cry at Exponential Moving Average (EMA) < 0.35 for 6 hops (~900 ms)
 *         - Exponential Moving Average (EMA) smoothing (alpha = 0.77)
 */
#define ESP_APA_CRY_DETECTION_DEFAULT_CONFIG()  {  \
    .gate = {                                      \
        .enable           = true,                  \
        .energy_threshold = 0.001f,                \
    },                                             \
    .fsm = {                                       \
        .start_threshold     = 0.85f,              \
        .end_threshold       = 0.35f,              \
        .start_confirm_count = 4,                  \
        .end_confirm_count   = 6,                  \
        .ema_alpha           = 0.77f,              \
    },                                             \
}

/**
 * @brief  Create a cry detector handle based on the provided configuration
 *
 * @note  This function does not start an internal task. The caller must feed
 *         16 kHz mono 16-bit PCM through `esp_apa_cry_detection_process()`.
 *
 * @param[in]   config  Cry detector configuration
 * @param[out]  handle  The cry detector handle. On failure, set to NULL
 *
 * @return
 *       - ESP_OK               Operation succeeded
 *       - ESP_ERR_INVALID_ARG  Invalid input parameter
 *       - ESP_ERR_NO_MEM       Failed to allocate memory
 */
esp_err_t esp_apa_cry_detection_open(const esp_apa_cry_detection_config_t *config,
                                     esp_apa_cry_detection_handle_t *handle);

/**
 * @brief  Feed PCM samples and run detection when one hop of audio has accumulated
 *
 * @note  `data` must already be 16 kHz mono signed 16-bit PCM (`int16_t`).
 *         This API does not resample.
 *         `samples` is the number of sampling points in `data`.
 *         Every call writes the latest status into `status`. A large chunk can
 *         complete several hops. Each hop uses the 1000 ms window ending at
 *         the hop boundary (`write_pos` minus leftover samples in the current
 *         hop). `status->event` is the last START/END in this call, or
 *         `ESP_APA_CRY_DETECTION_EVENT_NONE` if no hop ran. Other fields keep the last
 *         inference snapshot.
 *
 * @param[in]   handle   The cry detector handle
 * @param[in]   data     Input PCM buffer, 16-bit mono samples
 * @param[in]   samples  Number of int16 samples in `data`
 * @param[out]  status   Latest detection status snapshot
 *
 * @return
 *       - ESP_OK               Operation succeeded
 *       - ESP_ERR_INVALID_ARG  Invalid input parameter
 */
esp_err_t esp_apa_cry_detection_process(esp_apa_cry_detection_handle_t handle,
                                        const int16_t *data, size_t samples,
                                        esp_apa_cry_detection_status_t *status);

/**
 * @brief  Destroy cry detector handle and free internal resources
 *
 * @param[in]  handle  The cry detector handle
 *
 * @return
 *       - ESP_OK               Operation succeeded
 *       - ESP_ERR_INVALID_ARG  Invalid input parameter
 */
esp_err_t esp_apa_cry_detection_close(esp_apa_cry_detection_handle_t handle);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
