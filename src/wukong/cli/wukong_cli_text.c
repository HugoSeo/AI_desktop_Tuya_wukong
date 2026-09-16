/**
 * @file wukong_cli_text.c
 * @brief CLI text input command for automated testing.
 *
 * Registers BK CLI command "ai_text" to send text directly to the AI agent,
 * bypassing the voice (ASR) pipeline. Follows the same session pattern as
 * wukong_picture_input_recognize(): input_start → send_text → input_stop.
 *
 * Usage:  ai_text <text content>
 * Example: ai_text 今天天气怎么样
 */

#include <string.h>
#include "bk_cli.h"
#include "uni_log.h"
#include "tuya_ai_agent.h"
#include "wukong_ai_agent.h"

#define TAG "cli_text"

static void cli_ai_text_cmd(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv)
{
    if (argc < 2) {
        PR_ERR("Usage: ai_text <text>");
        return;
    }

    /* Concatenate argv[1..argc-1] into a single string (handles spaces) */
    char text[256] = {0};
    int offset = 0;
    for (int i = 1; i < argc && offset < (int)sizeof(text) - 1; i++) {
        if (i > 1 && offset < (int)sizeof(text) - 1) {
            text[offset++] = ' ';
        }
        int slen = strlen(argv[i]);
        if (offset + slen >= (int)sizeof(text)) {
            slen = (int)sizeof(text) - 1 - offset;
        }
        memcpy(text + offset, argv[i], slen);
        offset += slen;
    }
    text[offset] = '\0';

    tuya_ai_input_start(TRUE);
    OPERATE_RET rt = wukong_ai_agent_send_text(text);
    tuya_ai_input_stop();

    if (rt == OPRT_OK) {
        PR_NOTICE("ai_text: OK");
    } else {
        PR_ERR("ai_text: FAIL (%d)", rt);
    }
}

static const struct cli_command s_ai_text_cmds[] = {
    {"ai_text", "Send text to AI agent (replace voice input)", cli_ai_text_cmd},
};

int wukong_cli_text_init(void)
{
    return cli_register_commands(s_ai_text_cmds,
        sizeof(s_ai_text_cmds) / sizeof(s_ai_text_cmds[0]));
}
