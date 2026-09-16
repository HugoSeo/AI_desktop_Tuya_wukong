/**
 * @file tuya_tmm_manager_atop.h
 * @brief ATOP wrappers for device contact and room APIs.
 */

#ifndef __TUYA_TMM_MANAGER_ATOP_H__
#define __TUYA_TMM_MANAGER_ATOP_H__

#include "tuya_cloud_types.h"
#include "tuya_cloud_com_defs.h"
#include "ty_cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TUYA_TMM_OK 0
#define TUYA_TMM_FALSE -1

typedef VOID (*TUYA_TMM_MANAGER_ATOP_CONTACT_CB)(CHAR_T *id, CHAR_T *name,
                                                 CHAR_T *type, VOID *usrdata);

/**
 * @brief 设备初始化同步通讯录。
 * @param[out] result 云端返回的 ContactListItem 数组；可为 NULL，非 NULL 时调用方负责 ty_cJSON_Delete。
 * @return OPRT_OK 成功，其他值表示失败。
 */
OPERATE_RET tuya_tmm_manager_atop_contact_sync(ty_cJSON **result);

/**
 * @brief NFC 扫码添加好友，向目标设备发起好友申请。
 * @param[in] target_dev_id 对方设备 devId。
 * @return OPRT_OK 成功，其他值表示失败。
 */
OPERATE_RET tuya_tmm_manager_atop_contact_apply_devId(CONST CHAR_T *target_devId);

/**
 * @brief NFC 扫码添加好友，向目标设备发起好友申请。
 * @param[in] target_dev_id 对方设备 UUID。
 * @return OPRT_OK 成功，其他值表示失败。
 */
OPERATE_RET tuya_tmm_manager_atop_contact_apply_uuid(CONST CHAR_T *target_uuid);

/**
 * @brief 查询当前设备通讯录列表。
 * @param[out] result 云端返回的 ContactListItem 数组，调用方负责 ty_cJSON_Delete。
 * @return OPRT_OK 成功，其他值表示失败。
 */
OPERATE_RET tuya_tmm_manager_atop_contact_list(ty_cJSON **result);

/**
 * @brief 删除好友申请来源的联系人。
 * @param[in] contact_type 联系人类型，0=设备，1=用户。
 * @param[in] target_id 目标标识，设备 devId 或用户 uid。
 * @return OPRT_OK 成功；其他值表示失败。
 */
OPERATE_RET tuya_tmm_manager_atop_contact_delete(INT_T contact_type, CONST CHAR_T *target_id);

/**
 * @brief 创建通讯录临时房间。
 * @param[out] result 云端返回的 Room 对象，调用方负责 ty_cJSON_Delete。
 * @return OPRT_OK 成功，其他值表示失败。
 */
OPERATE_RET tuya_tmm_manager_atop_room_create(ty_cJSON **result);

/**
 * @brief 使用 4 位房间码加入通讯录房间。
 * @param[in] code 4 位房间码。
 * @param[out] result 云端返回的 Room 对象，调用方负责 ty_cJSON_Delete。
 * @return OPRT_OK 成功，其他值表示失败。
 */
OPERATE_RET tuya_tmm_manager_atop_room_join(CONST CHAR_T *code, ty_cJSON **result);

/**
 * @brief 查询房间成员列表。
 * @param[in] code 4 位房间码。
 * @param[out] result 云端返回的 Room 对象，调用方负责 ty_cJSON_Delete。
 * @return OPRT_OK 成功，其他值表示失败。
 */
OPERATE_RET tuya_tmm_manager_atop_room_members(CONST CHAR_T *code, ty_cJSON **result);

/**
 * @brief 向房间内指定成员批量发起好友申请。
 * @param[in] code 4 位房间码。
 * @param[in] targets 目标成员数组，每项格式为 "memberType:memberId"。
 * @param[in] target_count targets 数组元素个数。
 * @param[out] created_count 实际创建的申请数；可为 NULL。
 * @return OPRT_OK 成功，其他值表示失败。
 */
OPERATE_RET tuya_tmm_manager_atop_room_apply(CONST CHAR_T *code,
                                             CONST CHAR_T **targets,
                                             UINT_T target_count,
                                             INT_T *created_count);

/**
 * @brief 退出通讯录房间；房主退出时云端会解散房间。
 * @param[in] code 4 位房间码。
 * @return OPRT_OK 成功，其他值表示失败。
 */
OPERATE_RET tuya_tmm_manager_atop_room_leave(CONST CHAR_T *code);

/**
 * @brief 顺序执行通讯录初始化同步和通讯录列表查询，并逐条回调联系人。
 * @param[in] cb 联系人回调；可为 NULL，仅执行同步和查询。
 * @param[in] usrdata 透传给 cb 的用户数据。
 * @return OPRT_OK 成功，其他值表示失败。
 */
OPERATE_RET tuya_tmm_manager_atop_contact_sync_list(TUYA_TMM_MANAGER_ATOP_CONTACT_CB cb,
                                                    VOID *usrdata);

/**
 * @brief 遍历 ContactListItem 数组并逐条回调联系人。
 * @param[in] list ContactListItem 数组。
 * @param[in] cb 联系人回调；可为 NULL。
 * @param[in] usrdata 透传给 cb 的用户数据。
 * @return OPRT_OK 成功，其他值表示失败。
 */
OPERATE_RET tuya_tmm_manager_atop_contact_iterate(ty_cJSON *list,
                                                  TUYA_TMM_MANAGER_ATOP_CONTACT_CB cb,
                                                  VOID *usrdata);

#ifdef __cplusplus
}
#endif

#endif /* __TUYA_TMM_MANAGER_ATOP_H__ */
