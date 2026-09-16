/* No-op double for skill_tool.h: this suite (test_wukong_skill.c) exercises
 * the catalog (init/build_summary/read/reload/id-safety), not tool
 * registration — that is covered by toolkits/tools/tests (mcp_skill).
 * wukong_skill_init() still calls skill_tool_init() once, so it needs a
 * link-time stand-in here. */
#include "skill_tool.h"

OPERATE_RET skill_tool_init(VOID)
{
    return OPRT_OK;
}
