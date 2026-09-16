/**
 * @file claw_cli.h
 * @brief claw CLI 对外接口:统一入口 + dispatch(host 测)。
 */
#pragma once
#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

int claw_cli_init(void);
INT_T claw_config_dispatch(int argc, char **argv, CHAR_T *resp, int resp_cap);

#ifdef __cplusplus
}
#endif
