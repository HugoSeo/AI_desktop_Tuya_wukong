#include "wukong_video_input.h"
#include "tal_log.h"

static const VIDEO_INPUT_OPS_T *s_ops = NULL;

OPERATE_RET wukong_video_input_register(const VIDEO_INPUT_OPS_T *ops)
{
    if (!ops || !ops->start || !ops->stop || !ops->get_caps) {
        return OPRT_INVALID_PARM;
    }
    if (s_ops) {
        TAL_PR_ERR("video input source already registered");
        return OPRT_COM_ERROR;
    }
    s_ops = ops;
    return OPRT_OK;
}

const VIDEO_INPUT_OPS_T *wukong_video_input_get(VOID)
{
    return s_ops;
}
