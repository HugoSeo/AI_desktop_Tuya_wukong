# 当前文件所在目录
LOCAL_PATH := $(call my-dir)

#---------------------------------------

# 清除 LOCAL_xxx 变量
include $(CLEAR_VARS)
-include $(LOCAL_PATH)/build/tuya_app.config
-include $(LOCAL_PATH)/../../build/tuya_iot.config

# ---- 配置变更自愈 ----
# tuya_app_config.h 内容变化时清理本 app 的目标产物与库归档：增量构建不会删除被
# 配置选出去的旧 .o，且 ar 归档为增量更新（旧成员不随 .o 删除而剔除），残留会让
# 链接器选到旧配置的实现（如音频机制切换后同名符号串台）。配置未变时仅一次 md5sum。
ifneq ($(OUTPUT_DIR),)
WUKONG_APP_CFG_SUM   := $(shell md5sum $(LOCAL_PATH)/include/tuya_app_config.h 2>/dev/null | cut -d' ' -f1)
WUKONG_APP_CFG_STAMP := $(OUTPUT_DIR)/.tuya_app_config.md5
ifneq ($(WUKONG_APP_CFG_SUM),$(shell cat $(WUKONG_APP_CFG_STAMP) 2>/dev/null))
# 注意：以下产物路径与外层构建框架强耦合（OUTPUT_DIR/.objs 布局、lib<app>.a 命名来自
# scripts/mk 的 app.mk/xmake.mk），外层调整目录布局或命名时本段需同步，否则清理会静默落空
$(shell rm -rf $(OUTPUT_DIR)/.objs/static/apps/tuyaos_demo_wukong_ai \
               $(OUTPUT_DIR)/lib/libtuyaos_demo_wukong_ai.a \
               $(OUTPUT_DIR)/lib/libtuyaos_demo_wukong_ai.a.stripped; \
        mkdir -p $(OUTPUT_DIR); echo $(WUKONG_APP_CFG_SUM) > $(WUKONG_APP_CFG_STAMP))
ifneq ($(wildcard $(OUTPUT_DIR)/lib/libtuyaos_demo_wukong_ai.a),)
$(warning [app-config-guard] purge incomplete: libtuyaos_demo_wukong_ai.a still present, check build layout coupling)
endif
$(info [app-config-guard] tuya_app_config.h changed, stale app objects purged)
endif
endif

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
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/drivers/app_tuya_nfc/include
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/miscs/uart_codec/include
ifeq ($(CONFIG_ENABLE_BATTERY), y)
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/miscs/battery
endif
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/miscs/mftest
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/miscs/audio_analysis
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/miscs/audio_player
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/miscs/audio_player/include
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/miscs/audio_player/src/decoder/helix/include
ifeq ($(or $(CONFIG_AI_PLAYER_DECODER_OGGOPUS_ENABLE),$(CONFIG_AI_PLAYER_DECODER_OPUS_ENABLE),$(CONFIG_AI_PLAYER_DECODER_OPUS_VBR_ENABLE)), y)
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/miscs/audio_player/src/decoder/opus
endif
ifeq ($(CONFIG_AI_PLAYER_DECODER_OGGOPUS_ENABLE), y)
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/miscs/audio_player/src/decoder/ogg/include
endif
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/mode
# Unconditional: UI/MCP/detection/tuya_ai_toy.c include these headers
# regardless of camera/P2P config (stub-degraded when the feature is off).
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/wukong/video
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/miscs/camera

ifeq ($(CONFIG_T5AI_BOARD), y)
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/boards/T5AI_BOARD/
LOCAL_SRC_FILES := $(LOCAL_PATH)/src/boards/T5AI_BOARD/tuya_device_board.c
endif

ifeq ($(CONFIG_T5AI_BOARD_EVB), y)
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/boards/T5AI_BOARD_EVB/
LOCAL_SRC_FILES := $(LOCAL_PATH)/src/boards/T5AI_BOARD_EVB/tuya_device_board.c
endif

ifeq ($(CONFIG_T5AI_BOARD_EVB_PRO), y)  
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/boards/T5AI_BOARD_EVB_PRO/
LOCAL_SRC_FILES := $(LOCAL_PATH)/src/boards/T5AI_BOARD_EVB_PRO/tuya_device_board.c
endif

ifeq ($(CONFIG_T5AI_BOARD_EYES), y)
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/boards/T5AI_BOARD_EYES/
LOCAL_SRC_FILES := $(LOCAL_PATH)/src/boards/T5AI_BOARD_EYES/tuya_device_board.c
endif

ifeq ($(CONFIG_T5AI_BOARD_ROBOT), y)
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/boards/T5AI_BOARD_ROBOT/
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/miscs/servo_ctrl
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/miscs/gesture
LOCAL_SRC_FILES := $(LOCAL_PATH)/src/boards/T5AI_BOARD_ROBOT/tuya_device_board.c
endif

ifeq ($(CONFIG_T5AI_BOARD_DESKTOP), y)
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/boards/T5AI_BOARD_DESKTOP/
# LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/drivers/app_tuya_touch/tdd_touch/include
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/miscs/motion
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/drivers/app_tuya_imu/include
LOCAL_SRC_FILES := $(LOCAL_PATH)/src/boards/T5AI_BOARD_DESKTOP/tuya_device_board.c
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/drivers/app_tuya_imu  -name "*.c" -o -name "*.cpp" -o -name "*.cc")
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/miscs/motion  -name "*.c" -o -name "*.cpp" -o -name "*.cc")
endif

ifeq ($(CONFIG_T2AI_BOARD), y)
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/boards/T2AI_BOARD/
LOCAL_SRC_FILES := $(LOCAL_PATH)/src/boards/T2AI_BOARD/tuya_device_board.c
endif

ifeq ($(CONFIG_T1AI_BOARD), y)
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/boards/T1AI_BOARD/
LOCAL_SRC_FILES := $(LOCAL_PATH)/src/boards/T1AI_BOARD/tuya_device_board.c
endif

ifeq ($(CONFIG_T3_V_PRO_BOARD), y)
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/boards/T3_V_PRO_BOARD/
LOCAL_SRC_FILES := $(LOCAL_PATH)/src/boards/T3_V_PRO_BOARD/tuya_device_board.c
endif

ifeq ($(CONFIG_T3AI_BOARD), y)
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/boards/T3AI_BOARD/
LOCAL_SRC_FILES := $(LOCAL_PATH)/src/boards/T3AI_BOARD/tuya_device_board.c
endif

ifeq ($(CONFIG_RTL8720CF_VU2_BOARD), y)
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/boards/RTL8720CF_VU2_BOARD/
LOCAL_SRC_FILES := $(LOCAL_PATH)/src/boards/RTL8720CF_VU2_BOARD/tuya_device_board.c
endif

ifeq ($(CONFIG_WUKONG_BOARD_UBUNTU), y)
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/boards/Ubuntu/
LOCAL_SRC_FILES := $(LOCAL_PATH)/src/boards/Ubuntu/tuya_device_board.c
endif

# QEMU (mps2-an521) simulation board. QEMU has no DMA2D
# hardware, so force the simple full-refresh display port (ui_port_disp.c)
# regardless of the CONFIG_UI_DMA2D_ENABLE value baked into tuya_app_config.h;
# this must run before the DMA2D/port selection further down uses the var.
ifeq ($(CONFIG_WUKONG_BOARD_QEMU_M33), y)
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/boards/QEMU_M33_BOARD/
LOCAL_SRC_FILES := $(LOCAL_PATH)/src/boards/QEMU_M33_BOARD/tuya_device_board.c
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/boards/QEMU_M33_BOARD/tp_qemu_mouse_device.c
CONFIG_UI_DMA2D_ENABLE := n
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
# APP_VER may be empty during `make os`; fall back to git tag
VER = $(shell v='$(APP_VER)'; if [ -z "$$v" ]; then v=$$(cd $(LOCAL_PATH) && git describe --tags 2>/dev/null | sed -n 's,^\s*\(\(\w*-\)\?[0-9]\+\.[0-9]\+\(\.[0-9]\+\)\?\(-beta\.[0-9]\+\)*\).*,\1,p'); fi; echo $$v | grep -oP '\d*\.\d*\.\d*')
LOCAL_TUYA_SDK_CFLAGS = -DUSER_SW_VER=\"$(VER)\" -DAPP_BIN_NAME=\"$(APP_NAME)\"
# wukong includes via CFLAGS (not SDK_INC) to prevent recursive find from
# pulling in tm/tests/stubs/ which shadows real headers (ty_cJSON.h etc.)
LOCAL_TUYA_SDK_CFLAGS += -I$(LOCAL_PATH)/src/wukong
LOCAL_TUYA_SDK_CFLAGS += -I$(LOCAL_PATH)/src/wukong/audio
LOCAL_TUYA_SDK_CFLAGS += -I$(LOCAL_PATH)/src/wukong/audio/input
LOCAL_TUYA_SDK_CFLAGS += -I$(LOCAL_PATH)/src/wukong/audio/output
LOCAL_TUYA_SDK_CFLAGS += -I$(LOCAL_PATH)/src/wukong/assets
LOCAL_TUYA_SDK_CFLAGS += -I$(LOCAL_PATH)/src/wukong/toolkits/fc
LOCAL_TUYA_SDK_CFLAGS += -I$(LOCAL_PATH)/src/wukong/audio/frontend
LOCAL_TUYA_SDK_CFLAGS += -I$(LOCAL_PATH)/src/wukong/audio/frontend/aec_vad/tuya
LOCAL_TUYA_SDK_CFLAGS += -I$(LOCAL_PATH)/src/wukong/audio/frontend/kws
LOCAL_TUYA_SDK_CFLAGS += -I$(LOCAL_PATH)/src/wukong/toolkits/mcp
LOCAL_TUYA_SDK_CFLAGS += -I$(LOCAL_PATH)/src/wukong/cron
LOCAL_TUYA_SDK_CFLAGS += -I$(LOCAL_PATH)/src/wukong/tm
LOCAL_TUYA_SDK_CFLAGS += -I$(LOCAL_PATH)/src/wukong/storage
LOCAL_TUYA_SDK_CFLAGS += -I$(LOCAL_PATH)/src/wukong/utility
LOCAL_TUYA_SDK_CFLAGS += -I$(LOCAL_PATH)/src/wukong/provider
LOCAL_TUYA_SDK_CFLAGS += -I$(LOCAL_PATH)/src/wukong/provider/tuya
LOCAL_TUYA_SDK_CFLAGS += -I$(LOCAL_PATH)/src/wukong/provider/cube
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/wukong/channel

# src/drivers/app_tuya_display/tdd_lcd_driver/src/lcd_common.c registers the
# compiled-in LCD device list by hardware-module macro (pre-existing knob,
# not board-specific): LCD_MODULE_T35P128CQ selects lcd_rgb_ili9488_device,
# same panel T5AI_BOARD uses. Needed because lcd_common.c only recognizes
# T5AI_BOARD by name, not our board's CONFIG_WUKONG_BOARD_QEMU_M33. Must be
# added here (after the LOCAL_TUYA_SDK_CFLAGS hard-reset above), not in the
# board ifeq block near the top of this file, or it gets wiped out.
ifeq ($(CONFIG_WUKONG_BOARD_QEMU_M33), y)
LOCAL_TUYA_SDK_CFLAGS += -DLCD_MODULE_T35P128CQ=1
endif

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
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/hugo_ai_position_sensor.c
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/hugo_ai_com_lightboard.c
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/tdd_sw_i2c.c
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/drivers/app_tuya_driver/src/os/tal_gpio.c
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/drivers/app_tuya_driver/src/os/tal_uart.c
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/drivers/app_tuya_key -name "*.c" -o -name "*.cpp" -o -name "*.cc")
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/drivers/app_tuya_led -name "*.c" -o -name "*.cpp" -o -name "*.cc")
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/drivers/app_tuya_nfc -name "*.c" -o -name "*.cpp" -o -name "*.cc")
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/mode -name "*.c" -o -name "*.cpp" -o -name "*.cc")
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/wukong  -maxdepth 1 -name "*.c" -o -name "*.cpp" -o -name "*.cc")
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/wukong/provider/wukong_ai_provider.c
ifeq ($(CONFIG_ENABLE_PROVIDER_JD), y)
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/wukong/provider/jd -name "*.c")
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/wukong/provider/jd
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/wukong/provider/jd/joyinside
else ifeq ($(CONFIG_ENABLE_PROVIDER_CUBE), y)
# Cube provider: wukong vtable (cube/) + xiaozhi protocol core (cube/xiaozhi/).
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/wukong/provider/cube/wukong_provider_cube.c
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/wukong/provider/cube/xiaozhi/xiaozhi_protocol.c
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/wukong/provider/cube/xiaozhi/xiaozhi_protocol_register.c
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/wukong/provider/cube/xiaozhi/xiaozhi_protocol_websocket.c
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/wukong/provider/cube/xiaozhi/xiaozhi_server.c
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/wukong/provider/cube/xiaozhi/xiaozhi_board.c
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/wukong/provider/cube/xiaozhi
else ifeq ($(CONFIG_ENABLE_PROVIDER_CLAW), y)
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/wukong/provider/claw/wukong_provider_claw.c
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/wukong/provider/claw/llm/wukong_llm.c
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/wukong/provider/claw/config -name "*.c")
LOCAL_TUYA_SDK_CFLAGS += -I$(LOCAL_PATH)/src/wukong/provider/claw/config
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/wukong/provider/claw/cli -name "*.c")
LOCAL_TUYA_SDK_CFLAGS += -I$(LOCAL_PATH)/src/wukong/provider/claw
LOCAL_TUYA_SDK_CFLAGS += -I$(LOCAL_PATH)/src/wukong/provider/claw/llm
LOCAL_TUYA_SDK_CFLAGS += -I$(LOCAL_PATH)/src/wukong/provider/claw/cli
LOCAL_TUYA_SDK_CFLAGS += -I$(TUYA_PLATFORM_DIR)/t5_os/ap/components/bk_cli/include/bk_private
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/wukong/provider/claw/context/wukong_context.c
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/wukong/provider/claw/context/wukong_profile.c
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/wukong/provider/claw/memory/wukong_session.c
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/wukong/provider/claw/memory/wukong_compact.c
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/wukong/provider/claw/agent/wukong_agent_loop.c
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/wukong/provider/claw/memory/wukong_memory.c
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/wukong/provider/claw/tools/memory_tool.c
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/wukong/provider/claw/tools/profile_tool.c
LOCAL_TUYA_SDK_CFLAGS += -I$(LOCAL_PATH)/src/wukong/provider/claw/context
LOCAL_TUYA_SDK_CFLAGS += -I$(LOCAL_PATH)/src/wukong/provider/claw/agent
LOCAL_TUYA_SDK_CFLAGS += -I$(LOCAL_PATH)/src/wukong/provider/claw/memory
LOCAL_TUYA_SDK_CFLAGS += -I$(LOCAL_PATH)/src/wukong/provider/claw/tools
else
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/wukong/provider/tuya/wukong_provider_tuya.c
endif
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/wukong/channel/wukong_ai_channel.c
ifeq ($(CONFIG_ENABLE_CHAN_WECHAT), y)
LOCAL_TUYA_SDK_INC    += $(LOCAL_PATH)/src/wukong/channel/im_wechat
LOCAL_SRC_FILES       += $(LOCAL_PATH)/src/wukong/channel/im_wechat/im_wechat_channel.c
$(info --- WeChat iLink channel (ENABLE_CHAN_WECHAT) enabled ---)
endif
ifeq ($(CONFIG_ENABLE_CHAN_FEISHU), y)
LOCAL_TUYA_SDK_INC    += $(LOCAL_PATH)/src/wukong/channel/im_feishu
LOCAL_SRC_FILES       += $(LOCAL_PATH)/src/wukong/channel/im_feishu/feishu_proto.c
LOCAL_SRC_FILES       += $(LOCAL_PATH)/src/wukong/channel/im_feishu/feishu_channel.c
$(info --- Feishu (Lark) channel (ENABLE_CHAN_FEISHU) enabled ---)
endif
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/wukong/assets -name "*.c" -o -name "*.cpp" -o -name "*.cc")
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/wukong/cron -name "*.c" -o -name "*.cpp" -o -name "*.cc")
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/wukong/tm -maxdepth 1 -name "*.c")
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/wukong/storage -maxdepth 1 -name "*.c")
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/boards/common/wukong_storage_port.c
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/wukong/toolkits/fc -name "*.c" -o -name "*.cpp" -o -name "*.cc")
# toolkits base + skill member are provider-neutral: fc (compiled for every
# provider) dispatches through the base, so both build unconditionally.
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/wukong/toolkits/wukong_tool.c
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/wukong/toolkits/skill/wukong_skill.c
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/wukong/toolkits/skill/skill_tool.c
LOCAL_TUYA_SDK_CFLAGS += -I$(LOCAL_PATH)/src/wukong/toolkits
LOCAL_TUYA_SDK_CFLAGS += -I$(LOCAL_PATH)/src/wukong/toolkits/skill
ifeq ($(CONFIG_ENABLE_TUYA_TOOLKITS), y)
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/wukong/toolkits/wukong_toolkits.c
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/wukong/toolkits/mcp -maxdepth 1 -name "*.c" -o -name "*.cpp" -o -name "*.cc")
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/wukong/toolkits/tools/tool_init.c
ifeq ($(CONFIG_ENABLE_TOOLKITS_CONTROL), y)
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/wukong/toolkits/tools/tool_control.c -name "*.c" -o -name "*.cpp" -o -name "*.cc")
endif
ifeq ($(CONFIG_ENABLE_TOOLKITS_SYSTEM), y)
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/wukong/toolkits/tools/tool_system.c -name "*.c" -o -name "*.cpp" -o -name "*.cc")
endif
ifeq ($(CONFIG_ENABLE_TOOLKITS_TM), y)
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/wukong/toolkits/tools/tool_tm.c -name "*.c" -o -name "*.cpp" -o -name "*.cc")
endif
ifeq ($(CONFIG_ENABLE_TOOLKITS_CAMERA), y)
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/wukong/toolkits/tools/tool_camera.c -name "*.c" -o -name "*.cpp" -o -name "*.cc")
endif
ifeq ($(CONFIG_ENABLE_TOOLKITS_MOTION), y)
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/wukong/toolkits/tools/tool_motion.c -name "*.c" -o -name "*.cpp" -o -name "*.cc")
endif
ifeq ($(CONFIG_ENABLE_TOOLKITS_IMM), y)
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/wukong/toolkits/tools/tool_imm.c -name "*.c" -o -name "*.cpp" -o -name "*.cc")
endif
ifeq ($(CONFIG_ENABLE_TOOLKITS_SOCIAL), y)
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/wukong/toolkits/tools/tool_social.c -name "*.c" -o -name "*.cpp" -o -name "*.cc")
endif
ifeq ($(CONFIG_ENABLE_TOOLKITS_PLAYBACK), y)
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/wukong/toolkits/tools/tool_playback.c -name "*.c" -o -name "*.cpp" -o -name "*.cc")
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/wukong/audio/wukong_playback_ctrl.c
endif
endif
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/wukong/utility -name "*.c" -o -name "*.cpp" -o -name "*.cc")
# http_simple: lightweight HTTP helper, always compiled, no conditional
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/miscs/http_simple
LOCAL_SRC_FILES    += $(LOCAL_PATH)/src/miscs/http_simple/tuya_simple_http.c
# display 头文件路径无条件暴露:mode 源文件恒编译,其 wukong_ai_mode.h 按
# ENABLE_TUYA_UI || ENABLE_TUYA_DISPLAY 条件 include tuya_ai_display.h,
# 统一暴露头路径最简且无害(源码编译由 CONFIG_ENABLE_TUYA_DISPLAY 门控,
# 见下方 Display stack 块)
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/miscs/display
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
# ---- Audio frontend backend selection (SPRS 选中则切走，否则一律 fallback tuya) ----
ifeq ($(CONFIG_USING_SPRS_AUDIO_FRONTEND), y)
LOCAL_TUYA_SDK_CFLAGS += -I$(LOCAL_PATH)/src/wukong/audio/frontend/aec_vad/sprs
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/wukong/audio/frontend/wukong_audio_preprocess.c
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/wukong/audio/frontend/aec_vad/sprs/wukong_audio_sprs.c
INSTALL_SPRSLIB := $(shell mkdir -p $(LOCAL_PATH)/../../libs/app_libs && cp $(LOCAL_PATH)/src/wukong/audio/frontend/aec_vad/sprs/libsprsESNR_ver20260520_res20260410.a $(LOCAL_PATH)/../../libs/app_libs/ && echo "libsprsESNR copied" >&2)
else
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/wukong/audio/frontend/aec_vad/tuya/wukong_audio_aec_vad.c
endif
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/wukong/audio/input/wukong_audio_input_board.c
# 采集侧同名接口二选一（与 tal_audio 段同一开关，两层机制必须一致；见 tal_audio/README.md §7）：
#   =y 涂鸦机制 wukong pipeline（rb + process task + 应用层前端）
#   =n 平台机制直通（默认；tkl 旧链路，驱动内前端，处理后 mono 帧直入 recorder）
# 板载采集依赖 T5 专属的 tal_audio（非 T5 音频为外挂方案走 UART 链路，
# 板级配置须选 USING_UART_AUDIO_INPUT，本块不参与编译）
ifeq ($(CONFIG_ENABLE_WUKONG_AUDIO_PIPELINE), y)
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/wukong/audio/input/wukong_audio_pipeline.c
else
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/wukong/audio/input/wukong_audio_pipeline_platform.c
endif
endif

# player audio decode
# OPUS 系解码器(raw/OGG/VBR)按需编入, 与 AI_PLAYER_LITE 互斥(LITE 下三者均为 n)。
# 注: src/decoder/opus/ 目录仅含 opus 头文件(codec 实现为预编译库), 无源码可编,
#     故无需条件编入 opus 库源码, 仅需按需提供头文件路径(见 LOCAL_TUYA_SDK_INC)。
LOCAL_SRC_FILES += $(filter-out $(LOCAL_PATH)/src/miscs/audio_player/src/decoder/ogg/% \
					 $(LOCAL_PATH)/src/miscs/audio_player/src/decoder/decoder_oggopus.c \
					 $(LOCAL_PATH)/src/miscs/audio_player/src/decoder/decoder_opus.c \
					 $(LOCAL_PATH)/src/miscs/audio_player/src/decoder/decoder_opus_vbr.c, \
                     $(shell find $(LOCAL_PATH)/src/miscs/audio_player -name "*.c" -o -name "*.cpp" -o -name "*.cc"))
ifeq ($(CONFIG_AI_PLAYER_DECODER_OGGOPUS_ENABLE), y)
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/miscs/audio_player/src/decoder/ogg/ -name "*.c" -o -name "*.cpp" -o -name "*.cc")
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/miscs/audio_player/src/decoder/decoder_oggopus.c
endif
ifeq ($(CONFIG_AI_PLAYER_DECODER_OPUS_ENABLE), y)
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/miscs/audio_player/src/decoder/decoder_opus.c
endif
ifeq ($(CONFIG_AI_PLAYER_DECODER_OPUS_VBR_ENABLE), y)
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/miscs/audio_player/src/decoder/decoder_opus_vbr.c
endif


# ======== Shared DMA2D engine (decoupled from UI) ========
# Single owner of the DMA2D HW engine, used by BOTH the UI flush path and the
# camera-preview YUV->RGB conversion. Compiled whenever either consumer is on,
# so a headless (no-UI) camera build still gets HW acceleration and links.
TUYA_DMA2D_NEEDED :=
ifeq ($(CONFIG_ENABLE_TUYA_CAMERA), y)
TUYA_DMA2D_NEEDED := y
endif
ifeq ($(CONFIG_UI_DMA2D_ENABLE), y)
TUYA_DMA2D_NEEDED := y
endif
ifeq ($(TUYA_DMA2D_NEEDED), y)
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/miscs/display
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/miscs/display/tuya_dma2d.c
endif

ifeq ($(CONFIG_TUYA_MODULE_T5), y)
# Audio driver (tal_audio) -- T5 专属（板载音频 + 算法链路；T1/T2 等平台音频为外挂方案，走 UART 链路，不用 tal_audio）。
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/drivers/app_tuya_audio/tal_audio/include
# 音频机制二选一（同名接口实现；详见 tal_audio/README.md §7）：
#   =y 涂鸦机制 —— 新驱动 input/output + 其设备层 audio_devices/
#   =n 平台机制（默认）—— platform_mode/ 下两个同名接口实现（tkl 旧链路直通）
# 切换机制的残留清理由顶部"配置变更自愈"段自动完成
ifeq ($(CONFIG_ENABLE_WUKONG_AUDIO_PIPELINE), y)
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/drivers/app_tuya_audio/tal_audio/src/audio_devices -name "*.c" -o -name "*.cpp" -o -name "*.cc")
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/drivers/app_tuya_audio/tal_audio/src/tal_audio_input.c
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/drivers/app_tuya_audio/tal_audio/src/tal_audio_output.c
else
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/drivers/app_tuya_audio/tal_audio/src/platform_mode/tal_audio_input_platform.c
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/drivers/app_tuya_audio/tal_audio/src/platform_mode/tal_audio_output_platform.c
endif
endif


# ======== Display stack (LCD/TP drivers + display message bus) ========
# 与 UI 框架解耦:ENABLE_TUYA_DISPLAY 提供 tuya_ai_display 消息总线、
# tuya_display_hw 面板管理和 tal_display/tdd_lcd_driver/tal_tp/tdd_tp_driver
# 驱动栈。UI 框架依赖本块(Kconfig: ENABLE_TUYA_UI depends on ENABLE_TUYA_DISPLAY);
# 无 LVGL 的板(EYES 直推 GRAM)只开本开关。
ifeq ($(CONFIG_ENABLE_TUYA_DISPLAY), y)

# Display dispatch module (UI-agnostic message bus)
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/miscs/display/tuya_ai_display.c

# Display hardware control (LCD driver init, backlight — UI-agnostic)
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/miscs/display/tuya_display_hw.c

# Display & touch drivers
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/drivers/app_tuya_display/tdd_lcd_driver/include
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/drivers/app_tuya_display/tal_display/include
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/drivers/app_tuya_tp/tdd_tp_driver/include
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/drivers/app_tuya_tp/tal_tp/include
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/drivers/app_tuya_display/tdd_lcd_driver/src -name "*.c")
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/drivers/app_tuya_display/tal_display/src -name "*.c")
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/drivers/app_tuya_tp/tdd_tp_driver/src -name "*.c")
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/drivers/app_tuya_tp/tal_tp/src -name "*.c")
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/drivers/app_tuya_driver/src/os/tkl_touch.c

# TEF 表情动画播放器框架(miscs/tef):解码器(heatshrink/tinfl vendored) +
# 播放线程,脏块直推 GRAM,不经 LVGL。随显示栈编入:EYES 表情用常驻播放器
# (tef_player_init/play),UI 板 boot splash 用一次性播放(tef_player_play_once)。
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/miscs/tef
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/miscs/tef -name "*.c" -not -path "*/bench/*")
# TEF vs GIF 基准(CPU/内存/flash 对比):make app ... TEF_BENCH=1 编入,
# 启动时输出 [TEF-BENCH] 报告(见 miscs/tef/bench/)
ifeq ($(TEF_BENCH), 1)
LOCAL_TUYA_SDK_CFLAGS += -DTEF_BENCH=1
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/miscs/tef/bench -name "*.c")
endif

ifeq ($(CONFIG_T5AI_BOARD_EYES), y)
# st7735s 为 8bit SPI 面板:直推路径像素需字节交换(调色板加载时一次完成)
LOCAL_TUYA_SDK_CFLAGS += -DTEF_RGB565_BYTE_SWAP=1
# EYES 板侧:素材(eyes_tef,gen_assets.py 生成的 .tef C 数组 + 注册表)与接口调用(eyes_app.c)。
# 单眼素材(128x128)镜像双 panel,双眼素材(128x256)按行带路由(panel 0 = top, panel 1 = bottom)。
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/boards/T5AI_BOARD_EYES/ui/eyes_tef
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/boards/T5AI_BOARD_EYES/ui/eyes_app.c
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/boards/T5AI_BOARD_EYES/ui/eyes_tef -name "*.c")
endif

endif # CONFIG_ENABLE_TUYA_DISPLAY


ifeq ($(CONFIG_ENABLE_TUYA_UI), y)

# ======== UI Framework (Vue-inspired LVGL) ========
# 唯一 UI 实现，随 ENABLE_TUYA_UI 启用（旧 ENABLE_UI_FRAMEWORK 开关已移除）。
# LVGL 8.3.11 official source (unmodified, local copy)
UI_FW_DIR := $(LOCAL_PATH)/src/ui
LVGL_SRC_DIR := $(UI_FW_DIR)/lvgl

# RGB565 字节序反转(SPI 屏)现由 Kconfig 的 CONFIG_UI_LCD_RGB565_BYTE_SWAP 控制，
# 经 tuya_app_config.h 生成宏 UI_LCD_RGB565_BYTE_SWAP 传入 ui_port_disp_dma2d.c。

# Include paths: lv_conf.h must be found before lvgl.h
LOCAL_TUYA_SDK_CFLAGS += -DLV_CONF_INCLUDE_SIMPLE
LOCAL_TUYA_SDK_CFLAGS += -I$(UI_FW_DIR)/port
# compat shim dir: provides a minimal tuya_port_disp.h for the vendored
# libjpegturbo (which hardcodes that include) — replaces the retired old-GUI header.
LOCAL_TUYA_SDK_CFLAGS += -I$(UI_FW_DIR)/port/compat
LOCAL_TUYA_SDK_CFLAGS += -I$(LVGL_SRC_DIR)
LOCAL_TUYA_SDK_CFLAGS += -I$(LVGL_SRC_DIR)/src
LOCAL_TUYA_SDK_CFLAGS += -I$(UI_FW_DIR)/core
LOCAL_TUYA_SDK_CFLAGS += -I$(UI_FW_DIR)/style
LOCAL_TUYA_SDK_CFLAGS += -I$(UI_FW_DIR)/i18n
LOCAL_TUYA_SDK_CFLAGS += -I$(UI_FW_DIR)/widgets
LOCAL_TUYA_SDK_CFLAGS += -I$(UI_FW_DIR)/pages
LOCAL_TUYA_SDK_CFLAGS += -I$(UI_FW_DIR)/services

# Public headers (use CFLAGS to avoid recursive subdir expansion into tests/)
LOCAL_TUYA_SDK_CFLAGS += -I$(UI_FW_DIR)

# (display 消息总线 / tuya_display_hw / tal_display / tdd 驱动栈已迁至上方
#  CONFIG_ENABLE_TUYA_DISPLAY 块 —— UI 依赖显示,Kconfig 保证其必然开启)

# LVGL source files
LOCAL_SRC_FILES += $(shell find $(LVGL_SRC_DIR)/src -name "*.c")

# UI framework core (pure C, no LVGL dependency)
LOCAL_SRC_FILES += $(shell find $(UI_FW_DIR)/core -name "*.c")

# UI framework port (LVGL driver integration)
LOCAL_SRC_FILES += $(UI_FW_DIR)/port/ui_port.c
LOCAL_SRC_FILES += $(UI_FW_DIR)/port/ui_port_perf.c
LOCAL_SRC_FILES += $(UI_FW_DIR)/port/ui_port_indev.c
LOCAL_SRC_FILES += $(UI_FW_DIR)/port/tuya_ai_display_stub.c
# compat shim impl for the vendored libjpegturbo (paired with port/compat/tuya_port_disp.h)
LOCAL_SRC_FILES += $(UI_FW_DIR)/port/compat/tuya_port_disp.c
ifeq ($(CONFIG_UI_DMA2D_ENABLE), y)
LOCAL_SRC_FILES += $(UI_FW_DIR)/port/ui_port_disp_dma2d.c
else
LOCAL_SRC_FILES += $(UI_FW_DIR)/port/ui_port_disp_sw.c
endif

# UI framework style
LOCAL_SRC_FILES += $(shell find $(UI_FW_DIR)/style -name "*.c")

# UI framework i18n
LOCAL_SRC_FILES += $(shell find $(UI_FW_DIR)/i18n -name "*.c")

# UI framework widgets (reusable cross-page widgets)
LOCAL_SRC_FILES += $(shell find $(UI_FW_DIR)/widgets -name "*.c")

# UI framework pages + services — only for the standard Wukong UI.
# Board-UI boards (ROBOT/EVB/EYES) set CONFIG_UI_WUKONG_PAGES=n and skip these,
# dropping their camera/jpeg/p2p dependencies.
ifeq ($(CONFIG_UI_WUKONG_PAGES), y)
UI_ALL_PAGE_SRCS := $(shell find $(UI_FW_DIR)/pages -name "*.c" ! -name "_template.c")
UI_ALL_SVC_SRCS  := $(shell find $(UI_FW_DIR)/services -name "*.c")

# opt-out：列出「功能关闭时要剔除的文件」。新增可裁功能时加一块即可；
# core 页面/服务靠 glob 自动包含，无需登记。全开时本变量为空 → 等价现状。
UI_DISABLED_SRCS :=

ifneq ($(CONFIG_UI_FEATURE_CAMERA), y)
UI_DISABLED_SRCS += $(UI_FW_DIR)/pages/ui_page_camera.c \
                    $(UI_FW_DIR)/pages/ui_page_photo.c \
                    $(UI_FW_DIR)/services/ui_svc_camera.c \
                    $(UI_FW_DIR)/services/ui_svc_picture.c
endif
ifneq ($(CONFIG_UI_FEATURE_VIDEO), y)
UI_DISABLED_SRCS += $(UI_FW_DIR)/pages/ui_page_video.c \
                    $(UI_FW_DIR)/services/ui_svc_video.c \
                    $(UI_FW_DIR)/services/ui_svc_video_playback.c
endif
ifneq ($(CONFIG_UI_FEATURE_MUSIC), y)
UI_DISABLED_SRCS += $(UI_FW_DIR)/pages/ui_page_music.c \
                    $(UI_FW_DIR)/pages/ui_page_music_list.c \
                    $(UI_FW_DIR)/services/ui_svc_music.c
endif
ifneq ($(CONFIG_UI_FEATURE_RECORDING), y)
UI_DISABLED_SRCS += $(UI_FW_DIR)/pages/ui_page_recording.c \
                    $(UI_FW_DIR)/pages/ui_page_recording_list.c \
                    $(UI_FW_DIR)/pages/ui_page_recording_transcribe.c \
                    $(UI_FW_DIR)/services/ui_svc_recording.c \
                    $(UI_FW_DIR)/services/ui_svc_transcribe.c
endif
ifneq ($(CONFIG_UI_FEATURE_TIME), y)
UI_DISABLED_SRCS += $(UI_FW_DIR)/pages/ui_page_clock.c \
                    $(UI_FW_DIR)/pages/ui_page_schedule.c \
                    $(UI_FW_DIR)/services/ui_svc_tm.c
endif
ifneq ($(CONFIG_UI_FEATURE_DETECTION), y)
UI_DISABLED_SRCS += $(UI_FW_DIR)/pages/ui_page_detection.c \
                    $(UI_FW_DIR)/services/ui_svc_detection.c
endif
ifneq ($(CONFIG_UI_FEATURE_CALL), y)
UI_DISABLED_SRCS += $(UI_FW_DIR)/pages/ui_page_call.c \
                    $(UI_FW_DIR)/pages/ui_page_contacts.c \
                    $(UI_FW_DIR)/services/ui_svc_call.c
endif
ifneq ($(CONFIG_UI_FEATURE_FILES), y)
UI_DISABLED_SRCS += $(UI_FW_DIR)/pages/ui_page_files.c
endif
ifneq ($(CONFIG_UI_FEATURE_AUDIO_DIAG), y)
UI_DISABLED_SRCS += $(UI_FW_DIR)/pages/ui_page_audio_diag.c \
                    $(UI_FW_DIR)/services/ui_svc_audio_diag.c
endif
ifneq ($(CONFIG_UI_FEATURE_WEATHER), y)
UI_DISABLED_SRCS += $(UI_FW_DIR)/services/ui_svc_weather.c
endif

LOCAL_SRC_FILES += $(filter-out $(UI_DISABLED_SRCS), $(UI_ALL_PAGE_SRCS) $(UI_ALL_SVC_SRCS))
endif

# UI framework assets — 大字库(普惠体 CJK + montserrat 数字/图标子集)仅 wukong 页面
# 需要;板级 UI(CONFIG_UI_WUKONG_PAGES=n)自带字体,不编 assets/font/ 以省 flash。
ifeq ($(CONFIG_UI_WUKONG_PAGES), y)
LOCAL_SRC_FILES += $(shell find $(UI_FW_DIR)/assets/font -name "*.c")
endif
LOCAL_SRC_FILES += $(shell find $(UI_FW_DIR)/assets/icon -name "*.c")

# UI app entry
LOCAL_SRC_FILES += $(UI_FW_DIR)/ui_app.c

# ARM-2D GPU acceleration (DMA2D)
ifeq ($(CONFIG_UI_DMA2D_ENABLE), y)
CFLAGS := $(filter-out -mcpu=cortex-m33+nodsp, $(CFLAGS))
CFLAGS += -mcpu=cortex-m33
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/ui/vendor/libarm2d/include
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/ui/vendor/cmsis-dsp/Include
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/ui/vendor/libarm2d/src -name "*.c")
$(info --- UI DMA2D (ARM-2D) enabled ---)
endif

# ---- Board-specific UIs registered via ui_app_register_board_ui ----
# Shared old-gui support copied into boards/common/ (gui_common + status gifs + fonts).
ifeq ($(CONFIG_T5AI_BOARD_ROBOT), y)
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/boards/common
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/boards/common/gui_common.c
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/boards/common/res/fonts/puhui_robot.c
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/boards/common/res/fonts/font_awesome_16_4.c
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/boards/T5AI_BOARD_ROBOT/ui/robot -name "*.c" -o -name "*.cpp" -o -name "*.cc")
# ROBOT 表情/状态栏动图：TEF 素材(gen_assets.py 生成，lv_tef 播放，lv_gif 已退役)
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/boards/T5AI_BOARD_ROBOT/ui/robot_tef
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/boards/T5AI_BOARD_ROBOT/ui/robot_tef -name "*.c")
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/miscs/servo_ctrl/ -name "*.c" -o -name "*.cpp" -o -name "*.cc")
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/miscs/gesture/ -name "*.c" -o -name "*.cpp" -o -name "*.cc")
# 字节交换由 CONFIG_UI_LCD_RGB565_BYTE_SWAP（appconfig）控制，见 src/ui/Kconfig
endif

# EVB / EVB_PRO (小智 xiaozhi_app): shared gui_common + status gifs +
# puhui_3bp_18 / font_awesome_16_4 / font_emoji_64 fonts; emoji images stay board-local.
ifeq ($(CONFIG_T5AI_BOARD_EVB), y)
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/boards/common
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/boards/common/gui_common.c
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/boards/common/res/fonts/puhui_3bp_18.c
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/boards/common/res/fonts/font_awesome_16_4.c
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/boards/common/res/fonts/font_emoji_64.c
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/boards/T5AI_BOARD_EVB/ui/xiaozhi_app.c
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/boards/T5AI_BOARD_EVB/ui/emoji -name "*.c" -o -name "*.cpp" -o -name "*.cc")
endif

ifeq ($(CONFIG_T5AI_BOARD_EVB_PRO), y)
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/boards/common
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/boards/common/gui_common.c
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/boards/common/res/fonts/puhui_3bp_18.c
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/boards/common/res/fonts/font_awesome_16_4.c
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/boards/common/res/fonts/font_emoji_64.c
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/boards/T5AI_BOARD_EVB_PRO/ui/xiaozhi_app.c
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/boards/T5AI_BOARD_EVB_PRO/ui/emoji -name "*.c" -o -name "*.cpp" -o -name "*.cc")
endif

# (EYES 的 TEF 表情播放器在上方 CONFIG_ENABLE_TUYA_DISPLAY 块中编入,
#  与 UI 框架无关 —— EYES 直推 GRAM,不再走 lv_gif。)

# ---- Boot splash (LVGL-independent)
# TEF 动画开机图:解码走 miscs/tef 的 tef_player_play_once(随上方
# CONFIG_ENABLE_TUYA_DISPLAY 块编入,UI 依赖显示,Kconfig 保证其必然开启)。
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/boards/common/boot_splash
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/boards/common/boot_splash -name "*.c")

$(info --- UI Framework (Vue-LVGL) enabled ---)

endif # CONFIG_ENABLE_TUYA_UI

# ======== General app configuration (independent of UI choice) ========

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

# 当前否AI SDK
ifeq ($(CONFIG_TUYA_AI_SDK), y)
export TUYA_AI_SDK := y
else
export TUYA_AI_SDK :=
endif

ifeq ($(CONFIG_CODEC_BENCH_TEST), y)
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/miscs/codec_bench
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/miscs/codec_bench -name "*.c" -o -name "*.cpp" -o -name "*.cc")
endif

# ======== Video service framework (single video data source) ========
# Compiled whenever any producer/consumer exists: camera input or P2P.
TUYA_VIDEO_NEEDED :=
ifeq ($(CONFIG_ENABLE_TUYA_CAMERA), y)
TUYA_VIDEO_NEEDED := y
endif
ifeq ($(CONFIG_ENABLE_AI_MODE_P2P), y)
TUYA_VIDEO_NEEDED := y
endif
ifeq ($(TUYA_VIDEO_NEEDED), y)
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/wukong/video/wukong_video_input.c
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/wukong/video/wukong_video_service.c
endif

# ======== Camera video input source (tal_camera) ========
ifeq ($(CONFIG_USING_CAMERA_VIDEO_INPUT), y)
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/wukong/video/video_input_camera.c
endif

# ======== Local AVI record + callback playback (video-only) ========
ifeq ($(CONFIG_ENABLE_TY_AVI_MEDIA), y)
TY_AVI_DIR := $(LOCAL_PATH)/src/miscs/tuya_avi_service
LOCAL_TUYA_SDK_INC += $(TY_AVI_DIR)/include
LOCAL_TUYA_SDK_INC += $(TY_AVI_DIR)/recorder/include
LOCAL_TUYA_SDK_INC += $(TY_AVI_DIR)/third_party
LOCAL_TUYA_SDK_INC += $(TY_AVI_DIR)/src
LOCAL_SRC_FILES += $(TY_AVI_DIR)/src/avi_port.c
LOCAL_SRC_FILES += $(TY_AVI_DIR)/src/ty_video_osi_wrapper.c
LOCAL_SRC_FILES += $(TY_AVI_DIR)/src/ty_avi_recorder_adapter.c
LOCAL_SRC_FILES += $(TY_AVI_DIR)/src/ty_avi_player.c
LOCAL_SRC_FILES += $(shell find $(TY_AVI_DIR)/recorder -maxdepth 1 -name "*.c")
LOCAL_TUYA_SDK_CFLAGS += -DENABLE_TY_AVI_MEDIA=1
LOCAL_TUYA_SDK_CFLAGS += -DTY_AVI_ENABLE_AUDIO=0 -DTY_AVI_ENABLE_PLAYBACK=0
# The SDK already links an older monolithic libavi.a. Putting the adapted
# archive in libs/app_libs would make both variants participate in the final
# link and duplicate every common AVI_* symbol. Extract the adapted archive's
# objects into the application-components object tree instead: that archive is
# linked after the app and before the SDK libraries, so it is the sole provider
# selected for all AVI calls. The supplied objects target the BK7258/M33 ABI.
TY_AVI_PREBUILT_OBJ_DIR := $(_XMAKE_OBJS_DIR)/static/application_components/tuya_avi_service_prebuilt
TY_AVI_PREBUILT_STAMP := $(TY_AVI_PREBUILT_OBJ_DIR)/.extracted
$(TY_AVI_PREBUILT_STAMP): $(TY_AVI_DIR)/third_party/libavi.a
	@rm -rf $(TY_AVI_PREBUILT_OBJ_DIR)
	@mkdir -p $(TY_AVI_PREBUILT_OBJ_DIR)
	@cd $(TY_AVI_PREBUILT_OBJ_DIR) && $(AR) x $(abspath $<) && \
		for obj in *.obj; do mv "$$obj" "$${obj%.obj}.o"; done
	@touch $@
app_comp_static: $(TY_AVI_PREBUILT_STAMP)
$(info --- local AVI media (CONFIG_ENABLE_TY_AVI_MEDIA) enabled ---)
endif

# ======== Camera drivers + camera business layer ========
ifeq ($(CONFIG_ENABLE_TUYA_CAMERA), y)
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/drivers/app_tuya_camera/tal_camera/include
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/drivers/app_tuya_camera/tal_dvp/include
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/drivers/app_tuya_camera/tdd_camera_driver/include
# camera 模块依赖 display 模块的 ty_frame_buff.h（与 CONFIG_ENABLE_TUYA_UI 解耦）
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/drivers/app_tuya_display/tal_display/include
# (Removed old-GUI INCs lvgl/src/common + tuya_lvgl/include — tal_uvc no longer
#  includes tuya_port_disp.h; the new UI framework retires src/miscs/gui.)
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/drivers/app_tuya_camera/tal_camera/src/  -name "*.c" -o -name "*.cpp" -o -name "*.cc")
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/drivers/app_tuya_camera/tal_dvp/src/  -name "*.c" -o -name "*.cpp" -o -name "*.cc")
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/drivers/app_tuya_camera/tdd_camera_driver/src/  -name "*.c" -o -name "*.cpp" -o -name "*.cc")
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/drivers/app_tuya_camera/tal_uvc/include
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/drivers/app_tuya_camera/tal_uvc/src
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/drivers/app_tuya_camera/tal_uvc/src  -name "*.c" -o -name "*.cpp" -o -name "*.cc")
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/miscs/camera/tuya_camera.c
endif

# ======== P2P call business ========
ifeq ($(CONFIG_ENABLE_AI_MODE_P2P), y)
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/miscs/p2p
# TMM 三层（Manager/Stream/Control）以源码内嵌，不再作为 SDK 组件依赖（见 docs/adr）
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/miscs/p2p/tuya_tmm_control/include
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/miscs/p2p/tuya_tmm_manager/include
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/miscs/p2p/tuya_tmm_stream/include
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/miscs/p2p/ -name "*.c" -o -name "*.cpp" -o -name "*.cc")
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
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/ui/vendor/libjpegturbo/include
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/ui/vendor/libjpegturbo/src
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/ui/vendor/libjpegturbo/src -name "*.c" -o -name "*.cpp" -o -name "*.cc")
endif

# (TUYA_DMA2D_SHARE 已彻底移除：DMA2D 硬件加速统一由 miscs/display/tuya_dma2d
#  提供，按 ENABLE_TUYA_CAMERA || UI_DMA2D_ENABLE 编入；tal_image 仅保留纯软件转换。)

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

ifeq ($(CONFIG_ENABLE_CLI_TEXT_INPUT), y)
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/wukong/cli
LOCAL_TUYA_SDK_CFLAGS += -I$(TUYA_PLATFORM_DIR)/t5_os/ap/components/bk_cli/include/bk_private
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/wukong/cli -name "*.c")
LOCAL_TUYA_SDK_CFLAGS += -DENABLE_CLI_TEXT_INPUT=1
$(info --- cli text input (CONFIG_ENABLE_CLI_TEXT_INPUT) enabled ---)
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
