#ifndef __TUYA_BOOT_SPLASH_H__
#define __TUYA_BOOT_SPLASH_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * TEF 动画开机图:起后台线程,素材画布在整屏内居中,按素材 fps 播放一遍后
 * 定格最后一帧。必须在 tuya_display_hw_init() 之后、ui_app_init() 之前调用。
 * 失败仅记日志,不阻塞启动。素材用 tools 生成:
 *   src/miscs/tef/tools/tef_encoder.py <name>.gif  →  .tef C 数组
 */
OPERATE_RET tuya_boot_splash_show_tef(const uint8_t *data, uint32_t len);

/**
 * 等待开机画面收尾并交接。**阻塞直到动画完整播完一轮**(不打断),线程退出后
 * 返回——即“动画必须播放完整,UI 才能接管”。单轮时长有界,不会无限等待。
 * 幂等;未启动/已等过时为 no-op。由 ui_app_init() 最开头调用一次,
 * 确保 LVGL 抢占显示前动画线程已退出、无并发 flush。
 */
void tuya_boot_splash_stop(void);

/**
 * 回收开机画面保留的整屏帧缓冲(s_splash_buf,~W*H*2)。
 * 必须在 LVGL 已接管显示扫描之后调用(由 ui_port 渲染若干帧后触发);
 * 过早调用会释放仍被 RGB 屏扫描的显存,导致花屏。幂等(已释放/未分配则 no-op)。
 */
void tuya_boot_splash_free_canvas(void);

#ifdef __cplusplus
}
#endif

#endif /* __TUYA_BOOT_SPLASH_H__ */
