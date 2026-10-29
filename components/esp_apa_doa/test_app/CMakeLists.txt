cmake_minimum_required(VERSION 3.16)

get_filename_component(ESP_APA_DOA_ROOT "${CMAKE_SOURCE_DIR}/.." ABSOLUTE)
set(EXTRA_COMPONENT_DIRS "${ESP_APA_DOA_ROOT}")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp_apa_doa_test)
