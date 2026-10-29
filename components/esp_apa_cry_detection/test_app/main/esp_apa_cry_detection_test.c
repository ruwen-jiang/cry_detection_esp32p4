/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: Apache-2.0
 */

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "unity.h"
#include "unity_test_runner.h"
#include "esp_apa_cry_detection.h"

static const char *TAG = "CRY_TEST";

#define FS_HZ           16000
#define CHUNK_SAMPLES   800    /* 50 ms */
#define WINDOW_SAMPLES  16000  /* 1000 ms */
#define HOP_SAMPLES     2400   /* 150 ms algo hop; CHUNK_SAMPLES is only the feed size */

static void feed_zeros(esp_apa_cry_detection_handle_t h, size_t samples, esp_apa_cry_detection_status_t *st)
{
    int16_t chunk[CHUNK_SAMPLES];
    memset(chunk, 0, sizeof(chunk));
    size_t left = samples;
    while (left > 0) {
        size_t n = left > CHUNK_SAMPLES ? CHUNK_SAMPLES : left;
        TEST_ASSERT_EQUAL(ESP_OK, esp_apa_cry_detection_process(h, chunk, n, st));
        left -= n;
    }
}

static void feed_tone(esp_apa_cry_detection_handle_t h, size_t samples, int16_t amp, esp_apa_cry_detection_status_t *st)
{
    int16_t chunk[CHUNK_SAMPLES];
    size_t left = samples;
    int32_t phase = 0;
    while (left > 0) {
        size_t n = left > CHUNK_SAMPLES ? CHUNK_SAMPLES : left;
        for (size_t i = 0; i < n; ++i) {
            /* Cheap square-ish 250 Hz tone (64 samples/period @ 16 kHz). */
            chunk[i] = ((phase / 32) & 1) ? amp : (int16_t)-amp;
            phase++;
        }
        TEST_ASSERT_EQUAL(ESP_OK, esp_apa_cry_detection_process(h, chunk, n, st));
        left -= n;
    }
}

TEST_CASE("cry open/close with default config", "[esp_apa_cry_detection]")
{
    esp_apa_cry_detection_config_t cfg = ESP_APA_CRY_DETECTION_DEFAULT_CONFIG();
    esp_apa_cry_detection_handle_t h = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, esp_apa_cry_detection_open(&cfg, &h));
    TEST_ASSERT_NOT_NULL(h);
    TEST_ASSERT_EQUAL(ESP_OK, esp_apa_cry_detection_close(h));
}

TEST_CASE("cry open rejects invalid args", "[esp_apa_cry_detection]")
{
    esp_apa_cry_detection_config_t cfg = ESP_APA_CRY_DETECTION_DEFAULT_CONFIG();
    esp_apa_cry_detection_handle_t h = (void *)0x1;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, esp_apa_cry_detection_open(NULL, &h));
    TEST_ASSERT_NULL(h);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, esp_apa_cry_detection_open(&cfg, NULL));
    h = (void *)0x1;
    cfg.fsm.start_confirm_count = 0;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, esp_apa_cry_detection_open(&cfg, &h));
    TEST_ASSERT_NULL(h);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, esp_apa_cry_detection_close(NULL));
}

TEST_CASE("cry process rejects invalid args", "[esp_apa_cry_detection]")
{
    esp_apa_cry_detection_config_t cfg = ESP_APA_CRY_DETECTION_DEFAULT_CONFIG();
    esp_apa_cry_detection_handle_t h = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, esp_apa_cry_detection_open(&cfg, &h));

    int16_t pcm[CHUNK_SAMPLES] = {0};
    esp_apa_cry_detection_status_t st;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, esp_apa_cry_detection_process(NULL, pcm, CHUNK_SAMPLES, &st));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, esp_apa_cry_detection_process(h, NULL, CHUNK_SAMPLES, &st));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, esp_apa_cry_detection_process(h, pcm, 0, &st));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, esp_apa_cry_detection_process(h, pcm, CHUNK_SAMPLES, NULL));

    TEST_ASSERT_EQUAL(ESP_OK, esp_apa_cry_detection_close(h));
}

TEST_CASE("cry silence fills window then hops with gate closed", "[esp_apa_cry_detection]")
{
    esp_apa_cry_detection_config_t cfg = ESP_APA_CRY_DETECTION_DEFAULT_CONFIG();
    esp_apa_cry_detection_handle_t h = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, esp_apa_cry_detection_open(&cfg, &h));

    esp_apa_cry_detection_status_t st = {0};
    feed_zeros(h, WINDOW_SAMPLES, &st);
    /* First hop needs a full window; extra hop of zeros should run. */
    feed_zeros(h, HOP_SAMPLES, &st);

    TEST_ASSERT_TRUE(st.valid);
    TEST_ASSERT_FALSE(st.audio_active);
    TEST_ASSERT_EQUAL(ESP_APA_CRY_DETECTION_STATE_IDLE, st.state);
    TEST_ASSERT_EQUAL(ESP_APA_CRY_DETECTION_EVENT_NONE, st.event);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, st.cry_prob);
    TEST_ASSERT_GREATER_OR_EQUAL(1, st.inference_count);
    ESP_LOGI(TAG, "silence hops=%" PRIu32 " ema=%.3f", st.inference_count, st.ema_prob);

    TEST_ASSERT_EQUAL(ESP_OK, esp_apa_cry_detection_close(h));
}

TEST_CASE("cry one large chunk matches two hop-sized process calls", "[esp_apa_cry_detection]")
{
    esp_apa_cry_detection_config_t cfg = ESP_APA_CRY_DETECTION_DEFAULT_CONFIG();
    esp_apa_cry_detection_handle_t a = NULL;
    esp_apa_cry_detection_handle_t b = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, esp_apa_cry_detection_open(&cfg, &a));
    TEST_ASSERT_EQUAL(ESP_OK, esp_apa_cry_detection_open(&cfg, &b));

    esp_apa_cry_detection_status_t st_a = {0};
    esp_apa_cry_detection_status_t st_b = {0};
    /* 7 hops = 16800: first hop that can fill the 1 s window, leftover 0. */
    feed_zeros(a, 7 * HOP_SAMPLES, &st_a);
    feed_zeros(b, 7 * HOP_SAMPLES, &st_b);
    TEST_ASSERT_EQUAL(st_a.inference_count, st_b.inference_count);
    TEST_ASSERT_GREATER_OR_EQUAL(1, st_a.inference_count);
    uint32_t base = st_a.inference_count;

    int16_t *hop = calloc(2 * HOP_SAMPLES, sizeof(int16_t));
    TEST_ASSERT_NOT_NULL(hop);
    TEST_ASSERT_EQUAL(ESP_OK, esp_apa_cry_detection_process(b, hop, HOP_SAMPLES, &st_b));
    TEST_ASSERT_EQUAL(ESP_OK, esp_apa_cry_detection_process(b, hop, HOP_SAMPLES, &st_b));
    TEST_ASSERT_EQUAL(ESP_OK, esp_apa_cry_detection_process(a, hop, 2 * HOP_SAMPLES, &st_a));
    free(hop);

    TEST_ASSERT_EQUAL(st_b.inference_count, st_a.inference_count);
    TEST_ASSERT_EQUAL_FLOAT(st_b.cry_prob, st_a.cry_prob);
    TEST_ASSERT_EQUAL_FLOAT(st_b.ema_prob, st_a.ema_prob);
    TEST_ASSERT_EQUAL(st_b.state, st_a.state);
    TEST_ASSERT_EQUAL(st_b.event, st_a.event);
    TEST_ASSERT_EQUAL(base + 2, st_a.inference_count);

    TEST_ASSERT_EQUAL(ESP_OK, esp_apa_cry_detection_close(a));
    TEST_ASSERT_EQUAL(ESP_OK, esp_apa_cry_detection_close(b));
}

TEST_CASE("cry leftover hop aligns with one full hop", "[esp_apa_cry_detection]")
{
    const size_t first = 2000;
    const size_t second = HOP_SAMPLES - first;

    esp_apa_cry_detection_config_t cfg = ESP_APA_CRY_DETECTION_DEFAULT_CONFIG();
    esp_apa_cry_detection_handle_t a = NULL;
    esp_apa_cry_detection_handle_t b = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, esp_apa_cry_detection_open(&cfg, &a));
    TEST_ASSERT_EQUAL(ESP_OK, esp_apa_cry_detection_open(&cfg, &b));

    esp_apa_cry_detection_status_t st_a = {0};
    esp_apa_cry_detection_status_t st_b = {0};
    feed_tone(a, 7 * HOP_SAMPLES, 8000, &st_a);
    feed_tone(b, 7 * HOP_SAMPLES, 8000, &st_b);
    uint32_t base = st_a.inference_count;
    TEST_ASSERT_EQUAL(base, st_b.inference_count);
    TEST_ASSERT_GREATER_OR_EQUAL(1, base);

    int16_t *pcm = calloc(HOP_SAMPLES, sizeof(int16_t));
    TEST_ASSERT_NOT_NULL(pcm);
    int32_t phase = (int32_t)(7 * HOP_SAMPLES);
    for (size_t i = 0; i < HOP_SAMPLES; ++i) {
        pcm[i] = (((phase + (int32_t)i) / 32) & 1) ? 8000 : (int16_t)-8000;
    }
    TEST_ASSERT_EQUAL(ESP_OK, esp_apa_cry_detection_process(a, pcm, first, &st_a));
    TEST_ASSERT_EQUAL(base, st_a.inference_count);
    TEST_ASSERT_EQUAL(ESP_OK, esp_apa_cry_detection_process(a, pcm + first, second, &st_a));
    TEST_ASSERT_EQUAL(ESP_OK, esp_apa_cry_detection_process(b, pcm, HOP_SAMPLES, &st_b));
    free(pcm);

    TEST_ASSERT_EQUAL(base + 1, st_a.inference_count);
    TEST_ASSERT_EQUAL(st_b.inference_count, st_a.inference_count);
    TEST_ASSERT_EQUAL_FLOAT(st_b.cry_prob, st_a.cry_prob);

    TEST_ASSERT_EQUAL(ESP_OK, esp_apa_cry_detection_close(a));
    TEST_ASSERT_EQUAL(ESP_OK, esp_apa_cry_detection_close(b));
}

TEST_CASE("cry 4s chunk matches hop-sized feeds", "[esp_apa_cry_detection]")
{
    const size_t total = 4 * WINDOW_SAMPLES;
    esp_apa_cry_detection_config_t cfg = ESP_APA_CRY_DETECTION_DEFAULT_CONFIG();
    esp_apa_cry_detection_handle_t a = NULL;
    esp_apa_cry_detection_handle_t b = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, esp_apa_cry_detection_open(&cfg, &a));
    TEST_ASSERT_EQUAL(ESP_OK, esp_apa_cry_detection_open(&cfg, &b));

    int16_t *pcm = calloc(total, sizeof(int16_t));
    TEST_ASSERT_NOT_NULL(pcm);
    for (size_t i = 0; i < total; ++i) {
        pcm[i] = ((i / 32) & 1) ? 8000 : (int16_t)-8000;
    }

    esp_apa_cry_detection_status_t st_a = {0};
    esp_apa_cry_detection_status_t st_b = {0};
    size_t off = 0;
    while (off < total) {
        size_t n = HOP_SAMPLES;
        if (n > total - off) {
            n = total - off;
        }
        TEST_ASSERT_EQUAL(ESP_OK, esp_apa_cry_detection_process(b, pcm + off, n, &st_b));
        off += n;
    }
    TEST_ASSERT_EQUAL(ESP_OK, esp_apa_cry_detection_process(a, pcm, total, &st_a));
    free(pcm);

    const uint32_t hops = (uint32_t)(total / HOP_SAMPLES);
    const uint32_t skipped = (uint32_t)((WINDOW_SAMPLES - 1u) / HOP_SAMPLES);
    TEST_ASSERT_EQUAL(st_b.inference_count, st_a.inference_count);
    TEST_ASSERT_EQUAL(hops - skipped, st_a.inference_count);
    TEST_ASSERT_EQUAL_FLOAT(st_b.cry_prob, st_a.cry_prob);

    TEST_ASSERT_EQUAL(ESP_OK, esp_apa_cry_detection_close(a));
    TEST_ASSERT_EQUAL(ESP_OK, esp_apa_cry_detection_close(b));
}

TEST_CASE("cry tone then silence then tone still infers", "[esp_apa_cry_detection]")
{
    esp_apa_cry_detection_config_t cfg = ESP_APA_CRY_DETECTION_DEFAULT_CONFIG();
    esp_apa_cry_detection_handle_t h = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, esp_apa_cry_detection_open(&cfg, &h));

    esp_apa_cry_detection_status_t st = {0};
    feed_tone(h, WINDOW_SAMPLES + HOP_SAMPLES, 8000, &st);
    TEST_ASSERT_TRUE(st.audio_active);
    TEST_ASSERT_TRUE(st.valid);
    feed_zeros(h, WINDOW_SAMPLES + HOP_SAMPLES, &st);
    TEST_ASSERT_FALSE(st.audio_active);
    feed_tone(h, WINDOW_SAMPLES + HOP_SAMPLES, 8000, &st);
    TEST_ASSERT_TRUE(st.audio_active);
    TEST_ASSERT_TRUE(st.valid);

    TEST_ASSERT_EQUAL(ESP_OK, esp_apa_cry_detection_close(h));
}

TEST_CASE("cry loud tone opens gate and runs CNN hops", "[esp_apa_cry_detection]")
{
    esp_apa_cry_detection_config_t cfg = ESP_APA_CRY_DETECTION_DEFAULT_CONFIG();
    esp_apa_cry_detection_handle_t h = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, esp_apa_cry_detection_open(&cfg, &h));

    esp_apa_cry_detection_status_t st = {0};
    feed_tone(h, WINDOW_SAMPLES + 2 * HOP_SAMPLES, 8000, &st);

    TEST_ASSERT_TRUE(st.valid);
    TEST_ASSERT_TRUE(st.audio_active);
    TEST_ASSERT_GREATER_OR_EQUAL(2, st.inference_count);
    TEST_ASSERT_EQUAL(ESP_APA_CRY_DETECTION_EVENT_NONE, st.event);
    ESP_LOGI(TAG, "tone hops=%" PRIu32 " active=%d p=%.3f ema=%.3f state=%d",
             st.inference_count, (int)st.audio_active, st.cry_prob, st.ema_prob, (int)st.state);

    TEST_ASSERT_EQUAL(ESP_OK, esp_apa_cry_detection_close(h));
}

TEST_CASE("cry process without hop keeps last snapshot event NONE", "[esp_apa_cry_detection]")
{
    esp_apa_cry_detection_config_t cfg = ESP_APA_CRY_DETECTION_DEFAULT_CONFIG();
    esp_apa_cry_detection_handle_t h = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, esp_apa_cry_detection_open(&cfg, &h));

    int16_t pcm[16] = {0};
    esp_apa_cry_detection_status_t st;
    TEST_ASSERT_EQUAL(ESP_OK, esp_apa_cry_detection_process(h, pcm, 16, &st));
    TEST_ASSERT_EQUAL(ESP_APA_CRY_DETECTION_EVENT_NONE, st.event);
    TEST_ASSERT_EQUAL(0, st.inference_count);

    TEST_ASSERT_EQUAL(ESP_OK, esp_apa_cry_detection_close(h));
}
