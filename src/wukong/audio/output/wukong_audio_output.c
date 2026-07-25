/**
 * @file wukong_audio_output.c
 * @brief 
 * @version 0.1
 * @date 2025-12-10
 * 
 * @copyright Copyright (c) 2025 Tuya Inc. All Rights Reserved.
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
#include "tkl_audio.h"
#include "wukong_audio_output.h"

OPERATE_RET wukong_audio_output_init(WUKONG_AUDIO_OUTPUT_CFG_T *cfg)
{
    TUYA_CHECK_NULL_RETURN(g_audio_output_consumer.init, OPRT_RESOURCE_NOT_READY);
    return g_audio_output_consumer.init(cfg);
}

OPERATE_RET wukong_audio_output_deinit(VOID)
{
    TUYA_CHECK_NULL_RETURN(g_audio_output_consumer.deinit, OPRT_RESOURCE_NOT_READY);
    return g_audio_output_consumer.deinit();
}

OPERATE_RET wukong_audio_output_start(VOID)
{
    TUYA_CHECK_NULL_RETURN(g_audio_output_consumer.start, OPRT_RESOURCE_NOT_READY);
    return g_audio_output_consumer.start();
}

OPERATE_RET wukong_audio_output_write(UINT8_T *data, UINT16_T datalen)
{
    TUYA_CHECK_NULL_RETURN(g_audio_output_consumer.write, OPRT_RESOURCE_NOT_READY);
    return g_audio_output_consumer.write(data, datalen);
}

OPERATE_RET wukong_audio_output_stop(VOID)
{
    TUYA_CHECK_NULL_RETURN(g_audio_output_consumer.stop, OPRT_RESOURCE_NOT_READY);
    return g_audio_output_consumer.stop();
}

OPERATE_RET wukong_audio_output_set_vol(INT32_T volume)
{
    TUYA_CHECK_NULL_RETURN(g_audio_output_consumer.set_vol, OPRT_RESOURCE_NOT_READY);
    return g_audio_output_consumer.set_vol(volume);
}
