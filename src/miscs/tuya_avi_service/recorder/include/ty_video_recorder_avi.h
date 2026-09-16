#ifndef __TY_VIDEO_RECORDER_AVI_H__
#define __TY_VIDEO_RECORDER_AVI_H__

#include "ty_video_recorder_ctlr.h"

ty_avdk_err_t ty_avi_record_start(ty_video_recorder_ctlr_t *controller, char *file_path);
ty_avdk_err_t ty_avi_record_stop(ty_video_recorder_ctlr_t *controller, uint32_t duration_ms);
ty_avdk_err_t ty_avi_record_close(ty_video_recorder_ctlr_t *controller);
ty_avdk_err_t ty_avi_write_video_frame(ty_video_recorder_ctlr_t *controller, uint8_t *data, uint32_t length);
ty_avdk_err_t ty_avi_write_audio_data(ty_video_recorder_ctlr_t *controller, uint8_t *data, uint32_t length);

#endif
