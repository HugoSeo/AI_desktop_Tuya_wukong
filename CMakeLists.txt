# CMake build rules for tuyaos_demo_wukong_ai only.
# Conditionals mirror local.mk; options are read from build/tuya_app.config (Kconfig export)
# and optionally ../../build/tuya_iot.config (same format).

cmake_minimum_required(VERSION 3.10)

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER arm-none-eabi-g++)
set(CMAKE_READELF arm-none-eabi-readelf)
set(CMAKE_C_FLAGS "-g -Wall -mno-unaligned-access -ffunction-sections -fdata-sections -mcpu=cortex-m3 -std=gnu11 --specs=nosys.specs -Wno-unused-function -Wno-error=unused-variable")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --specs=nosys.specs")

project(tuyaos_demo_wukong_ai C CXX)

set(CMAKE_SYSTEM_PROCESSOR ARM)
set(ARCH arm)
set(CONFIG_CPU_ARM on)

set(LIBRARY_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../../libs)
set(ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../../lib)

if(NOT DEFINED APP_NAME)
    set(APP_NAME "tuyaos_demo_wukong_ai")
endif()
if(NOT DEFINED APP_VERSION)
    set(APP_VERSION "0.0.1")
endif()
if(DEFINED TUYAOS_SDK_VER)
    set(_TUYAOS_VER ${TUYAOS_SDK_VER})
else()
    set(_TUYAOS_VER 38)
endif()

message(STATUS ${CMAKE_C_COMPILER})
add_definitions(-DAPP_NAME="${APP_NAME}")
add_definitions(-DAPP_VERSION="${APP_VERSION}")
add_definitions(-DTUYAOS_VER=${_TUYAOS_VER})
add_compile_options(-Wno-missing-braces)

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------
set(APP_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(TUYA_OS_ROOT "${APP_DIR}/../..")
set(TUYA_SDK_INCLUDE_ROOT "${TUYA_OS_ROOT}/include")
set(TUYA_APP_CONFIG "${APP_DIR}/build/tuya_app.config")
set(TUYA_IOT_CONFIG "${TUYA_OS_ROOT}/build/tuya_iot.config")

# ---------------------------------------------------------------------------
# Kconfig (.config) loader -> TUYA_CFG_<KEY> (value: y, empty, or literal)
# ---------------------------------------------------------------------------
macro(tuya_kconfig_load _path)
    if(EXISTS "${_path}")
        file(STRINGS "${_path}" _tuya_k_lines)
        foreach(_ln ${_tuya_k_lines})
            if(_ln MATCHES "^CONFIG_([A-Za-z0-9_]+)=y$")
                set(TUYA_CFG_${CMAKE_MATCH_1} "y")
            elseif(_ln MATCHES "^# CONFIG_([A-Za-z0-9_]+) is not set$")
                set(TUYA_CFG_${CMAKE_MATCH_1} "")
            elseif(_ln MATCHES "^CONFIG_([A-Za-z0-9_]+)=(.*)$")
                set(TUYA_CFG_${CMAKE_MATCH_1} "${CMAKE_MATCH_2}")
            endif()
        endforeach()
    endif()
endmacro()

# App options first; IoT/board options may override if present
tuya_kconfig_load("${TUYA_APP_CONFIG}")
tuya_kconfig_load("${TUYA_IOT_CONFIG}")

if(NOT EXISTS "${TUYA_APP_CONFIG}")
    message(FATAL_ERROR "Missing ${TUYA_APP_CONFIG}. Generate it (menuconfig / build flow) before CMake.")
endif()

message(STATUS "tuya_app.config: ${TUYA_APP_CONFIG}")

# ---------------------------------------------------------------------------
# SDK headers under ../../include (packaged tree)
# ---------------------------------------------------------------------------
function(include_subdirectories root_dir)
    if(IS_DIRECTORY ${root_dir})
        include_directories(${root_dir})
    endif()
    file(GLOB ALL_SUB RELATIVE ${root_dir} ${root_dir}/*)
    foreach(sub ${ALL_SUB})
        if(IS_DIRECTORY ${root_dir}/${sub})
            # 检查是否是 lib_tflm 目录
            if("lib_tflm" MATCHES ${sub})
                # 如果是 lib_tflm，只添加 lib_tflm 和特定的子目录
                include_directories(${root_dir}/${sub})
                include_directories(${root_dir}/${sub}/third_party/flatbuffers/include)
                include_directories(${root_dir}/${sub}/third_party/gemmlowp)
            elseif(NOT (".git" MATCHES ${sub} OR "output" MATCHES ${sub}))
                # 如果不是 lib_tflm，递归地包含其他子目录
                include_subdirectories(${root_dir}/${sub})
            endif()
        endif()
    endforeach()
endfunction()

include_subdirectories("${TUYA_SDK_INCLUDE_ROOT}")

# ---------------------------------------------------------------------------
# Helpers: collect sources (macros so the output list name is passed by reference)
# ---------------------------------------------------------------------------
macro(wukong_append_sources out_var)
    foreach(_wuk_p ${ARGN})
        if(IS_DIRECTORY "${_wuk_p}")
            file(GLOB_RECURSE _wuk_g "${_wuk_p}/*.c" "${_wuk_p}/*.cc" "${_wuk_p}/*.cpp")
            list(APPEND ${out_var} ${_wuk_g})
        elseif(EXISTS "${_wuk_p}")
            list(APPEND ${out_var} "${_wuk_p}")
        endif()
    endforeach()
endmacro()

macro(wukong_append_sources_maxdepth1 out_var _dir)
    if(IS_DIRECTORY "${_dir}")
        file(GLOB _wuk_g1 "${_dir}/*.c" "${_dir}/*.cc" "${_dir}/*.cpp")
        list(APPEND ${out_var} ${_wuk_g1})
    endif()
endmacro()

set(APP_SRC "")
set(APP_INC "")

# ---------------------------------------------------------------------------
# Base include dirs (local.mk LOCAL_TUYA_SDK_INC, always-on portion)
# ---------------------------------------------------------------------------
list(APPEND APP_INC
    "${APP_DIR}/include"
    "${APP_DIR}/src/boards"
    "${APP_DIR}/src/drivers/app_tuya_driver/include"
    "${APP_DIR}/src/drivers/app_tuya_key/include"
    "${APP_DIR}/src/drivers/app_tuya_led/include"
    "${APP_DIR}/src/miscs/uart_codec/include"
    "${APP_DIR}/src/miscs/uart_codec/include/gfw_core"
    "${APP_DIR}/src/miscs/uart_codec/include/gfw_mcu_ota"
    "${APP_DIR}/src/miscs/uart_codec/src/gfw_core"
    "${APP_DIR}/src/miscs/battery"
#    "${APP_DIR}/src/miscs/mftest"
    "${APP_DIR}/src/miscs/audio_analysis"
    "${APP_DIR}/src/miscs/audio_player"
    "${APP_DIR}/src/miscs/audio_player/include"
    "${APP_DIR}/src/miscs/audio_player/src/decoder/helix/include"
    "${APP_DIR}/src/mode"
    "${APP_DIR}/src/wukong"
    "${APP_DIR}/src/wukong/audio"
    "${APP_DIR}/src/wukong/audio/input"
    "${APP_DIR}/src/wukong/audio/output"
    "${APP_DIR}/src/wukong/assets"
    "${APP_DIR}/src/wukong/skills"
    "${APP_DIR}/src/wukong/audio/frontend"
    "${APP_DIR}/src/wukong/audio/frontend/aec_vad/tuya"
    "${APP_DIR}/src/wukong/audio/frontend/kws"
    "${APP_DIR}/src/wukong/mcp"
    "${APP_DIR}/src/wukong/provider"
    "${APP_DIR}/src/wukong/channel"
    "${APP_DIR}/src/wukong/picture"
    "${APP_DIR}/src/wukong/cron"
    "${APP_DIR}/src/wukong/tm"
    "${APP_DIR}/src/wukong/utility"
)

if(TUYA_CFG_AI_PLAYER_DECODER_OGGOPUS_ENABLE STREQUAL "y" OR TUYA_CFG_AI_PLAYER_DECODER_OPUS_ENABLE STREQUAL "y")
    list(APPEND APP_INC "${APP_DIR}/src/miscs/audio_player/src/decoder/opus")
endif()
if(TUYA_CFG_AI_PLAYER_DECODER_OGGOPUS_ENABLE STREQUAL "y")
    list(APPEND APP_INC "${APP_DIR}/src/miscs/audio_player/src/decoder/ogg/include")
endif()

# ---------------------------------------------------------------------------
# Board selection (mutually exclusive in practice)
# ---------------------------------------------------------------------------
set(WUKONG_BOARD_SUBDIR "")
if(TUYA_CFG_T5AI_BOARD STREQUAL "y")
    set(WUKONG_BOARD_SUBDIR "T5AI_BOARD")
elseif(TUYA_CFG_T5AI_BOARD_EVB STREQUAL "y")
    set(WUKONG_BOARD_SUBDIR "T5AI_BOARD_EVB")
elseif(TUYA_CFG_T5AI_BOARD_EVB_PRO STREQUAL "y")
    set(WUKONG_BOARD_SUBDIR "T5AI_BOARD_EVB_PRO")
elseif(TUYA_CFG_T5AI_BOARD_EYES STREQUAL "y")
    set(WUKONG_BOARD_SUBDIR "T5AI_BOARD_EYES")
elseif(TUYA_CFG_T5AI_BOARD_ROBOT STREQUAL "y")
    set(WUKONG_BOARD_SUBDIR "T5AI_BOARD_ROBOT")
elseif(TUYA_CFG_T5AI_BOARD_DESKTOP STREQUAL "y")
    set(WUKONG_BOARD_SUBDIR "T5AI_BOARD_DESKTOP")
elseif(TUYA_CFG_T2AI_BOARD STREQUAL "y")
    set(WUKONG_BOARD_SUBDIR "T2AI_BOARD")
elseif(TUYA_CFG_LE270AI_BOARD STREQUAL "y")
    set(WUKONG_BOARD_SUBDIR "LE270AI_BOARD")
elseif(TUYA_CFG_L511_Y6E_BOARD STREQUAL "y")
    set(WUKONG_BOARD_SUBDIR "L511_Y6E_BOARD")
elseif(TUYA_CFG_L511_Y7PM_BOARD STREQUAL "y")
    set(WUKONG_BOARD_SUBDIR "L511_Y7PM_BOARD")
elseif(TUYA_CFG_WUKONG_BOARD_UBUNTU STREQUAL "y")
    set(WUKONG_BOARD_SUBDIR "Ubuntu")
endif()

if(WUKONG_BOARD_SUBDIR STREQUAL "T5AI_BOARD")
    list(APPEND APP_INC "${APP_DIR}/src/boards/T5AI_BOARD")
    list(APPEND APP_SRC "${APP_DIR}/src/boards/T5AI_BOARD/tuya_device_board.c")
elseif(WUKONG_BOARD_SUBDIR STREQUAL "T5AI_BOARD_EVB")
    list(APPEND APP_INC "${APP_DIR}/src/boards/T5AI_BOARD_EVB")
    list(APPEND APP_SRC "${APP_DIR}/src/boards/T5AI_BOARD_EVB/tuya_device_board.c")
elseif(WUKONG_BOARD_SUBDIR STREQUAL "T5AI_BOARD_EVB_PRO")
    list(APPEND APP_INC "${APP_DIR}/src/boards/T5AI_BOARD_EVB_PRO")
    list(APPEND APP_SRC "${APP_DIR}/src/boards/T5AI_BOARD_EVB_PRO/tuya_device_board.c")
elseif(WUKONG_BOARD_SUBDIR STREQUAL "T5AI_BOARD_EYES")
    list(APPEND APP_INC "${APP_DIR}/src/boards/T5AI_BOARD_EYES")
    list(APPEND APP_SRC "${APP_DIR}/src/boards/T5AI_BOARD_EYES/tuya_device_board.c")
elseif(WUKONG_BOARD_SUBDIR STREQUAL "T5AI_BOARD_ROBOT")
    list(APPEND APP_INC "${APP_DIR}/src/boards/T5AI_BOARD_ROBOT")
    list(APPEND APP_INC "${APP_DIR}/src/miscs/servo_ctrl")
    list(APPEND APP_INC "${APP_DIR}/src/miscs/gesture")
    list(APPEND APP_SRC "${APP_DIR}/src/boards/T5AI_BOARD_ROBOT/tuya_device_board.c")
elseif(WUKONG_BOARD_SUBDIR STREQUAL "T5AI_BOARD_DESKTOP")
    list(APPEND APP_INC "${APP_DIR}/src/boards/T5AI_BOARD_DESKTOP")
    list(APPEND APP_INC "${APP_DIR}/src/miscs/motion")
    list(APPEND APP_INC "${APP_DIR}/src/drivers/app_tuya_imu/include")
    list(APPEND APP_SRC "${APP_DIR}/src/boards/T5AI_BOARD_DESKTOP/tuya_device_board.c")
    list(APPEND APP_SRC "${APP_DIR}/src/boards/T5AI_BOARD_DESKTOP/tuya_device_camera.c")
    wukong_append_sources(APP_SRC "${APP_DIR}/src/drivers/app_tuya_imu")
    wukong_append_sources(APP_SRC "${APP_DIR}/src/miscs/motion")
elseif(WUKONG_BOARD_SUBDIR STREQUAL "T2AI_BOARD")
    list(APPEND APP_INC "${APP_DIR}/src/boards/T2AI_BOARD")
    list(APPEND APP_SRC "${APP_DIR}/src/boards/T2AI_BOARD/tuya_device_board.c")
elseif(WUKONG_BOARD_SUBDIR STREQUAL "LE270AI_BOARD")
    list(APPEND APP_INC "${APP_DIR}/src/boards/LE270AI_BOARD")
    list(APPEND APP_SRC "${APP_DIR}/src/boards/LE270AI_BOARD/tuya_device_board.c")
    list(APPEND APP_SRC "${APP_DIR}/src/boards/LE270AI_BOARD/tuya_cellular_cloud.c")
elseif(WUKONG_BOARD_SUBDIR STREQUAL "L511_Y6E_BOARD")
    list(APPEND APP_INC "${APP_DIR}/src/boards/L511_Y6E_BOARD")
    list(APPEND APP_SRC "${APP_DIR}/src/boards/L511_Y6E_BOARD/tuya_device_board.c")
    list(APPEND APP_SRC "${APP_DIR}/src/boards/L511_Y6E_BOARD/tuya_cellular_cloud.c")
elseif(WUKONG_BOARD_SUBDIR STREQUAL "L511_Y7PM_BOARD")
    list(APPEND APP_INC "${APP_DIR}/src/boards/L511_Y7PM_BOARD")
    list(APPEND APP_SRC "${APP_DIR}/src/boards/L511_Y7PM_BOARD/tuya_device_board.c")
    list(APPEND APP_SRC "${APP_DIR}/src/boards/L511_Y7PM_BOARD/tuya_cellular_cloud.c")
elseif(WUKONG_BOARD_SUBDIR STREQUAL "Ubuntu")
    list(APPEND APP_INC "${APP_DIR}/src/boards/Ubuntu")
    list(APPEND APP_SRC "${APP_DIR}/src/boards/Ubuntu/tuya_device_board.c")
else()
    message(WARNING "No board enabled in tuya_app.config (expected one CONFIG_*_BOARD=y).")
endif()

if(WUKONG_BOARD_SUBDIR)
    message(STATUS "Board: ${WUKONG_BOARD_SUBDIR} (from tuya_app.config)")
endif()

# ---------------------------------------------------------------------------
# Cellular dongle
# ---------------------------------------------------------------------------
if(TUYA_CFG_ENABLE_CELLULAR_DONGLE STREQUAL "y")
    list(APPEND APP_INC "${APP_DIR}/src/miscs/cellular")
    wukong_append_sources(APP_SRC "${APP_DIR}/src/miscs/cellular")
endif()

# ---------------------------------------------------------------------------
# Core application sources (local.mk)
# ---------------------------------------------------------------------------
list(APPEND APP_SRC
    "${APP_DIR}/src/tuya_app_main.c"
    "${APP_DIR}/src/tuya_ai_toy.c"
    "${APP_DIR}/src/tuya_ai_toy_led.c"
    "${APP_DIR}/src/tuya_ai_toy_key.c"
#    "${APP_DIR}/src/drivers/app_tuya_driver/src/os/tal_gpio.c"
#    "${APP_DIR}/src/drivers/app_tuya_driver/src/os/tal_uart.c"
)
# local.mk uses tuya_ai_toy_mcp.c; this tree may use wukong/mcp/*.c instead
if(EXISTS "${APP_DIR}/src/tuya_ai_toy_mcp.c")
    list(APPEND APP_SRC "${APP_DIR}/src/tuya_ai_toy_mcp.c")
else()
    wukong_append_sources_maxdepth1(APP_SRC "${APP_DIR}/src/wukong/mcp")
endif()
wukong_append_sources(APP_SRC "${APP_DIR}/src/drivers/app_tuya_key")
wukong_append_sources(APP_SRC "${APP_DIR}/src/drivers/app_tuya_led")
wukong_append_sources(APP_SRC "${APP_DIR}/src/mode")
wukong_append_sources_maxdepth1(APP_SRC "${APP_DIR}/src/wukong")
wukong_append_sources(APP_SRC "${APP_DIR}/src/wukong/provider")
wukong_append_sources(APP_SRC "${APP_DIR}/src/wukong/channel")
wukong_append_sources(APP_SRC "${APP_DIR}/src/wukong/assets")
wukong_append_sources(APP_SRC "${APP_DIR}/src/wukong/cron")
# Match local.mk: only tm/*.c (not tm/tests — host/unit tests)
wukong_append_sources_maxdepth1(APP_SRC "${APP_DIR}/src/wukong/tm")
wukong_append_sources(APP_SRC "${APP_DIR}/src/wukong/skills")

if(TUYA_CFG_ENABLE_TUYA_TOOLKITS STREQUAL "y")
    if(TUYA_CFG_ENABLE_TOOLKITS_CONTROL STREQUAL "y")
        list(APPEND APP_SRC "${APP_DIR}/src/wukong/mcp/tools/mcp_tool_control.c")
    endif()
    if(TUYA_CFG_ENABLE_TOOLKITS_TM STREQUAL "y")
        list(APPEND APP_SRC "${APP_DIR}/src/wukong/mcp/tools/mcp_tool_tm.c")
    endif()
    if(TUYA_CFG_ENABLE_TOOLKITS_CAMERA STREQUAL "y")
        list(APPEND APP_SRC "${APP_DIR}/src/wukong/mcp/tools/mcp_tool_camera.c")
    endif()
    if(TUYA_CFG_ENABLE_TOOLKITS_MOTION STREQUAL "y")
        list(APPEND APP_SRC "${APP_DIR}/src/wukong/mcp/tools/mcp_tool_motion.c")
    endif()
    if(TUYA_CFG_ENABLE_TOOLKITS_IMM STREQUAL "y")
        list(APPEND APP_SRC "${APP_DIR}/src/wukong/mcp/tools/mcp_tool_imm.c")
    endif()
    if(TUYA_CFG_ENABLE_TOOLKITS_SOCIAL STREQUAL "y")
        list(APPEND APP_SRC "${APP_DIR}/src/wukong/mcp/tools/mcp_tool_social.c")
    endif()
    if(TUYA_CFG_ENABLE_TOOLKITS_PLAYBACK STREQUAL "y")
        list(APPEND APP_SRC "${APP_DIR}/src/wukong/mcp/tools/mcp_tool_playback.c")
    endif()
endif()

if(TUYA_CFG_ENABLE_TUYA_PICTUR STREQUAL "y")
    wukong_append_sources(APP_SRC "${APP_DIR}/src/wukong/picture")
endif()

wukong_append_sources(APP_SRC "${APP_DIR}/src/wukong/utility")
list(APPEND APP_SRC
    "${APP_DIR}/src/wukong/audio/frontend/kws/wukong_kws.c"
    "${APP_DIR}/src/wukong/audio/frontend/wukong_audio_frontend.c"
    "${APP_DIR}/src/wukong/audio/wukong_audio_player.c"
    "${APP_DIR}/src/wukong/audio/wukong_playback_ctrl.c"
    "${APP_DIR}/src/wukong/audio/input/wukong_audio_input.c"
    "${APP_DIR}/src/wukong/audio/output/wukong_audio_output.c"
)

if(TUYA_CFG_ENABLE_BATTERY STREQUAL "y")
    wukong_append_sources(APP_SRC "${APP_DIR}/src/miscs/battery")
endif()

#if(NOT (TUYA_CFG_WUKONG_BOARD_UBUNTU STREQUAL "y"))
#    wukong_append_sources(APP_SRC "${APP_DIR}/src/miscs/mftest")
#endif()

# ---------------------------------------------------------------------------
# QR code
# ---------------------------------------------------------------------------
if(TUYA_CFG_ENABLE_QRCODE_ACTIVE STREQUAL "y")
    list(APPEND APP_INC "${APP_DIR}/src/miscs/libqrencode")
    wukong_append_sources(APP_SRC "${APP_DIR}/src/miscs/libqrencode")
endif()

# ---------------------------------------------------------------------------
# UART audio output / input
# ---------------------------------------------------------------------------
if(TUYA_CFG_USING_UART_AUDIO_OUTPUT STREQUAL "y")
    list(APPEND APP_SRC "${APP_DIR}/src/wukong/audio/output/wukong_audio_output_uart.c")
endif()

if(TUYA_CFG_USING_UART_AUDIO_INPUT STREQUAL "y")
    list(APPEND APP_SRC "${APP_DIR}/src/wukong/audio/input/wukong_audio_input_uart.c")
    wukong_append_sources(APP_SRC "${APP_DIR}/src/miscs/uart_codec/src")
    wukong_append_sources(APP_SRC "${APP_DIR}/src/wukong/audio/frontend/kws/uart")
endif()

# ---------------------------------------------------------------------------
# Board audio output / input
# ---------------------------------------------------------------------------
if(TUYA_CFG_USING_BOARD_AUDIO_OUTPUT STREQUAL "y")
    list(APPEND APP_SRC "${APP_DIR}/src/wukong/audio/output/wukong_audio_output_board.c")
endif()

if(TUYA_CFG_USING_BOARD_AUDIO_INPUT STREQUAL "y")
    if(TUYA_CFG_TUYA_MODULE_T5 STREQUAL "y")
        file(MAKE_DIRECTORY "${TUYA_OS_ROOT}/libs")
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${APP_DIR}/src/wukong/audio/frontend/kws/sndx/libsndxasr.a"
                "${TUYA_OS_ROOT}/libs/libsndxasr.a"
        )
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${APP_DIR}/src/wukong/audio/frontend/kws/tutuclear/libtutuClear.a"
                "${TUYA_OS_ROOT}/libs/libtutuClear.a"
        )
        wukong_append_sources(APP_SRC "${APP_DIR}/src/wukong/audio/frontend/kws/tutuclear")
        wukong_append_sources(APP_SRC "${APP_DIR}/src/wukong/audio/frontend/kws/sndx")
        wukong_append_sources(APP_SRC "${APP_DIR}/src/miscs/audio_analysis")
    endif()
    list(APPEND APP_SRC "${APP_DIR}/src/wukong/audio/frontend/aec_vad/tuya/wukong_audio_aec_vad.c")
    list(APPEND APP_SRC "${APP_DIR}/src/wukong/audio/input/wukong_audio_input_board.c")
endif()

# ---------------------------------------------------------------------------
# Audio player (decoder set from config)
# ---------------------------------------------------------------------------
file(GLOB_RECURSE _ap_all "${APP_DIR}/src/miscs/audio_player/*.c" "${APP_DIR}/src/miscs/audio_player/*.cc" "${APP_DIR}/src/miscs/audio_player/*.cpp")
list(FILTER _ap_all EXCLUDE REGEX "/src/decoder/ogg/")
list(FILTER _ap_all EXCLUDE REGEX "decoder_oggopus\\.c$")
list(FILTER _ap_all EXCLUDE REGEX "decoder_opus\\.c$")
list(APPEND APP_SRC ${_ap_all})

if(TUYA_CFG_AI_PLAYER_DECODER_OGGOPUS_ENABLE STREQUAL "y")
    wukong_append_sources(APP_SRC "${APP_DIR}/src/miscs/audio_player/src/decoder/ogg")
    list(APPEND APP_SRC "${APP_DIR}/src/miscs/audio_player/src/decoder/decoder_oggopus.c")
endif()
if(TUYA_CFG_AI_PLAYER_DECODER_OPUS_ENABLE STREQUAL "y")
    wukong_append_sources(APP_SRC "${APP_DIR}/src/miscs/audio_player/src/decoder/opus")
    list(APPEND APP_SRC "${APP_DIR}/src/miscs/audio_player/src/decoder/decoder_opus.c")
endif()

# ---------------------------------------------------------------------------
# UI / LVGL / touch / display (CONFIG_ENABLE_TUYA_UI)
# ---------------------------------------------------------------------------
set(WUKONG_TUYA_LVGL_VER "8")
if(TUYA_CFG_ENABLE_TUYA_UI STREQUAL "y")
    if(TUYA_CFG_TUYA_LVGL_VERSION STREQUAL "9")
        set(WUKONG_TUYA_LVGL_VER "9")
    endif()

    list(APPEND APP_INC
        "${APP_DIR}/src/drivers/app_tuya_display/tdd_lcd_driver/include"
        "${APP_DIR}/src/drivers/app_tuya_display/tal_display/include"
        "${APP_DIR}/src/drivers/app_tuya_tp/tdd_tp_driver/include"
        "${APP_DIR}/src/drivers/app_tuya_tp/tal_tp/include"
        "${APP_DIR}/src/miscs/gui/display"
        "${APP_DIR}/src/miscs/gui/lvgl/include"
        "${APP_DIR}/src/miscs/gui/tuya_lvgl/include"
        "${APP_DIR}/src/miscs/gui/tuya_lvgl/src/language/env_data"
        "${APP_DIR}/src/miscs/gui/tuya_lvgl_dp2widget/include"
        "${APP_DIR}/src/miscs/gui/tuya_lvgl_fs_compatible/include"
        "${APP_DIR}/src/miscs/gui/tuya_lvgl_gesture/inc"
        "${APP_DIR}/src/miscs/gui/tuya_lvgl_gesture/include"
        "${APP_DIR}/src/miscs/gui/tuya_lvgl_weather/include"
        "${APP_DIR}/src/miscs/gui/tuya_lvgl_cloud_recipe/include"
        "${APP_DIR}/src/miscs/gui/tuya_lvgl_cloud_recipe/src/kepler_file"
        "${APP_DIR}/src/miscs/gui/tuya_lvgl_resource_download/include"
    )

    list(APPEND APP_SRC
        "${APP_DIR}/src/miscs/gui/display/tuya_ai_display.c"
        "${APP_DIR}/src/miscs/gui/display/tuya_app_gui_main.c"
        "${APP_DIR}/src/miscs/gui/display/gui_common.c"
        "${APP_DIR}/src/miscs/gui/display/lv_mf_test.c"
        "${APP_DIR}/src/drivers/app_tuya_driver/src/os/tkl_touch.c"
    )
    wukong_append_sources(APP_SRC "${APP_DIR}/src/drivers/app_tuya_display/tdd_lcd_driver/src")
    wukong_append_sources(APP_SRC "${APP_DIR}/src/drivers/app_tuya_display/tal_display/src")
    wukong_append_sources(APP_SRC "${APP_DIR}/src/drivers/app_tuya_tp/tdd_tp_driver/src")
    wukong_append_sources(APP_SRC "${APP_DIR}/src/drivers/app_tuya_tp/tal_tp/src")
    wukong_append_sources(APP_SRC "${APP_DIR}/src/miscs/gui/display/ui/status")
    wukong_append_sources_maxdepth1(APP_SRC "${APP_DIR}/src/miscs/gui/display/ui")
    wukong_append_sources(APP_SRC "${APP_DIR}/src/miscs/gui/tuya_lvgl/src")
    wukong_append_sources(APP_SRC "${APP_DIR}/src/miscs/gui/tuya_lvgl_dp2widget/src")
    wukong_append_sources(APP_SRC "${APP_DIR}/src/miscs/gui/tuya_lvgl_fs_compatible/src")
    wukong_append_sources(APP_SRC "${APP_DIR}/src/miscs/gui/tuya_lvgl_gesture/src")
    wukong_append_sources(APP_SRC "${APP_DIR}/src/miscs/gui/tuya_lvgl_weather/src")
    wukong_append_sources(APP_SRC "${APP_DIR}/src/miscs/gui/tuya_lvgl_cloud_recipe/src")

    if(WUKONG_BOARD_SUBDIR STREQUAL "T5AI_BOARD")
        list(APPEND APP_SRC "${APP_DIR}/src/boards/T5AI_BOARD/ui/wechat_app.c")
    elseif(WUKONG_BOARD_SUBDIR STREQUAL "T5AI_BOARD_EYES")
        list(APPEND APP_SRC "${APP_DIR}/src/boards/T5AI_BOARD_EYES/ui/eyes_app.c")
        wukong_append_sources(APP_SRC "${APP_DIR}/src/boards/T5AI_BOARD_EYES/ui/eyes")
    elseif(WUKONG_BOARD_SUBDIR STREQUAL "T5AI_BOARD_EVB")
        list(APPEND APP_SRC "${APP_DIR}/src/boards/T5AI_BOARD_EVB/ui/xiaozhi_app.c")
        wukong_append_sources(APP_SRC "${APP_DIR}/src/boards/T5AI_BOARD_EVB/ui/emoji")
    elseif(WUKONG_BOARD_SUBDIR STREQUAL "T5AI_BOARD_EVB_PRO")
        list(APPEND APP_SRC "${APP_DIR}/src/boards/T5AI_BOARD_EVB_PRO/ui/xiaozhi_app.c")
        wukong_append_sources(APP_SRC "${APP_DIR}/src/boards/T5AI_BOARD_EVB_PRO/ui/emoji")
    elseif(WUKONG_BOARD_SUBDIR STREQUAL "T5AI_BOARD_ROBOT")
        wukong_append_sources(APP_SRC "${APP_DIR}/src/miscs/servo_ctrl")
        wukong_append_sources(APP_SRC "${APP_DIR}/src/miscs/gesture")
        list(APPEND APP_SRC "${APP_DIR}/src/boards/T5AI_BOARD_ROBOT/ui/robot_app.c")
        wukong_append_sources(APP_SRC "${APP_DIR}/src/boards/T5AI_BOARD_ROBOT/ui/robot")
    elseif(WUKONG_BOARD_SUBDIR STREQUAL "T5AI_BOARD_DESKTOP")
        list(APPEND APP_INC "${APP_DIR}/src/boards/T5AI_BOARD_DESKTOP/ui")
        list(APPEND APP_INC "${APP_DIR}/src/boards/T5AI_BOARD_DESKTOP/ui/res")
        wukong_append_sources(APP_SRC "${APP_DIR}/src/boards/T5AI_BOARD_DESKTOP/ui")
        list(APPEND APP_SRC "${APP_DIR}/src/boards/T5AI_BOARD_DESKTOP/ui/desktop_app.c")
        set(_desk_filter_out
            "${APP_DIR}/src/miscs/gui/display/ui/ai_img.c"
            "${APP_DIR}/src/miscs/gui/display/ui/font_awesome_16_4.c"
            "${APP_DIR}/src/miscs/gui/display/ui/font_awesome_20_4.c"
            "${APP_DIR}/src/miscs/gui/display/ui/font_awesome_30_4.c"
            "${APP_DIR}/src/miscs/gui/display/ui/font_emoji_64.c"
            "${APP_DIR}/src/miscs/gui/display/ui/FONT_SY_20.c"
            "${APP_DIR}/src/miscs/gui/display/ui/puhui_3bp_18.c"
            "${APP_DIR}/src/miscs/gui/display/ui/puhui_robot.c"
            "${APP_DIR}/src/miscs/gui/display/ui/user_img.c"
        )
        foreach(_x ${_desk_filter_out})
            list(REMOVE_ITEM APP_SRC "${_x}")
        endforeach()
    endif()

    if(WUKONG_TUYA_LVGL_VER STREQUAL "9")
        list(APPEND APP_INC "${APP_DIR}/src/miscs/gui/lvgl/src/common")
        list(APPEND APP_INC "${APP_DIR}/src/miscs/gui/lvgl/src/lvgl_v9/porting")
        wukong_append_sources(APP_SRC "${APP_DIR}/src/miscs/gui/lvgl/src")
        wukong_append_sources(APP_SRC "${APP_DIR}/src/miscs/gui/lvgl/src/lvgl_v9.3")
    else()
        list(APPEND APP_INC "${APP_DIR}/src/miscs/gui/lvgl/src/common")
        list(APPEND APP_INC "${APP_DIR}/src/miscs/gui/lvgl/src/lvgl_v8/porting")
        wukong_append_sources(APP_SRC "${APP_DIR}/src/miscs/gui/lvgl/src/common")
        wukong_append_sources(APP_SRC "${APP_DIR}/src/miscs/gui/lvgl/src/lvgl_v8")
    endif()

    if(TUYA_CFG_TUYA_LVGL_DRAW_WITH_ARM2D STREQUAL "y")
        list(APPEND APP_INC "${APP_DIR}/src/miscs/gui/libarm2d/include")
        list(APPEND APP_INC "${APP_DIR}/src/miscs/gui/tuya_lvgl/src/img_utility/cmsis-dsp/Include")
        list(APPEND APP_INC "${APP_DIR}/src/miscs/gui/tuya_lvgl/src/img_utility/miniz")
        list(APPEND APP_INC "${APP_DIR}/src/miscs/gui/tuya_lvgl/src/img_utility/libjpeg_turbo_encode")
        list(APPEND APP_INC "${APP_DIR}/src/miscs/gui/tuya_lvgl/src/img_utility/libjpeg_turbo_decode")
        wukong_append_sources(APP_SRC "${APP_DIR}/src/miscs/gui/libarm2d/src")
    endif()

    if(TUYA_CFG_TUYA_LIBJPEG_TURBO STREQUAL "y")
        list(APPEND APP_INC "${APP_DIR}/src/miscs/gui/libjpegturbo/include")
        list(APPEND APP_INC "${APP_DIR}/src/miscs/gui/libjpegturbo/src")
        wukong_append_sources(APP_SRC "${APP_DIR}/src/miscs/gui/libjpegturbo/src")
    endif()

    if(TUYA_CFG_TUYA_LIB_FREETYPE STREQUAL "y")
        list(APPEND APP_INC "${APP_DIR}/src/miscs/gui/libfreetype/include")
        wukong_append_sources(APP_SRC "${APP_DIR}/src/miscs/gui/libfreetype/src")
    endif()
endif()

# ---------------------------------------------------------------------------
# Codec bench / camera / filesystem download
# ---------------------------------------------------------------------------
if(TUYA_CFG_CODEC_BENCH_TEST STREQUAL "y")
    list(APPEND APP_INC "${APP_DIR}/src/miscs/codec_bench")
    wukong_append_sources(APP_SRC "${APP_DIR}/src/miscs/codec_bench")
endif()

if(TUYA_CFG_ENABLE_TUYA_CAMERA STREQUAL "y")
    list(APPEND APP_INC
        "${APP_DIR}/src/wukong/video"
        "${APP_DIR}/src/miscs/p2p"
        "${APP_DIR}/src/drivers/app_tuya_camera/tal_camera/include"
        "${APP_DIR}/src/drivers/app_tuya_camera/tal_dvp/include"
        "${APP_DIR}/src/drivers/app_tuya_camera/tdd_camera_driver/include"
        "${APP_DIR}/src/drivers/app_tuya_camera/tal_uvc/include"
        "${APP_DIR}/src/drivers/app_tuya_camera/tal_uvc/src"
    )
    list(APPEND APP_SRC "${APP_DIR}/src/tuya_ai_toy_camera.c")
    wukong_append_sources(APP_SRC "${APP_DIR}/src/wukong/video")
    wukong_append_sources(APP_SRC "${APP_DIR}/src/drivers/app_tuya_camera/tal_camera/src")
    wukong_append_sources(APP_SRC "${APP_DIR}/src/drivers/app_tuya_camera/tal_dvp/src")
    wukong_append_sources(APP_SRC "${APP_DIR}/src/drivers/app_tuya_camera/tdd_camera_driver/src")
    wukong_append_sources(APP_SRC "${APP_DIR}/src/miscs/p2p")
    wukong_append_sources(APP_SRC "${APP_DIR}/src/drivers/app_tuya_camera/tal_uvc/src")
endif()

if(TUYA_CFG_TUYA_FILE_SYSTEM STREQUAL "y")
    wukong_append_sources(APP_SRC "${APP_DIR}/src/miscs/gui/tuya_lvgl_resource_download/src")
endif()

# ---------------------------------------------------------------------------
# Final include list (app + SDK tree)
# ---------------------------------------------------------------------------
set(ALL_APP_INCLUDES
    "${TUYA_SDK_INCLUDE_ROOT}"
    ${SDK_HEADER_INCLUDE_DIRS}
    ${APP_INC}
)
list(REMOVE_DUPLICATES ALL_APP_INCLUDES)
list(REMOVE_DUPLICATES APP_SRC)

# ---------------------------------------------------------------------------
# Compile definitions / flags (LOCAL_TUYA_SDK_CFLAGS + UI/LVGL)
# ---------------------------------------------------------------------------
string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" USER_SW_VER_STR "${APP_VERSION}")
if(NOT USER_SW_VER_STR)
    set(USER_SW_VER_STR "${APP_VERSION}")
endif()

set(APP_COMPILE_DEFS
    "USER_SW_VER=\"${USER_SW_VER_STR}\""
    "APP_BIN_NAME=\"${APP_NAME}\""
    "AI_PLAYER_SUPPORT_DEFAULT_CONSUMER=0"
)

if(TUYA_CFG_WUKONG_BOARD_UBUNTU STREQUAL "y" AND TUYA_CFG_USING_UART_AUDIO_INPUT STREQUAL "y")
    list(APPEND APP_COMPILE_DEFS "UART_CODEC_SPK_FLOWCTL_YIELD_MS=30")
endif()

if(TUYA_CFG_ENABLE_TUYA_UI STREQUAL "y")
    list(APPEND APP_COMPILE_DEFS
        "EEZ_FOR_LVGL"
        "LV_LVGL_H_INCLUDE_SIMPLE"
        "CONFIG_FREETYPE"
        "TUYA_LVGL_VERSION=${WUKONG_TUYA_LVGL_VER}"
    )
endif()

if(TUYA_CFG_TUYA_AI_SDK STREQUAL "y")
    list(APPEND APP_COMPILE_DEFS "TUYA_AI_SDK=1")
endif()

# ---------------------------------------------------------------------------
# Target — branched by platform series
# ---------------------------------------------------------------------------
if(TUYA_CFG_L511_Y6E_BOARD STREQUAL "y")
    # ==== L511_Y6E series: build executable firmware image (EC716E) ====
    set(_L511_ROOT "${TUYA_OS_ROOT}")
    set(_L511_FLASH_LD "${_L511_ROOT}/ldscripts/ec7xx_0h00_flash_output.ld")
    set(_L511_OUT_HEX "${CMAKE_CURRENT_BINARY_DIR}/${APP_VERSION}")
    set(_L511_MKSIMAGE "${_L511_ROOT}/tools/fcelf")

    get_filename_component(_gcc_dir "${CMAKE_C_COMPILER}" DIRECTORY)
    if(NOT CMAKE_OBJCOPY)
        set(CMAKE_OBJCOPY "${_gcc_dir}/arm-none-eabi-objcopy")
    endif()
    if(NOT CMAKE_OBJDUMP)
        set(CMAKE_OBJDUMP "${_gcc_dir}/arm-none-eabi-objdump")
    endif()
    set(_CMAKE_SIZE "${_gcc_dir}/arm-none-eabi-size")

    # Override flags to match toolchain-gcc.cmake (nano.specs, nostartfiles, mthumb)
    set(CMAKE_C_FLAGS "-mcpu=cortex-m3 -mthumb -nostartfiles -mapcs-frame -specs=nano.specs -Os -g -ffunction-sections -fdata-sections -fno-isolate-erroneous-paths-dereference -freorder-blocks-algorithm=stc -Wall -Wno-format -gdwarf-2 -std=gnu99 -Wno-unused-function -Wno-error=unused-variable")
    set(CMAKE_CXX_FLAGS "-mcpu=cortex-m3 -mthumb -nostartfiles -mapcs-frame -specs=nano.specs -Os -g -ffunction-sections -fdata-sections -std=c++11 -fno-exceptions -fno-rtti -fno-threadsafe-statics")
    set(CMAKE_CXX_STANDARD 11)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    add_definitions(-DARM_MATH_CM3)

    foreach(_ln opendriver L511C_Y6 driver freertos middleware_ec psnv startup
                tcpipmgr openapi yrcompress httpclient lwip ping lfs sntp ps psl1 psif
                osa middleware_ec_private ccio driver_private usb_private tuyaos_adapter
                tuyaos lzma)
        set(_plib_${_ln} "${_L511_ROOT}/libs/lib${_ln}.a")
    endforeach()

    function(add_appimg target ldscript)
        set(_target_map "${_L511_OUT_HEX}/${target}.map")
        set(_target_bin "${_L511_OUT_HEX}/${target}.bin")
        set(_target_img "${_L511_OUT_HEX}/${target}.binpkg")
        set(_target_size "${_L511_OUT_HEX}/${target}.size")

        add_executable(${target} ${ARGN})
        target_link_directories(${target} PRIVATE "${_L511_ROOT}/libs")
        set_target_properties(${target} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${_L511_OUT_HEX}"
            SUFFIX ".elf"
        )

        target_link_options(${target} PRIVATE
            -T ${ldscript}
            -Wl,-Map=${_target_map}
            -Wl,--gc-sections
            -Wl,--cref
            -Wl,--wrap=_malloc_r
            -Wl,--wrap=_free_r
            -Wl,--wrap=_realloc_r
            -Wl,--defsym,main=tuya_app_main
            -Wl,--defsym,_heap_memory_start=end_ap_data
            -Wl,--defsym,_heap_memory_end=start_up_buffer
        )

        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${target}> ${_target_bin}
            COMMAND ${CMAKE_OBJDUMP} -h ${_L511_OUT_HEX}/${target}.elf > ${_target_size}
            COMMAND ${_CMAKE_SIZE} -G ${_L511_OUT_HEX}/${target}.elf >> ${_target_size}
        )

        file(GLOB _lib_files "${_L511_ROOT}/libs/*.a")
        foreach(_lib ${_lib_files})
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${_CMAKE_SIZE} ${_lib} -t -G >> ${_target_size}
            )
        endforeach()

        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND cp ${_target_bin} ${_L511_OUT_HEX}/${target}_unZip.bin
            COMMAND cp ${_L511_ROOT}/sysfw/mem_map.txt ${_L511_OUT_HEX}
            COMMAND ${_L511_MKSIMAGE} -C
                -bin ${_L511_OUT_HEX}/${target}_unZip.bin
                -cfg ${_L511_ROOT}/sysfw/sectionInfo_ec716e.json
                -map ${_target_map} -out ${_target_bin}
            COMMAND ${_L511_MKSIMAGE} -M
                -input ${_L511_ROOT}/sysfw/ap_bootloader.bin -addrname BL_PKGIMG_LNA -flashsize BOOTLOADER_PKGIMG_LIMIT_SIZE
                -input ${_target_bin} -addrname AP_PKGIMG_LNA -flashsize AP_PKGIMG_LIMIT_SIZE
                -input ${_L511_ROOT}/sysfw/cp-demo-flash.bin -addrname CP_PKGIMG_LNA -flashsize CP_PKGIMG_LIMIT_SIZE
                -input ${_L511_ROOT}/sysfw/ec716e_PrMgrCfg.json.bin -addrname XPKGDPRM_VIRTIMG_MERGE_LNA -flashsize XPKG_VIRTIMG_LOAD_SIZE
                -input ${_L511_ROOT}/sysfw/format_ec716e.json.bin -addrname XPKGDCMN_VIRTIMG_MERGE_LNA -flashsize XPKG_VIRTIMG_LOAD_SIZE
                -input ${_L511_ROOT}/sysfw/config_ec716e_prd_uart.baseini.bin -addrname XPKGDCMN_VIRTIMG_MERGE_LNA -flashsize XPKG_VIRTIMG_LOAD_SIZE
                -input ${_L511_ROOT}/sysfw/config_ec716e_prd_usb.baseini.bin -addrname XPKGDCMN_VIRTIMG_MERGE_LNA -flashsize XPKG_VIRTIMG_LOAD_SIZE
                -pkgmode 1 -banoldtool 1 -productname "EC716E_PRD"
                -def ${_L511_ROOT}/sysfw/mem_map.txt
                -outfile ${_target_img}
        )
    endfunction()

    set(WUKONG_TARGET ${APP_NAME})
    add_appimg(${WUKONG_TARGET} ${_L511_FLASH_LD} ${APP_SRC})
    target_include_directories(${WUKONG_TARGET} PRIVATE ${ALL_APP_INCLUDES})
    target_compile_definitions(${WUKONG_TARGET} PRIVATE ${APP_COMPILE_DEFS})

    # components.mk support (application_components from platform tree)
    if(EXISTS "${CMAKE_SOURCE_DIR}/components.mk")
        file(READ "${CMAKE_SOURCE_DIR}/components.mk" _cfg_comps)
        string(REPLACE "COMPONENTS:=" "" _comp_list "${_cfg_comps}")
        string(REPLACE " " ";" _app_comps "${_comp_list}")
        set(_comps_root "${_L511_ROOT}/application_components")
        foreach(_sub ${_app_comps})
            if(EXISTS "${_comps_root}/${_sub}")
                message(STATUS "add app component: ${_sub}")
                include_subdirectories("${_comps_root}/${_sub}")
                file(GLOB_RECURSE _comp_srcs "${_comps_root}/${_sub}/src/*.c" "${_comps_root}/${_sub}/src/*.cc")
                target_sources(${WUKONG_TARGET} PRIVATE ${_comp_srcs})
            endif()
        endforeach()
    endif()

    target_link_libraries(${WUKONG_TARGET} PRIVATE
        -Wl,-start-group -Wl,--whole-archive
        ${_plib_opendriver} ${_plib_L511C_Y6} ${_plib_driver} ${_plib_freertos}
        ${_plib_middleware_ec} ${_plib_psnv} ${_plib_startup} ${_plib_tcpipmgr}
        ${_plib_openapi} ${_plib_yrcompress} ${_plib_httpclient} ${_plib_lwip}
        ${_plib_ping} ${_plib_lfs} ${_plib_sntp} ${_plib_ps} ${_plib_psl1}
        ${_plib_psif} ${_plib_osa} ${_plib_middleware_ec_private} ${_plib_ccio}
        ${_plib_driver_private} ${_plib_usb_private} ${_plib_tuyaos_adapter}
        ${_plib_tuyaos} ${_plib_lzma}
        -Wl,--no-whole-archive -Wl,--end-group
        -lc -lm -Wl,--no-undefined -Wl,--no-print-map-discarded
    )

elseif(TUYA_CFG_L511_Y7PM_BOARD STREQUAL "y")
    # ==== L511_Y7PM series: build executable firmware image (EC718PM) ====
    set(_L511_ROOT "${TUYA_OS_ROOT}")
    set(_L511_FLASH_LD "${_L511_ROOT}/ldscripts/flash_output.ld")
    set(_L511_OUT_HEX "${CMAKE_CURRENT_BINARY_DIR}/${APP_VERSION}")
    set(_L511_MKSIMAGE "${_L511_ROOT}/tools/fcelf")

    get_filename_component(_gcc_dir "${CMAKE_C_COMPILER}" DIRECTORY)
    if(NOT CMAKE_OBJCOPY)
        set(CMAKE_OBJCOPY "${_gcc_dir}/arm-none-eabi-objcopy")
    endif()
    if(NOT CMAKE_OBJDUMP)
        set(CMAKE_OBJDUMP "${_gcc_dir}/arm-none-eabi-objdump")
    endif()
    set(_CMAKE_SIZE "${_gcc_dir}/arm-none-eabi-size")

    set(CMAKE_C_FLAGS "-mcpu=cortex-m3 -mthumb -nostartfiles -mapcs-frame -specs=nano.specs -Os -g -ffunction-sections -fdata-sections -fno-isolate-erroneous-paths-dereference -freorder-blocks-algorithm=stc -Wall -Wno-format -gdwarf-2 -std=gnu99 -Wno-unused-function -Wno-error=unused-variable")
    set(CMAKE_CXX_FLAGS "-mcpu=cortex-m3 -mthumb -nostartfiles -mapcs-frame -specs=nano.specs -Os -g -ffunction-sections -fdata-sections -std=c++11 -fno-exceptions -fno-rtti -fno-threadsafe-statics")
    set(CMAKE_CXX_STANDARD 11)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    add_definitions(-DARM_MATH_CM3)

    file(GLOB _y7pm_libs "${_L511_ROOT}/libs/*.a")
    list(FILTER _y7pm_libs EXCLUDE REGEX "libtuyaos\\.a$")

    function(add_appimg target ldscript)
        set(_target_map "${_L511_OUT_HEX}/${target}.map")
        set(_target_bin "${_L511_OUT_HEX}/${target}.bin")
        set(_target_img "${_L511_OUT_HEX}/${target}.binpkg")
        set(_target_size "${_L511_OUT_HEX}/${target}.size")

        add_executable(${target} ${ARGN})
        target_link_directories(${target} PRIVATE "${_L511_ROOT}/libs")
        set_target_properties(${target} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${_L511_OUT_HEX}"
            SUFFIX ".elf"
        )

        target_link_options(${target} PRIVATE
            -T ${ldscript}
            -Wl,-Map=${_target_map}
            -Wl,--gc-sections
            -Wl,--cref
            -Wl,--defsym,main=tuya_app_main
            -Wl,--defsym,_heap_memory_start=end_ap_data
            -Wl,--defsym,_heap_memory_end=start_up_buffer
        )

        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${target}> ${_target_bin}
            COMMAND ${CMAKE_OBJDUMP} -h ${_L511_OUT_HEX}/${target}.elf > ${_target_size}
            COMMAND ${_CMAKE_SIZE} -G ${_L511_OUT_HEX}/${target}.elf >> ${_target_size}
        )

        file(GLOB _lib_files "${_L511_ROOT}/libs/*.a")
        foreach(_lib ${_lib_files})
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${_CMAKE_SIZE} ${_lib} -t -G >> ${_target_size}
            )
        endforeach()

        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND cp ${_target_bin} ${_L511_OUT_HEX}/${target}_unZip.bin
            COMMAND cp ${_L511_ROOT}/sysfw/mem_map.txt ${_L511_OUT_HEX}
            COMMAND ${_L511_MKSIMAGE} -C
                -bin ${_L511_OUT_HEX}/${target}_unZip.bin
                -cfg ${_L511_ROOT}/sysfw/sectionInfo_ec718pm.json
                -map ${_target_map} -out ${_target_bin}
            COMMAND ${_L511_MKSIMAGE} -M
                -input ${_L511_ROOT}/sysfw/ap_bootloader.bin -addrname BL_PKGIMG_LNA -flashsize BOOTLOADER_PKGIMG_LIMIT_SIZE
                -input ${_target_bin} -addrname AP_PKGIMG_LNA -flashsize AP_PKGIMG_LIMIT_SIZE
                -input ${_L511_ROOT}/sysfw/cp-demo-flash.bin -addrname CP_PKGIMG_LNA -flashsize CP_PKGIMG_LIMIT_SIZE
                -input ${_L511_ROOT}/sysfw/ec718pm_PrMgrCfg.json.bin -addrname XPKGDPRM_VIRTIMG_MERGE_LNA -flashsize XPKG_VIRTIMG_LOAD_SIZE
                -input ${_L511_ROOT}/sysfw/format_ec718pm.json.bin -addrname XPKGDCMN_VIRTIMG_MERGE_LNA -flashsize XPKG_VIRTIMG_LOAD_SIZE
                -input ${_L511_ROOT}/sysfw/config_ec718pm_prd_uart.baseini.bin -addrname XPKGDCMN_VIRTIMG_MERGE_LNA -flashsize XPKG_VIRTIMG_LOAD_SIZE
                -input ${_L511_ROOT}/sysfw/config_ec718pm_prd_usb.baseini.bin -addrname XPKGDCMN_VIRTIMG_MERGE_LNA -flashsize XPKG_VIRTIMG_LOAD_SIZE
                -pkgmode 1 -banoldtool 1 -productname "EC718PM_PRD"
                -def ${_L511_ROOT}/sysfw/mem_map.txt
                -outfile ${_target_img}
        )
    endfunction()

    set(WUKONG_TARGET ${APP_NAME})
    add_appimg(${WUKONG_TARGET} ${_L511_FLASH_LD} ${APP_SRC})
    target_include_directories(${WUKONG_TARGET} PRIVATE ${ALL_APP_INCLUDES})
    target_compile_definitions(${WUKONG_TARGET} PRIVATE ${APP_COMPILE_DEFS})

    # components.mk support (application_components from platform tree)
    if(EXISTS "${CMAKE_SOURCE_DIR}/components.mk")
        file(READ "${CMAKE_SOURCE_DIR}/components.mk" _cfg_comps)
        string(REPLACE "COMPONENTS:=" "" _comp_list "${_cfg_comps}")
        string(REPLACE " " ";" _app_comps "${_comp_list}")
        set(_comps_root "${_L511_ROOT}/application_components")
        foreach(_sub ${_app_comps})
            if(EXISTS "${_comps_root}/${_sub}")
                message(STATUS "add app component: ${_sub}")
                include_subdirectories("${_comps_root}/${_sub}")
                file(GLOB_RECURSE _comp_srcs "${_comps_root}/${_sub}/src/*.c" "${_comps_root}/${_sub}/src/*.cc")
                target_sources(${WUKONG_TARGET} PRIVATE ${_comp_srcs})
            endif()
        endforeach()
    endif()

    target_link_libraries(${WUKONG_TARGET} PRIVATE
        -Wl,-start-group -Wl,--whole-archive
        ${_y7pm_libs}
        -Wl,--no-whole-archive
        ${_L511_ROOT}/libs/libtuyaos.a
        -Wl,--end-group
        -lc -lm -Wl,--no-undefined -Wl,--no-print-map-discarded
    )

else()
    # ==== Other platforms (LE270, etc.): build static library ====
    set(WUKONG_TARGET app)
    add_library(${WUKONG_TARGET} STATIC ${APP_SRC})
    target_include_directories(${WUKONG_TARGET} PRIVATE ${ALL_APP_INCLUDES})
    target_compile_definitions(${WUKONG_TARGET} PRIVATE ${APP_COMPILE_DEFS})
endif()

# ---------------------------------------------------------------------------
# Platform-common compile options
# ---------------------------------------------------------------------------
if(TUYA_CFG_ENABLE_TUYA_UI STREQUAL "y" AND TUYA_CFG_TUYA_LVGL_DRAW_WITH_ARM2D STREQUAL "y")
    target_compile_options(${WUKONG_TARGET} PRIVATE -mcpu=cortex-m33)
endif()

if(TUYA_CFG_ENABLE_TUYA_UI STREQUAL "y" AND WUKONG_TUYA_LVGL_VER STREQUAL "9")
    target_compile_options(${WUKONG_TARGET} PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:-DTUYA_LVGL_VERSION=9>
        $<$<COMPILE_LANGUAGE:CXX>:-DEEZ_FOR_LVGL>
        $<$<COMPILE_LANGUAGE:CXX>:-DLV_LVGL_H_INCLUDE_SIMPLE>
        $<$<COMPILE_LANGUAGE:CXX>:-DCONFIG_FREETYPE>
    )
endif()
