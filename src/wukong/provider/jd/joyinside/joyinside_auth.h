/**
 * @file joyinside_auth.h
 * @brief joyinside auth module
 * @version 0.1
 * @date 2025-12-15
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

#ifndef __JOYINSIDE_AUTH_H__
#define __JOYINSIDE_AUTH_H__

#include "tuya_cloud_types.h"
#include "httpc.h"

typedef struct {
    CHAR_T *accessToken;
    UINT_T expireIn;
    CHAR_T *refreshToken;
    UINT_T refreshExpireIn;
} JD_TOKEN_S;

typedef struct {
    CHAR_T *vendorId;
    CHAR_T *appId;
    CHAR_T *ak;
    CHAR_T *sk;
} JD_AK_KEY_S;

typedef struct {
    /*JD_DEV_TYPE*/
    CHAR_T *type;
    /*platform created or registered interface returned*/
    CHAR_T *botId;
    /*deviceId*/
    CHAR_T *sn;
    /*device name*/
    CHAR_T *name;
    /*device model*/
    CHAR_T *deviceModel;
    /*timbre id assigned by operation platform*/
    CHAR_T *timbreId;
    /*description information*/
    CHAR_T *desc;
} JD_DEV_INFO_S;

typedef struct {
    JD_AK_KEY_S a;
    JD_DEV_INFO_S d;
} JD_SIGN_S;

#define JD_ENV_PROD "joyinside.jd.com"
#define JD_ENV_TEST "uat-joyinside.3.cn"
#define JD_ENV JI_ENV_PROD
#define JD_DEV_TYPE_PROD "PHYSICAL_ROBOT"
#define JD_DEV_TYPE_TEST "APP_ROBOT"
#define JD_DEV_TYPE JD_DEV_TYPE_PROD

typedef USHORT_T JD_GET_TOKEN_ERR_E;
#define JD_TOKEN_INVALID_PARM 1001
#define JD_TOKEN_VER_ERR 1002
#define JD_TOKEN_TS_INVALID 1003
#define JD_TOKEN_AUTH_FAILED 1004
#define JD_TOKEN_SIGN_ERR 1005
#define JD_TOKEN_SYSTEM_ERR 1006

/**
 * @brief joyinside device register
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET joyinside_dev_register(VOID);

/**
 * @brief add auth header to http session
 *
 * @param[in] session http session
 * @param[in] param user param
 * @return none
 */
VOID joyinside_add_auth_header(http_session_t session, VOID* param);

/**
 * @brief set joyinside auth info
 *
 * @param[in] s auth sign info
 * @return none
 */
VOID joyinside_set_auth_info(JD_SIGN_S *s);

/**
 * @brief joyinside auth token
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET joyinside_auth_token(VOID);

/**
 * @brief joyinside auth init
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET joyinside_auth_init(VOID);

/**
 * @brief joyinside auth deinit
 *
 */
VOID joyinside_auth_deinit(VOID);

/**
 * @brief get ak key
 *
 * @return ak key
 */
JD_AK_KEY_S *joyinside_get_ak_key(VOID);

/**
 * @brief get device info
 *
 * @return device info
 */
JD_DEV_INFO_S *joyinside_get_dev_info(VOID);

/**
 * @brief get token info
 *
 * @return token info
 */
JD_TOKEN_S *joyinside_get_token_info(VOID);
#endif // __JOYINSIDE_AUTH_H__