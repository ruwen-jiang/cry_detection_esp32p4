/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: Apache-2.0
 */

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_board_manager_includes.h"
#include "esp_codec_dev.h"
#include "esp_ns.h"
#include "esp_apa_cry_detection.h"

static const char *TAG = "INFANT_CRY";

#define SAMPLE_RATE     16000
#define MIC_IN_GAIN_DB  36.0f
#ifndef MIC_CHANNELS
#define MIC_CHANNELS  1
#endif  /* MIC_CHANNELS */
#define NS_FRAME_MS         10
#define NS_FRAME_SAMPLES    (SAMPLE_RATE * NS_FRAME_MS / 1000)  /* 160 */
#define PROCESS_MS          50
#define PROCESS_SAMPLES     (SAMPLE_RATE * PROCESS_MS / 1000)  /* 800 */
#define CAPTURE_TASK_STACK  4096
#define DETECT_TASK_STACK   8192
#define PCM_Q_DEPTH         8

static esp_codec_dev_handle_t s_mic_dev;
static esp_apa_cry_detection_handle_t s_cry_handle;
static QueueHandle_t s_pcm_free;
static QueueHandle_t s_pcm_full;

static void handle_status(const esp_apa_cry_detection_status_t *status)
{
    if (status->event == ESP_APA_CRY_DETECTION_EVENT_START) {
        ESP_LOGW(TAG, "*** BABY STARTED CRYING *** ema=%.3f p=%.3f [#%" PRIu32 "]",
                 status->ema_prob, status->cry_prob, status->inference_count);
    } else if (status->event == ESP_APA_CRY_DETECTION_EVENT_END) {
        ESP_LOGI(TAG, "Baby stopped crying. ema=%.3f [#%" PRIu32 "]",
                 status->ema_prob, status->inference_count);
    }
}

static void pcm_to_mono(const int16_t *in, int16_t *out, int frames, int channels)
{
    if (channels <= 1) {
        memcpy(out, in, (size_t)frames * sizeof(int16_t));
        return;
    }
    for (int i = 0; i < frames; ++i) {
        int32_t acc = 0;
        for (int c = 0; c < channels; ++c) {
            acc += in[i * channels + c];
        }
        out[i] = (int16_t)(acc / channels);
    }
}

static void detect_task(void *arg)
{
    (void)arg;
    while (true) {
        int16_t *chunk = NULL;
        if (xQueueReceive(s_pcm_full, &chunk, portMAX_DELAY) != pdTRUE || chunk == NULL) {
            continue;
        }
        esp_apa_cry_detection_status_t status;
        esp_err_t err = esp_apa_cry_detection_process(s_cry_handle, chunk, PROCESS_SAMPLES, &status);
        xQueueSend(s_pcm_free, &chunk, portMAX_DELAY);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "process failed: %s", esp_err_to_name(err));
            continue;
        }
        handle_status(&status);
    }
}

static void capture_task(void *arg)
{
    (void)arg;

    ns_handle_t ns = ns_create(NS_FRAME_MS);
    assert(ns);

    int16_t *raw = malloc((size_t)NS_FRAME_SAMPLES * MIC_CHANNELS * sizeof(int16_t));
    int16_t *mono = malloc((size_t)NS_FRAME_SAMPLES * sizeof(int16_t));
    int16_t *ns_out = malloc((size_t)NS_FRAME_SAMPLES * sizeof(int16_t));
    int16_t *work = calloc(PROCESS_SAMPLES, sizeof(int16_t));
    assert(raw && mono && ns_out && work);

    esp_codec_dev_sample_info_t fs = {
        .sample_rate = SAMPLE_RATE,
        .channel = MIC_CHANNELS,
        .bits_per_sample = 16,
    };
    ESP_ERROR_CHECK(esp_codec_dev_open(s_mic_dev, &fs));
    ESP_ERROR_CHECK(esp_codec_dev_set_in_gain(s_mic_dev, MIC_IN_GAIN_DB));

    ESP_LOGI(TAG, "capture_task: ns=%dms nch=%d process=%dms",
             NS_FRAME_MS, MIC_CHANNELS, PROCESS_MS);

    size_t acc = 0;
    const size_t raw_bytes = (size_t)NS_FRAME_SAMPLES * MIC_CHANNELS * sizeof(int16_t);
    while (true) {
        int ret = esp_codec_dev_read(s_mic_dev, raw, (int)raw_bytes);
        if (ret != ESP_CODEC_DEV_OK) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        pcm_to_mono(raw, mono, NS_FRAME_SAMPLES, MIC_CHANNELS);
        ns_process(ns, mono, ns_out);

        memcpy(work + acc, ns_out, NS_FRAME_SAMPLES * sizeof(int16_t));
        acc += NS_FRAME_SAMPLES;
        if (acc < PROCESS_SAMPLES) {
            continue;
        }

        int16_t *slot = NULL;
        if (xQueueReceive(s_pcm_free, &slot, 0) == pdTRUE) {
            memcpy(slot, work, PROCESS_SAMPLES * sizeof(int16_t));
            if (xQueueSend(s_pcm_full, &slot, 0) != pdTRUE) {
                xQueueSend(s_pcm_free, &slot, 0);
            }
        }
        acc = 0;
    }
}

static void pcm_queues_init(void)
{
    s_pcm_free = xQueueCreate(PCM_Q_DEPTH, sizeof(int16_t *));
    s_pcm_full = xQueueCreate(PCM_Q_DEPTH, sizeof(int16_t *));
    assert(s_pcm_free && s_pcm_full);
    for (int i = 0; i < PCM_Q_DEPTH; ++i) {
        int16_t *buf = calloc(PROCESS_SAMPLES, sizeof(int16_t));
        assert(buf);
        BaseType_t ok = xQueueSend(s_pcm_free, &buf, 0);
        assert(ok == pdTRUE);
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Initializing board...");
    ESP_ERROR_CHECK(esp_board_manager_init());
    esp_board_manager_print_board_info();

    dev_audio_codec_handles_t *adc_handle = NULL;
    ESP_ERROR_CHECK(esp_board_manager_get_device_handle("audio_adc",
                                                        (void **)&adc_handle));
    s_mic_dev = adc_handle->codec_dev;

    esp_apa_cry_detection_config_t cry_cfg = ESP_APA_CRY_DETECTION_DEFAULT_CONFIG();
    ESP_ERROR_CHECK(esp_apa_cry_detection_open(&cry_cfg, &s_cry_handle));
    ESP_LOGI(TAG, "Cry detector opened — NS=esp_ns (%dms), process=%dms",
             NS_FRAME_MS, PROCESS_MS);

    pcm_queues_init();
    xTaskCreatePinnedToCore(capture_task, "capture", CAPTURE_TASK_STACK, NULL, 6, NULL, 1);
    xTaskCreatePinnedToCore(detect_task, "detect", DETECT_TASK_STACK, NULL, 5, NULL, 0);

    ESP_LOGI(TAG, "Cry detection running");
}
