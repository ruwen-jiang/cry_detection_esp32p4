/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: Apache-2.0
 */

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_chip_info.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "unity.h"
#include "unity_test_runner.h"
#include "esp_apa_cry_detection.h"

static const char *TAG = "CRY_PERF";

#define FS_HZ             16000
#define CHUNK_SAMPLES     800
#define WINDOW_SAMPLES    16000
#define HOP_SAMPLES       2400  /* 150 ms algo hop */
#define HOP_PERIOD_US     150000
#define WINDOW_PERIOD_US  1000000  /* CPU-loading denominator: 1 s CNN window */
#define N_TIMED_HOPS      30

typedef struct {
    size_t  total8;
    size_t  intern;
    size_t  intern8;
    size_t  spiram;
    size_t  dma;
    size_t  intern_largest;
    size_t  spiram_largest;
} heap_snap_t;

typedef struct {
    int      n;
    int64_t  sum_us;
    int64_t  min_us;
    int64_t  max_us;
} hop_stats_t;

static void heap_snap(heap_snap_t *s)
{
    s->total8 = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    s->intern = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    s->intern8 = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    s->spiram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    s->dma = heap_caps_get_free_size(MALLOC_CAP_DMA);
    s->intern_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    s->spiram_largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
}

static void hop_stats_init(hop_stats_t *st)
{
    st->n = 0;
    st->sum_us = 0;
    st->min_us = INT64_MAX;
    st->max_us = 0;
}

static void hop_stats_add(hop_stats_t *st, int64_t us)
{
    if (us < 0) {
        us = 0;
    }
    st->n++;
    st->sum_us += us;
    if (us < st->min_us) {
        st->min_us = us;
    }
    if (us > st->max_us) {
        st->max_us = us;
    }
}

static int64_t hop_stats_avg(const hop_stats_t *st)
{
    return (st->n > 0) ? (st->sum_us / st->n) : 0;
}

static float cpu_load_pct(int64_t hop_us)
{
    return (float)hop_us * 100.0f / (float)WINDOW_PERIOD_US;
}

static void print_heap_snap(const char *label, const heap_snap_t *s)
{
    ESP_LOGI(TAG, "%s free: 8bit=%u intern=%u intern8=%u spiram=%u dma=%u  "
                  "largest intern=%u spiram=%u",
             label,
             (unsigned)s->total8, (unsigned)s->intern, (unsigned)s->intern8,
             (unsigned)s->spiram, (unsigned)s->dma,
             (unsigned)s->intern_largest, (unsigned)s->spiram_largest);
}

static void print_heap_delta(const char *label, const heap_snap_t *before,
                             const heap_snap_t *after)
{
    ESP_LOGI(TAG, "%s used (delta free): 8bit=%d intern=%d intern8=%d spiram=%d dma=%d",
             label,
             (int)before->total8 - (int)after->total8,
             (int)before->intern - (int)after->intern,
             (int)before->intern8 - (int)after->intern8,
             (int)before->spiram - (int)after->spiram,
             (int)before->dma - (int)after->dma);
}

static void print_expected_heap(void)
{
    /* Matches current open() / detection_create / apa_logmel_create. */
    ESP_LOGI(TAG, "expected heap (bytes, approx; SPIRAM targets use PSRAM only):");
    ESP_LOGI(TAG, "  ring 2s int16            64000  (PSRAM)");
    ESP_LOGI(TAG, "  window 1s int16          32000  (PSRAM)");
    ESP_LOGI(TAG, "  CNN q8_a/q8_b/conv_tmp   30720  (PSRAM)");
    ESP_LOGI(TAG, "  mel float 40x64          10240  (PSRAM)");
    ESP_LOGI(TAG, "  logmel raw+cache         20480  (PSRAM)");
    ESP_LOGI(TAG, "  logmel power 257 float    1028  (PSRAM)");
    ESP_LOGI(TAG, "  fft_buf 514 int16 (~16B align) ~1040 (PSRAM)");
    ESP_LOGI(TAG, "  gmf_fft plan/twiddle     ~1600  (PSRAM)");
    ESP_LOGI(TAG, "  handles (cry/det/logmel/fsm)  few hundred (PSRAM)");
    ESP_LOGI(TAG, "  model tables stay in .rodata (flash), not heap");
}

static void fill_tone(int16_t *dst, size_t n, int32_t *phase, int16_t amp)
{
    for (size_t i = 0; i < n; ++i) {
        dst[i] = (((*phase) / 32) & 1) ? amp : (int16_t)-amp;
        (*phase)++;
    }
}

static void print_hop_stats(const char *label, const hop_stats_t *st)
{
    int64_t avg = hop_stats_avg(st);
    ESP_LOGI(TAG, "%s: n=%d min=%" PRId64 " avg=%" PRId64 " max=%" PRId64 " us  "
                  "cpu-loading min/avg/max = %.2f / %.2f / %.2f %%  (vs %d ms window)",
             label, st->n, (st->n ? st->min_us : 0), avg, st->max_us,
             cpu_load_pct(st->n ? st->min_us : 0),
             cpu_load_pct(avg),
             cpu_load_pct(st->max_us),
             WINDOW_PERIOD_US / 1000);
}

TEST_CASE("cry perf: cpu load and heap", "[esp_apa_cry_detection][perf]")
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    ESP_LOGI(TAG, "=== esp_apa_cry_detection perf ===");
    ESP_LOGI(TAG, "target=%s cores=%d rev=%d cpu_freq_mhz=%d heap_total_8bit=%u",
             CONFIG_IDF_TARGET, chip.cores, chip.revision,
             CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
             (unsigned)heap_caps_get_total_size(MALLOC_CAP_8BIT));
    print_expected_heap();

    heap_snap_t h0, h_open, h_run, h_close;
    heap_snap(&h0);
    print_heap_snap("before open", &h0);

    esp_apa_cry_detection_config_t cfg = ESP_APA_CRY_DETECTION_DEFAULT_CONFIG();
    esp_apa_cry_detection_handle_t h = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, esp_apa_cry_detection_open(&cfg, &h));
    TEST_ASSERT_NOT_NULL(h);

    heap_snap(&h_open);
    print_heap_snap("after open", &h_open);
    print_heap_delta("open()", &h0, &h_open);

    int16_t chunk[CHUNK_SAMPLES];
    int32_t phase = 0;
    esp_apa_cry_detection_status_t st = {0};

    /* Fill until the first full-window hop (hop 150 ms, so 16000 is not a hop boundary). */
    int64_t first_hop_us = -1;
    int fill_iters = 0;
    while (st.inference_count < 1 && fill_iters < 40) {
        fill_tone(chunk, CHUNK_SAMPLES, &phase, 8000);
        uint32_t before = st.inference_count;
        int64_t t0 = esp_timer_get_time();
        TEST_ASSERT_EQUAL(ESP_OK, esp_apa_cry_detection_process(h, chunk, CHUNK_SAMPLES, &st));
        int64_t dt = esp_timer_get_time() - t0;
        if (st.inference_count > before && first_hop_us < 0) {
            first_hop_us = dt;
        }
        fill_iters++;
    }
    TEST_ASSERT_GREATER_OR_EQUAL(1, st.inference_count);
    TEST_ASSERT_TRUE(st.audio_active);
    ESP_LOGI(TAG, "first CNN hop (cold Log-Mel extract): %" PRId64 " us  cpu_load=%.2f %%",
             first_hop_us, cpu_load_pct(first_hop_us < 0 ? 0 : first_hop_us));

    hop_stats_t cnn = {0};
    hop_stats_t gate = {0};
    hop_stats_init(&cnn);
    hop_stats_init(&gate);

    int16_t *hop_tone = calloc(HOP_SAMPLES, sizeof(int16_t));
    int16_t *hop_zero = calloc(HOP_SAMPLES, sizeof(int16_t));
    TEST_ASSERT_NOT_NULL(hop_tone);
    TEST_ASSERT_NOT_NULL(hop_zero);

    for (int i = 0; i < N_TIMED_HOPS; ++i) {
        fill_tone(hop_tone, HOP_SAMPLES, &phase, 8000);
        uint32_t before = st.inference_count;
        int64_t t0 = esp_timer_get_time();
        TEST_ASSERT_EQUAL(ESP_OK, esp_apa_cry_detection_process(h, hop_tone, HOP_SAMPLES, &st));
        int64_t dt = esp_timer_get_time() - t0;
        TEST_ASSERT_EQUAL(before + 1, st.inference_count);
        TEST_ASSERT_TRUE(st.audio_active);
        hop_stats_add(&cnn, dt);
    }
    print_hop_stats("streaming CNN hop (gate open)", &cnn);

    /* Drain the 1 s window so RMS gate actually closes before timing. */
    memset(hop_zero, 0, HOP_SAMPLES * sizeof(int16_t));
    for (int i = 0; i < 8; ++i) {
        TEST_ASSERT_EQUAL(ESP_OK, esp_apa_cry_detection_process(h, hop_zero, HOP_SAMPLES, &st));
    }
    TEST_ASSERT_FALSE(st.audio_active);

    for (int i = 0; i < N_TIMED_HOPS; ++i) {
        uint32_t before = st.inference_count;
        int64_t t0 = esp_timer_get_time();
        TEST_ASSERT_EQUAL(ESP_OK, esp_apa_cry_detection_process(h, hop_zero, HOP_SAMPLES, &st));
        int64_t dt = esp_timer_get_time() - t0;
        TEST_ASSERT_EQUAL(before + 1, st.inference_count);
        hop_stats_add(&gate, dt);
    }
    print_hop_stats("gate-closed hop (CNN skipped)", &gate);

    free(hop_tone);
    free(hop_zero);
    hop_tone = NULL;
    hop_zero = NULL;

    heap_snap(&h_run);
    print_heap_snap("after hops", &h_run);
    print_heap_delta("process() extra vs open", &h_open, &h_run);

    TEST_ASSERT_EQUAL(ESP_OK, esp_apa_cry_detection_close(h));
    heap_snap(&h_close);
    print_heap_snap("after close", &h_close);
    print_heap_delta("leaked vs before open", &h0, &h_close);

    int64_t cnn_avg = hop_stats_avg(&cnn);
    int64_t gate_avg = hop_stats_avg(&gate);

    printf("\nCRY_PERF_BEGIN\n");
    printf("target=%s\n", CONFIG_IDF_TARGET);
    printf("cpu_mhz=%d\n", CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);
    printf("hop_ms=%d\n", HOP_PERIOD_US / 1000);
    printf("heap_open_8bit=%d\n", (int)h0.total8 - (int)h_open.total8);
    printf("heap_open_internal=%d\n", (int)h0.intern - (int)h_open.intern);
    printf("heap_open_internal8=%d\n", (int)h0.intern8 - (int)h_open.intern8);
    printf("heap_open_spiram=%d\n", (int)h0.spiram - (int)h_open.spiram);
    printf("heap_open_dma=%d\n", (int)h0.dma - (int)h_open.dma);
    printf("heap_process_extra_8bit=%d\n", (int)h_open.total8 - (int)h_run.total8);
    printf("first_cnn_hop_us=%" PRId64 "\n", first_hop_us);
    printf("first_cnn_cpu_pct=%.2f\n", cpu_load_pct(first_hop_us < 0 ? 0 : first_hop_us));
    printf("stream_cnn_n=%d\n", cnn.n);
    printf("stream_cnn_min_us=%" PRId64 "\n", cnn.min_us);
    printf("stream_cnn_avg_us=%" PRId64 "\n", cnn_avg);
    printf("stream_cnn_max_us=%" PRId64 "\n", cnn.max_us);
    printf("stream_cnn_cpu_pct=%.2f\n", cpu_load_pct(cnn_avg));
    printf("gate_n=%d\n", gate.n);
    printf("gate_min_us=%" PRId64 "\n", gate.min_us);
    printf("gate_avg_us=%" PRId64 "\n", gate_avg);
    printf("gate_max_us=%" PRId64 "\n", gate.max_us);
    printf("gate_cpu_pct=%.2f\n", cpu_load_pct(gate_avg));
    printf("CRY_PERF_END\n");

    TEST_ASSERT_EQUAL(N_TIMED_HOPS, cnn.n);
    TEST_ASSERT_EQUAL(N_TIMED_HOPS, gate.n);
    /* One hop must finish well inside the 150 ms hop period on P4. */
    TEST_ASSERT_LESS_THAN(HOP_PERIOD_US, cnn.max_us);
}
