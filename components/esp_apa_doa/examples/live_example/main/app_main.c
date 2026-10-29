/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#include "dev_audio_codec.h"
#include "esp_apa_doa.h"
#include "esp_board_manager_includes.h"
#include "esp_codec_dev.h"

static const char *TAG = "LIVE_DOA";

#define MICS                2
#define MIC_SIDE_M          0.043f   /* S31-Korvo-1 mic spacing, meters */
#define SAMPLE_RATE         16000
#define BITS_PER_SAMPLE     16
#define STREAM_CHANNELS     2
#define REC_GAIN_DB         36.0f
#define NUM_BINS            12
#define DOA_TASK_STACK      (8192 * 2)
#define DOA_TASK_PRIO       6
#define DOA_TASK_CORE       1
#define CODEC_READ_RETRY_MS 5

static const int MIC_SLOTS[MICS] = {0, 1};

/**
 * @brief  Codec ADC capture context for multi-slot PCM reads.
 */
typedef struct {
    esp_codec_dev_handle_t  dev;              /*!< Opened codec device handle */
    int16_t                *raw_pcm;          /*!< Interleaved raw read buffer */
    int                     raw_cap_frames;   /*!< Capacity of raw_pcm in frames */
} adc_ctx_t;

static adc_ctx_t s_adc;

/**
 * @brief  Map 2-mic azimuth (0..180 deg) to a direction bin index.
 */
static int az_to_bin_2mic(float az)
{
    int b = (int)((az / 180.0f) * NUM_BINS + 0.5f);
    if (b < 0) {
        b = 0;
    } else if (b >= NUM_BINS) {
        b = NUM_BINS - 1;
    }
    return b;
}

/**
 * @brief  Return a short label for a 2-mic direction bin.
 */
static const char *bin_name_2mic(int b)
{
    static const char *names_12[12] = {
        "left", "L-F", "L-F2", "L-F3", "L-F4", "L-F5",
        "front", "R-F5", "R-F4", "R-F3", "R-F2", "right",
    };
    if (b < 0 || b >= NUM_BINS) {
        return "?";
    }
    return names_12[b];
}

/**
 * @brief  Log accepted DOA estimates to UART.
 */
static void log_result(const esp_apa_doa_result_t *res)
{
    if (!res->valid) {
        return;
    }
    const int bin = az_to_bin_2mic(res->azimuth_deg);
    ESP_LOGI(TAG, "az=%6.1f bin=%2d %-7s q=%.1f",
             res->azimuth_deg, bin, bin_name_2mic(bin), res->quality);
}

/**
 * @brief  Open audio_adc via board manager and configure capture format.
 */
static esp_err_t adc_init(void)
{
    ESP_RETURN_ON_ERROR(esp_board_manager_init_device_by_name(ESP_BOARD_DEVICE_NAME_AUDIO_ADC),
                        TAG, "Init audio_adc failed");

    dev_audio_codec_handles_t *adc = NULL;
    ESP_RETURN_ON_ERROR(esp_board_manager_get_device_handle(ESP_BOARD_DEVICE_NAME_AUDIO_ADC,
                                                            (void **)&adc),
                        TAG, "Get audio_adc handle failed");
    ESP_RETURN_ON_FALSE(adc && adc->codec_dev, ESP_FAIL, TAG, "Audio_adc handle unavailable");

    esp_codec_dev_sample_info_t fs = {
        .sample_rate = SAMPLE_RATE,
        .channel = 4,
        .bits_per_sample = BITS_PER_SAMPLE,
        .channel_mask = ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0) | ESP_CODEC_DEV_MAKE_CHANNEL_MASK(2),
    };
    ESP_RETURN_ON_FALSE(esp_codec_dev_open(adc->codec_dev, &fs) == ESP_CODEC_DEV_OK,
                        ESP_FAIL, TAG, "Open ADC codec failed");

    s_adc.dev = adc->codec_dev;
    esp_codec_dev_set_in_gain(s_adc.dev, REC_GAIN_DB);
    ESP_LOGI(TAG, "ADC ready: %d Hz, %d stream ch", SAMPLE_RATE, STREAM_CHANNELS);
    return ESP_OK;
}

/**
 * @brief  Close codec device and free the raw PCM buffer.
 */
static void adc_deinit(void)
{
    if (s_adc.dev) {
        esp_codec_dev_close(s_adc.dev);
        s_adc.dev = NULL;
    }
    free(s_adc.raw_pcm);
    s_adc.raw_pcm = NULL;
    s_adc.raw_cap_frames = 0;
}

/**
 * @brief  Read interleaved raw PCM and pack mic slots into pcm_out.
 */
static esp_err_t adc_read_packed(int16_t *pcm_out, int frames)
{
    ESP_RETURN_ON_FALSE(pcm_out && frames > 0, ESP_ERR_INVALID_ARG, TAG, "Read failed: invalid args");

    if (frames > s_adc.raw_cap_frames) {
        int16_t *p = heap_caps_realloc(s_adc.raw_pcm,
                                       sizeof(int16_t) * frames * STREAM_CHANNELS,
                                       MALLOC_CAP_DEFAULT);
        ESP_RETURN_ON_FALSE(p, ESP_ERR_NO_MEM, TAG, "Raw buffer alloc failed");
        s_adc.raw_pcm = p;
        s_adc.raw_cap_frames = frames;
    }

    const int bytes = frames * STREAM_CHANNELS * (int)sizeof(int16_t);
    if (esp_codec_dev_read(s_adc.dev, s_adc.raw_pcm, bytes) != ESP_CODEC_DEV_OK) {
        static int err_n = 0;
        if ((err_n++ % 64) == 0) {
            ESP_LOGW(TAG, "Codec read failed (x%d)", err_n);
        }
        return ESP_FAIL;
    }

    for (int n = 0; n < frames; n++) {
        for (int m = 0; m < MICS; m++) {
            pcm_out[n * MICS + m] = s_adc.raw_pcm[n * STREAM_CHANNELS + MIC_SLOTS[m]];
        }
    }
    return ESP_OK;
}

/**
 * @brief  DOA processing loop: capture PCM, run esp_apa_doa, log results.
 */
static void doa_task(void *arg)
{
    (void)arg;

    esp_apa_doa_cfg_t cfg = ESP_APA_DOA_CFG_DEFAULT();
    ESP_APA_DOA_PRESET_RESPONSIVE(&cfg);
    cfg.fmax_hz = 4000.0f;
    cfg.mic_num = MICS;
    const float mic_pos[MICS][2] = ESP_APA_DOA_MIC_POS_LINEAR(MIC_SIDE_M);
    memcpy(cfg.mic_pos, mic_pos, sizeof(mic_pos));

    esp_apa_doa_handle_t doa = NULL;
    if (esp_apa_doa_create(&cfg, &doa) != ESP_OK) {
        ESP_LOGE(TAG, "DOA create failed: esp_apa_doa_create");
        adc_deinit();
        vTaskDelete(NULL);
        return;
    }

    uint16_t chunk_samples_u16 = 0;
    if (esp_apa_doa_get_frame_samples(doa, &chunk_samples_u16) != ESP_OK) {
        ESP_LOGE(TAG, "Get frame samples failed: esp_apa_doa_get_frame_samples");
        esp_apa_doa_destroy(doa);
        adc_deinit();
        vTaskDelete(NULL);
        return;
    }
    const int chunk_samples = (int)chunk_samples_u16;

    int16_t *pcm = heap_caps_malloc(sizeof(int16_t) * MICS * chunk_samples, MALLOC_CAP_DEFAULT);
    if (pcm == NULL) {
        ESP_LOGE(TAG, "PCM buffer alloc failed");
        esp_apa_doa_destroy(doa);
        adc_deinit();
        vTaskDelete(NULL);
        return;
    }

    esp_apa_doa_result_t res = {0};
    ESP_LOGI(TAG, "2-mic DOA running: mics=%d chunk=%d", MICS, chunk_samples);

    while (1) {
        if (adc_read_packed(pcm, chunk_samples) != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(CODEC_READ_RETRY_MS));
            continue;
        }

        if (esp_apa_doa_process(doa, pcm, (size_t)MICS * (size_t)chunk_samples, &res) != ESP_OK) {
            continue;
        }
        log_result(&res);
    }

    free(pcm);
    esp_apa_doa_destroy(doa);
    adc_deinit();
    vTaskDelete(NULL);
}

void app_main(void)
{
    ESP_LOGI(TAG, "Live DOA example start");

    ESP_RETURN_VOID_ON_ERROR(esp_board_manager_init(), TAG, "Board manager init failed");
    esp_board_manager_print_board_info();

    ESP_RETURN_VOID_ON_ERROR(adc_init(), TAG, "ADC init failed");

    xTaskCreatePinnedToCore(doa_task, "live_doa_task", DOA_TASK_STACK, NULL,
                            DOA_TASK_PRIO, NULL, DOA_TASK_CORE);
}
