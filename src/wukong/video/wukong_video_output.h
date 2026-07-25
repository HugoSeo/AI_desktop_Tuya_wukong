#ifndef __WUKONG_VIDEO_OUTPUT_H__
#define __WUKONG_VIDEO_OUTPUT_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "tuya_cloud_types.h"

typedef INT_T (*WUKONG_VIDEO_CB)(UINT8_T data, INT_T len);
OPERATE_RET wukong_video_output_init(WUKONG_VIDEO_CB cb);
OPERATE_RET wukong_video_output_deinit(VOID);

#ifdef __cplusplus
}
#endif

#endif