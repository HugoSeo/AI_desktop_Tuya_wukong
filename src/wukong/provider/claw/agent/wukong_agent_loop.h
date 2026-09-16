#pragma once
#include "wukong_ai_provider.h"
#ifdef __cplusplus
extern "C" {
#endif

/**
 * Multi-round agent turn: context_build -> provider->ops->llm_infer, looping
 * while the model returns tool_calls (execute via wukong_tool_exec, append the
 * assistant+tool messages to runtime, re-infer) up to claw_config_get()->agent_max_iter.
 * tool_call_count==0 -> take resp.text as the reply. out_text is heap; caller frees.
 */
OPERATE_RET wukong_agent_loop_run(CONST CHAR_T *chat_id, CONST CHAR_T *text,
                                  WUKONG_AI_PROVIDER_T *provider, CHAR_T **out_text);

/* Full flow: loop_run -> channel_output(reply) -> event notify. */
OPERATE_RET wukong_agent_process(CONST CHAR_T *chat_id, CONST CHAR_T *channel,
                                 CONST CHAR_T *text, WUKONG_AI_PROVIDER_T *provider);

#ifdef __cplusplus
}
#endif
