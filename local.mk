# 当前文件所在目录
LOCAL_PATH := $(call my-dir)

#---------------------------------------

# 清除 LOCAL_xxx 变量
include $(CLEAR_VARS)
-include $(LOCAL_PATH)/build/tuya_app.config
-include $(LOCAL_PATH)/../../build/tuya_iot.config

# 当前模块名
LOCAL_MODULE := $(notdir $(LOCAL_PATH))

# 模块内部的SECTIONS： 仅本组件生效
ifeq ($(CONFIG_ENABLE_MULTI_SECTION_UPGRADE), y)
LOCAL_SECTION_NAME :=  $(CONFIG_MULTI_SECION_NAME)
endif

# 模块对外头文件（只能是目录）
# 加载至CFLAGS中提供给其他组件使用；打包进SDK产物中；
LOCAL_TUYA_SDK_INC := $(LOCAL_PATH)/include
# LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/drivers/app_tuya_driver/include
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/drivers/app_tuya_key/include
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/drivers/app_tuya_led/include
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/miscs/uart_codec/include
ifeq ($(CONFIG_ENABLE_BATTERY), y)
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/miscs/battery
endif
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/miscs/mftest
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/miscs/audio_analysis
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/miscs/audio_player
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/miscs/audio_player/include
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/miscs/audio_player/src/decoder/helix/include
ifeq ($(or $(CONFIG_AI_PLAYER_DECODER_OGGOPUS_ENABLE),$(CONFIG_AI_PLAYER_DECODER_OPUS_ENABLE)), y)
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/miscs/audio_player/src/decoder/opus
endif
ifeq ($(CONFIG_AI_PLAYER_DECODER_OGGOPUS_ENABLE), y)
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/miscs/audio_player/src/decoder/ogg/include
endif
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/mode

ifeq ($(CONFIG_T5AI_BOARD), y)
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/boards/T5AI_BOARD/
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/boards/T5AI_BOARD/ui/
LOCAL_SRC_FILES := $(LOCAL_PATH)/src/boards/T5AI_BOARD/tuya_device_board.c
ifeq ($(CONFIG_ENABLE_TUYA_CAMERA), y)
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/boards/T5AI_BOARD/tuya_device_camera.c
endif
endif

ifeq ($(CONFIG_T5AI_BOARD_EVB), y)
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/boards/T5AI_BOARD_EVB/
LOCAL_SRC_FILES := $(LOCAL_PATH)/src/boards/T5AI_BOARD_EVB/tuya_device_board.c
ifeq ($(CONFIG_ENABLE_TUYA_CAMERA), y)
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/boards/T5AI_BOARD_EVB/tuya_device_camera.c
endif
endif

ifeq ($(CONFIG_T5AI_BOARD_EVB_PRO), y)  
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/boards/T5AI_BOARD_EVB_PRO/
LOCAL_SRC_FILES := $(LOCAL_PATH)/src/boards/T5AI_BOARD_EVB_PRO/tuya_device_board.c
ifeq ($(CONFIG_ENABLE_TUYA_CAMERA), y)
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/boards/T5AI_BOARD_EVB_PRO/tuya_device_camera.c
endif
endif

ifeq ($(CONFIG_T5AI_BOARD_EYES), y)
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/boards/T5AI_BOARD_EYES/
LOCAL_SRC_FILES := $(LOCAL_PATH)/src/boards/T5AI_BOARD_EYES/tuya_device_board.c
ifeq ($(CONFIG_ENABLE_TUYA_CAMERA), y)
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/boards/T5AI_BOARD_EYES/tuya_device_camera.c
endif
endif

ifeq ($(CONFIG_T5AI_BOARD_ROBOT), y)
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/boards/T5AI_BOARD_ROBOT/
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/miscs/servo_ctrl
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/miscs/gesture
LOCAL_SRC_FILES := $(LOCAL_PATH)/src/boards/T5AI_BOARD_ROBOT/tuya_device_board.c
ifeq ($(CONFIG_ENABLE_TUYA_CAMERA), y)
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/boards/T5AI_BOARD_ROBOT/tuya_device_camera.c
endif
endif

ifeq ($(CONFIG_T5AI_BOARD_DESKTOP), y)
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/boards/T5AI_BOARD_DESKTOP/
# LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/drivers/app_tuya_touch/tdd_touch/include
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/miscs/motion
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/drivers/app_tuya_imu/include
LOCAL_SRC_FILES := $(LOCAL_PATH)/src/boards/T5AI_BOARD_DESKTOP/tuya_device_board.c
ifeq ($(CONFIG_ENABLE_TUYA_CAMERA), y)
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/boards/T5AI_BOARD_DESKTOP/tuya_device_camera.c
endif
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/drivers/app_tuya_imu  -name "*.c" -o -name "*.cpp" -o -name "*.cc")
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/miscs/motion  -name "*.c" -o -name "*.cpp" -o -name "*.cc")
endif

ifeq ($(CONFIG_T2AI_BOARD), y)
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/boards/T2AI_BOARD/
LOCAL_SRC_FILES := $(LOCAL_PATH)/src/boards/T2AI_BOARD/tuya_device_board.c
ifeq ($(CONFIG_ENABLE_TUYA_CAMERA), y)
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/boards/T2AI_BOARD/tuya_device_camera.c
endif
endif

ifeq ($(CONFIG_T1AI_BOARD), y)
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/boards/T1AI_BOARD/
LOCAL_SRC_FILES := $(LOCAL_PATH)/src/boards/T1AI_BOARD/tuya_device_board.c
ifeq ($(CONFIG_ENABLE_TUYA_CAMERA), y)
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/boards/T1AI_BOARD/tuya_device_camera.c
endif
endif

ifeq ($(CONFIG_T3_V_PRO_BOARD), y)
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/boards/T3_V_PRO_BOARD/
LOCAL_SRC_FILES := $(LOCAL_PATH)/src/boards/T3_V_PRO_BOARD/tuya_device_board.c
ifeq ($(CONFIG_ENABLE_TUYA_CAMERA), y)
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/boards/T3_V_PRO_BOARD/tuya_device_camera.c
endif
endif

ifeq ($(CONFIG_T3AI_BOARD), y)
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/boards/T3AI_BOARD/
LOCAL_SRC_FILES := $(LOCAL_PATH)/src/boards/T3AI_BOARD/tuya_device_board.c
ifeq ($(CONFIG_ENABLE_TUYA_CAMERA), y)
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/boards/T3AI_BOARD/tuya_device_camera.c
endif
endif

ifeq ($(CONFIG_RTL8720CF_VU2_BOARD), y)
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/boards/RTL8720CF_VU2_BOARD/
LOCAL_SRC_FILES := $(LOCAL_PATH)/src/boards/RTL8720CF_VU2_BOARD/tuya_device_board.c
ifeq ($(CONFIG_ENABLE_TUYA_CAMERA), y)
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/boards/RTL8720CF_VU2_BOARD/tuya_device_camera.c
endif
endif

ifeq ($(CONFIG_WUKONG_BOARD_UBUNTU), y)
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/boards/Ubuntu/
LOCAL_SRC_FILES := $(LOCAL_PATH)/src/boards/Ubuntu/tuya_device_board.c
ifeq ($(CONFIG_ENABLE_TUYA_CAMERA), y)
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/boards/Ubuntu/tuya_device_camera.c
endif
endif

# Board common header (tuya_board_config.h)
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/boards

ifeq ($(CONFIG_ENABLE_CELLULAR_DONGLE), y)
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/miscs/cellular
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/miscs/linkpolicy
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/miscs/cellular/ -name "*.c" -o -name "*.cpp" -o -name "*.cc")
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/miscs/linkpolicy/ -name "*.c" -o -name "*.cpp" -o -name "*.cc")
endif

# 模块对外CFLAGS：其他组件编译时可感知到
VER = $(shell echo $(APP_VER) | grep -oP '\d*\.\d*\.\d*')
LOCAL_TUYA_SDK_CFLAGS = -DUSER_SW_VER=\"$(VER)\" -DAPP_BIN_NAME=\"$(APP_NAME)\" -DAI_PLAYER_SUPPORT_DEFAULT_CONSUMER=0
# wukong includes via CFLAGS (not SDK_INC) to prevent recursive find from
# pulling in tm/tests/stubs/ which shadows real headers (ty_cJSON.h etc.)
LOCAL_TUYA_SDK_CFLAGS += -I$(LOCAL_PATH)/src/wukong
LOCAL_TUYA_SDK_CFLAGS += -I$(LOCAL_PATH)/src/wukong/audio
LOCAL_TUYA_SDK_CFLAGS += -I$(LOCAL_PATH)/src/wukong/audio/input
LOCAL_TUYA_SDK_CFLAGS += -I$(LOCAL_PATH)/src/wukong/audio/output
LOCAL_TUYA_SDK_CFLAGS += -I$(LOCAL_PATH)/src/wukong/assets
LOCAL_TUYA_SDK_CFLAGS += -I$(LOCAL_PATH)/src/wukong/skills
LOCAL_TUYA_SDK_CFLAGS += -I$(LOCAL_PATH)/src/wukong/audio/frontend
LOCAL_TUYA_SDK_CFLAGS += -I$(LOCAL_PATH)/src/wukong/audio/frontend/aec_vad/tuya
LOCAL_TUYA_SDK_CFLAGS += -I$(LOCAL_PATH)/src/wukong/audio/frontend/kws
LOCAL_TUYA_SDK_CFLAGS += -I$(LOCAL_PATH)/src/wukong/mcp
LOCAL_TUYA_SDK_CFLAGS += -I$(LOCAL_PATH)/src/wukong/cron
LOCAL_TUYA_SDK_CFLAGS += -I$(LOCAL_PATH)/src/wukong/tm
LOCAL_TUYA_SDK_CFLAGS += -I$(LOCAL_PATH)/src/wukong/utility
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/wukong/provider
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/wukong/channel

# ifneq ($(APP_PACK_FLAG), 1)  
# LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/application_components
# LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/application_drivers
# endif


# 模块源代码
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/tuya_app_main.c 
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/tuya_ai_toy.c 
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/tuya_ai_toy_mcp.c 
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/tuya_ai_toy_led.c 
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/tuya_ai_toy_key.c 
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/hugo_ai_desktop.c
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/hugo_ai_face.c
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/drivers/app_tuya_driver/src/os/tal_gpio.c
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/drivers/app_tuya_driver/src/os/tal_uart.c
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/drivers/app_tuya_key -name "*.c" -o -name "*.cpp" -o -name "*.cc")
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/drivers/app_tuya_led -name "*.c" -o -name "*.cpp" -o -name "*.cc")
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/mode -name "*.c" -o -name "*.cpp" -o -name "*.cc")
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/wukong  -maxdepth 1 -name "*.c" -o -name "*.cpp" -o -name "*.cc")
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/wukong/provider/wukong_ai_provider.c
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/wukong/provider/tuya/wukong_provider_tuya.c
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/wukong/channel/wukong_ai_channel.c
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/wukong/assets -name "*.c" -o -name "*.cpp" -o -name "*.cc")
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/wukong/cron -name "*.c" -o -name "*.cpp" -o -name "*.cc")
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/wukong/tm -maxdepth 1 -name "*.c")
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/wukong/skills -name "*.c" -o -name "*.cpp" -o -name "*.cc")
ifeq ($(CONFIG_ENABLE_TUYA_TOOLKITS), y)
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/wukong/mcp -maxdepth 1 -name "*.c" -o -name "*.cpp" -o -name "*.cc")
ifeq ($(CONFIG_ENABLE_TOOLKITS_CONTROL), y)
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/wukong/mcp/tools/mcp_tool_control.c -name "*.c" -o -name "*.cpp" -o -name "*.cc")
endif
ifeq ($(CONFIG_ENABLE_TOOLKITS_TM), y) 
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/wukong/mcp/tools/mcp_tool_tm.c -name "*.c" -o -name "*.cpp" -o -name "*.cc")
endif
ifeq ($(CONFIG_ENABLE_TOOLKITS_CAMERA), y)
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/wukong/mcp/tools/mcp_tool_camera.c -name "*.c" -o -name "*.cpp" -o -name "*.cc")
endif
ifeq ($(CONFIG_ENABLE_TOOLKITS_MOTION), y)
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/wukong/mcp/tools/mcp_tool_motion.c -name "*.c" -o -name "*.cpp" -o -name "*.cc")
endif
ifeq ($(CONFIG_ENABLE_TOOLKITS_IMM), y)
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/wukong/mcp/tools/mcp_tool_imm.c -name "*.c" -o -name "*.cpp" -o -name "*.cc")
endif
ifeq ($(CONFIG_ENABLE_TOOLKITS_SOCIAL), y)
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/wukong/mcp/tools/mcp_tool_social.c -name "*.c" -o -name "*.cpp" -o -name "*.cc")
endif
ifeq ($(CONFIG_ENABLE_TOOLKITS_PLAYBACK), y)
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/wukong/mcp/tools/mcp_tool_playback.c -name "*.c" -o -name "*.cpp" -o -name "*.cc")
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/wukong/audio/wukong_playback_ctrl.c
endif
endif
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/wukong/utility -name "*.c" -o -name "*.cpp" -o -name "*.cc")
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/wukong/audio/frontend/kws/wukong_kws.c
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/wukong/audio/frontend/wukong_audio_frontend.c
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/wukong/audio/wukong_audio_player.c
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/wukong/audio/input/wukong_audio_input.c
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/wukong/audio/output/wukong_audio_output.c
ifeq ($(CONFIG_ENABLE_BATTERY), y)
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/miscs/battery  -name "*.c" -o -name "*.cpp" -o -name "*.cc")
endif
ifeq ($(CONFIG_ENABLE_AI_MF_TEST), y)
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/miscs/mftest  -name "*.c" -o -name "*.cpp" -o -name "*.cc")
endif

ifeq ($(CONFIG_ENABLE_QRCODE_ACTIVE), y)
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/miscs/libqrencode
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/miscs/libqrencode  -name "*.c" -o -name "*.cpp" -o -name "*.cc")
endif

# uart audio output
ifeq ($(CONFIG_USING_UART_AUDIO_OUTPUT), y)
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/wukong/audio/output/wukong_audio_output_uart.c
endif

# uart audio input
ifeq ($(CONFIG_USING_UART_AUDIO_INPUT), y)
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/wukong/audio/input/wukong_audio_input_uart.c
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/miscs/uart_codec/src -name "*.c" -o -name "*.cpp" -o -name "*.cc")
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/wukong/audio/frontend/kws/uart -name "*.c" -o -name "*.cpp" -o -name "*.cc")
ifeq ($(CONFIG_WUKONG_BOARD_UBUNTU), y)
LOCAL_TUYA_SDK_CFLAGS += -DUART_CODEC_SPK_FLOWCTL_YIELD_MS=30
endif
endif

# board audio output
ifeq ($(CONFIG_USING_BOARD_AUDIO_OUTPUT), y)
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/wukong/audio/output/wukong_audio_output_board.c
endif


# bpard audio input
ifeq ($(CONFIG_USING_BOARD_AUDIO_INPUT), y)
ifeq ($(CONFIG_TUYA_MODULE_T5), y)
INSTALL_SNDXKWS := $(shell mkdir -p  $(LOCAL_PATH)/../../libs/app_libs && cp $(LOCAL_PATH)/src/wukong/audio/frontend/kws/sndx/libsndxasr.a $(LOCAL_PATH)/../../libs/app_libs/ && echo "libsndxasr.a copied" >&2)
INSTALL_TUTUKWS := $(shell mkdir -p  $(LOCAL_PATH)/../../libs/app_libs && cp $(LOCAL_PATH)/src/wukong/audio/frontend/kws/tutuclear/libtutuClear.a $(LOCAL_PATH)/../../libs/app_libs/ && echo "libtutuClear.a copied" >&2)
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/wukong/audio/frontend/kws/tutuclear -name "*.c" -o -name "*.cpp" -o -name "*.cc")
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/wukong/audio/frontend/kws/sndx -name "*.c" -o -name "*.cpp" -o -name "*.cc")
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/miscs/audio_analysis  -name "*.c" -o -name "*.cpp" -o -name "*.cc")
endif
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/wukong/audio/frontend/aec_vad/tuya/wukong_audio_aec_vad.c
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/wukong/audio/input/wukong_audio_input_board.c
endif

# player audio decode
LOCAL_SRC_FILES += $(filter-out $(LOCAL_PATH)/src/miscs/audio_player/src/decoder/ogg/% \
					 $(LOCAL_PATH)/src/miscs/audio_player/src/decoder/decoder_oggopus.c \
					 $(LOCAL_PATH)/src/miscs/audio_player/src/decoder/decoder_opus.c, \
                     $(shell find $(LOCAL_PATH)/src/miscs/audio_player -name "*.c" -o -name "*.cpp" -o -name "*.cc"))
ifeq ($(CONFIG_AI_PLAYER_DECODER_OGGOPUS_ENABLE), y)
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/miscs/audio_player/src/decoder/ogg/ -name "*.c" -o -name "*.cpp" -o -name "*.cc")
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/miscs/audio_player/src/decoder/decoder_oggopus.c
endif
ifeq ($(CONFIG_AI_PLAYER_DECODER_OPUS_ENABLE), y)
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/miscs/audio_player/src/decoder/opus/ -name "*.c" -o -name "*.cpp" -o -name "*.cc")
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/miscs/audio_player/src/decoder/decoder_opus.c
endif


ifeq ($(CONFIG_ENABLE_TUYA_UI), y)
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/drivers/app_tuya_display/tdd_lcd_driver/include
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/drivers/app_tuya_display/tal_display/include
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/drivers/app_tuya_tp/tdd_tp_driver/include
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/drivers/app_tuya_tp/tal_tp/include
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/miscs/gui/display
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/miscs/gui/lvgl/include
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/miscs/gui/tuya_lvgl/include
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/miscs/gui/tuya_lvgl/src/language/env_data
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/miscs/gui/tuya_lvgl_dp2widget/include
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/miscs/gui/tuya_lvgl_fs_compatible/include
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/miscs/gui/tuya_lvgl_gesture/inc
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/miscs/gui/tuya_lvgl_gesture/include
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/miscs/gui/tuya_lvgl_weather/include
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/miscs/gui/tuya_lvgl_cloud_recipe/include
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/miscs/gui/tuya_lvgl_cloud_recipe/src/kepler_file
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/miscs/gui/tuya_lvgl_resource_download/include
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/miscs/gui/display/tuya_ai_display.c 
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/miscs/gui/display/tuya_app_gui_main.c 
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/miscs/gui/display/gui_common.c 
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/miscs/gui/display/lv_mf_test.c 
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/drivers/app_tuya_driver/src/os/tkl_touch.c
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/drivers/app_tuya_display/tdd_lcd_driver/src -name "*.c" -o -name "*.cpp" -o -name "*.cc")
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/drivers/app_tuya_display/tal_display/src -name "*.c" -o -name "*.cpp" -o -name "*.cc")
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/drivers/app_tuya_tp/tdd_tp_driver/src -name "*.c" -o -name "*.cpp" -o -name "*.cc")
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/drivers/app_tuya_tp/tal_tp/src -name "*.c" -o -name "*.cpp" -o -name "*.cc")
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/miscs/gui/display/ui/status  -name "*.c" -o -name "*.cpp" -o -name "*.cc")
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/miscs/gui/display/ui -maxdepth 1 -name "*.c" -o -name "*.cpp" -o -name "*.cc")
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/miscs/gui/tuya_lvgl/src -name "*.c" -o -name "*.cpp" -o -name "*.cc")
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/miscs/gui/tuya_lvgl_dp2widget/src -name "*.c" -o -name "*.cpp" -o -name "*.cc")
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/miscs/gui/tuya_lvgl_fs_compatible/src -name "*.c" -o -name "*.cpp" -o -name "*.cc")
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/miscs/gui/tuya_lvgl_gesture/src -name "*.c" -o -name "*.cpp" -o -name "*.cc")
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/miscs/gui/tuya_lvgl_weather/src -name "*.c" -o -name "*.cpp" -o -name "*.cc")
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/miscs/gui/tuya_lvgl_cloud_recipe/src -name "*.c" -o -name "*.cpp" -o -name "*.cc")

ifeq ($(CONFIG_T5AI_BOARD), y)

ifeq ($(CONFIG_ENABLE_VIEW_UI), y)
# view/ layout (post-refactor):
#   app_ui.c   - board-level UI entry hooks consumed by tuya_ai_display.c
#   include/   - all public/private UI headers, flat
#   runtime/   - framework internals: ui_action, ui_nav, ui_dispatch
#   screens/   - per-screen implementations (ui_home, ui_chat, ...)
#   assets/    - LVGL resources baked into firmware:
#                font/ (font C arrays), icon/ (icon C arrays),
#                res/  (PNG sources + png_to_lvgl_v8_icon_c.py generator)
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/view/include
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/view/app_ui.c
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/view/runtime -maxdepth 1 -name "*.c" -o -name "*.cpp" -o -name "*.cc")
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/view/screens -maxdepth 1 -name "*.c" -o -name "*.cpp" -o -name "*.cc")
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/view/assets -name "*.c" -o -name "*.cpp" -o -name "*.cc")
else ifeq ($(CONFIG_ENABLE_T5AI_BOARD_UI_WECHAT), y)
LOCAL_TUYA_SDK_CFLAGS += -DCONFIG_ENABLE_T5AI_BOARD_UI_WECHAT
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/boards/T5AI_BOARD/ui/wechat_app.c
endif
endif

ifeq ($(CONFIG_T5AI_BOARD_EYES), y)
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/boards/T5AI_BOARD_EYES/ui/eyes_app.c
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/boards/T5AI_BOARD_EYES/ui/eyes  -name "*.c" -o -name "*.cpp" -o -name "*.cc")
endif

ifeq ($(CONFIG_T5AI_BOARD_EVB), y)
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/boards/T5AI_BOARD_EVB/ui/xiaozhi_app.c
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/boards/T5AI_BOARD_EVB/ui/emoji  -name "*.c" -o -name "*.cpp" -o -name "*.cc")
endif

ifeq ($(CONFIG_T5AI_BOARD_EVB_PRO), y)
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/boards/T5AI_BOARD_EVB_PRO/ui/xiaozhi_app.c
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/boards/T5AI_BOARD_EVB_PRO/ui/emoji  -name "*.c" -o -name "*.cpp" -o -name "*.cc")
endif

ifeq ($(CONFIG_T5AI_BOARD_ROBOT), y)
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/miscs/servo_ctrl/ -name "*.c" -o -name "*.cpp" -o -name "*.cc")
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/miscs/gesture/ -name "*.c" -o -name "*.cpp" -o -name "*.cc")
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/boards/T5AI_BOARD_ROBOT/ui/robot_app.c
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/boards/T5AI_BOARD_ROBOT/ui/robot  -name "*.c" -o -name "*.cpp" -o -name "*.cc")
endif

ifeq ($(CONFIG_T5AI_BOARD_DESKTOP), y)
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/boards/T5AI_BOARD_DESKTOP/ui/
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/boards/T5AI_BOARD_DESKTOP/ui/res
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/boards/T5AI_BOARD_DESKTOP/ui/  -name "*.c" -o -name "*.cpp" -o -name "*.cc")
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/boards/T5AI_BOARD_DESKTOP/ui/desktop_app.c

LOCAL_SRC_FILES := $(filter-out \
  $(LOCAL_PATH)/src/miscs/gui/display/ui/ai_img.c \
  $(LOCAL_PATH)/src/miscs/gui/display/ui/font_awesome_16_4.c \
  $(LOCAL_PATH)/src/miscs/gui/display/ui/font_awesome_20_4.c \
  $(LOCAL_PATH)/src/miscs/gui/display/ui/font_awesome_30_4.c \
  $(LOCAL_PATH)/src/miscs/gui/display/ui/font_emoji_64.c \
  $(LOCAL_PATH)/src/miscs/gui/display/ui/FONT_SY_20.c \
  $(LOCAL_PATH)/src/miscs/gui/display/ui/puhui_3bp_18.c \
  $(LOCAL_PATH)/src/miscs/gui/display/ui/puhui_robot.c \
  $(LOCAL_PATH)/src/miscs/gui/display/ui/user_img.c \
  ,  $(LOCAL_SRC_FILES))
endif

# 涂鸦模组类型：
ifeq ($(CONFIG_TUYA_MODULE_T5), y)
export CONFIG_TUYA_MODULE_T5 := y
export CONFIG_TUYA_MODULE_T2AI :=
$(info --- T5 module enabled ---)
else ifeq ($(CONFIG_TUYA_MODULE_T2AI), y)
export CONFIG_TUYA_MODULE_T2AI := y
export CONFIG_TUYA_MODULE_T5 :=
$(info --- T2AI module enabled ---)
else
$(info --- unknown module type ? ---)
endif

# CPU架构选择(是否SMP)：
ifeq ($(CONFIG_TUYA_CPU_ARCH_SMP), y)
export CONFIG_TUYA_CPU_ARCH_SMP := y
$(info --- cpu arch smp enabled ---)
else
export CONFIG_TUYA_CPU_ARCH_SMP :=
$(info --- cpu arch smp disabled ---)
endif

# LVGL版本库选择(8,9)：
ifeq ($(CONFIG_TUYA_LVGL_VERSION), 9)
export TUYA_LVGL_VERSION := 9
LOCAL_TUYA_SDK_CFLAGS += -DEEZ_FOR_LVGL -DLV_LVGL_H_INCLUDE_SIMPLE -DCONFIG_FREETYPE
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/miscs/gui/lvgl/src/common
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/miscs/gui/lvgl/src/lvgl_v9/porting
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/miscs/gui/lvgl/src -name "*.c" -o -name "*.cpp" -o -name "*.cc")
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/miscs/gui/lvgl/src/lvgl_v9.3 -name "*.c" -o -name "*.cpp" -o -name "*.cc")
$(info *** menuconfig lvgl-v9 ***)
else
export TUYA_LVGL_VERSION := 8
LOCAL_TUYA_SDK_CFLAGS += -DEEZ_FOR_LVGL -DLV_LVGL_H_INCLUDE_SIMPLE -DCONFIG_FREETYPE
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/miscs/gui/lvgl/src/common
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/miscs/gui/lvgl/src/lvgl_v8/porting
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/miscs/gui/lvgl/src/common -name "*.c" -o -name "*.cpp" -o -name "*.cc")
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/miscs/gui/lvgl/src/lvgl_v8 -name "*.c" -o -name "*.cpp" -o -name "*.cc")
$(info *** menuconfig lvgl-v8 ***)
endif

# LVGL启动ARM-2D渲染(开启DSP)
ifeq ($(CONFIG_TUYA_LVGL_DRAW_WITH_ARM2D), y)
export CONFIG_TUYA_LVGL_DRAW_WITH_ARM2D := y
CFLAGS := $(filter-out -mcpu=cortex-m33+nodsp, $(CFLAGS))
CFLAGS += -mcpu=cortex-m33
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/miscs/gui/libarm2d/include
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/miscs/gui/tuya_lvgl/src/img_utility/cmsis-dsp/Include
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/miscs/gui/tuya_lvgl/src/img_utility/miniz
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/miscs/gui/tuya_lvgl/src/img_utility/libjpeg_turbo_encode
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/miscs/gui/tuya_lvgl/src/img_utility/libjpeg_turbo_decode
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/miscs/gui/libarm2d/src -name "*.c" -o -name "*.cpp" -o -name "*.cc")
$(info --- lvgl draw with arm2d enabled ---)
else
export CONFIG_TUYA_LVGL_DRAW_WITH_ARM2D :=
$(info --- lvgl draw with arm2d disabled ---)
endif

# 当前否AI SDK
ifeq ($(CONFIG_TUYA_AI_SDK), y)
export TUYA_AI_SDK := y
else
export TUYA_AI_SDK :=
endif

# jpeg解码库配置：
ifeq ($(CONFIG_TUYA_LIBJPEG_TURBO), y)
export CONFIG_TUYA_LIBJPEG_TURBO := y
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/miscs/gui/libjpegturbo/include
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/miscs/gui/libjpegturbo/src
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/miscs/gui/libjpegturbo/src -name "*.c" -o -name "*.cpp" -o -name "*.cc")
$(info --- libjpeg-turbo component enabled ---)
else
export CONFIG_TUYA_LIBJPEG_TURBO :=
$(info --- libjpeg-turbo component disabled ---)
endif

# freetype字库引擎组件配置：
ifeq ($(CONFIG_TUYA_LIB_FREETYPE), y)
export CONFIG_TUYA_LIB_FREETYPE := y
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/miscs/gui/libfreetype/include
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/miscs/gui/libfreetype/src -name "*.c" -o -name "*.cpp" -o -name "*.cc")
# gzip/*.c (except ftgzip.c) are #include'd by ftgzip.c as one TU; compiling them
# separately creates duplicate zlib symbols and collides with miniz.
LOCAL_SRC_FILES := $(filter-out \
    $(LOCAL_PATH)/src/miscs/gui/libfreetype/src/gzip/adler32.c \
    $(LOCAL_PATH)/src/miscs/gui/libfreetype/src/gzip/crc32.c \
    $(LOCAL_PATH)/src/miscs/gui/libfreetype/src/gzip/inffast.c \
    $(LOCAL_PATH)/src/miscs/gui/libfreetype/src/gzip/inflate.c \
    $(LOCAL_PATH)/src/miscs/gui/libfreetype/src/gzip/inftrees.c \
    $(LOCAL_PATH)/src/miscs/gui/libfreetype/src/gzip/zutil.c, \
    $(LOCAL_SRC_FILES))
$(info --- freetype component enabled ---)
else
export CONFIG_TUYA_LIB_FREETYPE :=
$(info --- freetype component disabled ---)
endif

LOCAL_TUYA_SDK_CFLAGS += -DTUYA_LVGL_VERSION=$(TUYA_LVGL_VERSION)
#模块对外CXXFLAGS：LVGL-v9中C++文件依赖
ifeq (${TUYA_LVGL_VERSION},9)
#$(info ***** use tuya lvgl v9 add cxxflags*****)
CXXFLAGS += -DTUYA_LVGL_VERSION=$(TUYA_LVGL_VERSION)
CXXFLAGS += $(filter-out -std=c99 -std=gnu99 -Wno-old-style-declaration, $(CFLAGS))
endif
endif

ifeq ($(CONFIG_CODEC_BENCH_TEST), y)
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/miscs/codec_bench
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/miscs/codec_bench -name "*.c" -o -name "*.cpp" -o -name "*.cc")
endif

ifeq ($(CONFIG_ENABLE_TUYA_CAMERA), y)
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/wukong/video
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/miscs/p2p
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/drivers/app_tuya_camera/tal_camera/include
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/drivers/app_tuya_camera/tal_dvp/include
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/drivers/app_tuya_camera/tdd_camera_driver/include
# camera 模块依赖 display 模块的 ty_frame_buff.h（与 CONFIG_ENABLE_TUYA_UI 解耦）
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/drivers/app_tuya_display/tal_display/include
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/tuya_ai_toy_camera.c
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/wukong/video  -name "*.c" -o -name "*.cpp" -o -name "*.cc")
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/drivers/app_tuya_camera/tal_camera/src/  -name "*.c" -o -name "*.cpp" -o -name "*.cc")
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/drivers/app_tuya_camera/tal_dvp/src/  -name "*.c" -o -name "*.cpp" -o -name "*.cc")
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/drivers/app_tuya_camera/tdd_camera_driver/src/  -name "*.c" -o -name "*.cpp" -o -name "*.cc")
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/miscs/p2p/ -name "*.c" -o -name "*.cpp" -o -name "*.cc")
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/drivers/app_tuya_camera/tal_uvc/include
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/drivers/app_tuya_camera/tal_uvc/src
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/drivers/app_tuya_camera/tal_uvc/src  -name "*.c" -o -name "*.cpp" -o -name "*.cc")
endif

ifeq ($(CONFIG_TUYA_FILE_SYSTEM), y)
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/miscs/gui/tuya_lvgl_resource_download/src -name "*.c" -o -name "*.cpp" -o -name "*.cc")
endif

ifeq ($(CONFIG_ENABLE_AI_MODE_DETECTION), y)
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/miscs/svc_media_alg/include
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/miscs/svc_media_alg/src -name "*.c" -o -name "*.cpp" -o -name "*.cc")
endif

# Wukong picture / image_album / tal_image (see build/APPconfig)
ifeq ($(CONFIG_ENABLE_TAL_IMAGE), y)
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/miscs/tal_image/include
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/miscs/tal_image/src/tjpgd
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/miscs/tal_image/src -name "*.c" -o -name "*.cpp" -o -name "*.cc")

ifeq ($(CONFIG_TUYA_LIBJPEG_TURBO), y)
LOCAL_TUYA_SDK_CFLAGS += -DENABLE_LIBJPEGTURBO=1
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/miscs/gui/libjpegturbo/include
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/miscs/gui/libjpegturbo/src
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/miscs/gui/libjpegturbo/src -name "*.c" -o -name "*.cpp" -o -name "*.cc")
endif

ifeq ($(CONFIG_TUYA_DMA2D_SHARE), y)
LOCAL_TUYA_SDK_CFLAGS += -DTUYA_DMA2D_SHARE=1
endif

$(info --- tal_image (CONFIG_ENABLE_TAL_IMAGE) enabled ---)
endif

ifeq ($(CONFIG_ENABLE_IMAGE_ALBUM), y)
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/miscs/image_album/include
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/miscs/image_album/src -name "*.c" -o -name "*.cpp" -o -name "*.cc")
ifeq ($(CONFIG_ENABLE_IMAGE_ALBUM_STORAGE_MEM), y)
LOCAL_TUYA_SDK_CFLAGS += -DENABLE_IMAGE_ALBUM_STORAGE_MEM=1
endif
ifeq ($(CONFIG_ENABLE_IMAGE_ALBUM_STORAGE_SD), y)
LOCAL_TUYA_SDK_CFLAGS += -DENABLE_IMAGE_ALBUM_STORAGE_SD=1
endif
$(info --- image_album (CONFIG_ENABLE_IMAGE_ALBUM) enabled ---)
endif

ifeq ($(CONFIG_ENABLE_TUYA_PICTURE), y)
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/wukong/picture
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/wukong/picture -name "*.c" -o -name "*.cpp" -o -name "*.cc")
LOCAL_TUYA_SDK_CFLAGS += -DENABLE_TUYA_PICTURE=1
$(info --- wukong picture (CONFIG_ENABLE_TUYA_PICTURE) enabled ---)
endif

# 模块内部CFLAGS：仅供本组件使用
LOCAL_CFLAGS :=

# 全局变量赋值
TUYA_SDK_INC += $(LOCAL_TUYA_SDK_INC)  # 此行勿修改
TUYA_SDK_CFLAGS += $(LOCAL_TUYA_SDK_CFLAGS)  # 此行勿修改

# 生成静态库
include $(BUILD_STATIC_LIBRARY)

# 生成动态库
include $(BUILD_SHARED_LIBRARY)

# 导出编译详情
include $(OUT_COMPILE_INFO)


#---------------------------------------

