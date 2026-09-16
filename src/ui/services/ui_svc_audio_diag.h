#ifndef __UI_SVC_AUDIO_DIAG_H__
#define __UI_SVC_AUDIO_DIAG_H__

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file ui_svc_audio_diag.h
 * @brief 音频诊断服务 — UI 层访问 audio_dump 的唯一入口（隔离 FS 与 miscs 依赖）。
 *
 * 通道值与 AUDIO_DUMP_CHANNEL_E 一致：0=关闭 1=UART 2=局域网 3=SD卡。
 * 运行时状态，不持久化到 KV；掉电恢复为「关闭」。
 */

/** @return true 当至少一个真实通道在当前配置下可用（否则设置页不显示入口）。 */
bool ui_svc_audio_diag_available(void);

/** @return 位掩码 (1u<<channel)：哪些通道可用，供页面灰显不可用项。 */
uint32_t ui_svc_audio_diag_caps(void);

/** @return 当前通道 (AUDIO_DUMP_CHANNEL_E)。 */
uint8_t ui_svc_audio_diag_channel_get(void);

/** @brief 切换通道（运行时，不写 KV）。SD 通道在此用 ui_fs_path 构造目录后调后端。 */
void ui_svc_audio_diag_channel_set(uint8_t ch);

#ifdef __cplusplus
}
#endif

#endif /* __UI_SVC_AUDIO_DIAG_H__ */
