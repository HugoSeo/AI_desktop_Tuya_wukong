#include "ty_video_recorder_avi.h"
#include "ty_avi_recorder.h"
#include "avilib.h"
#include "ty_video_osi_wrapper.h"
#include "avi_port.h"

#include "uni_log.h"

#include <string.h>

#define TAG "ty_recorder_avi"
/* A single-frame blocking write taking this long almost always means the
 * storage device (SD card) is stalled, not the encoder. */
#define TY_AVI_FRAME_WRITE_STALL_MS    800
#define TY_AVI_FRAME_STALL_LOG_GAP_MS  1000

static uint32_t s_last_stall_log_tick;

ty_avdk_err_t ty_avi_record_start(ty_video_recorder_ctlr_t *controller, char *file_path)
{
    TY_AVDK_RETURN_ON_FALSE(controller, TY_AVDK_ERR_INVAL, TAG, "controller null");
    TY_AVDK_RETURN_ON_FALSE(file_path, TY_AVDK_ERR_INVAL, TAG, "path null");

    if (ty_video_osi_funcs_init() != 0) {
        return TY_AVDK_ERR_IO;
    }

    avi_t *avi = NULL;
    AVI_open_output_file(&avi, file_path, AVI_MEM_PSRAM);
    if (avi == NULL) {
        PR_ERR("open avi failed: %s", file_path);
        return TY_AVDK_ERR_IO;
    }

    if (tal_mutex_create_init(&controller->avi_write_mutex) != OPRT_OK) {
        AVI_close(avi);
        return TY_AVDK_ERR_IO;
    }

    controller->avi_handle = avi;

    char compressor[8] = {0};
    if (controller->config.record_format == TY_VIDEO_RECORDER_FORMAT_MJPEG) {
        memcpy(compressor, "MJPG", 4);
    } else {
        PR_ERR("unsupported video format %u", controller->config.record_format);
        tal_mutex_release(controller->avi_write_mutex);
        controller->avi_write_mutex = NULL;
        AVI_close(avi);
        controller->avi_handle = NULL;
        return TY_AVDK_ERR_INVAL;
    }

    double fps = controller->config.record_framerate ? controller->config.record_framerate : 20.0;
    uint32_t width = controller->config.video_width ? controller->config.video_width : 480;
    uint32_t height = controller->config.video_height ? controller->config.video_height : 480;
    AVI_set_video(avi, width, height, fps, compressor);

    if (controller->config.audio_channels > 0) {
        uint32_t rate = controller->config.audio_rate ? controller->config.audio_rate : 16000;
        uint32_t bits = controller->config.audio_bits ? controller->config.audio_bits : 16;
        AVI_set_audio(avi, controller->config.audio_channels, rate, bits, WAVE_FORMAT_PCM);
    }

    PR_NOTICE("avi record start %s %ux%u@%.1ffps", file_path, width, height, fps);
    return TY_AVDK_ERR_OK;
}

ty_avdk_err_t ty_avi_record_stop(ty_video_recorder_ctlr_t *controller, uint32_t duration_ms)
{
    ty_avdk_err_t ret = TY_AVDK_ERR_OK;
    TY_AVDK_RETURN_ON_FALSE(controller, TY_AVDK_ERR_INVAL, TAG, "controller null");

    if (controller->avi_handle == NULL) {
        return TY_AVDK_ERR_OK;
    }

    avi_t *avi = (avi_t *)controller->avi_handle;
    tal_mutex_lock(controller->avi_write_mutex);

    if (duration_ms > 0 && controller->video_frame_count > 0) {
        AVI_update_video_frame_rate_by_duration(avi, duration_ms);
        PR_NOTICE("avi record close frames=%u dur=%ums fps=%.2f audio=%uB",
                  controller->video_frame_count, duration_ms,
                  (double)controller->video_frame_count * 1000.0 / (double)duration_ms,
                  controller->audio_bytes_written);
    }

    if (AVI_close(avi) != 0) {
        ret = TY_AVDK_ERR_IO;
    }
    controller->avi_handle = NULL;

    tal_mutex_unlock(controller->avi_write_mutex);
    tal_mutex_release(controller->avi_write_mutex);
    controller->avi_write_mutex = NULL;

    return ret;
}

ty_avdk_err_t ty_avi_record_close(ty_video_recorder_ctlr_t *controller)
{
    return ty_avi_record_stop(controller, 0);
}

ty_avdk_err_t ty_avi_write_video_frame(ty_video_recorder_ctlr_t *controller, uint8_t *data, uint32_t length)
{
    TY_AVDK_RETURN_ON_FALSE(controller && data, TY_AVDK_ERR_INVAL, TAG, "invalid param");

    avi_t *avi = (avi_t *)controller->avi_handle;
    if (avi == NULL) {
        return TY_AVDK_ERR_INVAL;
    }

    uint32_t now = (uint32_t)sys_port.get_tick();
    uint32_t wr_start = now;
    uint32_t elapsed;
    tal_mutex_lock(controller->avi_write_mutex);
    ty_avdk_err_t ret = TY_AVDK_ERR_OK;
    if (AVI_write_frame(avi, (char *)data, length) < 0) {
        ret = TY_AVDK_ERR_IO;
        ty_avi_rec_stat_on_write_error(1);
    } else {
        controller->video_frame_count++;
        ty_avi_rec_stat_on_avi_frame();
    }
    tal_mutex_unlock(controller->avi_write_mutex);

    elapsed = (uint32_t)sys_port.get_tick() - wr_start;
    if (ret == TY_AVDK_ERR_OK) {
        ty_avi_rec_stat_on_write(length, elapsed, 1);
    }
    /* Rate-limited so a multi-second stall doesn't itself flood the
     * (blocking) UART log and make the stall worse. */
    if (elapsed > TY_AVI_FRAME_WRITE_STALL_MS &&
        (uint32_t)sys_port.get_tick() - s_last_stall_log_tick > TY_AVI_FRAME_STALL_LOG_GAP_MS) {
        PR_ERR("avi frame write took %ums (len=%u) - storage may be stalled", elapsed, length);
        s_last_stall_log_tick = (uint32_t)sys_port.get_tick();
    }

    controller->last_frame_time_ms = now;
    return ret;
}

ty_avdk_err_t ty_avi_write_audio_data(ty_video_recorder_ctlr_t *controller, uint8_t *data, uint32_t length)
{
    TY_AVDK_RETURN_ON_FALSE(controller && data, TY_AVDK_ERR_INVAL, TAG, "invalid param");

    avi_t *avi = (avi_t *)controller->avi_handle;
    if (avi == NULL || avi->a_chans == 0) {
        return TY_AVDK_ERR_OK;
    }

    uint32_t wr_start = (uint32_t)sys_port.get_tick();
    tal_mutex_lock(controller->avi_write_mutex);
    ty_avdk_err_t ret = TY_AVDK_ERR_OK;
    if (AVI_write_audio(avi, (char *)data, length) < 0) {
        ret = TY_AVDK_ERR_IO;
        ty_avi_rec_stat_on_write_error(0);
    }
    tal_mutex_unlock(controller->avi_write_mutex);
    if (ret == TY_AVDK_ERR_OK) {
        ty_avi_rec_stat_on_write(length, (uint32_t)sys_port.get_tick() - wr_start, 0);
    }
    return ret;
}
