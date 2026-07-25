# 当前文件所在目录
#LOCAL_PATH := $(call my-dir)

# 当前cpu为SMP架构
ifeq ($(CONFIG_TUYA_CPU_ARCH_SMP), y)
$(info --- cpu smp enabled in lvgl app component ---)
# LVGL对freetype的依赖及v9对RTOS的依赖
ifeq ($(CONFIG_TUYA_LIB_FREETYPE), y)
#$(info ... depend tuya freetype app component ...)
else
#$(info ... depend vendor freetype ...)
LOCAL_TUYA_SDK_CFLAGS += -I$(TUYAOS_BUILD_PATH)/../t5_os/ap/include/modules/freetype
endif
LOCAL_TUYA_SDK_CFLAGS += -I$(TUYAOS_BUILD_PATH)/../t5_os/ap/components/fatfs/
LOCAL_TUYA_SDK_CFLAGS += -I$(TUYAOS_BUILD_PATH)/../t5_os/ap/components/bk_rtos/freertos
LOCAL_TUYA_SDK_CFLAGS += -I$(TUYAOS_BUILD_PATH)/../t5_os/ap/components/os_source/freertos_smp_v2p0/FreeRTOS-Kernel/include/freertos
LOCAL_TUYA_SDK_CFLAGS += -I$(TUYAOS_BUILD_PATH)/../t5_os/ap/components/os_source/freertos_smp_v2p0/FreeRTOS-Kernel/portable/GCC/ARM_CM33_NTZ/non_secure
else
$(info --- cpu smp disabled in lvgl app component ---)
# LVGL对RTOS的依赖
LOCAL_TUYA_SDK_CFLAGS += -I$(TUYAOS_BUILD_PATH)/../t5_os/bk_idk/components/bk_rtos/freertos
LOCAL_TUYA_SDK_CFLAGS += -I$(TUYAOS_BUILD_PATH)/../t5_os/bk_idk/components/os_source/freertos_v10/include
LOCAL_TUYA_SDK_CFLAGS += -I$(TUYAOS_BUILD_PATH)/../t5_os/bk_idk/components/os_source/freertos_v10/portable/GCC/ARM_CM33_NTZ/non_secure
# LVGL对freetype的依赖
LOCAL_TUYA_SDK_CFLAGS += -I$(TUYAOS_BUILD_PATH)/../t5_os/components/freetype/include
LOCAL_TUYA_SDK_CFLAGS += -I$(TUYAOS_BUILD_PATH)/../t5_os/bk_idk/components/fatfs/
LOCAL_TUYA_SDK_CFLAGS += -I$(TUYAOS_BUILD_PATH)/../t5_os/bk_idk/include/modules/freetype
endif

