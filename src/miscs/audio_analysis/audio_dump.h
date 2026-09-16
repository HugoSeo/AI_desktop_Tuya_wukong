/**
 * @file audio_dump.h
 * @brief audio_dump module is used to 
 * @version 0.1
 * @date 2025-06-25
 */

#ifndef __AUDIO_DUMP_H__
#define __AUDIO_DUMP_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************
************************macro define************************
***********************************************************/
typedef uint32_t AUDIO_TEST_EVENT_E;
#define AUDIO_TEST_EVENT_PLAY_BGM          0x01
#define AUDIO_TEST_EVENT_SET_VOLUME        0x02
#define AUDIO_TEST_EVENT_SET_MICGAIN       0x03
#define AUDIO_TEST_EVENT_SET_ALG_PARA      0x04
#define AUDIO_TEST_EVENT_GET_ALG_PARA      0x05
#define AUDIO_TEST_EVENT_DUMP_ALG_PARA     0x06
#define AUDIO_TEST_EVENT_NET_DUMP_AUDIO    0x07

#define AUDIO_DUMP_MIC          0
#define AUDIO_DUMP_REF          1
#define AUDIO_DUMP_AEC          2
#define AUDIO_DUMP_KWS          3
#define AUDIO_DUMP_VAD          4
#define AUDIO_DUMP_MAX          5

/* SDCARD 双缓冲块大小：假设 16kHz/16bit/单声道。每块约 5s。
 * 总占用 = AUDIO_DUMP_MAX x 2 x AUDIO_DUMP_BLOCK_SIZE = 5 x 2 x 160KB = 1.6MB。
 * （10s/3.2MB 在内存受限设备上申请会失败，5s 留足余量；落盘 <1s 仍无缝。） */
#define AUDIO_DUMP_BLOCK_SEC    5
#define AUDIO_DUMP_BLOCK_SIZE   (16000 * 2 * AUDIO_DUMP_BLOCK_SEC)   /* 160000 bytes */

/***********************************************************
********************* Dump Channel ************************
***********************************************************/
typedef enum {
    AUDIO_DUMP_CH_OFF    = 0,   /* 停止 dump */
    AUDIO_DUMP_CH_UART   = 1,   /* PSRAM 累积 → UART 导出（需 ENABLE_EXT_RAM） */
    AUDIO_DUMP_CH_LAN    = 2,   /* AI Monitor 网络广播（需 ENABLE_APP_AI_MONITOR） */
    AUDIO_DUMP_CH_SDCARD = 3,   /* 5 路各写一个 .pcm 文件（需 ENABLE_EXT_RAM） */
} AUDIO_DUMP_CHANNEL_E;

/***********************************************************
************** Audio Dump Network Control Commands *********
***********************************************************/
typedef enum {
    AUDIO_DUMP_CMD_ENABLE       = 0x01,
    AUDIO_DUMP_CMD_RESET        = 0x02,
    AUDIO_DUMP_CMD_DUMP_NET     = 0x03,
    AUDIO_DUMP_CMD_REALTIME     = 0x04,
    AUDIO_DUMP_CMD_PLAY_BGM     = 0x05,
    AUDIO_DUMP_CMD_VOLUME       = 0x06,
    AUDIO_DUMP_CMD_MICGAIN      = 0x07,
    AUDIO_DUMP_CMD_GET_STATUS   = 0x08,
    AUDIO_DUMP_CMD_GET_ALL      = 0x09,
} AUDIO_DUMP_CMD_E;

/***********************************************************
********************function declaration********************
***********************************************************/

VOID audio_dump_write(INT_T type, uint8_t *data, uint16_t datalen);

VOID audio_dump_enable(VOID);

VOID audio_dump_disable(VOID);

VOID audio_dump_reset(VOID);

VOID audio_dump_with_net(INT_T type);

VOID audio_play_bgm(INT_T type, INT_T freq);

VOID audio_set_volume(INT_T volume);

VOID audio_set_micgain(INT_T micgain);

/**
 * @brief 切换 dump 输出通道。
 * @param ch     目标通道。
 * @param sd_dir 仅 AUDIO_DUMP_CH_SDCARD 时使用：目标目录，须以 '/' 结尾且已存在；
 *               其余通道忽略，可传 NULL。
 * @return OPRT_OK 成功；OPRT_NOT_SUPPORTED 该通道当前未编译；其它为错误码。
 * @note  线程安全。切到 OFF 或切换通道时，旧 SD 通道会先把缓存落盘再释放。
 */
OPERATE_RET audio_dump_set_channel(AUDIO_DUMP_CHANNEL_E ch, const char *sd_dir);

/** @brief 当前 dump 通道。 */
AUDIO_DUMP_CHANNEL_E audio_dump_get_channel(void);

/** @brief 位掩码 (1u<<AUDIO_DUMP_CHANNEL_E)：当前编译配置下哪些通道可用。OFF 恒可用。 */
uint32_t audio_dump_channel_caps(void);

#ifdef __cplusplus
}
#endif

#endif /* __AUDIO_DUMP_H__ */
