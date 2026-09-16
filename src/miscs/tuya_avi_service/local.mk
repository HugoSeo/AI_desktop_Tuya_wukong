LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := $(notdir $(LOCAL_PATH))

LOCAL_TUYA_SDK_INC := $(LOCAL_PATH)/include \
                      $(LOCAL_PATH)/recorder/include \
                      $(LOCAL_PATH)/third_party \
                      $(LOCAL_PATH)/src \
                      $(LOCAL_PATH)/../tal_image/include

LOCAL_TUYA_SDK_CFLAGS := -DENABLE_TY_AVI_MEDIA=1 \
                         -DTY_AVI_ENABLE_AUDIO=0 \
                         -DTY_AVI_ENABLE_PLAYBACK=0

# Demo-safe sources only: callback video playback + video-only recording.
# The legacy ADC/DAC helper is intentionally not part of this component.
LOCAL_SRC_FILES := $(LOCAL_PATH)/src/avi_port.c \
                   $(LOCAL_PATH)/src/ty_video_osi_wrapper.c \
                   $(LOCAL_PATH)/src/ty_avi_recorder_adapter.c \
                   $(LOCAL_PATH)/src/ty_avi_player.c \
                   $(LOCAL_PATH)/src/ty_avi_playlist.c \
                   $(shell find $(LOCAL_PATH)/recorder -maxdepth 1 -name "*.c")

LOCAL_CFLAGS :=

TUYA_SDK_INC += $(LOCAL_TUYA_SDK_INC)
TUYA_SDK_CFLAGS += $(LOCAL_TUYA_SDK_CFLAGS)

include $(BUILD_STATIC_LIBRARY)
include $(BUILD_SHARED_LIBRARY)
include $(OUT_COMPILE_INFO)
