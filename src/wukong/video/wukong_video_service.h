/**
 * @file wukong_video_service.h
 * @brief Video service layer — who frames go to.
 *
 * All video consumers (camera app features, P2P, AI agent) subscribe here
 * as peers. First subscriber of a format starts the input source stream;
 * the last unsubscriber stops it. When no input source is configured
 * (USING_NONE_VIDEO_INPUT) every call degrades safely — callers never
 * need #ifdef.
 */
#ifndef __WUKONG_VIDEO_SERVICE_H__
#define __WUKONG_VIDEO_SERVICE_H__

#include "wukong_video_input.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VIDEO_CONSUMER_PREVIEW = 0,  /* camera app: viewfinder preview */
    VIDEO_CONSUMER_DETECTION,    /* camera app: motion-detection frame grab */
    VIDEO_CONSUMER_P2P,          /* P2P audio/video call */
    VIDEO_CONSUMER_AI_AGENT,     /* reserved: multimodal agent uplink */
    VIDEO_CONSUMER_AVI_RECORD,   /* local MJPEG AVI recording */
    VIDEO_CONSUMER_MAX,
} VIDEO_CONSUMER_E;
/* Snapshot takes no consumer slot: it is a pass-through input-source op,
 * orthogonal to stream subscription and safe to run alongside any. */

/** Init the service and the configured input source. Safe to call once at boot;
 *  when the source is absent or fails to init, the service stays up in
 *  degraded mode (available() == FALSE). */
OPERATE_RET wukong_video_init(VOID);

BOOL_T      wukong_video_available(VIDEO_FMT_E fmt);
OPERATE_RET wukong_video_get_caps(VIDEO_INPUT_CAPS_T *caps);
OPERATE_RET wukong_video_subscribe(VIDEO_FMT_E fmt, VIDEO_CONSUMER_E consumer,
                                   VIDEO_FRAME_CB cb, VOID *ctx);
OPERATE_RET wukong_video_unsubscribe(VIDEO_FMT_E fmt, VIDEO_CONSUMER_E consumer);
OPERATE_RET wukong_video_snapshot(BYTE_T **jpeg, UINT_T *len, UINT_T timeout_ms);

#ifdef __cplusplus
}
#endif
#endif /* __WUKONG_VIDEO_SERVICE_H__ */
