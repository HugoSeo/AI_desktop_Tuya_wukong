/**
 * @file wukong_llm.h
 * @brief Claw LLM runtime — instantiable per-environment LLM chat over HTTP.
 */
#pragma once
#include "wukong_ai_provider.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct wukong_llm_runtime WUKONG_LLM_RT_T;

OPERATE_RET wukong_llm_runtime_create(CONST WUKONG_AI_PROVIDER_CFG_T *cfg,
                                      WUKONG_LLM_RT_T **out_rt);
/* On failure, *out_error may be set to a heap-allocated string that the caller
 * must release with tal_free. */
OPERATE_RET wukong_llm_runtime_chat(WUKONG_LLM_RT_T *rt,
                                    CONST WUKONG_LLM_REQ_T *req,
                                    WUKONG_LLM_RESP_T *resp,
                                    CHAR_T **out_error);
VOID_T wukong_llm_runtime_destroy(WUKONG_LLM_RT_T *rt);
VOID_T wukong_llm_resp_free(WUKONG_LLM_RESP_T *resp);

#ifdef __cplusplus
}
#endif
