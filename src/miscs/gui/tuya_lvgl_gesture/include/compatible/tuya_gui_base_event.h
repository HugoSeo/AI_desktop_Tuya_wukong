/**
 * @file base_event.h
 * @brief tuya simple event module
 * @version 1.0
 * @date 2019-10-30
 *
 * @copyright Copyright (c) tuya.inc 2019
 *
 */

#ifndef __TUYA_GUI_BASE_EVENT_H__
#define __TUYA_GUI_BASE_EVENT_H__

#include "tuya_gui_compatible.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TUYA_SUBSCRIBE_ASYNC            //订阅及取消订阅异步处理(避免图形使用中死机?)
/**
 * @brief max length of event name
 *
 */
#ifndef EVENT_NAME_MAX_LEN
#define EVENT_NAME_MAX_LEN (16)  // move to tuya_iot_config.h. use kconfig config. default is 16
#endif

/**
 * @brief max length of event description
 *
 */
#define EVENT_DESC_MAX_LEN (32)

/**
 * @brief subscriber type
 *
 */
typedef BYTE_T SUBSCRIBE_TYPE_E;
#define SUBSCRIBE_TYPE_NORMAL    0  // normal type, dispatch by the subscribe order, remove when unsubscribe
#define SUBSCRIBE_TYPE_EMERGENCY 1  // emergency type, dispatch first, remove when unsubscribe
#define SUBSCRIBE_TYPE_ONETIME   2  // one time type, dispatch by the subscribe order, remove after first time dispath

/**
 * @brief event subscribe callback function type
 *
 */
typedef INT_T(*EVENT_SUBSCRIBE_CB)(VOID_T *data);

typedef BYTE_T BASE_EVENT_MODE_E;
#define BASE_EVENT_SUBSCRIBE        0
#define BASE_EVENT_UNSUBSCRIBE      1

typedef struct {
    char name[EVENT_NAME_MAX_LEN+1];    // 名称，用于记录该node关注的事件信息
    char desc[EVENT_DESC_MAX_LEN+1];    // 描述，用于记录该node的相关信息用于定位问题
    EVENT_SUBSCRIBE_CB cb;              // 事件处理函数
    SUBSCRIBE_TYPE_E type;              // 是否紧急标志

    BASE_EVENT_MODE_E mode; 
} TY_EVENT_MSG_S;

/** 
 * @brief: 发布指定事件，会通知所有订阅该事件的订阅者处理
 *
 * @param[in] name: 事件名，事件标识，字符串，16字节长的
 * @param[in] data: 事件数据，数据和事件类型绑定，发布事件和订阅事件必须使用同一事件数据类型定义
 * @param[in] len: 事件数据长度
 *
 * @return int: 0成功，非0，请参照tuya error code描述文档 
 */
int tuya_publish(const char *name, void *data);

/** 
 * @brief: 订阅指定事件，会通过回调函数处理消息发布内容
 *
 * @param[in] name: 事件名，事件标识，字符串，16字节长的
 * @param[in] desc: 描述信息，表面订阅者身份、目的，32字节长度，方便定位问题
 * @param[in] cb: 事件处理回调函数
 * @param[in] is_emergency: 紧急事件，必须第一个处理
 *
 * @note： desc、cb构成了一个二元组，这个二元组标识一个唯一订阅者，同一个desc不同的cb，也会认为是不同的
 *      订阅。在事件发布之前就订阅的，可以收到事件；在事件发布之后订阅的，收不到之前发布的事件。
 *
 * @return int: 0成功，非0，请参照tuya error code描述文档 
 */
int tuya_subscribe(const char *name, const char *desc, const EVENT_SUBSCRIBE_CB cb, int is_emergency);

/** 
 * @brief: 订阅指定事件，会通过回调函数处理消息发布内容
 *
 * @param[in] name: 事件名，事件标识，字符串，16字节长的
 * @param[in] desc: 描述信息，订阅者身份、目的，32字节长度，方便定位问题
 * @param[in] cb: 事件处理回调函数
 *
 * @note： desc、cb构成了一个二元组，这个二元组标识一个唯一订阅者，同一个desc不同的cb，也会认为是不同的
 *      订阅
 *
 * @return int: 0成功，非0，请参照tuya error code描述文档 
 */
int tuya_unsubscribe(const char *name, const char *desc, EVENT_SUBSCRIBE_CB cb);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /*__BASE_EVENT_H__ */


