/**
 * @file gfw_core_cmd.c
 * @brief
 * @version 0.1
 * @date 2023-06-14
 *
 * @copyright Copyright (c) 2023 Tuya Inc. All Rights Reserved.
 *
 * Permission is hereby granted, to any person obtaining a copy of this software and
 * associated documentation files (the "Software"), Under the premise of complying
 * with the license of the third-party open source software contained in the software,
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software.
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 */

#include "uni_log.h"
#include "gfw_core_cmd.h"

#if defined(ENABLE_GFW_CMD_STANDALONE_SECTION) && (ENABLE_GFW_CMD_STANDALONE_SECTION == 1)
#else
#define MAX_GFW_CMD_CNT 64
STATIC AUDIO_GFW_CMD_CFG_T s_gfw_cmd_list[MAX_GFW_CMD_CNT];
STATIC UINT_T s_gfw_cmd_cnt;
#endif
STATIC AUDIO_GFW_CMD_HANDLE_CB s_gfw_cmd_handle_cb = NULL;

/**
 * @brief
 *
 * @param data
 * @return OPERATE_RET
 */
OPERATE_RET gfw_core_cmd_dispatch(AUDIO_GFW_CMD_DATA_T *data)
{
    if (s_gfw_cmd_handle_cb) {
        return s_gfw_cmd_handle_cb(data);
    }
#if defined(ENABLE_GFW_CMD_STANDALONE_SECTION) && (ENABLE_GFW_CMD_STANDALONE_SECTION == 1)
    extern unsigned int _gfw_cmd_begin;
    extern unsigned int _gfw_cmd_end;

    AUDIO_GFW_CMD_CFG_T *cmd = NULL;
    UINT_T i = 0;
    UINT_T count = ((UINT_T)&_gfw_cmd_end - (UINT_T)&_gfw_cmd_begin) / SIZEOF(AUDIO_GFW_CMD_CFG_T);

    for (i = 0; i < count; i++) {
        cmd = (AUDIO_GFW_CMD_CFG_T *)((UINT_T)&_gfw_cmd_begin + i * SIZEOF(AUDIO_GFW_CMD_CFG_T));
#else
    AUDIO_GFW_CMD_CFG_T *cmd = NULL;
    UINT_T i = 0;
    UINT_T count = s_gfw_cmd_cnt;
    for (i = 0; i < count; i++) {
        cmd = &s_gfw_cmd_list[i];
#endif
        if (cmd->cmd == data->cmd) {
            if (INVALID_SUBCMD != cmd->sub_cmd) {
                if (cmd->sub_cmd != data->data[0]) {
                    continue; // sub_cmd not match
                } else {
                    // transfer to sub_cmd
                    data->cmd = data->data[0];
                    data->data ++;
                    data->len --;
                }
            }
            if (cmd->cb) {
                cmd->cb(data);
            }
            break;
        }
    }

    if (i >= count) {
        PR_DEBUG("cmd %02X or sub_cmd %02X not found", data->cmd, data->data[0]);
    }

    return OPRT_OK;
}

/**
 * @brief Reserved
 *
 * @param cmd
 * @return OPERATE_RET
 */
OPERATE_RET gfw_core_cmd_register(CONST AUDIO_GFW_CMD_CFG_T *cmds, UINT_T count)
{
    OPERATE_RET rt = OPRT_OK;

#if defined(ENABLE_GFW_CMD_STANDALONE_SECTION) && (ENABLE_GFW_CMD_STANDALONE_SECTION == 1)
#else
    if ((NULL == cmds) || (count == 0)) {
        return OPRT_INVALID_PARM;
    }

    if (s_gfw_cmd_cnt + count >= MAX_GFW_CMD_CNT) {
        PR_ERR("too many cmds");
        return OPRT_EXCEED_UPPER_LIMIT;
    }

    memcpy(&s_gfw_cmd_list[s_gfw_cmd_cnt], cmds, count * SIZEOF(AUDIO_GFW_CMD_CFG_T));
    s_gfw_cmd_cnt += count;

#endif

    return rt;
}

VOID gfw_core_cmd_ext_handle_reg(AUDIO_GFW_CMD_HANDLE_CB cb)
{
    s_gfw_cmd_handle_cb = cb;
}
