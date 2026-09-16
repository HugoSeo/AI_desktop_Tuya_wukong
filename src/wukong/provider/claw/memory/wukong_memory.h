#pragma once
#include "tuya_cloud_types.h"
#include "ty_cJSON.h"
#include "claw_config.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Load indexes.json + memories.json into the in-memory cache (empty on absence). */
OPERATE_RET  wukong_memory_init(VOID_T);
/* Injection string: "The user's long-term memory (keyword → ids).\n<kw: ids>\n"...;
 * NULL when empty. Caller frees with tal_free. */
CHAR_T      *wukong_memory_build_index(VOID_T);
/* CRUD (Task 2) — out_json heap JSON string, caller frees. */
/* List the memory index as {"success":true,"count":N,
 * "memories":[{"id","tags":[..]},...]} — ids and tags only, content stays
 * behind memory_get so the tool result (which enters the persisted session
 * history) stays small. Browsing does not bump access counts. */
OPERATE_RET  wukong_memory_list(CHAR_T **out_json);
OPERATE_RET  wukong_memory_get(CONST CHAR_T *CONST *ids, UINT_T n, CHAR_T **out_json);
OPERATE_RET  wukong_memory_save(CONST CHAR_T *content, CONST CHAR_T *CONST *tags,
                                UINT_T ntag, INT_T importance, CHAR_T **out_id);
OPERATE_RET  wukong_memory_update(CONST CHAR_T *id, CONST ty_cJSON *updates);
OPERATE_RET  wukong_memory_delete(CONST CHAR_T *id);
VOID_T       wukong_memory_reload(VOID_T);

#ifdef __cplusplus
}
#endif
