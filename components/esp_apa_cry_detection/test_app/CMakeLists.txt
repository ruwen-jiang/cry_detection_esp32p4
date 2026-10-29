# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.16)

get_filename_component(ESP_APA_CRY_DETECTION_ROOT "${CMAKE_SOURCE_DIR}/.." ABSOLUTE)
set(EXTRA_COMPONENT_DIRS "${ESP_APA_CRY_DETECTION_ROOT}")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp_apa_cry_detection_test)
