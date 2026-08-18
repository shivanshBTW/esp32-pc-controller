# ESP32-S3 Remote PC Controller (WakeType)
cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED ENV{ESP_MATTER_PATH})
    set(ENV{ESP_MATTER_PATH} "$ENV{HOME}/esp/esp-matter")
endif()

if(NOT EXISTS "$ENV{ESP_MATTER_PATH}/components/esp_matter")
    message(FATAL_ERROR "ESP-Matter SDK not found at $ENV{ESP_MATTER_PATH}. See README Matter section.")
endif()

set(ESP_MATTER_PATH $ENV{ESP_MATTER_PATH})
set(MATTER_SDK_PATH ${ESP_MATTER_PATH}/connectedhomeip/connectedhomeip)
set(PROJECT_VER "0.3.0-dev")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
if(EXISTS "${ESP_MATTER_PATH}/examples/common/cmake_common/components_include.cmake")
    include(${ESP_MATTER_PATH}/examples/common/cmake_common/components_include.cmake)
endif()

set(EXTRA_COMPONENT_DIRS
    "${MATTER_SDK_PATH}/config/esp32/components"
    "${ESP_MATTER_PATH}/components"
    ${extra_components_dirs_append})

project(esp32_pc_controller)

idf_build_set_property(CXX_COMPILE_OPTIONS "-std=gnu++17;-Os;-DCHIP_HAVE_CONFIG_H;-Wno-overloaded-virtual" APPEND)
idf_build_set_property(C_COMPILE_OPTIONS "-Os" APPEND)
idf_build_set_property(COMPILE_OPTIONS "-Wno-format-nonliteral;-Wno-format-security" APPEND)
