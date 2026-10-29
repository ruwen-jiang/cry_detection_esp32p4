# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: Apache-2.0

idf_component_register(INCLUDE_DIRS "include")

get_filename_component(BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR} NAME)
add_prebuilt_library(${BASE_DIR} "${CMAKE_CURRENT_SOURCE_DIR}/libs/${CONFIG_IDF_TARGET}/libesp_apa_doa.a"
                     PRIV_REQUIRES ${BASE_DIR})
target_link_libraries(${COMPONENT_LIB} INTERFACE "-L ${CMAKE_CURRENT_SOURCE_DIR}/libs/${CONFIG_IDF_TARGET}")
target_link_libraries(${COMPONENT_LIB} INTERFACE esp_apa_doa)
