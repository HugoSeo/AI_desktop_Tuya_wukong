#include "ty_video_recorder.h"
#include "ty_video_recorder_ctlr.h"
#include "uni_log.h"

#define TAG "ty_video_recorder"

ty_avdk_err_t ty_video_recorder_open(ty_video_recorder_handle_t handler)
{
    TY_AVDK_RETURN_ON_FALSE(handler && handler->open, TY_AVDK_ERR_INVAL, TAG, "invalid handle");
    return handler->open(handler);
}

ty_avdk_err_t ty_video_recorder_close(ty_video_recorder_handle_t handler)
{
    TY_AVDK_RETURN_ON_FALSE(handler && handler->close, TY_AVDK_ERR_INVAL, TAG, "invalid handle");
    return handler->close(handler);
}

ty_avdk_err_t ty_video_recorder_start(ty_video_recorder_handle_t handler, char *file_path, uint32_t record_type)
{
    TY_AVDK_RETURN_ON_FALSE(handler && handler->start, TY_AVDK_ERR_INVAL, TAG, "invalid handle");
    return handler->start(handler, file_path, record_type);
}

ty_avdk_err_t ty_video_recorder_stop(ty_video_recorder_handle_t handler)
{
    TY_AVDK_RETURN_ON_FALSE(handler && handler->stop, TY_AVDK_ERR_INVAL, TAG, "invalid handle");
    return handler->stop(handler);
}

ty_avdk_err_t ty_video_recorder_delete(ty_video_recorder_handle_t handler)
{
    TY_AVDK_RETURN_ON_FALSE(handler && handler->delete_recorder, TY_AVDK_ERR_INVAL, TAG, "invalid handle");
    return handler->delete_recorder(handler);
}

ty_avdk_err_t ty_video_recorder_new(ty_video_recorder_handle_t *handle, ty_video_recorder_config_t *config)
{
    TY_AVDK_RETURN_ON_FALSE(handle && config, TY_AVDK_ERR_INVAL, TAG, "invalid param");
    TY_AVDK_RETURN_ON_FALSE(*handle == NULL, TY_AVDK_ERR_INVAL, TAG, "handle exists");
    return ty_video_recorder_ctlr_new(handle, config);
}
