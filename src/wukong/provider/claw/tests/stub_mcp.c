/* MCP registry double: seeds one real tool into the actual registry (now
 * wukong_tool.c, linked alongside wukong_llm.c for this suite) so
 * wukong_tool_build_schema returns a non-empty tools[] for the request-body
 * assertions. The registry (register/list/build_schema) lives in wukong_tool.c
 * itself, so we seed it via the real WUKONG_TOOL_ADD instead of faking it here.
 * The tool is AGENT-visible so build_schema (agent snapshot) includes it. */
#include "wukong_tool.h"

STATIC OPERATE_RET __stub_play_music_handler(CONST CHAR_T *name, CONST ty_cJSON *arguments,
                                             ty_cJSON **out_content, VOID *user_data)
{
    (void)name; (void)arguments; (void)user_data;
    if (out_content) *out_content = NULL;
    return OPRT_OK;
}

__attribute__((constructor))
static void __stub_mcp_seed_registry(void)
{
    WUKONG_TOOL_ADD("play_music", "play a song", __stub_play_music_handler, NULL,
                    WUKONG_TOOL_AGENT);
}
