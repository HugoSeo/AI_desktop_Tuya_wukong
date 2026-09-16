/* toolkits/wukong_toolkits.c */
#include "wukong_toolkits.h"
#include "tal_log.h"

OPERATE_RET wukong_tools_init(VOID_T);   /* tools/tool_init.c */
OPERATE_RET wukong_mcp_init(VOID_T);     /* mcp/ */
OPERATE_RET wukong_fc_init(VOID_T);      /* fc/ */
OPERATE_RET wukong_skill_init(VOID_T);   /* skill/ */

OPERATE_RET wukong_toolkits_init(VOID_T)
{
    OPERATE_RET rt = OPRT_OK;
    TUYA_CALL_ERR_RETURN(wukong_tools_init());   /* 必须第一: 工具先就位 */
    TUYA_CALL_ERR_LOG(wukong_mcp_init());
    TUYA_CALL_ERR_LOG(wukong_fc_init());
    TUYA_CALL_ERR_LOG(wukong_skill_init());
    return rt;
}
