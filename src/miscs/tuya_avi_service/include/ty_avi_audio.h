#ifndef __TY_AVI_AUDIO_H__
#define __TY_AVI_AUDIO_H__

#include "tuya_cloud_types.h"

OPERATE_RET ty_avi_audio_rec_start(INT32_T mic_vol, uint8_t enable_preview, INT32_T spk_vol);
OPERATE_RET ty_avi_audio_rec_set_preview(uint8_t on, INT32_T spk_vol);
void ty_avi_audio_rec_stop(void);
int ty_avi_audio_rec_read(uint8_t *buf, uint32_t len);
int ty_avi_audio_rec_try_read(uint8_t *buf, uint32_t len);

#endif
