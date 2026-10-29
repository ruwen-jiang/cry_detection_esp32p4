/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "unity.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_apa_doa.h"

/* ------------------------------------------------------------------ */
/* Config                                                             */
/* ------------------------------------------------------------------ */

static const char *TAG = "DOA_TEST";

#define FS_HZ       16000.0f
#define SOUND_C     343.0f
#define NUM_TONES   24
#define FEED_CHUNKS 63       /* ~1 s at chunk 256 */

/* 2-mic linear 65 mm; 3-mic triangle 65 mm; 4-mic rectangle 46 x 48 mm. */
static const float MIC_2[2][2] = {{-0.0325f, 0.0f}, {0.0325f, 0.0f}};
static const float MIC_3[3][2] = ESP_APA_DOA_MIC_POS_TRIANGLE(0.065f);
static const float MIC_4[4][2] = ESP_APA_DOA_MIC_POS_RECT(0.046f, 0.048f);

/* ------------------------------------------------------------------ */
/* Shared helpers                                                     */
/* ------------------------------------------------------------------ */

/* Absolute circular difference between two azimuths, 0..180. */
static float az_diff(float a, float b)
{
    float d = fmodf(fabsf(a - b), 360.0f);
    return d > 180.0f ? 360.0f - d : d;
}

/* Synthesize a plane wave from az_true and return the last azimuth the engine
 * reports; asserts that at least one valid estimate was produced. */
static float doa_measure_azimuth(int mic_num, const float mic_pos[][2], float az_true)
{
    esp_apa_doa_cfg_t cfg = ESP_APA_DOA_CFG_DEFAULT();
    cfg.sample_rate_hz = FS_HZ;
    cfg.mic_num = mic_num;
    memcpy(cfg.mic_pos, mic_pos, sizeof(float) * (size_t)mic_num * 2);

    esp_apa_doa_handle_t doa = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, esp_apa_doa_create(&cfg, &doa));
    uint16_t chunk = 0;
    TEST_ASSERT_EQUAL(ESP_OK, esp_apa_doa_get_frame_samples(doa, &chunk));

    /* Exact per-mic delay: mics closer to the source receive earlier, so
     * x_m(t) = s(t + (p_m . d)/c) for source direction d = (sin, cos). */
    const float th = az_true * (float)M_PI / 180.0f;
    const float dx = sinf(th), dy = cosf(th);
    float delay[ESP_APA_DOA_MAX_MICS];
    for (int m = 0; m < mic_num; m++) {
        delay[m] = (mic_pos[m][0] * dx + mic_pos[m][1] * dy) / SOUND_C;
    }

    /* Broadband source: tones spanning the analysis band, same phase on every
     * mic (phase does not bias the delay estimate). */
    float freq[NUM_TONES];
    for (int k = 0; k < NUM_TONES; k++) {
        freq[k] = 350.0f + (2450.0f - 350.0f) * (float)k / (float)(NUM_TONES - 1);
    }
    const float amp = 0.9f * 32767.0f / (float)NUM_TONES; /* peak-bounded */

    int16_t *pcm = malloc((size_t)chunk * (size_t)mic_num * sizeof(int16_t));
    TEST_ASSERT_NOT_NULL(pcm);

    esp_apa_doa_result_t res = {0};
    bool got = false;
    float az = 0.0f;
    for (int c = 0; c < FEED_CHUNKS; c++) {
        for (int i = 0; i < chunk; i++) {
            const float t = (float)(c * chunk + i) / FS_HZ;
            for (int m = 0; m < mic_num; m++) {
                float s = 0.0f;
                for (int k = 0; k < NUM_TONES; k++) {
                    s += cosf(2.0f * (float)M_PI * freq[k] * (t + delay[m]));
                }
                pcm[i * mic_num + m] = (int16_t)lrintf(s * amp);
            }
        }
        if (esp_apa_doa_process(doa, pcm, (size_t)mic_num * (size_t)chunk, &res) == ESP_OK && res.valid) {
            got = true;
            az = res.azimuth_deg;
        }
        vTaskDelay(1); /* yield so the idle task can feed the watchdog */
    }
    free(pcm);
    esp_apa_doa_destroy(doa);

    TEST_ASSERT_TRUE_MESSAGE(got, "no valid DOA estimate produced");
    return az;
}

static void check_angle(int mic_num, const float mic_pos[][2],
                        float true_az, float expect_az, float tol)
{
    const float got = doa_measure_azimuth(mic_num, mic_pos, true_az);
    ESP_LOGI(TAG, "true=%6.1f expect=%6.1f got=%6.1f", true_az, expect_az, got);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(tol, 0.0f, az_diff(got, expect_az),
                                     "azimuth out of tolerance");
}

/* ------------------------------------------------------------------ */
/* Basic API                                                          */
/* ------------------------------------------------------------------ */

TEST_CASE("esp_apa_doa: basic API create/process/destroy", "[esp_apa_doa][api]")
{
    /* Invalid configs are rejected. */
    esp_apa_doa_cfg_t bad = {0};
    esp_apa_doa_handle_t h = NULL;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, esp_apa_doa_create(&bad, &h));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, esp_apa_doa_create(NULL, &h));

    esp_apa_doa_cfg_t cfg = ESP_APA_DOA_CFG_DEFAULT();
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, esp_apa_doa_create(&cfg, NULL));

    /* Valid create exposes chunk size. */
    TEST_ASSERT_EQUAL(ESP_OK, esp_apa_doa_create(&cfg, &h));
    TEST_ASSERT_NOT_NULL(h);
    uint16_t chunk = 0;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, esp_apa_doa_get_frame_samples(h, NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, esp_apa_doa_get_frame_samples(NULL, &chunk));
    TEST_ASSERT_EQUAL(ESP_OK, esp_apa_doa_get_frame_samples(h, &chunk));
    TEST_ASSERT_GREATER_THAN(0, chunk);

    /* process() rejects malformed input. */
    int16_t pcm[4] = {0};
    esp_apa_doa_result_t out;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_SIZE, esp_apa_doa_process(h, pcm, 1, &out));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, esp_apa_doa_process(NULL, pcm, sizeof(pcm), &out));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, esp_apa_doa_process(h, NULL, sizeof(pcm), &out));

    TEST_ASSERT_EQUAL(ESP_OK, esp_apa_doa_reset(h));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, esp_apa_doa_reset(NULL));

    TEST_ASSERT_EQUAL(ESP_OK, esp_apa_doa_destroy(h));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, esp_apa_doa_destroy(NULL));
}

/* ------------------------------------------------------------------ */
/* Angle accuracy: 2-mic (0..180, front hemisphere; expect = 90 + true) */
/* ------------------------------------------------------------------ */

TEST_CASE("esp_apa_doa: 2-mic azimuth front-left",  "[esp_apa_doa][angle]") { check_angle(2, MIC_2, -45.0f, 45.0f,  15.0f); }
TEST_CASE("esp_apa_doa: 2-mic azimuth front",       "[esp_apa_doa][angle]") { check_angle(2, MIC_2,   0.0f, 90.0f,  15.0f); }
TEST_CASE("esp_apa_doa: 2-mic azimuth front-right", "[esp_apa_doa][angle]") { check_angle(2, MIC_2,  45.0f, 135.0f, 15.0f); }

/* ------------------------------------------------------------------ */
/* Angle accuracy: 3-mic / 4-mic (full 360; expect = true)            */
/* ------------------------------------------------------------------ */

TEST_CASE("esp_apa_doa: 3-mic azimuth 0",   "[esp_apa_doa][angle]") { check_angle(3, MIC_3,   0.0f,   0.0f, 20.0f); }
TEST_CASE("esp_apa_doa: 3-mic azimuth 90",  "[esp_apa_doa][angle]") { check_angle(3, MIC_3,  90.0f,  90.0f, 20.0f); }
TEST_CASE("esp_apa_doa: 3-mic azimuth 180", "[esp_apa_doa][angle]") { check_angle(3, MIC_3, 180.0f, 180.0f, 20.0f); }
TEST_CASE("esp_apa_doa: 3-mic azimuth 270", "[esp_apa_doa][angle]") { check_angle(3, MIC_3, 270.0f, 270.0f, 20.0f); }

TEST_CASE("esp_apa_doa: 4-mic azimuth 0",   "[esp_apa_doa][angle]") { check_angle(4, MIC_4,   0.0f,   0.0f, 20.0f); }
TEST_CASE("esp_apa_doa: 4-mic azimuth 90",  "[esp_apa_doa][angle]") { check_angle(4, MIC_4,  90.0f,  90.0f, 20.0f); }
TEST_CASE("esp_apa_doa: 4-mic azimuth 225", "[esp_apa_doa][angle]") { check_angle(4, MIC_4, 225.0f, 225.0f, 20.0f); }
