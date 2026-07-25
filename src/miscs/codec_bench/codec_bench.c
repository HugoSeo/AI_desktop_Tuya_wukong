/**
 * @file codec_bench.c
 * @brief Codec bench mode: key init and callback to trigger encoder test (no AI toy).
 * @version 0.1
 * @date 2026-03-06
 * @copyright Copyright (c) 2024 Tuya Inc. All Rights Reserved.
 */

/***********************************************************************
 ** INCLUDE **
 **********************************************************************/
#include "codec_bench.h"
#include "codec_bench_encoder.h"
#include "tuya_ai_toy_key.h"
#include "tal_log.h"
#include "tuya_key.h"

/***********************************************************************
 ** CONSTANT ( MACRO AND ENUM ) **
 **********************************************************************/
#define CODEC_BENCH_SEQ_KEY_MS  200
#define CODEC_BENCH_LONG_KEY_MS 400

/***********************************************************************
 ** VARIABLE **
 **********************************************************************/

/***********************************************************************
 ** FUNCTION **
 **********************************************************************/
STATIC VOID __codec_bench_key_cb(UINT_T port, PUSH_KEY_TYPE_E type, INT_T cnt)
{
    (VOID)port;
    (VOID)cnt;
    if (type == NORMAL_KEY) {
        codec_bench_encoder_trigger();
    }
}

OPERATE_RET codec_bench_start(CONST TY_AI_TOY_CFG_T *cfg)
{
    if (cfg == NULL) {
        TAL_PR_ERR("codec_bench_start: cfg null");
        return OPRT_INVALID_PARM;
    }
    OPERATE_RET rt = tuya_ai_toy_key_init(cfg->audio_trigger_pin, TRUE,
                                          CODEC_BENCH_SEQ_KEY_MS, CODEC_BENCH_LONG_KEY_MS,
                                          __codec_bench_key_cb);
    if (rt == OPRT_OK) {
        TAL_PR_NOTICE("codec bench: key triggers encoder test only");
    }
    return rt;
}
