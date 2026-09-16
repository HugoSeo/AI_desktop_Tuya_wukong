#pragma once
#include "wukong_ai_provider.h"
#include "ty_cJSON.h"
#ifdef __cplusplus
extern "C" {
#endif
/**
 * Assemble one LLM request (ContextBundle → WUKONG_LLM_REQ_T) by direct
 * composition. M2a sources: profile (SOUL/USER/IDENTITY), each injected as
 * its own section. runtime_messages = per-request tool history (M2a: pass
 * NULL). Caller frees with wukong_context_free().
 */
OPERATE_RET wukong_context_build(CONST CHAR_T *chat_id, CONST CHAR_T *user_text,
                                 ty_cJSON *runtime_messages, WUKONG_LLM_REQ_T *out);
VOID_T wukong_context_free(WUKONG_LLM_REQ_T *out);
#ifdef __cplusplus
}
#endif
