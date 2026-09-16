/**
 * @file tool_init.c
 * @brief wukong_tools_init — register the enabled tool modules onto the base.
 *
 * Central gating point: each device tool module is registered under its
 * ENABLE_TOOLKITS_* switch (order preserved from the old wukong_mcp.c).
 * claw's own tools (memory/profile) and skill no longer have a section here:
 * they live in their owning domain and self-register (memory/profile from
 * provider/claw's __claw_init; skill from wukong_skill_init) — this file
 * only carries the generic device tools, with no provider dependency.
 *
 * @copyright Copyright (c) 2025 Tuya Inc. All Rights Reserved.
 */

#include "tal_log.h"
#include "tuya_app_config.h"
#include "wukong_tool.h"

#include "tool_control.h"
#include "tool_system.h"
#include "tool_tm.h"
#if defined(ENABLE_TOOLKITS_PLAYBACK) && (ENABLE_TOOLKITS_PLAYBACK == 1)
#include "tool_playback.h"
#include "wukong_playback_ctrl.h"
#endif
#include "tool_imm.h"
#include "tool_social.h"
#include "tool_camera.h"
#include "tool_motion.h"

OPERATE_RET wukong_tools_init(VOID_T)
{
    OPERATE_RET rt = OPRT_OK;

#if defined(ENABLE_TOOLKITS_CONTROL) && (ENABLE_TOOLKITS_CONTROL == 1)
    TUYA_CALL_ERR_LOG(tool_control_init());
#endif
#if defined(ENABLE_TOOLKITS_SYSTEM) && (ENABLE_TOOLKITS_SYSTEM == 1)
    TUYA_CALL_ERR_LOG(tool_system_init());
#endif
#if defined(ENABLE_TOOLKITS_TM) && (ENABLE_TOOLKITS_TM == 1)
    TUYA_CALL_ERR_LOG(tool_tm_init());
#endif
#if defined(ENABLE_TOOLKITS_PLAYBACK) && (ENABLE_TOOLKITS_PLAYBACK == 1)
    TUYA_CALL_ERR_LOG(wukong_playback_ctrl_init());
    TUYA_CALL_ERR_LOG(tool_playback_init());
#endif
#if defined(ENABLE_TOOLKITS_IMM) && (ENABLE_TOOLKITS_IMM == 1)
    TUYA_CALL_ERR_LOG(tool_imm_init());
#endif
#if defined(ENABLE_TOOLKITS_SOCIAL) && (ENABLE_TOOLKITS_SOCIAL == 1)
    TUYA_CALL_ERR_LOG(tool_social_init());
#endif
#if defined(ENABLE_TOOLKITS_CAMERA) && (ENABLE_TOOLKITS_CAMERA == 1)
    TUYA_CALL_ERR_LOG(tool_camera_init());
#endif
#if defined(ENABLE_TOOLKITS_MOTION) && (ENABLE_TOOLKITS_MOTION == 1)
    TUYA_CALL_ERR_LOG(tool_motion_init());
#endif

    return rt;
}
