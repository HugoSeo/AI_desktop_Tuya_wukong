/**
 * @file ty_gfw_mcu_ota.c
 * @brief
 * @version 0.1
 * @date 2023-06-15
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
#include "tal_workqueue.h"
#include "iot_httpc.h"
#include "tal_workq_service.h"
#include "tuya_svc_upgrade.h"
#include "ws_db_gw.h"
#include "gfw_mcu_ota/gfw_mcu_ota.h"
#include "tdd_comm_audio.h"
#include "gfw_core/gfw_core_cmd.h"
// #include "ty_gfw_core_cmd.h"
#include "tuya_iot_com_api.h"
// #include "ty_gfw_mcu_ota.h"


#define MCU_RECV_TIME 5 // MCU recv timeout
#define MCU_AGAIN_UP  5000
#define MCU_FRIST_UP  1000

#define UPGRADE_DEV_E   UCHAR_T
#define NONE_DEV_UPGRADE    0x00
#define IN_DEV_UPGRADE      0x01

#define UPGRADE_STATE_E UCHAR_T
#define UPGRADE_STATE_DETECT  0x00
#define UPGRADE_STATE_NEWEST  0x01
#define UPGRADE_STATE_RUNNING 0x02
#define UPGRADE_STATE_SUCC    0x03
#define UPGRADE_STATE_FAILED  0x04

typedef enum {
    ACK_IDE = 0,      //空闲态,包已发出,未返回
    ACK_START_RETURN, //收到开始响应包
    ACK_TRANS_RETURN, //收到数据响应包
    ACK_TIME_OUT,     //接收响应超时
    ACK_FAIL,         //失败
} ACK_STAT_E;

typedef struct {
    UCHAR_T ack_stat;                   //下载返回响应标志
    UCHAR_T prt_ver;                    //帧协议版本
    UINT_T offset;                      //包偏移
    UINT_T file_size;                   //文件大小
    BOOL_T IsMcuUgReset;                //设备升级MCU重启标志
    BOOL_T IsMcuUpgrading;              //MCU升级状态标志
    UINT_T pack_size;                   //包大小
    UINT_T download_size;               //已经下载的大小
} TY_MCU_OTA_S;

STATIC TY_MCU_OTA_S sg_mcu_ota_ctx = {0};


STATIC int __mcu_ota_ver_proc()
{
    OPERATE_RET op_ret = OPRT_OK;

    if (!sg_mcu_ota_ctx.IsMcuUgReset) {
        return op_ret;
    }

    PR_DEBUG("audio ota reset");

    ty_publish_event(GFW_EVENT_VERSION_UPDATE, NULL); // op_ret = ty_gfw_mcu_update_attachs_ver();
    sg_mcu_ota_ctx.IsMcuUgReset = FALSE;
    return op_ret;
}

STATIC OPERATE_RET __mcu_ota_start_cb(IN CONST FW_UG_S *fw)
{
    PR_DEBUG("mcu ota start");
    sg_mcu_ota_ctx.file_size = fw->file_size;
    sg_mcu_ota_ctx.ack_stat = ACK_IDE;
    sg_mcu_ota_ctx.offset = 0;
    UCHAR_T send_data[8] = {0};
    UINT_T send_len = 4;

    send_data[0] = (sg_mcu_ota_ctx.file_size >> 24) & 0xFF;
    send_data[1] = (sg_mcu_ota_ctx.file_size >> 16) & 0xFF;
    send_data[2] = (sg_mcu_ota_ctx.file_size >> 8) & 0xFF;
    send_data[3] = (sg_mcu_ota_ctx.file_size) & 0xFF;
    if (fw->tp > DEV_NM_NOT_ATH_SNGL) {
        send_data[4] = fw->tp;
        send_len = 8;
    }

    OPERATE_RET op_ret = OPRT_OK;
    op_ret = gfw_core_cmd_send_with_params(AUDIO_GFW_CMD_TYPE_INSTANT, GFW_CMD_OTA_START, send_data, send_len, MCU_RECV_TIME * 1000, NULL, 3);
    if (OPRT_OK != op_ret) {
        PR_ERR("send syn err:%d", op_ret);
    }
    return op_ret;
}

STATIC OPERATE_RET __mcu_ota_trans_cb(IN BYTE_T *p_data, IN UINT_T data_len)
{
    OPERATE_RET op_ret = OPRT_OK;
    WORD_T len = 0;
    UCHAR_T *send_buf = NULL;
    UCHAR_T *ori_buf = NULL;
    PR_DEBUG("upgrade len = %d,FreeHeapSize = %d", data_len, tal_system_get_free_heap_size());

    if (sg_mcu_ota_ctx.ack_stat == ACK_FAIL) {
        return OPRT_COM_ERROR;
    }
    if (sg_mcu_ota_ctx.prt_ver == 0x00) {
        len = data_len + 2;
    } else {
        len = data_len + 4;
    }
    ori_buf = Malloc(len + MIN_FRAME_LEN); // MIN_FRAME_LEN used for zero copy
    if (NULL == ori_buf) {
        return OPRT_MALLOC_FAILED;
    }

    send_buf = ori_buf + (MIN_FRAME_LEN - 1);

    if (sg_mcu_ota_ctx.prt_ver == 0x00) {
        send_buf[0] = ((sg_mcu_ota_ctx.offset >> 8) & 0xFF);
        send_buf[1] = (sg_mcu_ota_ctx.offset & 0xFF);
    } else {
        send_buf[0] = ((sg_mcu_ota_ctx.offset >> 24) & 0xFF);
        send_buf[1] = ((sg_mcu_ota_ctx.offset >> 16) & 0xFF);
        send_buf[2] = ((sg_mcu_ota_ctx.offset >> 8) & 0xFF);
        send_buf[3] = (sg_mcu_ota_ctx.offset & 0xFF);
    }
    memcpy(send_buf + len - data_len, p_data, data_len);

    op_ret = gfw_core_cmd_send_with_params(AUDIO_GFW_CMD_TYPE_ZEROCOPY, GFW_CMD_OTA_TRANS, send_buf, len, MCU_RECV_TIME * 1000, NULL, 3);
    if (OPRT_OK != op_ret) {
        PR_ERR("send sync err:%d", op_ret);
        sg_mcu_ota_ctx.ack_stat = ACK_FAIL;
        goto DOWN_ERR;
    }

    sg_mcu_ota_ctx.offset += data_len;

    if (sg_mcu_ota_ctx.offset == sg_mcu_ota_ctx.file_size) {
        PR_NOTICE("OTA pkg transmit FINISH, now send a empty package.");
        memset(send_buf, 0, len);
        if (sg_mcu_ota_ctx.prt_ver == 0x00) {
            send_buf[0] = ((sg_mcu_ota_ctx.offset >> 8) & 0xFF);
            send_buf[1] = (sg_mcu_ota_ctx.offset & 0xFF);
            len = 2;
        } else {
            send_buf[0] = ((sg_mcu_ota_ctx.offset >> 24) & 0xFF);
            send_buf[1] = ((sg_mcu_ota_ctx.offset >> 16) & 0xFF);
            send_buf[2] = ((sg_mcu_ota_ctx.offset >> 8) & 0xFF);
            send_buf[3] = (sg_mcu_ota_ctx.offset & 0xFF);
            len = 4;
        }
        op_ret = gfw_core_cmd_send_instant(GFW_CMD_OTA_TRANS, send_buf, len);
        if (OPRT_OK != op_ret) {
            PR_ERR("send instant err:%d", op_ret);
            return op_ret;
        }

        sg_mcu_ota_ctx.IsMcuUgReset = TRUE;
        PR_NOTICE("Publish ota end event!");
        ty_publish_event(GFW_EVENT_AUDIO_OTA_END, NULL); //通知mcu升级结束
    }
    if (NULL != ori_buf) {
        Free(ori_buf);
    }
    return OPRT_OK;

DOWN_ERR:
    if (NULL != ori_buf) {
        Free(ori_buf);
    }

    return OPRT_COM_ERROR;
}

/**
 * @brief 收到OTA启动命令的回复
 * @param[in] p_name: 命令字名
 * @param[in] p_cmd_data: 命令字内容
 * @return 执行结果,返回 OPRT_OK表示成功，返回 其他 则表示失败
 */
STATIC OPERATE_RET __gfw_cmd_mcu_ota_start(AUDIO_GFW_CMD_DATA_T *data)
{
    PR_DEBUG("mcu ota start");
    if (NULL == data) {
        PR_ERR("data is null");
        return OPRT_INVALID_PARM;
    }

    WORD_T data_len = data->len;
    UINT_T size_unit = 256;

    if (data_len > 0) {
        switch (data->data[0]) {
        case 0:
            size_unit = 256;
            break;
        case 1:
            size_unit = 512;
            break;
        case 2:
            size_unit = 1024;
            break;
        case 3:
            size_unit = 2048;
            break;
        case 4:
            size_unit = 3072;
            break;
        case 5:
            size_unit = 4096;
            break;
        default:
            size_unit = 256;
            break;
        }
    }

    sg_mcu_ota_ctx.pack_size = size_unit;
    sg_mcu_ota_ctx.prt_ver = data->ver;
    sg_mcu_ota_ctx.ack_stat = ACK_START_RETURN;

    return OPRT_OK;
}

/**
 * @brief 收到OTA数据传输命令的回复
 * @param[in] p_name: 命令字名
 * @param[in] p_cmd_data: 命令字内容
 * @return 执行结果,返回 OPRT_OK表示成功，返回 其他 则表示失败
 */
STATIC OPERATE_RET __gfw_cmd_mcu_ota_trans(AUDIO_GFW_CMD_DATA_T *data)
{
    PR_DEBUG("ota trans");
    sg_mcu_ota_ctx.ack_stat = ACK_TRANS_RETURN;
    return OPRT_OK;
}

STATIC OPERATE_RET __mcu_ota_down_load_data_cb(IN CONST FW_UG_S *fw, IN CONST UINT_T total_len,
                                               IN CONST UINT_T offset, IN CONST BYTE_T *data, IN CONST UINT_T len,
                                               OUT UINT_T *remain_len, IN PVOID_T pri_data)
{
    UINT_T send_len = 0;
    UINT_T send_unit_len = sg_mcu_ota_ctx.pack_size;
    BYTE_T *p_data = (BYTE_T *)data;

    if ((len < send_unit_len) && (sg_mcu_ota_ctx.download_size + len < fw->file_size)) {
        PR_ERR("not invalid len:%d", len);
        *remain_len = len;
        return OPRT_OK;
    }
    if (sg_mcu_ota_ctx.download_size >= fw->file_size) {
        PR_ERR("download_size%d, file size:%d", sg_mcu_ota_ctx.download_size, fw->file_size);
        *remain_len = 0;
        return OPRT_OK;
    }
    PR_DEBUG("download_size = %d,get len:%d", sg_mcu_ota_ctx.download_size, len);
    OPERATE_RET op_ret = OPRT_OK;
    send_len = len;
    while (send_len >= send_unit_len) {
        PR_DEBUG("upgrade_trans_cb");
        op_ret = __mcu_ota_trans_cb(&p_data[len - send_len], send_unit_len);
        if (op_ret != OPRT_OK) {
            PR_ERR("MCU upgrade file failed!");
            goto DOWN_LOAD_ERR;
        }
        sg_mcu_ota_ctx.download_size += send_unit_len;
        send_len -= send_unit_len;
    }

    PR_DEBUG("send_len----%d---file_size :%d---send_len:%d", send_len, fw->file_size, sg_mcu_ota_ctx.download_size);
    if ((send_len != 0) && (send_len >= fw->file_size - sg_mcu_ota_ctx.download_size)) {
        op_ret = __mcu_ota_trans_cb(&p_data[len - send_len], fw->file_size - sg_mcu_ota_ctx.download_size);
        if (OPRT_OK != op_ret) {
            PR_ERR("upgrade_trans_cb fault:%d", op_ret);
            goto DOWN_LOAD_ERR;
        }
        send_len -= (fw->file_size - sg_mcu_ota_ctx.download_size);
        sg_mcu_ota_ctx.download_size += (fw->file_size - sg_mcu_ota_ctx.download_size);
    }

    if (sg_mcu_ota_ctx.download_size >= fw->file_size) {
        PR_NOTICE("recv all data of user file");
    }
    *remain_len = send_len;
    return OPRT_OK;

DOWN_LOAD_ERR:
    return OPRT_COM_ERROR;
}

STATIC OPERATE_RET __mcu_ota_dev_ug_notify_cb(IN CONST FW_UG_S *fw, IN CONST INT_T download_result,
                                              IN PVOID_T pri_data)
{
    if (OPRT_OK == download_result) { // update success
        PR_NOTICE("the gateway upgrade success");
    } else {
        PR_ERR("the gateway upgrade failed");
    }

    sg_mcu_ota_ctx.IsMcuUpgrading = FALSE;
    return OPRT_OK;
}

/**
 * @brief Handler to process download result
 * @param[in] download_result download result, 0 indicates success
 * @return 执行结果,返回 OPRT_OK表示成功，返回 其他 则表示失败
 */
OPERATE_RET gfw_mcu_ota_dev_ug_notify_cb(IN CONST INT_T download_result)
{
    if (OPRT_OK == download_result) { // update success
        PR_NOTICE("the gateway upgrade success");
    } else {
        PR_ERR("the gateway upgrade failed");
    }

    sg_mcu_ota_ctx.IsMcuUpgrading = FALSE;
    return OPRT_OK;
}

BOOL_T gfw_is_in_mcu_ota_reset()
{
    // PR_DEBUG("in mcu ota:%d", sg_mcu_ota_ctx.IsMcuUgReset);
    return sg_mcu_ota_ctx.IsMcuUgReset;
}

BOOL_T gfw_is_in_mcu_ota_upgrade()
{
    return sg_mcu_ota_ctx.IsMcuUpgrading;
}

OPERATE_RET gfw_mcu_ota_ug_inform_cb(IN CONST FW_UG_S *fw)
{
    PR_DEBUG("Rev GW Upgrade Info");
    PR_DEBUG("fw->fw_url:%s", fw->fw_url);
    PR_DEBUG("fw->fw_hmac:%s", fw->fw_hmac);
    PR_DEBUG("fw->sw_ver:%s", fw->sw_ver);
    PR_DEBUG("fw->file_size:%d", fw->file_size);
    OPERATE_RET op_ret = OPRT_OK;

    sg_mcu_ota_ctx.download_size = 0;
    sg_mcu_ota_ctx.IsMcuUpgrading = TRUE;
    op_ret = __mcu_ota_start_cb(fw);
    if (OPRT_OK != op_ret) {
        PR_ERR("upgrade_start_cb fault:%d", op_ret);
        set_gw_ext_stat(EXT_NORMAL_S);
        sg_mcu_ota_ctx.IsMcuUpgrading = FALSE;
        return op_ret;
    }

    PR_DEBUG("start tuya_iot_upgrade_dev");

    op_ret = tuya_iot_upgrade_gw(fw, __mcu_ota_down_load_data_cb, __mcu_ota_dev_ug_notify_cb, NULL);
    if (OPRT_OK != op_ret) {
        PR_ERR("tuya_iot_upgrade_dev err:%d", op_ret);
    }

    return op_ret;
}

INT_T gfw_mcu_ota_pre_ug_inform_cb(IN CONST FW_UG_S *fw)
{
    PR_NOTICE("pre_gw_ug_inform_cb");
    return TUS_RD;
}

CONST AUDIO_GFW_CMD_CFG_T s_gfw_audio_ota_cmd[] GFW_CMD_SECTION = {
    {GFW_CMD_OTA_START, INVALID_SUBCMD, __gfw_cmd_mcu_ota_start},
    {GFW_CMD_OTA_TRANS, INVALID_SUBCMD, __gfw_cmd_mcu_ota_trans},
};

OPERATE_RET gfw_mcu_ota_init(VOID)
{
    OPERATE_RET op_ret = OPRT_OK;

    op_ret = gfw_core_cmd_register(s_gfw_audio_ota_cmd, CNTSOF(s_gfw_audio_ota_cmd));
    if (OPRT_OK != op_ret) {
        PR_ERR("gfw_core_cmd_register op_ret:%d", op_ret);
        return op_ret;
    }

    op_ret = ty_subscribe_event(GFW_EVENT_AUDIO_VERSION, "mcu.ver", __mcu_ota_ver_proc, SUBSCRIBE_TYPE_NORMAL);
    if (OPRT_OK != op_ret) {
        PR_ERR("ty_subscribe_event op_ret:%d", op_ret);
        return op_ret;
    }

    return op_ret;
}
