# 当前文件所在目录
LOCAL_PATH := $(call my-dir)

#ifeq ($(APP_PACK_FLAG),1)  # 生成产物包的时候此组件开源输出
#TUYA_APP_OPENSOURCE += $(LOCAL_PATH)
#endif

#---------------------------------------

# 清除 LOCAL_xxx 变量
include $(CLEAR_VARS)

# 模块对外头文件（只能是目录）
# 加载至CFLAGS中提供给其他组件使用；打包进SDK产物中；
ifeq ($(APP_PACK_FLAG),1)
LOCAL_TUYA_SDK_INC := $(LOCAL_PATH)/include
#LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/inc
endif

# 当前模块名
LOCAL_MODULE := $(notdir $(LOCAL_PATH))

# 模块源代码
LOCAL_SRC_FILES := $(shell find $(LOCAL_PATH)/src -name "*.c" -o -name "*.cpp" -o -name "*.cc")

# 模块的 CFLAGS
LOCAL_CFLAGS := -I$(LOCAL_PATH)/include -I$(LOCAL_PATH)/include/compatible -I$(LOCAL_PATH)/inc -Wall -Werror -Wno-unused-function

TUYA_SDK_CFLAGS += -I$(LOCAL_PATH)/include -I$(LOCAL_PATH)/include/compatible -I$(LOCAL_PATH)/inc 

# 全局变量赋值
TUYA_SDK_INC += $(LOCAL_TUYA_SDK_INC)  # 此行勿修改

# 模块内部的SECTIONS： 仅本组件生效
ifeq ($(CONFIG_ENABLE_MULTI_SECTION_UPGRADE), y)
LOCAL_SECTION_NAME :=  $(CONFIG_MULTI_SECION_NAME)
endif

# 生成静态库
include $(BUILD_STATIC_LIBRARY)

# 生成动态库
include $(BUILD_SHARED_LIBRARY)

#---------------------------------------

