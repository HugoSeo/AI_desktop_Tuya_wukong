# 当前文件所在目录
LOCAL_PATH := $(call my-dir)

#---------------------------------------

# 清除 LOCAL_xxx 变量
include $(CLEAR_VARS)

ifeq ($(CONFIG_ENABLE_AI_PLAYER),y)

# 当前模块名
LOCAL_MODULE := $(notdir $(LOCAL_PATH))

# 模块对外头文件（只能是目录）
# 加载至CFLAGS中提供给其他组件使用；打包进SDK产物中；
TUYA_SDK_INC += $(LOCAL_PATH)/include

# 模块对外CFLAGS：其他组件编译时可感知到
TUYA_SDK_CFLAGS +=

# 模块源代码
LOCAL_SRC_FILES := $(filter-out $(LOCAL_PATH)/src/decoder/ogg/% \
					$(LOCAL_PATH)/src/decoder/decoder_oggopus.c \
					$(LOCAL_PATH)/src/decoder/decoder_opus.c, \
					$(shell find $(LOCAL_PATH)/src -name "*.c" -o -name "*.cpp" -o -name "*.cc"))
ifeq ($(CONFIG_AI_PLAYER_DECODER_OGGOPUS_ENABLE), y)
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/decoder/ogg/ -name "*.c" -o -name "*.cpp" -o -name "*.cc")
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/decoder/decoder_oggopus.c
endif
ifeq ($(CONFIG_AI_PLAYER_DECODER_OPUS_ENABLE), y)
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/decoder/opus/ -name "*.c" -o -name "*.cpp" -o -name "*.cc")
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/decoder/decoder_opus.c
endif

ifeq ($(CONFIG_AI_PLAYER_LITE), y)
# Exclude datasink, resample, and mixer (not needed in lite mode)
LOCAL_SRC_FILES := $(filter-out \
    $(LOCAL_PATH)/src/datasink/% \
    $(LOCAL_PATH)/src/resample/% \
    $(LOCAL_PATH)/src/mixer/%, \
    $(LOCAL_SRC_FILES))
LOCAL_SRC_FILES += $(LOCAL_PATH)/src/svc_ai_player_lite.c
endif

# 模块内部CFLAGS：仅供本组件使用
LOCAL_CFLAGS :=

LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/decoder/helix/include
ifeq ($(or $(CONFIG_AI_PLAYER_DECODER_OGGOPUS_ENABLE),$(CONFIG_AI_PLAYER_DECODER_OPUS_ENABLE)), y)
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/decoder/opus
endif
ifeq ($(CONFIG_AI_PLAYER_DECODER_OGGOPUS_ENABLE), y)
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/decoder/ogg/include
endif

TUYA_SDK_INC += $(LOCAL_TUYA_SDK_INC)  # 此行勿修改
TUYA_SDK_CFLAGS += $(LOCAL_TUYA_SDK_CFLAGS)  # 此行勿修改

# 生成静态库
include $(BUILD_STATIC_LIBRARY)

# 生成动态库
include $(BUILD_SHARED_LIBRARY)

endif # CONFIG_ENABLE_AI_PLAYER

#---------------------------------------

