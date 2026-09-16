/**
 * @file claw_cli_config.c
 * @brief "claw_config" 命令逻辑:set/list claw 运行时凭证(纯分发,不碰 KV)。注册在 claw_cli.c。
 *
 * Usage:
 *   claw_config set feishu <app_id> <app_secret>
 *   claw_config set llm <base_url> <model> <api_key>
 *   claw_config list
 */
#include "claw_cli.h"
#include <string.h>
#include <stdio.h>

/* host 测 stub 顶替这些接口,故 extern 不 include channel/provider 头。 */
extern OPERATE_RET feishu_channel_set_creds(CONST CHAR_T *app_id, CONST CHAR_T *app_secret);
extern OPERATE_RET feishu_channel_get_creds_masked(CHAR_T *out, UINT_T out_cap);
extern OPERATE_RET wukong_provider_claw_set_llm(CONST CHAR_T *base_url, CONST CHAR_T *model, CONST CHAR_T *api_key);
extern OPERATE_RET wukong_provider_claw_get_llm_masked(CHAR_T *out, UINT_T out_cap);

#define CLAW_CONFIG_USAGE \
    "usage: claw_config set feishu <app_id> <app_secret>\n" \
    "       claw_config set llm <base_url> <model> <api_key>\n" \
    "       claw_config list\n"

INT_T claw_config_dispatch(int argc, char **argv, CHAR_T *resp, int resp_cap)
{
    if (NULL == resp || resp_cap <= 0) {
        return -1;
    }
    resp[0] = '\0';

    if (argc >= 3 && 0 == strcmp(argv[1], "set") && 0 == strcmp(argv[2], "feishu")) {
        if (argc < 5) {
            snprintf(resp, resp_cap, "%s", CLAW_CONFIG_USAGE);
            return -1;
        }
        OPERATE_RET rt = feishu_channel_set_creds(argv[3], argv[4]);
        if (OPRT_OK != rt) {
            snprintf(resp, resp_cap, "set feishu failed (%d)", (int)rt);
            return (INT_T)rt;
        }
        snprintf(resp, resp_cap, "OK");
        return 0;
    }

    if (argc >= 3 && 0 == strcmp(argv[1], "set") && 0 == strcmp(argv[2], "llm")) {
        if (argc < 6) {
            snprintf(resp, resp_cap, "%s", CLAW_CONFIG_USAGE);
            return -1;
        }
        OPERATE_RET rt = wukong_provider_claw_set_llm(argv[3], argv[4], argv[5]);
        if (OPRT_OK != rt) {
            snprintf(resp, resp_cap, "set llm failed (%d)", (int)rt);
            return (INT_T)rt;
        }
        snprintf(resp, resp_cap, "OK");
        return 0;
    }

    if (argc >= 2 && 0 == strcmp(argv[1], "list")) {
        CHAR_T masked[128] = {0};
        CHAR_T llm[192] = {0};
        feishu_channel_get_creds_masked(masked, sizeof(masked));
        wukong_provider_claw_get_llm_masked(llm, sizeof(llm));
        snprintf(resp, resp_cap, "%s\n%s\n", masked, llm);
        return 0;
    }

    snprintf(resp, resp_cap, "%s", CLAW_CONFIG_USAGE);
    return -1;
}

void cli_claw_config_cmd(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv)
{
    (VOID_T)claw_config_dispatch(argc, argv, pcWriteBuffer, xWriteBufferLen);
}
