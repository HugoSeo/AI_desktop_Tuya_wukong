/*
 * tuya_ai_linkpolicy.h
 * Copyright (C) 2025 cc <cc@tuya>
 *
 * Distributed under terms of the MIT license.
 */

#ifndef __TUYA_AI_LINKPOLICY_H__
#define __TUYA_AI_LINKPOLICY_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// 链路策略: 0=仅4G, 1=WiFi/4G自动切换
#define LINKPOLICY_4G_ONLY     0
#define LINKPOLICY_AUTO_SWITCH 1

OPERATE_RET tuya_ai_linkpolicy_init(VOID_T);
OPERATE_RET tuya_ai_linkpolicy_set(INT_T mode);

void tuya_ai_linkpolicy_cli_cmd(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif /* !__TUYA_AI_LINKPOLICY_H__ */
