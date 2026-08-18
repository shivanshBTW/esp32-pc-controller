idf_component_register(
    SRCS
        "main.c"
        "relay_controller.c"
        "pc_controller.c"
        "pc_state.c"
        "hid_controller.c"
        "network.c"
        "captive_dns.c"
        "nvs_prefs.c"
        "mdns_service.c"
        "local_api.c"
        "web_ui.c"
        "ota_http.c"
        "matter_controller.cpp"
        "diagnostics.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        nvs_flash
        esp_wifi
        esp_netif
        esp_event
        esp_http_server
        esp_timer
        esp_system
        app_update
        driver
        json
        mdns
        lwip
        bt
        esp_matter
)

# Optional local secrets.env (repo root), like a JS .env — never commit.
set(_wt_secrets "${CMAKE_SOURCE_DIR}/secrets.env")
set(_wt_device_key "")
set(_wt_force_key "0")
if(EXISTS "${_wt_secrets}")
    file(STRINGS "${_wt_secrets}" _wt_lines)
    foreach(_line IN LISTS _wt_lines)
        if(_line MATCHES "^[ \t]*#" OR _line MATCHES "^[ \t]*$")
            continue()
        endif()
        if(_line MATCHES "^[ \t]*WAKETYPE_DEVICE_KEY[ \t]*=[ \t]*(.*)$")
            set(_wt_device_key "${CMAKE_MATCH_1}")
            string(STRIP "${_wt_device_key}" _wt_device_key)
            # Strip optional surrounding quotes
            string(REGEX REPLACE "^\"(.*)\"$" "\\1" _wt_device_key "${_wt_device_key}")
            string(REGEX REPLACE "^'(.*)'$" "\\1" _wt_device_key "${_wt_device_key}")
        elseif(_line MATCHES "^[ \t]*WAKETYPE_FORCE_DEVICE_KEY[ \t]*=[ \t]*(.*)$")
            set(_wt_force_key "${CMAKE_MATCH_1}")
            string(STRIP "${_wt_force_key}" _wt_force_key)
        endif()
    endforeach()
endif()

if(_wt_device_key STREQUAL "")
    message(STATUS "WakeType: no secrets.env device key — firmware will generate a random key if NVS empty")
else()
    message(STATUS "WakeType: using device key from secrets.env (force=${_wt_force_key})")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE "WAKETYPE_DEVICE_KEY=\"${_wt_device_key}\"")
endif()

if(_wt_force_key STREQUAL "1" OR _wt_force_key STREQUAL "true" OR _wt_force_key STREQUAL "yes")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE WAKETYPE_FORCE_DEVICE_KEY=1)
endif()
