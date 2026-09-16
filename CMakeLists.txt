# CMake build rules for tuyaos_demo_wukong_ai only.
# Conditionals mirror local.mk; options are read from build/tuya_app.config (Kconfig export)
# and optionally ../../build/tuya_iot.config (same format).

cmake_minimum_required(VERSION 3.10)

if(NOT DEFINED APP_NAME)
    set(APP_NAME "tuyaos_demo_wukong_ai")
endif()
if(NOT DEFINED APP_VERSION)
    set(APP_VERSION "0.0.1")
endif()

# ---------------------------------------------------------------------------
# Toolchain — branched by platform series (must be before project())
# ---------------------------------------------------------------------------
if(DEFINED VENDOR_SDK_PATH)
    # ==== XY4100LC series: RISC-V toolchain (build_app.sh passes VENDOR_SDK_PATH) ====
    include(${VENDOR_SDK_PATH}/sdk/4100L_SDK/buildtools/tools/cmake/riscv64-gcc-toolchain.cmake)
    set(CMAKE_TRY_COMPILE_TARGET_TYPE "STATIC_LIBRARY")
    set(CMAKE_BUILD_TYPE "Release")
    set(CMAKE_C_STANDARD_REQUIRED ON)
    set(_WUKONG_XY4100LC_BUILD TRUE)
    project(${APP_NAME} LANGUAGES C ASM CXX)
else()
    # ==== ARM series (L511 / LE270 / T5 / Ubuntu) ====
    set(CMAKE_SYSTEM_NAME Generic)
    set(CMAKE_C_COMPILER arm-none-eabi-gcc)
    set(CMAKE_CXX_COMPILER arm-none-eabi-g++)
    set(CMAKE_READELF arm-none-eabi-readelf)
    set(CMAKE_C_FLAGS "-g -Wall -mno-unaligned-access -ffunction-sections -fdata-sections -mcpu=cortex-m3 -std=gnu11 --specs=nosys.specs -Wno-unused-function -Wno-error=unused-variable")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --specs=nosys.specs")
    set(_WUKONG_XY4100LC_BUILD FALSE)
    project(tuyaos_demo_wukong_ai C CXX)
    set(CMAKE_SYSTEM_PROCESSOR ARM)
    set(ARCH arm)
    set(CONFIG_CPU_ARM on)
endif()

set(LIBRARY_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../../libs)
set(ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../../lib)

if(DEFINED TUYAOS_SDK_VER)
    set(_TUYAOS_VER ${TUYAOS_SDK_VER})
else()
    set(_TUYAOS_VER 38)
endif()

message(STATUS ${CMAKE_C_COMPILER})
add_definitions(-DAPP_NAME="${APP_NAME}")
add_definitions(-DAPP_VERSION="${APP_VERSION}")
add_definitions(-DTUYAOS_VER=${_TUYAOS_VER})
if(_WUKONG_XY4100LC_BUILD)
    add_definitions(-DUSER_SW_VER="${APP_VERSION}")
    add_definitions(-DAPP_BIN_NAME="${APP_NAME}")
    add_definitions(-DUSE_TUYA_MAIN=1)
    add_definitions(-DCLI_UART_NUM=3)
endif()
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
    "${APP_DIR}/src/miscs/display"
    "${APP_DIR}/src/miscs/audio_analysis"
    "${APP_DIR}/src/miscs/audio_player"
    "${APP_DIR}/src/miscs/audio_player/include"
    "${APP_DIR}/src/miscs/audio_player/src/decoder/helix/include"
    "${APP_DIR}/src/mode"
    # Unconditional: UI/MCP/detection/tuya_ai_toy.c include these headers
    # regardless of camera/P2P config (stub-degraded when the feature is off).
    "${APP_DIR}/src/wukong/video"
    "${APP_DIR}/src/miscs/camera"
    "${APP_DIR}/src/wukong"
    "${APP_DIR}/src/wukong/audio"
    "${APP_DIR}/src/wukong/audio/input"
    "${APP_DIR}/src/wukong/audio/output"
    "${APP_DIR}/src/wukong/assets"
    "${APP_DIR}/src/wukong/toolkits"
    "${APP_DIR}/src/wukong/toolkits/fc"
    "${APP_DIR}/src/wukong/toolkits/mcp"
    "${APP_DIR}/src/wukong/toolkits/skill"
    "${APP_DIR}/src/wukong/audio/frontend"
    "${APP_DIR}/src/wukong/audio/frontend/aec_vad/tuya"
    "${APP_DIR}/src/wukong/audio/frontend/kws"
    "${APP_DIR}/src/wukong/provider"
    "${APP_DIR}/src/wukong/provider/tuya"
    "${APP_DIR}/src/wukong/provider/cube"
    "${APP_DIR}/src/wukong/provider/jd"
    "${APP_DIR}/src/wukong/provider/jd/joyinside"
    "${APP_DIR}/src/wukong/channel"
    "${APP_DIR}/src/wukong/picture"
    "${APP_DIR}/src/wukong/cron"
    "${APP_DIR}/src/wukong/tm"
    "${APP_DIR}/src/wukong/storage"
    "${APP_DIR}/src/wukong/utility"
)

if(TUYA_CFG_AI_PLAYER_DECODER_OGGOPUS_ENABLE STREQUAL "y" OR TUYA_CFG_AI_PLAYER_DECODER_OPUS_ENABLE STREQUAL "y" OR TUYA_CFG_AI_PLAYER_DECODER_OPUS_VBR_ENABLE STREQUAL "y")
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
    if(TUYA_CFG_ENABLE_TUYA_DISPLAY STREQUAL "y")
        list(APPEND APP_INC "${APP_DIR}/src/boards/L511_Y7PM_BOARD/lcd_demo")
        list(APPEND APP_SRC "${APP_DIR}/src/boards/L511_Y7PM_BOARD/lcd_demo/wukong_demo_lcd_anim.c")
    endif()
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
wukong_append_sources(APP_SRC "${APP_DIR}/src/drivers/app_tuya_key")
wukong_append_sources(APP_SRC "${APP_DIR}/src/drivers/app_tuya_led")
wukong_append_sources(APP_SRC "${APP_DIR}/src/mode")
wukong_append_sources_maxdepth1(APP_SRC "${APP_DIR}/src/wukong")
list(APPEND APP_SRC "${APP_DIR}/src/wukong/provider/wukong_ai_provider.c")
if(TUYA_CFG_ENABLE_PROVIDER_JD STREQUAL "y")
    wukong_append_sources(APP_SRC "${APP_DIR}/src/wukong/provider/jd")
else()
    list(APPEND APP_SRC "${APP_DIR}/src/wukong/provider/tuya/wukong_provider_tuya.c")
endif()
list(APPEND APP_SRC "${APP_DIR}/src/wukong/channel/wukong_ai_channel.c")
wukong_append_sources(APP_SRC "${APP_DIR}/src/wukong/assets")
wukong_append_sources(APP_SRC "${APP_DIR}/src/wukong/cron")
# Match local.mk: only tm/*.c (not tm/tests — host/unit tests)
wukong_append_sources_maxdepth1(APP_SRC "${APP_DIR}/src/wukong/tm")
# Match local.mk: only store/*.c (not store/tests — host/unit tests)
wukong_append_sources_maxdepth1(APP_SRC "${APP_DIR}/src/wukong/storage")
# Precise single-file append (not wukong_append_sources_maxdepth1 on
# src/boards/common): that dir also holds gui_common.c, which CMake does not
# currently collect (local.mk adds it only per-board); a maxdepth1 glob here
# would newly pull it into every CMake build as an unintended side effect.
wukong_append_sources(APP_SRC "${APP_DIR}/src/boards/common/wukong_storage_port.c")
wukong_append_sources(APP_SRC "${APP_DIR}/src/wukong/toolkits/fc")
list(APPEND APP_SRC
    "${APP_DIR}/src/wukong/toolkits/wukong_tool.c"
    "${APP_DIR}/src/wukong/toolkits/skill/wukong_skill.c"
    "${APP_DIR}/src/wukong/toolkits/skill/skill_tool.c"
)

if(TUYA_CFG_ENABLE_TUYA_TOOLKITS STREQUAL "y")
    list(APPEND APP_SRC "${APP_DIR}/src/wukong/toolkits/wukong_toolkits.c")
    wukong_append_sources_maxdepth1(APP_SRC "${APP_DIR}/src/wukong/toolkits/mcp")
    list(APPEND APP_SRC "${APP_DIR}/src/wukong/toolkits/tools/tool_init.c")
    if(TUYA_CFG_ENABLE_TOOLKITS_CONTROL STREQUAL "y")
        list(APPEND APP_SRC "${APP_DIR}/src/wukong/toolkits/tools/tool_control.c")
    endif()
    if(TUYA_CFG_ENABLE_TOOLKITS_SYSTEM STREQUAL "y")
        list(APPEND APP_SRC "${APP_DIR}/src/wukong/toolkits/tools/tool_system.c")
    endif()
    if(TUYA_CFG_ENABLE_TOOLKITS_TM STREQUAL "y")
        list(APPEND APP_SRC "${APP_DIR}/src/wukong/toolkits/tools/tool_tm.c")
    endif()
    if(TUYA_CFG_ENABLE_TOOLKITS_CAMERA STREQUAL "y")
        list(APPEND APP_SRC "${APP_DIR}/src/wukong/toolkits/tools/tool_camera.c")
    endif()
    if(TUYA_CFG_ENABLE_TOOLKITS_MOTION STREQUAL "y")
        list(APPEND APP_SRC "${APP_DIR}/src/wukong/toolkits/tools/tool_motion.c")
    endif()
    if(TUYA_CFG_ENABLE_TOOLKITS_IMM STREQUAL "y")
        list(APPEND APP_SRC "${APP_DIR}/src/wukong/toolkits/tools/tool_imm.c")
    endif()
    if(TUYA_CFG_ENABLE_TOOLKITS_SOCIAL STREQUAL "y")
        list(APPEND APP_SRC "${APP_DIR}/src/wukong/toolkits/tools/tool_social.c")
    endif()
    if(TUYA_CFG_ENABLE_TOOLKITS_PLAYBACK STREQUAL "y")
        list(APPEND APP_SRC
            "${APP_DIR}/src/wukong/toolkits/tools/tool_playback.c"
            "${APP_DIR}/src/wukong/audio/wukong_playback_ctrl.c"
        )
    endif()
endif()

if(TUYA_CFG_ENABLE_TUYA_PICTURE STREQUAL "y")
    wukong_append_sources(APP_SRC "${APP_DIR}/src/wukong/picture")
endif()

wukong_append_sources(APP_SRC "${APP_DIR}/src/wukong/utility")
list(APPEND APP_SRC
    "${APP_DIR}/src/wukong/audio/frontend/kws/wukong_kws.c"
    "${APP_DIR}/src/wukong/audio/frontend/wukong_audio_frontend.c"
    "${APP_DIR}/src/wukong/audio/wukong_audio_player.c"
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
list(FILTER _ap_all EXCLUDE REGEX "decoder_opus_vbr\\.c$")
list(APPEND APP_SRC ${_ap_all})

if(TUYA_CFG_AI_PLAYER_DECODER_OGGOPUS_ENABLE STREQUAL "y")
    wukong_append_sources(APP_SRC "${APP_DIR}/src/miscs/audio_player/src/decoder/ogg")
    list(APPEND APP_SRC "${APP_DIR}/src/miscs/audio_player/src/decoder/decoder_oggopus.c")
endif()
if(TUYA_CFG_AI_PLAYER_DECODER_OPUS_ENABLE STREQUAL "y")
    wukong_append_sources(APP_SRC "${APP_DIR}/src/miscs/audio_player/src/decoder/opus")
    list(APPEND APP_SRC "${APP_DIR}/src/miscs/audio_player/src/decoder/decoder_opus.c")
endif()
if(TUYA_CFG_AI_PLAYER_DECODER_OPUS_VBR_ENABLE STREQUAL "y")
    list(APPEND APP_SRC "${APP_DIR}/src/miscs/audio_player/src/decoder/decoder_opus_vbr.c")
endif()

# Collect compile defs early — display/UI blocks may append before the target section
set(APP_COMPILE_DEFS "")

# ---------------------------------------------------------------------------
# Display stack (CONFIG_ENABLE_TUYA_DISPLAY) — paths match local.mk / miscs/display
# (same sources formerly under miscs/gui/display in the old UI block)
# ---------------------------------------------------------------------------
if(TUYA_CFG_ENABLE_TUYA_DISPLAY STREQUAL "y")
    list(APPEND APP_INC
        "${APP_DIR}/src/miscs/display"
        "${APP_DIR}/src/drivers/app_tuya_display/tdd_lcd_driver/include"
        "${APP_DIR}/src/drivers/app_tuya_display/tal_display/include"
        "${APP_DIR}/src/drivers/app_tuya_tp/tdd_tp_driver/include"
        "${APP_DIR}/src/drivers/app_tuya_tp/tal_tp/include"
        "${APP_DIR}/src/miscs/tef"
    )
    list(APPEND APP_SRC
        "${APP_DIR}/src/miscs/display/tuya_ai_display.c"
        "${APP_DIR}/src/miscs/display/tuya_display_hw.c"
    )
    wukong_append_sources(APP_SRC "${APP_DIR}/src/drivers/app_tuya_display/tdd_lcd_driver/src")
    wukong_append_sources(APP_SRC "${APP_DIR}/src/drivers/app_tuya_display/tal_display/src")
    wukong_append_sources(APP_SRC "${APP_DIR}/src/drivers/app_tuya_tp/tdd_tp_driver/src")
    wukong_append_sources(APP_SRC "${APP_DIR}/src/drivers/app_tuya_tp/tal_tp/src")
    # tkl_touch is Beken T5 capacitive-touch HAL (needs driver/touch.h); not for L511
    if(WUKONG_BOARD_SUBDIR MATCHES "^T5AI_BOARD" OR TUYA_CFG_TUYA_MODULE_T5 STREQUAL "y")
        list(APPEND APP_SRC "${APP_DIR}/src/drivers/app_tuya_driver/src/os/tkl_touch.c")
    endif()
    # TEF player (exclude bench/)
    file(GLOB_RECURSE _tef_srcs "${APP_DIR}/src/miscs/tef/*.c")
    list(FILTER _tef_srcs EXCLUDE REGEX "/bench/")
    list(APPEND APP_SRC ${_tef_srcs})
    if(WUKONG_BOARD_SUBDIR STREQUAL "T5AI_BOARD_EYES")
        list(APPEND APP_COMPILE_DEFS "TEF_RGB565_BYTE_SWAP=1")
        list(APPEND APP_INC "${APP_DIR}/src/boards/T5AI_BOARD_EYES/ui/eyes_tef")
        list(APPEND APP_SRC "${APP_DIR}/src/boards/T5AI_BOARD_EYES/ui/eyes_app.c")
        wukong_append_sources(APP_SRC "${APP_DIR}/src/boards/T5AI_BOARD_EYES/ui/eyes_tef")
    endif()
endif()

# ---------------------------------------------------------------------------
# UI framework (CONFIG_ENABLE_TUYA_UI) — src/ui (mirrors local.mk)
# ---------------------------------------------------------------------------
if(TUYA_CFG_ENABLE_TUYA_UI STREQUAL "y")
    set(WUKONG_UI_DIR "${APP_DIR}/src/ui")
    set(WUKONG_LVGL_DIR "${WUKONG_UI_DIR}/lvgl")

    list(APPEND APP_INC
        "${WUKONG_UI_DIR}"
        "${WUKONG_UI_DIR}/port"
        "${WUKONG_UI_DIR}/port/compat"
        "${WUKONG_LVGL_DIR}"
        "${WUKONG_LVGL_DIR}/src"
        "${WUKONG_UI_DIR}/core"
        "${WUKONG_UI_DIR}/style"
        "${WUKONG_UI_DIR}/i18n"
        "${WUKONG_UI_DIR}/widgets"
        "${WUKONG_UI_DIR}/pages"
        "${WUKONG_UI_DIR}/services"
        "${APP_DIR}/src/boards/common/boot_splash"
    )
    list(APPEND APP_COMPILE_DEFS "LV_CONF_INCLUDE_SIMPLE")

    wukong_append_sources(APP_SRC "${WUKONG_LVGL_DIR}/src")
    wukong_append_sources(APP_SRC "${WUKONG_UI_DIR}/core")
    list(APPEND APP_SRC
        "${WUKONG_UI_DIR}/port/ui_port.c"
        "${WUKONG_UI_DIR}/port/ui_port_perf.c"
        "${WUKONG_UI_DIR}/port/ui_port_indev.c"
        "${WUKONG_UI_DIR}/port/tuya_ai_display_stub.c"
        "${WUKONG_UI_DIR}/port/compat/tuya_port_disp.c"
    )
    if(TUYA_CFG_UI_DMA2D_ENABLE STREQUAL "y")
        list(APPEND APP_SRC "${WUKONG_UI_DIR}/port/ui_port_disp_dma2d.c")
    else()
        list(APPEND APP_SRC "${WUKONG_UI_DIR}/port/ui_port_disp_sw.c")
    endif()
    wukong_append_sources(APP_SRC "${WUKONG_UI_DIR}/style")
    wukong_append_sources(APP_SRC "${WUKONG_UI_DIR}/i18n")
    wukong_append_sources(APP_SRC "${WUKONG_UI_DIR}/widgets")

    if(TUYA_CFG_UI_WUKONG_PAGES STREQUAL "y")
        file(GLOB _ui_page_srcs "${WUKONG_UI_DIR}/pages/*.c")
        list(REMOVE_ITEM _ui_page_srcs "${WUKONG_UI_DIR}/pages/_template.c")
        file(GLOB _ui_svc_srcs "${WUKONG_UI_DIR}/services/*.c")
        set(_ui_disabled_srcs "")
        if(NOT TUYA_CFG_UI_FEATURE_CAMERA STREQUAL "y")
            list(APPEND _ui_disabled_srcs
                "${WUKONG_UI_DIR}/pages/ui_page_camera.c"
                "${WUKONG_UI_DIR}/pages/ui_page_photo.c"
                "${WUKONG_UI_DIR}/services/ui_svc_camera.c"
                "${WUKONG_UI_DIR}/services/ui_svc_picture.c"
            )
        endif()
        if(NOT TUYA_CFG_UI_FEATURE_VIDEO STREQUAL "y")
            list(APPEND _ui_disabled_srcs
                "${WUKONG_UI_DIR}/pages/ui_page_video.c"
                "${WUKONG_UI_DIR}/services/ui_svc_video.c"
                "${WUKONG_UI_DIR}/services/ui_svc_video_playback.c"
            )
        endif()
        if(NOT TUYA_CFG_UI_FEATURE_MUSIC STREQUAL "y")
            list(APPEND _ui_disabled_srcs
                "${WUKONG_UI_DIR}/pages/ui_page_music.c"
                "${WUKONG_UI_DIR}/pages/ui_page_music_list.c"
                "${WUKONG_UI_DIR}/services/ui_svc_music.c"
            )
        endif()
        if(NOT TUYA_CFG_UI_FEATURE_RECORDING STREQUAL "y")
            list(APPEND _ui_disabled_srcs
                "${WUKONG_UI_DIR}/pages/ui_page_recording.c"
                "${WUKONG_UI_DIR}/pages/ui_page_recording_list.c"
                "${WUKONG_UI_DIR}/pages/ui_page_recording_transcribe.c"
                "${WUKONG_UI_DIR}/services/ui_svc_recording.c"
                "${WUKONG_UI_DIR}/services/ui_svc_transcribe.c"
            )
        endif()
        if(NOT TUYA_CFG_UI_FEATURE_TIME STREQUAL "y")
            list(APPEND _ui_disabled_srcs
                "${WUKONG_UI_DIR}/pages/ui_page_clock.c"
                "${WUKONG_UI_DIR}/pages/ui_page_schedule.c"
                "${WUKONG_UI_DIR}/services/ui_svc_tm.c"
            )
        endif()
        if(NOT TUYA_CFG_UI_FEATURE_DETECTION STREQUAL "y")
            list(APPEND _ui_disabled_srcs
                "${WUKONG_UI_DIR}/pages/ui_page_detection.c"
                "${WUKONG_UI_DIR}/services/ui_svc_detection.c"
            )
        endif()
        if(NOT TUYA_CFG_UI_FEATURE_CALL STREQUAL "y")
            list(APPEND _ui_disabled_srcs
                "${WUKONG_UI_DIR}/pages/ui_page_call.c"
                "${WUKONG_UI_DIR}/pages/ui_page_contacts.c"
                "${WUKONG_UI_DIR}/services/ui_svc_call.c"
            )
        endif()
        if(NOT TUYA_CFG_UI_FEATURE_FILES STREQUAL "y")
            list(APPEND _ui_disabled_srcs "${WUKONG_UI_DIR}/pages/ui_page_files.c")
        endif()
        if(NOT TUYA_CFG_UI_FEATURE_AUDIO_DIAG STREQUAL "y")
            list(APPEND _ui_disabled_srcs
                "${WUKONG_UI_DIR}/pages/ui_page_audio_diag.c"
                "${WUKONG_UI_DIR}/services/ui_svc_audio_diag.c"
            )
        endif()
        if(NOT TUYA_CFG_UI_FEATURE_WEATHER STREQUAL "y")
            list(APPEND _ui_disabled_srcs "${WUKONG_UI_DIR}/services/ui_svc_weather.c")
        endif()
        foreach(_ui_src ${_ui_page_srcs} ${_ui_svc_srcs})
            if(NOT _ui_src IN_LIST _ui_disabled_srcs)
                list(APPEND APP_SRC "${_ui_src}")
            endif()
        endforeach()
        wukong_append_sources(APP_SRC "${WUKONG_UI_DIR}/assets/font")
    endif()
    wukong_append_sources(APP_SRC "${WUKONG_UI_DIR}/assets/icon")
    list(APPEND APP_SRC "${WUKONG_UI_DIR}/ui_app.c")
    wukong_append_sources(APP_SRC "${APP_DIR}/src/boards/common/boot_splash")

    if(TUYA_CFG_UI_DMA2D_ENABLE STREQUAL "y")
        list(APPEND APP_INC
            "${WUKONG_UI_DIR}/vendor/libarm2d/include"
            "${WUKONG_UI_DIR}/vendor/cmsis-dsp/Include"
        )
        wukong_append_sources(APP_SRC "${WUKONG_UI_DIR}/vendor/libarm2d/src")
    endif()

    if(WUKONG_BOARD_SUBDIR STREQUAL "T5AI_BOARD_ROBOT")
        list(APPEND APP_INC
            "${APP_DIR}/src/boards/common"
            "${APP_DIR}/src/boards/T5AI_BOARD_ROBOT/ui/robot_tef"
        )
        list(APPEND APP_SRC
            "${APP_DIR}/src/boards/common/gui_common.c"
            "${APP_DIR}/src/boards/common/res/fonts/puhui_robot.c"
            "${APP_DIR}/src/boards/common/res/fonts/font_awesome_16_4.c"
        )
        wukong_append_sources(APP_SRC "${APP_DIR}/src/boards/T5AI_BOARD_ROBOT/ui/robot")
        wukong_append_sources(APP_SRC "${APP_DIR}/src/boards/T5AI_BOARD_ROBOT/ui/robot_tef")
        wukong_append_sources(APP_SRC "${APP_DIR}/src/miscs/servo_ctrl")
        wukong_append_sources(APP_SRC "${APP_DIR}/src/miscs/gesture")
    elseif(WUKONG_BOARD_SUBDIR STREQUAL "T5AI_BOARD_EVB")
        list(APPEND APP_INC "${APP_DIR}/src/boards/common")
        list(APPEND APP_SRC
            "${APP_DIR}/src/boards/common/gui_common.c"
            "${APP_DIR}/src/boards/common/res/fonts/puhui_3bp_18.c"
            "${APP_DIR}/src/boards/common/res/fonts/font_awesome_16_4.c"
            "${APP_DIR}/src/boards/common/res/fonts/font_emoji_64.c"
            "${APP_DIR}/src/boards/T5AI_BOARD_EVB/ui/xiaozhi_app.c"
        )
        wukong_append_sources(APP_SRC "${APP_DIR}/src/boards/T5AI_BOARD_EVB/ui/emoji")
    elseif(WUKONG_BOARD_SUBDIR STREQUAL "T5AI_BOARD_EVB_PRO")
        list(APPEND APP_INC "${APP_DIR}/src/boards/common")
        list(APPEND APP_SRC
            "${APP_DIR}/src/boards/common/gui_common.c"
            "${APP_DIR}/src/boards/common/res/fonts/puhui_3bp_18.c"
            "${APP_DIR}/src/boards/common/res/fonts/font_awesome_16_4.c"
            "${APP_DIR}/src/boards/common/res/fonts/font_emoji_64.c"
            "${APP_DIR}/src/boards/T5AI_BOARD_EVB_PRO/ui/xiaozhi_app.c"
        )
        wukong_append_sources(APP_SRC "${APP_DIR}/src/boards/T5AI_BOARD_EVB_PRO/ui/emoji")
    endif()
endif()

# ---------------------------------------------------------------------------
# Codec bench / camera / filesystem download
# ---------------------------------------------------------------------------
if(TUYA_CFG_CODEC_BENCH_TEST STREQUAL "y")
    list(APPEND APP_INC "${APP_DIR}/src/miscs/codec_bench")
    wukong_append_sources(APP_SRC "${APP_DIR}/src/miscs/codec_bench")
endif()

# Video service framework (single video data source): compiled whenever any
# producer/consumer exists (camera input or P2P) — mirrors local.mk.
if(TUYA_CFG_ENABLE_TUYA_CAMERA STREQUAL "y" OR TUYA_CFG_ENABLE_AI_MODE_P2P STREQUAL "y")
    list(APPEND APP_SRC
        "${APP_DIR}/src/wukong/video/wukong_video_input.c"
        "${APP_DIR}/src/wukong/video/wukong_video_service.c"
    )
endif()

# Camera video input source (tal_camera)
if(TUYA_CFG_USING_CAMERA_VIDEO_INPUT STREQUAL "y")
    list(APPEND APP_SRC "${APP_DIR}/src/wukong/video/video_input_camera.c")
endif()

# Camera drivers + camera business layer
if(TUYA_CFG_ENABLE_TUYA_CAMERA STREQUAL "y")
    list(APPEND APP_INC
        "${APP_DIR}/src/drivers/app_tuya_camera/tal_camera/include"
        "${APP_DIR}/src/drivers/app_tuya_camera/tal_dvp/include"
        "${APP_DIR}/src/drivers/app_tuya_camera/tdd_camera_driver/include"
        # camera depends on tal_display's ty_frame_buff.h even without UI (mirrors local.mk)
        "${APP_DIR}/src/drivers/app_tuya_display/tal_display/include"
        "${APP_DIR}/src/drivers/app_tuya_camera/tal_uvc/include"
        "${APP_DIR}/src/drivers/app_tuya_camera/tal_uvc/src"
    )
    wukong_append_sources(APP_SRC "${APP_DIR}/src/drivers/app_tuya_camera/tal_camera/src")
    wukong_append_sources(APP_SRC "${APP_DIR}/src/drivers/app_tuya_camera/tal_dvp/src")
    wukong_append_sources(APP_SRC "${APP_DIR}/src/drivers/app_tuya_camera/tdd_camera_driver/src")
    wukong_append_sources(APP_SRC "${APP_DIR}/src/drivers/app_tuya_camera/tal_uvc/src")
    list(APPEND APP_SRC "${APP_DIR}/src/miscs/camera/tuya_camera.c")
endif()

# P2P call business
if(TUYA_CFG_ENABLE_AI_MODE_P2P STREQUAL "y")
    list(APPEND APP_INC "${APP_DIR}/src/miscs/p2p")
    wukong_append_sources(APP_SRC "${APP_DIR}/src/miscs/p2p")
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

list(APPEND APP_COMPILE_DEFS
    "USER_SW_VER=\"${USER_SW_VER_STR}\""
    "APP_BIN_NAME=\"${APP_NAME}\""
    "AI_PLAYER_SUPPORT_DEFAULT_CONSUMER=0"
)

if(TUYA_CFG_WUKONG_BOARD_UBUNTU STREQUAL "y" AND TUYA_CFG_USING_UART_AUDIO_INPUT STREQUAL "y")
    list(APPEND APP_COMPILE_DEFS "UART_CODEC_SPK_FLOWCTL_YIELD_MS=30")
endif()

if(TUYA_CFG_TUYA_AI_SDK STREQUAL "y")
    list(APPEND APP_COMPILE_DEFS "TUYA_AI_SDK=1")
endif()

if(TUYA_CFG_L511_Y7PM_BOARD STREQUAL "y" AND TUYA_CFG_ENABLE_TUYA_DISPLAY STREQUAL "y")
    list(APPEND APP_COMPILE_DEFS "TEF_RGB565_BYTE_SWAP=1")
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

elseif(_WUKONG_XY4100LC_BUILD)
    # ==== XY4100LC series: build static library (RISC-V, linked by xybuild) ====
    set(_XY4100LC_COMPILE_OPTS
        "-march=rv64imacxthead"
        "-mabi=lp64"
        "-mcmodel=medlow"
        "-Os"
        "-fmessage-length=0"
        "-fsigned-char"
        "-ffunction-sections"
        "-fdata-sections"
        "-ffreestanding"
        "-fcommon"
        "-mstrict-align"
        "-g3"
    )
    set(WUKONG_TARGET app)
    add_library(${WUKONG_TARGET} STATIC ${APP_SRC})
    target_include_directories(${WUKONG_TARGET} PRIVATE ${ALL_APP_INCLUDES})
    target_compile_definitions(${WUKONG_TARGET} PRIVATE ${APP_COMPILE_DEFS})
    target_compile_features(${WUKONG_TARGET} PRIVATE c_std_11)
    target_compile_options(${WUKONG_TARGET} PRIVATE ${_XY4100LC_COMPILE_OPTS})
    set(_app_lib_out "${CMAKE_CURRENT_SOURCE_DIR}/build")
    file(MAKE_DIRECTORY "${_app_lib_out}")
    set_target_properties(${WUKONG_TARGET} PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY "${_app_lib_out}"
        OUTPUT_NAME "tuya_app"
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
if(NOT _WUKONG_XY4100LC_BUILD AND TUYA_CFG_ENABLE_TUYA_UI STREQUAL "y" AND TUYA_CFG_UI_DMA2D_ENABLE STREQUAL "y")
    target_compile_options(${WUKONG_TARGET} PRIVATE -mcpu=cortex-m33)
endif()
