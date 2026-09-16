/**
 * @file wukong_audio_sprs.h
 * @brief SPRS ESNR dual-mic AEC/NR/VAD frontend implementation header
 *
 * Exports g_sprs_frontend_ops for board-level registration via
 * wukong_audio_frontend_register(). Requires libsprsESNR linked in.
 *
 * @copyright Copyright (c) Tuya Inc.
 */
#ifndef __WUKONG_AUDIO_SPRS_H__
#define __WUKONG_AUDIO_SPRS_H__

#include "wukong_audio_frontend.h"

#ifdef __cplusplus
extern "C" {
#endif

extern WUKONG_AUDIO_FRONTEND_OPS_T g_sprs_frontend_ops;

#ifdef __cplusplus
}
#endif

#endif /* __WUKONG_AUDIO_SPRS_H__ */
