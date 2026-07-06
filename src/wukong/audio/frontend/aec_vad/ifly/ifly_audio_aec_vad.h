/**
 * @file ifly_audio_aec_vad.h
 * @brief iFlytek dual-mic AEC + Tuya RNN-VAD frontend implementation header
 *
 * Exports g_ifly_frontend_ops for board-level registration via
 * wukong_audio_frontend_register().
 *
 * @version 1.0
 * @date 2026-03-25
 * @copyright Copyright (c) Tuya Inc.
 */

#ifndef __IFLY_AUDIO_AEC_VAD_H__
#define __IFLY_AUDIO_AEC_VAD_H__

#include "wukong_audio_frontend.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * Public ops table
 * --------------------------------------------------------------------------- */

/**
 * @brief iFlytek dual-mic AEC + Tuya RNN VAD frontend ops
 *
 * Register via wukong_audio_frontend_register(&g_ifly_frontend_ops) before
 * calling wukong_audio_frontend_init().
 *
 * @note Requires libifly_aec_wakeup.a linked into the project.
 *       Call ifly_auth_init() (auth.c) before init.
 */
extern WUKONG_AUDIO_FRONTEND_OPS_T g_ifly_frontend_ops;

#ifdef __cplusplus
}
#endif

#endif /* __IFLY_AUDIO_AEC_VAD_H__ */
