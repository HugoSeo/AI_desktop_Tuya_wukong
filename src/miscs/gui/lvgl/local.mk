# 当前文件所在目录
LOCAL_PATH := $(call my-dir)

ifeq ($(APP_PACK_FLAG),1)  # 生成产物包的时候此组件开源输出
TUYA_APP_OPENSOURCE += $(LOCAL_PATH)
endif
#---------------------------------------

# 清除 LOCAL_xxx 变量
include $(CLEAR_VARS)

# 当前模块名
LOCAL_MODULE := $(notdir $(LOCAL_PATH))

ifneq (,$(filter 8 9,$(TUYA_LVGL_VERSION)))

ifeq (${TUYA_LVGL_VERSION},9)
$(info ------ use tuya lvgl v9 ------)
TUYAOS_ADAPTER_INC := $(TUYAOS_BUILD_PATH)/tuyaos_adapter/include
$(info TUYAOS_ADAPTER_INC is $(TUYAOS_ADAPTER_INC))
LOCAL_TUYA_LVGL_V9_CXXFLAGS := $(addprefix -I ,$(shell find $(TUYAOS_ADAPTER_INC) -type d))
LOCAL_TUYA_LVGL_V9_CXXFLAGS += -I$(TUYAOS_BUILD_PATH)/../../../include/base/include
#$(info LOCAL_TUYA_LVGL_V9_CXXFLAGS is $(LOCAL_TUYA_LVGL_V9_CXXFLAGS))
CXXFLAGS += $(LOCAL_TUYA_LVGL_V9_CXXFLAGS)
else
$(info ------ use tuya lvgl v8 ------)
endif

# 模块对外头文件（只能是目录）
# 加载至CFLAGS中提供给其他组件使用；开发框架发布此组件开源时不再单独打包至include文件夹；
ifneq ($(APP_PACK_FLAG), 1) 
LOCAL_TUYA_SDK_INC := $(LOCAL_PATH)/include
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/common
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/lvgl_v$(TUYA_LVGL_VERSION)/porting
endif

# 模块对外CFLAGS：其他组件编译时可感知到
LOCAL_TUYA_SDK_CFLAGS := -I$(LOCAL_PATH)/include
LOCAL_TUYA_SDK_CFLAGS += -I$(LOCAL_PATH)/src/common
LOCAL_TUYA_SDK_CFLAGS += -I$(LOCAL_PATH)/src/lvgl_v$(TUYA_LVGL_VERSION)/porting

# 对开发环境依赖
ifeq (${CONFIG_TUYA_MODULE_T5},y)
-include $(LOCAL_PATH)/vendor_env.mk
endif

# 模块源代码
LOCAL_SRC_FILES := $(shell find $(LOCAL_PATH)/src/lvgl_v$(TUYA_LVGL_VERSION) -name "*.c" -o -name "*.cpp" -o -name "*.cc")
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/common -name "*.c" -o -name "*.cpp" -o -name "*.cc")

# 模块内部CFLAGS：仅供本组件使用
LOCAL_CFLAGS := -I$(LOCAL_PATH)/include
LOCAL_CFLAGS += -I$(LOCAL_PATH)/src/common
LOCAL_CFLAGS += -I$(LOCAL_PATH)/src/lvgl_v$(TUYA_LVGL_VERSION)/porting

# 全局变量赋值
TUYA_SDK_INC += $(LOCAL_TUYA_SDK_INC)  # 此行勿修改
TUYA_SDK_CFLAGS += $(LOCAL_TUYA_SDK_CFLAGS)  # 此行勿修改

# 模块内部的SECTIONS： 仅本组件生效
ifeq ($(CONFIG_ENABLE_MULTI_SECTION_UPGRADE), y)
LOCAL_SECTION_NAME :=  $(CONFIG_MULTI_SECION_NAME)
endif

ifneq ($(APP_PACK_FLAG),1)  # 生成产物包的时候，开源输出不用再生成静态库
# 生成静态库
include $(BUILD_STATIC_LIBRARY)

# 生成动态库
include $(BUILD_SHARED_LIBRARY)
endif

# 导出编译详情
include $(OUT_COMPILE_INFO)

else
$(info ------ unknown tuya lvgl version-$(TUYA_LVGL_VERSION)------)
endif
#---------------------------------------
