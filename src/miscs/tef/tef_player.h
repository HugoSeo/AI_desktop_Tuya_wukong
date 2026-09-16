/**
 * @file tef_player.h
 * @brief TEF (Tuya Emotion Format) 表情动画播放器 — 脏块直推 GRAM。
 *
 * 独立于 LVGL 的动画通路：解码线程按素材 fps 节拍解出每帧的脏 tile，
 * 通过 tal_display_flush 子窗口直推到 SPI 屏 GRAM（未变化区域由 GRAM 保持）。
 * 素材为编译进固件的 .tef 常量数组（见 ui/eyes_tef/）。
 *
 * @copyright Copyright (c) tuya.inc 2026
 */
#ifndef __TEF_PLAYER_H__
#define __TEF_PLAYER_H__

#include <stdint.h>
#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化播放器：枚举物理 panel、创建解码线程。
 *        须在 tuya_display_hw_init() 之后调用（app_ui_init 时机即可）。
 */
OPERATE_RET tef_player_init(void);

/**
 * @brief 切换播放素材（异步，下一帧边界生效；循环播放直至再次切换）。
 * @param data .tef 文件字节流（须常驻，如 flash 常量数组）
 * @param len  字节数
 */
OPERATE_RET tef_player_play(const uint8_t *data, uint32_t len);

/**
 * @brief 一次性阻塞播放：逐帧解码到调用者提供的 RGB565 画布（在 dst_w×dst_h
 *        窗口内居中），每帧调 commit_cb 由调用者上屏；按素材 fps 节拍走完
 *        一遍后返回，末帧像素留在画布，工作缓冲用完即还。
 *        供 boot splash 等一次性场景直接使用——不依赖 tef_player_init()/播放
 *        线程，但与它们共享解压器单例，不得并发调用。
 * @param data      .tef 字节流（播放期间须有效）
 * @param len       字节数
 * @param dst       目标画布左上角（RGB565）
 * @param dst_w     目标窗口宽（像素）；素材画布须 ≤ 窗口
 * @param dst_h     目标窗口高（像素）
 * @param stride_px 画布行距（像素，≥ dst_w）
 * @param commit_cb 每帧渲染完成回调（调用者在此把画布提交上屏）
 * @param ud        回调透传参数
 */
OPERATE_RET tef_player_play_once(const uint8_t *data, uint32_t len,
                                 uint16_t *dst, uint16_t dst_w, uint16_t dst_h,
                                 uint16_t stride_px,
                                 void (*commit_cb)(void *ud), void *ud);

/* ------------------------------------------------------------------------ */
/* 逐帧步进解码器（lv_tef 等外部画布场景）。每实例私有解压器，多实例可并存；
 * 但同一实例的所有 tef_dec_* 调用须在同一线程串行执行（LVGL timer 即满足）。
 * 与常驻播放器/play_once/bench 无共享状态，可共存。                          */
typedef struct tef_dec tef_dec_t;

/**
 * @brief 打开素材、分配解码工作缓冲（PSRAM）。
 * @param out  成功时返回句柄（失败置 NULL）
 * @param data .tef 字节流（open 后须保持常驻，如 flash 常量数组）
 * @param len  字节数
 */
OPERATE_RET tef_dec_open(tef_dec_t **out, const uint8_t *data, uint32_t len);

uint16_t tef_dec_width(const tef_dec_t *d);     /* 素材画布宽（像素） */
uint16_t tef_dec_height(const tef_dec_t *d);    /* 素材画布高（像素） */
uint32_t tef_dec_frame_ms(const tef_dec_t *d);  /* 帧间隔 ms（全文件统一 fps） */

/**
 * @brief 设置渲染目标画布（native RGB565）。next 前必须调用一次。
 * @param dst       画布左上角；须容得下 width×height，且跨帧常驻
 *                  （TEF replace 语义靠画布保留未变化区域）
 * @param stride_px 行距（像素，≥ width）
 */
OPERATE_RET tef_dec_set_dst(tef_dec_t *d, uint16_t *dst, uint16_t stride_px);

/**
 * @brief 解下一帧到画布；末帧之后自动回卷到帧 0（循环播放）。
 *        失败（素材截断/损坏）后画布内容保持最后成功帧。
 */
OPERATE_RET tef_dec_next(tef_dec_t *d);

/** @brief 关闭并释放全部工作缓冲（画布归调用方）。d 为 NULL 安全。 */
void tef_dec_close(tef_dec_t *d);

#ifdef __cplusplus
}
#endif

#endif /* __TEF_PLAYER_H__ */
