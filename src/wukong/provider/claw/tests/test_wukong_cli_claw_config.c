/* claw_config CLI dispatch host test: set feishu 分发 + usage 错误 + list。
 * 飞书接口以本文件 stub 顶替(host 不链接 feishu 实现)。 */
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include "tuya_cloud_types.h"

/* --- stub feishu 接口，记录被调用的入参 --- */
static char  g_last_id[64], g_last_secret[64];
static int   g_set_creds_calls;
OPERATE_RET feishu_channel_set_creds(CONST CHAR_T *app_id, CONST CHAR_T *app_secret) {
    g_set_creds_calls++;
    snprintf(g_last_id, sizeof(g_last_id), "%s", app_id ? app_id : "");
    snprintf(g_last_secret, sizeof(g_last_secret), "%s", app_secret ? app_secret : "");
    return OPRT_OK;
}
OPERATE_RET feishu_channel_get_creds_masked(CHAR_T *out, UINT_T out_cap) {
    snprintf(out, out_cap, "feishu app_id=cli_x app_secret=abc****");
    return OPRT_OK;
}

/* --- stub llm provider 接口(Task3),记录被调用的入参 --- */
static char g_llm_url[64], g_llm_model[64], g_llm_key[64];
static int  g_set_llm_calls;
OPERATE_RET wukong_provider_claw_set_llm(CONST CHAR_T *base_url, CONST CHAR_T *model, CONST CHAR_T *api_key) {
    g_set_llm_calls++;
    snprintf(g_llm_url, sizeof(g_llm_url), "%s", base_url ? base_url : "");
    snprintf(g_llm_model, sizeof(g_llm_model), "%s", model ? model : "");
    snprintf(g_llm_key, sizeof(g_llm_key), "%s", api_key ? api_key : "");
    return OPRT_OK;
}
OPERATE_RET wukong_provider_claw_get_llm_masked(CHAR_T *out, UINT_T out_cap) {
    snprintf(out, out_cap, "llm base_url=u model=m api_key=abc****");
    return OPRT_OK;
}

INT_T claw_config_dispatch(int argc, char **argv, CHAR_T *resp, int resp_cap);

static void reset(void){ g_set_creds_calls=0; g_last_id[0]=0; g_last_secret[0]=0; }
static void reset_llm(void){ g_set_llm_calls=0; g_llm_url[0]=0; g_llm_model[0]=0; g_llm_key[0]=0; }

int main(void) {
    char resp[256];

    /* set feishu <id> <secret> -> 调 set_creds，入参透传 */
    reset();
    char *a1[] = {"claw_config","set","feishu","cli_abc","sec_xyz"};
    assert(claw_config_dispatch(5, a1, resp, sizeof(resp)) == 0);
    assert(g_set_creds_calls == 1);
    assert(strcmp(g_last_id, "cli_abc") == 0);
    assert(strcmp(g_last_secret, "sec_xyz") == 0);

    /* set feishu 缺参 -> usage，不调 set_creds */
    reset();
    char *a2[] = {"claw_config","set","feishu","only_id"};
    assert(claw_config_dispatch(4, a2, resp, sizeof(resp)) != 0);
    assert(g_set_creds_calls == 0);
    assert(strstr(resp, "usage") != NULL);

    /* 未知子命令 -> usage */
    char *a3[] = {"claw_config","frob"};
    assert(claw_config_dispatch(2, a3, resp, sizeof(resp)) != 0);
    assert(strstr(resp, "usage") != NULL);

    /* list -> 打码串来自 masked getter */
    char *a4[] = {"claw_config","list"};
    assert(claw_config_dispatch(2, a4, resp, sizeof(resp)) == 0);
    assert(strstr(resp, "app_secret=abc****") != NULL);

    /* set llm <base_url> <model> <api_key> -> 调 set_llm，入参透传 */
    reset_llm();
    char *a5[] = {"claw_config","set","llm","https://api.x","gpt-x","sk-xyz"};
    assert(claw_config_dispatch(6, a5, resp, sizeof(resp)) == 0);
    assert(g_set_llm_calls == 1);
    assert(strcmp(g_llm_url, "https://api.x") == 0);
    assert(strcmp(g_llm_model, "gpt-x") == 0);
    assert(strcmp(g_llm_key, "sk-xyz") == 0);

    /* set llm 缺参 -> usage，不调 set_llm */
    reset_llm();
    char *a6[] = {"claw_config","set","llm","only_url","only_model"};
    assert(claw_config_dispatch(5, a6, resp, sizeof(resp)) != 0);
    assert(g_set_llm_calls == 0);
    assert(strstr(resp, "usage") != NULL);

    /* list -> 也含 LLM 打码段 */
    char *a7[] = {"claw_config","list"};
    assert(claw_config_dispatch(2, a7, resp, sizeof(resp)) == 0);
    assert(strstr(resp, "api_key=abc****") != NULL);

    printf("cli_config feishu-only: all pass\n");
    return 0;
}
