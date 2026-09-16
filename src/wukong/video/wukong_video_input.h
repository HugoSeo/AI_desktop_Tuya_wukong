/**
 * @file wukong_video_input.h
 * @brief Video input source layer — where frames come from.
 *
 * A single input source (camera today; screen/external encoder later)
 * implements VIDEO_INPUT_OPS_T and registers here. The service layer
 * (wukong_video_service) drives it; business code never includes this
 * header except to implement a new source.
 */
#ifndef __WUKONG_VIDEO_INPUT_H__
#define __WUKONG_VIDEO_INPUT_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VIDEO_FMT_YUV422 = 0,
    VIDEO_FMT_MJPEG,
    VIDEO_FMT_H264,
    VIDEO_FMT_MAX,
} VIDEO_FMT_E;

/** Frame descriptor. Data is valid only for the duration of the callback. */
typedef struct {
    UCHAR_T    *data;
    UINT_T      length;
    USHORT_T    width;
    USHORT_T    height;
    UINT_T      timestamp_ms;
    VIDEO_FMT_E fmt;
    BOOL_T      is_key_frame;   /* H264 only, FALSE otherwise */
} VIDEO_FRAME_T;

/** Frame callback. Runs on the source's dispatch thread; must not block. */
typedef VOID (*VIDEO_FRAME_CB)(const VIDEO_FRAME_T *frame, VOID *ctx);

typedef struct {
    USHORT_T width;
    USHORT_T height;
    UCHAR_T  fps;
    UINT_T   fmt_mask;          /* bitmask of (1u << VIDEO_FMT_x) */
} VIDEO_INPUT_CAPS_T;

#define VIDEO_FMT_BIT(fmt) (1u << (fmt))

/**
 * Input source operations. start()/stop() are invoked by the service layer
 * on first-subscriber / last-unsubscriber transitions only — the source
 * does NOT need consumer refcounting. Hardware constraints (e.g. DVP
 * dual-stream JPEG+YUV vs H264+YUV exclusivity) are handled inside the
 * source; conflicting requests return an error which the service passes
 * through untouched.
 */
typedef struct {
    OPERATE_RET (*init)(VOID);
    OPERATE_RET (*deinit)(VOID);
    OPERATE_RET (*start)(VIDEO_FMT_E fmt, VIDEO_FRAME_CB cb, VOID *ctx);
    OPERATE_RET (*stop)(VIDEO_FMT_E fmt);
    OPERATE_RET (*get_caps)(VIDEO_INPUT_CAPS_T *caps);
    /** Optional (NULL = unsupported): one-shot JPEG grab, may run while
     *  streams are active. out buffer is PSRAM; caller frees with tal_psram_free. */
    OPERATE_RET (*snapshot)(BYTE_T **jpeg, UINT_T *len, UINT_T timeout_ms);
} VIDEO_INPUT_OPS_T;

/** Register the (single) input source. Second registration returns OPRT_COM_ERROR. */
OPERATE_RET wukong_video_input_register(const VIDEO_INPUT_OPS_T *ops);

/** Service-layer accessor. NULL when no source registered. */
const VIDEO_INPUT_OPS_T *wukong_video_input_get(VOID);

#ifdef __cplusplus
}
#endif
#endif /* __WUKONG_VIDEO_INPUT_H__ */
