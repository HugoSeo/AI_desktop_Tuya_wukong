#include "wukong_video_input.h"
#include "wukong_ai_agent.h"

OPERATE_RET wukong_video_input(UINT8_T *data, UINT_T len)
{
    return wukong_ai_agent_send_video(data, len);
}