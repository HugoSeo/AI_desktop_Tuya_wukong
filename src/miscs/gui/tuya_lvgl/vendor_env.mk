# 当前文件所在目录
#LOCAL_PATH := $(call my-dir)

# 当前cpu为SMP架构
ifeq ($(CONFIG_TUYA_CPU_ARCH_SMP), y)
$(info --- cpu smp enabled in core app component---)
LOCAL_TUYA_SDK_CFLAGS += -I$(TUYAOS_BUILD_PATH)/../t5_os/build/bk7258/tuya_app/bk7258/config
LOCAL_TUYA_SDK_CFLAGS += -I$(TUYAOS_BUILD_PATH)/../t5_os/build/bk7258/tuya_app/bk7258/armino_as_lib/bk7258/config
LOCAL_TUYA_SDK_CFLAGS += -I$(TUYAOS_BUILD_PATH)/../t5_os/build/bk7258/tuya_app/bk7258_ap/config
LOCAL_TUYA_SDK_CFLAGS += -I$(TUYAOS_BUILD_PATH)/../t5_os/build/bk7258/tuya_app/bk7258_ap/armino_as_lib/bk7258_ap/config

LOCAL_TUYA_SDK_CFLAGS += -I$(TUYAOS_BUILD_PATH)/../t5_os/ap/include
LOCAL_TUYA_SDK_CFLAGS += -I$(TUYAOS_BUILD_PATH)/../t5_os/ap/include/common
LOCAL_TUYA_SDK_CFLAGS += -I$(TUYAOS_BUILD_PATH)/../t5_os/ap/components/bk_rtos/include
LOCAL_TUYA_SDK_CFLAGS += -I$(TUYAOS_BUILD_PATH)/../t5_os/ap/components/bk_libs/bk7258_ap/config
LOCAL_TUYA_SDK_CFLAGS += -I$(TUYAOS_BUILD_PATH)/../t5_os/ap/middleware/driver/include/bk_private
LOCAL_TUYA_SDK_CFLAGS += -I$(TUYAOS_BUILD_PATH)/../t5_os/ap/components/lwip_intf_v2_1/lwip-2.1.2/port
LOCAL_TUYA_SDK_CFLAGS += -I$(TUYAOS_BUILD_PATH)/../t5_os/ap/components/lwip_intf_v2_1/lwip-2.1.2/src/include
LOCAL_TUYA_SDK_CFLAGS += -I$(TUYAOS_BUILD_PATH)/../t5_os/ap/components/lwip_intf_v2_1/lwip-2.1.2/src/include/compat
LOCAL_TUYA_SDK_CFLAGS += -I$(TUYAOS_BUILD_PATH)/../t5_os/ap/components/lwip_intf_v2_1/lwip-2.1.2/src/include/lwip
LOCAL_TUYA_SDK_CFLAGS += -I$(TUYAOS_BUILD_PATH)/../t5_os/ap/components/lwip_intf_v2_1/lwip-2.1.2/src/include/netif

LOCAL_TUYA_SDK_CFLAGS += -I$(TUYAOS_BUILD_PATH)/../t5_os/ap/components/bk_vfs
LOCAL_TUYA_SDK_CFLAGS += -I$(TUYAOS_BUILD_PATH)/../t5_os/ap/components/multimedia/include
LOCAL_TUYA_SDK_CFLAGS += -I$(TUYAOS_BUILD_PATH)/../t5_os/ap/components/multimedia/mailbox
LOCAL_TUYA_SDK_CFLAGS += -I$(TUYAOS_BUILD_PATH)/../t5_os/ap/components/bk_common/include
LOCAL_TUYA_SDK_CFLAGS += -I$(TUYAOS_BUILD_PATH)/../t5_os/ap/components/media_service/include
LOCAL_TUYA_SDK_CFLAGS += -I$(TUYAOS_BUILD_PATH)/../t5_os/ap/components/multimedia/pipeline

else
$(info --- cpu smp disabled in core app component---)
LOCAL_TUYA_SDK_CFLAGS += -I$(TUYAOS_BUILD_PATH)/../t5_os/build/bk7258/config
LOCAL_TUYA_SDK_CFLAGS += -I$(TUYAOS_BUILD_PATH)/../t5_os/build/bk7258/armino_as_lib/bk7258/config
LOCAL_TUYA_SDK_CFLAGS += -I$(TUYAOS_BUILD_PATH)/../t5_os/build/bk7258_cp1/config
LOCAL_TUYA_SDK_CFLAGS += -I$(TUYAOS_BUILD_PATH)/../t5_os/build/bk7258_cp1/armino_as_lib/bk7258_cp1/config
LOCAL_TUYA_SDK_CFLAGS += -I$(TUYAOS_BUILD_PATH)/../t5_os/build/bk7258_cp2/config
LOCAL_TUYA_SDK_CFLAGS += -I$(TUYAOS_BUILD_PATH)/../t5_os/build/bk7258_cp2/armino_as_lib/bk7258_cp2/config
LOCAL_TUYA_SDK_CFLAGS += -I$(TUYAOS_BUILD_PATH)/../t5_os/bk_idk/middleware/driver/include/bk_private
LOCAL_TUYA_SDK_CFLAGS += -I$(TUYAOS_BUILD_PATH)/../t5_os/bk_idk/include
LOCAL_TUYA_SDK_CFLAGS += -I$(TUYAOS_BUILD_PATH)/../t5_os/bk_idk/include/common
LOCAL_TUYA_SDK_CFLAGS += -I$(TUYAOS_BUILD_PATH)/../t5_os/bk_idk/components/bk_rtos/include
LOCAL_TUYA_SDK_CFLAGS += -I$(TUYAOS_BUILD_PATH)/../t5_os/bk_idk/components/lwip_intf_v2_1/lwip-2.1.2/port
LOCAL_TUYA_SDK_CFLAGS += -I$(TUYAOS_BUILD_PATH)/../t5_os/bk_idk/components/lwip_intf_v2_1/lwip-2.1.2/src/include
LOCAL_TUYA_SDK_CFLAGS += -I$(TUYAOS_BUILD_PATH)/../t5_os/bk_idk/components/lwip_intf_v2_1/lwip-2.1.2/src/include/compat
LOCAL_TUYA_SDK_CFLAGS += -I$(TUYAOS_BUILD_PATH)/../t5_os/bk_idk/components/lwip_intf_v2_1/lwip-2.1.2/src/include/lwip
LOCAL_TUYA_SDK_CFLAGS += -I$(TUYAOS_BUILD_PATH)/../t5_os/bk_idk/components/lwip_intf_v2_1/lwip-2.1.2/src/include/netif

LOCAL_TUYA_SDK_CFLAGS += -I$(TUYAOS_BUILD_PATH)/../t5_os/components/multimedia/include
LOCAL_TUYA_SDK_CFLAGS += -I$(TUYAOS_BUILD_PATH)/../t5_os/components/multimedia/mailbox
LOCAL_TUYA_SDK_CFLAGS += -I$(TUYAOS_BUILD_PATH)/../t5_os/components/media_service/include
LOCAL_TUYA_SDK_CFLAGS += -I$(TUYAOS_BUILD_PATH)/../t5_os/components/multimedia/pipeline

LOCAL_TUYA_SDK_CFLAGS += -I$(TUYAOS_BUILD_PATH)/../t5_os/bk_idk/components/bk_vfs
LOCAL_TUYA_SDK_CFLAGS += -I$(TUYAOS_BUILD_PATH)/../t5_os/bk_idk/components/bk_common/include

LOCAL_TUYA_SDK_CFLAGS += -I$(TUYAOS_BUILD_PATH)/../t5_os/bk_idk/components/bk_rtos/freertos
LOCAL_TUYA_SDK_CFLAGS += -I$(TUYAOS_BUILD_PATH)/../t5_os/bk_idk/components/os_source/freertos_v10/include
LOCAL_TUYA_SDK_CFLAGS += -I$(TUYAOS_BUILD_PATH)/../t5_os/bk_idk/components/os_source/freertos_v10/portable/GCC/ARM_CM33_NTZ/non_secure
endif

