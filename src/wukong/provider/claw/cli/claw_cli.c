/**
 * @file claw_cli.c
 * @brief claw CLI 注册入口:把 claw_config 命令注册进 bk_cli。平台相关(bk_cli)集中在此。
 */
#include "claw_cli.h"
#include "tuya_app_config.h"

#if defined(TUYA_MODULE_T5) && (TUYA_MODULE_T5 == 1)
#include "bk_cli.h"

extern void cli_claw_config_cmd(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv);

static const struct cli_command s_claw_cmds[] = {
    {"claw_config", "Set/list claw runtime creds (feishu)", cli_claw_config_cmd},
};

int claw_cli_init(void)
{
    return cli_register_commands(s_claw_cmds, sizeof(s_claw_cmds) / sizeof(s_claw_cmds[0]));
}
#else
int claw_cli_init(void)
{
    return 0;
}
#endif
