# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# esp-sr pulls latest esp-dl. Its BMP headers use
# `typedef struct { T x{...}; } name;` which GCC 15 / C++26
# (IDF latest, -Werror) rejects as -Wnon-c-typedef-for-linkage.
# -Wno- form is ignored on older GCC that do not know the warning.
idf_build_set_property(CXX_COMPILE_OPTIONS "-Wno-non-c-typedef-for-linkage" APPEND)

project(infant_cry_detection)
