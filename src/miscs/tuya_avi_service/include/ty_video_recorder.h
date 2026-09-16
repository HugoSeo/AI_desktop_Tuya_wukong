#ifndef __TY_VIDEO_RECORDER_H__
#define __TY_VIDEO_RECORDER_H__

#include "ty_video_recorder_types.h"

ty_avdk_err_t ty_video_recorder_new(ty_video_recorder_handle_t *handle, ty_video_recorder_config_t *config);
ty_avdk_err_t ty_video_recorder_open(ty_video_recorder_handle_t handler);
ty_avdk_err_t ty_video_recorder_close(ty_video_recorder_handle_t handler);
ty_avdk_err_t ty_video_recorder_start(ty_video_recorder_handle_t handler, char *file_path, uint32_t record_type);
ty_avdk_err_t ty_video_recorder_stop(ty_video_recorder_handle_t handler);
ty_avdk_err_t ty_video_recorder_delete(ty_video_recorder_handle_t handler);

#endif
