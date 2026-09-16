/**
 * @file tuya_tmm_manager_atop.c
 * @brief ATOP wrappers for device contact and room APIs.
 */

#include <stdio.h>
#include <string.h>

#include "tuya_iot_internal_api.h"
#include "uni_log.h"

#include "tuya_tmm_manager_atop.h"

#define TMM_ATOP_VER                     "1.0"

#define TMM_ATOP_CONTACT_SYNC            "thing.ai.contact.sync"
#define TMM_ATOP_CONTACT_APPLY           "thing.ai.contact.apply"
#define TMM_ATOP_CONTACT_LIST            "thing.ai.contact.list"
#define TMM_ATOP_CONTACT_DELETE          "thing.ai.contact.delete"
#define TMM_ATOP_ROOM_CREATE             "thing.ai.contact.room.create"
#define TMM_ATOP_ROOM_JOIN               "thing.ai.contact.room.join"
#define TMM_ATOP_ROOM_MEMBERS            "thing.ai.contact.room.members"
#define TMM_ATOP_ROOM_APPLY              "thing.ai.contact.room.apply"
#define TMM_ATOP_ROOM_LEAVE              "thing.ai.contact.room.leave"

/**
 * @brief 统一封装 ATOP POST 调用。
 * @param[in] api ATOP DOT 接口名。
 * @param[in] ver ATOP 接口版本。
 * @param[in] body 请求体 JSON 字符串；无入参接口可传 NULL。
 * @param[out] result 云端返回结果；可为 NULL，非 NULL 时调用方负责释放。
 * @return OPRT_OK 成功，其他值表示失败。
 */
STATIC OPERATE_RET __tmm_atop_post(CONST CHAR_T *api, CONST CHAR_T *ver,
                                   CHAR_T *body, ty_cJSON **result)
{
    OPERATE_RET rt = OPRT_OK;

    if (result != NULL) {
        *result = NULL;
    }

    rt = iot_httpc_common_post_simple(api, ver, body, NULL, result);
    if (rt != OPRT_OK) {
        PR_ERR("tmm atop %s failed, rt=%d", api, rt);
        return rt;
    }

    return OPRT_OK;
}

/**
 * @brief 构造仅包含房间码的请求体。
 * @param[in] code 4 位房间码。
 * @return JSON 字符串，调用方负责 ty_cJSON_FreeBuffer；失败返回 NULL。
 */
STATIC CHAR_T *__tmm_atop_build_code_body(CONST CHAR_T *code)
{
    ty_cJSON *root = NULL;
    CHAR_T *body = NULL;

    if (code == NULL || code[0] == '\0') {
        return NULL;
    }

    root = ty_cJSON_CreateObject();
    if (root == NULL) {
        return NULL;
    }

    ty_cJSON_AddStringToObject(root, "code", code);
    body = ty_cJSON_PrintUnformatted(root);
    ty_cJSON_Delete(root);

    return body;
}

/**
 * @brief 设备初始化同步通讯录。
 * @param[out] result 云端返回的 ContactListItem 数组；可为 NULL，非 NULL 时调用方负责 ty_cJSON_Delete。
 * @return OPRT_OK 成功，其他值表示失败。
 */
OPERATE_RET tuya_tmm_manager_atop_contact_sync(ty_cJSON **result)
{
    return __tmm_atop_post(TMM_ATOP_CONTACT_SYNC, TMM_ATOP_VER, NULL, result);
}

/**
 * @brief NFC 扫码添加好友，向目标设备发起好友申请。
 * @param[in] target_dev_id 对方设备 devId。
 * @return OPRT_OK 成功，其他值表示失败。
 */
OPERATE_RET tuya_tmm_manager_atop_contact_apply_devId(CONST CHAR_T *target_devId)
{
    ty_cJSON *root = NULL;
    CHAR_T *body = NULL;
    OPERATE_RET rt = OPRT_OK;

    if (target_devId == NULL || target_devId[0] == '\0') {
        return OPRT_INVALID_PARM;
    }

    root = ty_cJSON_CreateObject();
    if (root == NULL) {
        return OPRT_MALLOC_FAILED;
    }

    ty_cJSON_AddStringToObject(root, "targetDevId", target_devId);
    ty_cJSON_AddStringToObject(root, "targetUuid", "");
    body = ty_cJSON_PrintUnformatted(root);
    ty_cJSON_Delete(root);
    if (body == NULL) {
        return OPRT_MALLOC_FAILED;
    }

    rt = __tmm_atop_post(TMM_ATOP_CONTACT_APPLY, TMM_ATOP_VER, body, NULL);
    ty_cJSON_FreeBuffer(body);

    return rt;
}

/**
 * @brief NFC 扫码添加好友，向目标设备发起好友申请。
 * @param[in] target_dev_id 对方设备 UUID。
 * @return OPRT_OK 成功，其他值表示失败。
 */
OPERATE_RET tuya_tmm_manager_atop_contact_apply_uuid(CONST CHAR_T *target_uuid)
{
    ty_cJSON *root = NULL;
    CHAR_T *body = NULL;
    OPERATE_RET rt = OPRT_OK;

    if (target_uuid == NULL || target_uuid[0] == '\0') {
        return OPRT_INVALID_PARM;
    }

    root = ty_cJSON_CreateObject();
    if (root == NULL) {
        return OPRT_MALLOC_FAILED;
    }

    ty_cJSON_AddStringToObject(root, "targetDevId", "");
    ty_cJSON_AddStringToObject(root, "targetUuid", target_uuid);
    body = ty_cJSON_PrintUnformatted(root);
    ty_cJSON_Delete(root);
    if (body == NULL) {
        return OPRT_MALLOC_FAILED;
    }

    rt = __tmm_atop_post(TMM_ATOP_CONTACT_APPLY, TMM_ATOP_VER, body, NULL);
    ty_cJSON_FreeBuffer(body);

    return rt;
}

/**
 * @brief 查询当前设备通讯录列表。
 * @param[out] result 云端返回的 ContactListItem 数组，调用方负责 ty_cJSON_Delete。
 * @return OPRT_OK 成功，其他值表示失败。
 */
OPERATE_RET tuya_tmm_manager_atop_contact_list(ty_cJSON **result)
{
    if (result == NULL) {
        return OPRT_INVALID_PARM;
    }

    return __tmm_atop_post(TMM_ATOP_CONTACT_LIST, TMM_ATOP_VER, NULL, result);
}

/**
 * @brief 删除好友申请来源的联系人。
 * @param[in] contact_type 联系人类型，0=设备，1=用户。
 * @param[in] target_id 目标标识，设备 devId 或用户 uid。
 * @return OPRT_OK 成功；其他值表示失败。
 */
OPERATE_RET tuya_tmm_manager_atop_contact_delete(INT_T contact_type, CONST CHAR_T *target_id)
{
    ty_cJSON *root = NULL;
    CHAR_T *body = NULL;
    OPERATE_RET rt = OPRT_OK;

    if ((contact_type != 0 && contact_type != 1) || target_id == NULL || target_id[0] == '\0') {
        return OPRT_INVALID_PARM;
    }

    root = ty_cJSON_CreateObject();
    if (root == NULL) {
        return OPRT_MALLOC_FAILED;
    }

    ty_cJSON_AddNumberToObject(root, "contactType", contact_type);
    ty_cJSON_AddStringToObject(root, "targetId", target_id);
    body = ty_cJSON_PrintUnformatted(root);
    ty_cJSON_Delete(root);
    if (body == NULL) {
        return OPRT_MALLOC_FAILED;
    }

    rt = __tmm_atop_post(TMM_ATOP_CONTACT_DELETE, TMM_ATOP_VER, body, NULL);
    ty_cJSON_FreeBuffer(body);
    PR_NOTICE("tmm contact delete post rt=%d", rt);

    return rt;
}

/**
 * @brief 创建通讯录临时房间。
 * @param[out] result 云端返回的 Room 对象，调用方负责 ty_cJSON_Delete。
 * @return OPRT_OK 成功，其他值表示失败。
 */
OPERATE_RET tuya_tmm_manager_atop_room_create(ty_cJSON **result)
{
    if (result == NULL) {
        return OPRT_INVALID_PARM;
    }

    return __tmm_atop_post(TMM_ATOP_ROOM_CREATE, TMM_ATOP_VER, NULL, result);
}

/**
 * @brief 使用 4 位房间码加入通讯录房间。
 * @param[in] code 4 位房间码。
 * @param[out] result 云端返回的 Room 对象，调用方负责 ty_cJSON_Delete。
 * @return OPRT_OK 成功，其他值表示失败。
 */
OPERATE_RET tuya_tmm_manager_atop_room_join(CONST CHAR_T *code, ty_cJSON **result)
{
    CHAR_T *body = NULL;
    OPERATE_RET rt = OPRT_OK;

    if (result == NULL || code == NULL || code[0] == '\0') {
        return OPRT_INVALID_PARM;
    }

    body = __tmm_atop_build_code_body(code);
    if (body == NULL) {
        return OPRT_MALLOC_FAILED;
    }

    rt = __tmm_atop_post(TMM_ATOP_ROOM_JOIN, TMM_ATOP_VER, body, result);
    ty_cJSON_FreeBuffer(body);

    return rt;
}

/**
 * @brief 查询房间成员列表。
 * @param[in] code 4 位房间码。
 * @param[out] result 云端返回的 Room 对象，调用方负责 ty_cJSON_Delete。
 * @return OPRT_OK 成功，其他值表示失败。
 */
OPERATE_RET tuya_tmm_manager_atop_room_members(CONST CHAR_T *code, ty_cJSON **result)
{
    CHAR_T *body = NULL;
    OPERATE_RET rt = OPRT_OK;

    if (result == NULL || code == NULL || code[0] == '\0') {
        return OPRT_INVALID_PARM;
    }

    body = __tmm_atop_build_code_body(code);
    if (body == NULL) {
        return OPRT_MALLOC_FAILED;
    }

    rt = __tmm_atop_post(TMM_ATOP_ROOM_MEMBERS, TMM_ATOP_VER, body, result);
    ty_cJSON_FreeBuffer(body);

    return rt;
}

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
                                             INT_T *created_count)
{
    ty_cJSON *root = NULL;
    ty_cJSON *arr = NULL;
    ty_cJSON *result = NULL;
    CHAR_T *body = NULL;
    OPERATE_RET rt = OPRT_OK;

    if (created_count != NULL) {
        *created_count = 0;
    }
    if (code == NULL || code[0] == '\0' || targets == NULL || target_count == 0) {
        return OPRT_INVALID_PARM;
    }

    root = ty_cJSON_CreateObject();
    arr = ty_cJSON_CreateArray();
    if (root == NULL || arr == NULL) {
        if (root != NULL) {
            ty_cJSON_Delete(root);
        }
        if (arr != NULL) {
            ty_cJSON_Delete(arr);
        }
        return OPRT_MALLOC_FAILED;
    }

    ty_cJSON_AddStringToObject(root, "code", code);
    ty_cJSON_AddItemToObject(root, "targets", arr);
    for (UINT_T i = 0; i < target_count; i++) {
        if (targets[i] != NULL && targets[i][0] != '\0') {
            ty_cJSON_AddItemToArray(arr, ty_cJSON_CreateString(targets[i]));
        }
    }

    body = ty_cJSON_PrintUnformatted(root);
    ty_cJSON_Delete(root);
    if (body == NULL) {
        return OPRT_MALLOC_FAILED;
    }

    rt = __tmm_atop_post(TMM_ATOP_ROOM_APPLY, TMM_ATOP_VER, body, &result);
    ty_cJSON_FreeBuffer(body);
    if (rt != OPRT_OK) {
        return rt;
    }

    if (created_count != NULL && result != NULL && ty_cJSON_IsNumber(result)) {
        *created_count = result->valueint;
    }
    if (result != NULL) {
        ty_cJSON_Delete(result);
    }

    return OPRT_OK;
}

/**
 * @brief 退出通讯录房间；房主退出时云端会解散房间。
 * @param[in] code 4 位房间码。
 * @return OPRT_OK 成功，其他值表示失败。
 */
OPERATE_RET tuya_tmm_manager_atop_room_leave(CONST CHAR_T *code)
{
    CHAR_T *body = NULL;
    OPERATE_RET rt = OPRT_OK;

    if (code == NULL || code[0] == '\0') {
        return OPRT_INVALID_PARM;
    }

    body = __tmm_atop_build_code_body(code);
    if (body == NULL) {
        return OPRT_MALLOC_FAILED;
    }

    rt = __tmm_atop_post(TMM_ATOP_ROOM_LEAVE, TMM_ATOP_VER, body, NULL);
    ty_cJSON_FreeBuffer(body);

    return rt;
}

/**
 * @brief 遍历 ContactListItem 数组并逐条回调联系人。
 * @param[in] list ContactListItem 数组。
 * @param[in] cb 联系人回调；可为 NULL。
 * @param[in] usrdata 透传给 cb 的用户数据。
 * @return OPRT_OK 成功，其他值表示失败。
 */
OPERATE_RET tuya_tmm_manager_atop_contact_iterate(ty_cJSON *list,
                                                  TUYA_TMM_MANAGER_ATOP_CONTACT_CB cb,
                                                  VOID *usrdata)
{
    INT_T count = 0;

    if (list == NULL || !ty_cJSON_IsArray(list)) {
        return OPRT_INVALID_PARM;
    }

    count = ty_cJSON_GetArraySize(list);
    for (INT_T index = 0; index < count; index++) {
        ty_cJSON *item = ty_cJSON_GetArrayItem(list, index);
        ty_cJSON *id = NULL;
        ty_cJSON *name = NULL;
        ty_cJSON *type = NULL;
        CHAR_T type_str[8] = {0};

        if (item == NULL || !ty_cJSON_IsObject(item)) {
            continue;
        }

        id = ty_cJSON_GetObjectItem(item, "resourceId");
        name = ty_cJSON_GetObjectItem(item, "resourceName");
        type = ty_cJSON_GetObjectItem(item, "resourceType");
        if (id == NULL || !ty_cJSON_IsString(id) || id->valuestring == NULL ||
            type == NULL || !ty_cJSON_IsNumber(type)) {
            PR_WARN("tmm contact item invalid");
            continue;
        }

        snprintf(type_str, sizeof(type_str), "%d", type->valueint);
        PR_DEBUG("tmm contact: id:%s, name:%s, type:%d",
                 id->valuestring,
                 (name != NULL && ty_cJSON_IsString(name) && name->valuestring != NULL) ?
                 name->valuestring : "",
                 type->valueint);
        if (cb != NULL) {
            cb(id->valuestring,
               (name != NULL && ty_cJSON_IsString(name) && name->valuestring != NULL) ?
               name->valuestring : id->valuestring,
               type_str,
               usrdata);
        }
    }

    return OPRT_OK;
}

/**
 * @brief 顺序执行通讯录初始化同步和通讯录列表查询，并逐条回调联系人。
 * @param[in] cb 联系人回调；可为 NULL，仅执行同步和查询。
 * @param[in] usrdata 透传给 cb 的用户数据。
 * @return OPRT_OK 成功，其他值表示失败。
 */
OPERATE_RET tuya_tmm_manager_atop_contact_sync_list(TUYA_TMM_MANAGER_ATOP_CONTACT_CB cb,
                                                    VOID *usrdata)
{
    ty_cJSON *sync_result = NULL;
    ty_cJSON *list_result = NULL;
    OPERATE_RET rt = OPRT_OK;

    rt = tuya_tmm_manager_atop_contact_sync(&sync_result);
    if (sync_result != NULL) {
        ty_cJSON_Delete(sync_result);
    }
    if (rt != OPRT_OK) {
        return rt;
    }

    rt = tuya_tmm_manager_atop_contact_list(&list_result);
    if (rt != OPRT_OK) {
        if (list_result != NULL) {
            ty_cJSON_Delete(list_result);
        }
        return rt;
    }

    rt = tuya_tmm_manager_atop_contact_iterate(list_result, cb, usrdata);
    ty_cJSON_Delete(list_result);

    return rt;
}
