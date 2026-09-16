/**
 * @file hugo_ai_desktop.h
 * @brief Tuya AI Toy module - public API and types.
 *
 * Declares configuration, state types and control interfaces for the AI toy
 * (wukong) application: init, DP processing, trigger mode and timers.
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

#ifndef __HUGO_AI_FACE_H__
#define __HUGO_AI_FACE_H__

#include "tuya_cloud_types.h"
#if defined(ENABLE_WIFI_SERVICE) && (ENABLE_WIFI_SERVICE == 1)
#include "tuya_cloud_wifi_defs.h"
#endif
#include "tal_sw_timer.h"
#include "tal_workqueue.h"
#include "smart_frame.h"
#include "wukong_ai_mode.h"
#include "tuya_cloud_com_defs.h"

#define FACE_ENABLE

#define FACE_PW_PIN             TUYA_GPIO_NUM_26

#define Face_Buffln             64
#define FACE_CMD_HEAD           0xef
#define FACE_CMD_HEAD1          0xaa

#define RANDOM_SIZE             4
#define KEY_SIZE                16

#define RXBUFFERSIZE            64 //64


enum  //模块应答指令说明
{
    MR_SUCCESS    = 0,     // 成功
    MR_REJECTED   = 1,     // 模块拒绝该命令
    MR_ABORTED    = 2,     // 录入/解锁算法终止
    MR_FAILED4_CAMERA = 4, // 相机打开失败
    MR_FAILED4_UNKNOWNREASON = 5, // 未知错误
    MR_FAILED4_INVALIDPARAM = 6,  // 无效参数
    MR_FAILED4_NOMEMORY = 7,      // 内存不足
    MR_FAILED4_UNKNOWNUSER = 8,   // 没有已录入的用户
    MR_FAILED4_MAXUSER = 9,       // 录入超过最大用户数量
    MR_FAILED4_FACEENROLLED = 10, // 人脸已录入
    MR_FAILED4_LIVENESSCHECK = 12,// 活全检测失败
    MR_FAILED4_TIMEOUT = 13,      // 录入或解锁超时
    MR_FAILED4_AUTHORIZATION = 14,// 加密芯片授权失败
    MR_FAILED4_CAMERAFOV = 15,    // camera fov test failed
    MR_FAILED4_CAMERAQUA = 16,    // camera quality test failed
    MR_FAILED4_CAMERASTRU = 17,   // camera structure test failed
    MR_FAILED4_BOOT_TIMEOUT = 18, // boot up timeout
    MR_FAILED4_READ_FILE = 19,    // read file failed
    MR_FAILED4_WRITE_FILE = 20,   // write file failed
    MR_FAILED4_NO_ENCRYPT = 21,   // 通信协议未加密
    MR_FAILED4_NO_RGBIMAGE= 23,   // rgb image is not ready

//  MR_SUCCESS    = 0,     // success
    MR_FAIL   = 24,    // fail
//  MR_FAILED4_FACEENROLLED = 10, // this face has been enrolled
//  MR_FAILED4_TIMEOUT = 13,      // exceed the time limit
};

enum
{
    FACE_UP       = 0x10,      // 录入朝上人脸
    FACE_MIDDLE = 0x01,       // 录入正向人脸
    FACE_LEFT    = 0x04,       // 录入朝左人脸
    FACE_RIGHT   = 0x02,      // 录入朝右人脸
    FACE_DOWN   = 0x08,       // 录入朝下人脸
    FACE_UNDEFINE= 0x00,       // 未定义
    /* msg face direction end */
};

enum
{
    // Module to Host (M>h)
    MID_REPLY = 0x00,    //  Reply是模块对主控发送出的命令的应答，对于主控的每条命令，模块最终都会进行reply
    MID_NOTE  = 0x01,     //   模组READY
    MID_IMAGE = 0X02,     //   模块给主控传送图片
    // Host to Module (H->M)
    MID_RESET = 0x10,     // 取消处理命令，进入待机状态
    MID_GETSTATUS = 0x11, // 立即返回模组当前状态
    MID_VERIFY = 0x12,    // 鉴权解锁
    MID_ENROLL = 0x13,    // 新用户录入
    MID_ENROLL_SINGLE = 0x1D,    // 单帧录入
    MID_ENROLL_ITG = 0x26,    // 集成支持并扩展所有录入方式
    MID_SNAPIMAGE = 0x16, //  抓拍图片并存储到本地
    MID_GETSAVEDIMAGE = 0x17, // 获取待上传图片大小
    MID_UPLOADIMAGE = 0x18,   // 将本地存储的图片上传到主控
    MID_DELUSER = 0x20,   // 删除一个注册用户
    MID_DELALL  = 0x21,   // 删除所有注册用户
    MID_GETUSERINFO = 0x22,   // 获得某个注册用户的信息
    MID_FACERESET = 0x23,     //重置算法状态，如果正在进行交互式录入，会清空已录入的方向
    MID_GET_ALL_USERID = 0x24,  // 获取所有已注册用户的数量和ID
    MID_GET_VERSION =  0x30,   // 获得软件版本信息
    MID_WRITE_SN = 0x31,      // write sn to board
    MID_START_OTA = 0x40,     //进入OTA升级模式
    MID_STOP_OTA = 0x41,      //退出OTA模式模组重启
    MID_GET_OTA_STATUS = 0x42,//获取OTA状态以及传输升级包的起始包序号
    MID_OTA_HEADER = 0x43,    //发送升级包的大小，总包数，分包的大小，升级包的md5值
    MID_OTA_PACKET = 0x44,    //发送升级包：包序号、包大小、包数据
    MID_INIT_ENCRYPTION = 0x50,     //设置加密随机数
    MID_CONFIG_BAUDRATE = 0x51,     //OTA模式下设定通信口波特率
    MID_SET_RELEASE_ENC_KEY = 0x52, //设定量产加密秘钥序列，掉电会保存
    MID_SET_DEBUG_ENC_KEY = 0x53,   //设定调试加密秘钥序列，掉电丢失
    MID_GET_LOGFILE = 0x60,         //获取log文件的大小
    MID_UPLOAD_LOGFILE = 0x61,      //将保存的log上传至主控
//#ifdef Palm_EN
    PALM_ENROLL_SINGLE = 0x80,      //掌静脉注册
    PALM_VERIFY = 0x81,             //掌静脉识别
    PALM_DELALL = 0x82,             //掌静脉删除所有用户
    PALM_DEL_UID = 0x83,            //掌静脉删除指定用户
    PALM_GET_ALL_UID = 0x84,        //掌静脉获取所有用户
    PALM_ENROLL_ITG = 0x85,
//#endif
    MID_POWERDOWN = 0xED,           //模组断电前，保存简单的log
    MID_DEBUG_MODE = 0xF0,          //使能debug模式， 会存储所有的图片和较多的log。
    MID_GET_DEBUG_INFO = 0xF1,      //获取debug模式下存储的数据包大小
    MID_UPLOAD_DEBUG_INFO = 0xF2,   //上传debug模式下存储的数据包
    MID_DEMOMODE = 0xFE,            // 进入演示模式
    MID_MAX = 0xFF,                 // 保留
    /* communication message ID definitions end */
};

enum
{
    FACE_STANDBY=1,
    FACE_GET_VERSION,
    FACE_GET_VERSION_ACK,
    FACE_SET_KEY,
    FACE_SET_KEY_ACK,
    FACE_SET_DEBUG_ENC_KEY,       //设置密钥
    FACE_SET_DEBUG_ENC_KEY_ACK,       //设置密钥应答
    FACE_SET_RELEASE_KEY,  //设置密钥参数
    FACE_VERIFY,            //人脸解锁
    FACE_VERIFY_ACK,
    FACE_ADD_USER_ACK1,
    FACE_ADD_USER_ACK2,
    FACE_ADD_USER_ACK3,
    FACE_ADD_USER_ACK4,
    FACE_ADD_USER_ACK5,
    FACE_DEL_ONE_ACK,
    FACE_DEL_ALL_ACK,
//#ifdef Palm_EN
    PALM_ADD_USER_ACK,      //掌静脉注册响应
    PALM_DEL_ONE_ACK,       //掌静脉删除单个用户响应
    PALM_DEL_ALL_ACK,       //掌静脉删除所有响应
//#endif
    FACE_POWER_DOWN,
};



enum
{
    SET_INPW = 0,
    ADD_USER_FACE = 1,
    DEL_USER_FACE,
    DELALL_USER_FACE_ACK,
    DEL_ALL_FACE,    
    FACE_MANAGER,
    System_Rest_Wait,
};

typedef enum
{
  DISABLE = 0,
  ENABLE = !DISABLE
} ;



VOID Face_Init(VOID);
VOID hugo_Face_uart_task(VOID);
VOID hugo_ai_face_timer(VOID);
VOID hugo_ai_face_intimer(VOID);

// VOID face_name_write(BYTE_T face_id, BYTE_T *data);
// UINT8_T face_name_read(BYTE_T face_id, BYTE_T *data);
UINT8_T face_name_store(BYTE_T *data);
UINT8_T face_name_get(BYTE_T *data);

unsigned char GetCRC(const unsigned char *pData, unsigned char len);
// UINT8_T  Face_RXtime;
extern UINT8_T face_voice_flag;

#endif