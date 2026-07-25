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

#ifdef __cplusplus
}
#endif

#endif /* __AUDIO_DUMP_H__ */
