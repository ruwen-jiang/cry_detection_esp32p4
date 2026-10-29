# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: Apache-2.0

idf_component_register(INCLUDE_DIRS "include")

add_prebuilt_library(esp_apa_cry_detection_prebuilt
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/${CONFIG_IDF_TARGET}/libesp_apa_cry_detection.a"
    PRIV_REQUIRES espressif__gmf_fft
)
target_link_libraries(${COMPONENT_LIB} INTERFACE esp_apa_cry_detection_prebuilt)
target_link_libraries(${COMPONENT_LIB} INTERFACE m)
target_link_options(${COMPONENT_LIB} INTERFACE
    "-Wl,--wrap=esp_gmf_fft_calloc_aligned"
    "-Wl,-u,__wrap_esp_gmf_fft_calloc_aligned"
)
