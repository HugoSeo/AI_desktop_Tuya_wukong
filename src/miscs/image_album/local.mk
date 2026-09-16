# 当前文件所在目录
LOCAL_PATH := $(call my-dir)

#---------------------------------------
ifeq ($(APP_PACK_FLAG), 1) 
# 组件对外开源
TUYA_APP_OPENSOURCE += $(LOCAL_PATH)

# 加载至CFLAGS中提供给其他组件使用；打包进SDK产物中；
LOCAL_TUYA_SDK_INC := $(LOCAL_PATH)/include

# 全局变量赋值
TUYA_APP_OPENSOURCE_INC += $(LOCAL_TUYA_SDK_INC)  # 此行勿修改

else
# 清除 LOCAL_xxx 变量
include $(CLEAR_VARS)

# 当前模块名
LOCAL_MODULE := $(notdir $(LOCAL_PATH))

# 模块对外头文件（只能是目录）
# 加载至CFLAGS中提供给其他组件使用；打包进SDK产物中；
LOCAL_TUYA_SDK_INC := $(LOCAL_PATH)/include

# 模块对外CFLAGS：其他组件编译时可感知到
LOCAL_TUYA_SDK_CFLAGS := 

# 模块源代码
LOCAL_SRC_FILES := $(shell find $(LOCAL_PATH)/src -name "*.c" -o -name "*.cpp" -o -name "*.cc")

# 模块内部CFLAGS：仅供本组件使用
LOCAL_CFLAGS := -DENABLE_IMAGE_ALBUM_STORAGE_MEM=1
# SD/文件系统后端源文件（storage/image_album_storage_sd.c）随上面的
# find 通配无条件编译，不受本行开关控制。它不在此自注册：由 app 侧（如
# wukong_picture）调用 image_album_storage_sd_register_with_root() 装配，
# 因为它需要一个本组件不持有的根路径（wukong_storage 挂载点）。
# ENABLE_IMAGE_ALBUM_STORAGE_SD 只是消费方（app/wukong_picture Kconfig）
# 用来决定要不要发起这次装配调用的开关，与本组件的编译无关。

# 全局变量赋值
TUYA_SDK_INC += $(LOCAL_TUYA_SDK_INC)  # 此行勿修改
TUYA_SDK_CFLAGS += $(LOCAL_TUYA_SDK_CFLAGS)  # 此行勿修改

# 生成静态库
include $(BUILD_STATIC_LIBRARY)

# 生成动态库
include $(BUILD_SHARED_LIBRARY)

# 导出编译详情
include $(OUT_COMPILE_INFO)

endif
#---------------------------------------

