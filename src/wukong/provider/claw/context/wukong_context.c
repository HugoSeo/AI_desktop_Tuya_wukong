#include "wukong_context.h"
#include "wukong_profile.h"
#include "wukong_session.h"
#include "wukong_skill.h"
#include "wukong_memory.h"
#include "ty_cJSON.h"
#include "tal_memory.h"
#include "mix_method.h"   /* mm_strdup */
#include <string.h>
#include <stdio.h>

/* Internal builders: __build_system_prompt / __build_history_messages / __build_tools_json */

/*
 * Append a "## <title>\n<body>\n\n" section onto *dst (grow via malloc+copy,
 * free the old buffer). Skips (no-op, OPRT_OK) if body is NULL/empty. On
 * allocation failure, frees *dst and sets it to NULL so callers can detect
 * failure by testing the pointer; already-NULL *dst short-circuits so a
 * prior failure propagates through subsequent calls instead of silently
 * "healing" into a partial prompt.
 */
STATIC OPERATE_RET __append_section(CHAR_T **dst, CONST CHAR_T *title, CONST CHAR_T *body)
{
    if (!dst) return OPRT_INVALID_PARM;
    if (!*dst) return OPRT_MALLOC_FAILED;    /* propagate prior failure */
    if (!body || !*body) return OPRT_OK;     /* nothing to append */

    INT_T old_len = (INT_T)strlen(*dst);
    INT_T add_len = snprintf(NULL, 0, "## %s\n%s\n\n", title, body);
    if (add_len < 0) return OPRT_COM_ERROR;

    CHAR_T *buf = (CHAR_T *)tal_malloc((SIZE_T)(old_len + add_len + 1));
    if (!buf) {
        tal_free(*dst);
        *dst = NULL;
        return OPRT_MALLOC_FAILED;
    }

    memcpy(buf, *dst, (SIZE_T)old_len);
    snprintf(buf + old_len, (SIZE_T)(add_len + 1), "## %s\n%s\n\n", title, body);

    tal_free(*dst);
    *dst = buf;
    return OPRT_OK;
}

STATIC CHAR_T *__build_system_prompt(CONST CHAR_T *chat_id)
{
    /* Profile sections: SOUL/USER from fs (claw ns) with built-in default;
     * IDENTITY from compile-time macro. Future: append memory/skill sections. */
    CHAR_T *s = mm_strdup("");
    __append_section(&s, "Persona", wukong_profile_soul());
    __append_section(&s, "User", wukong_profile_user());
    __append_section(&s, "Device", wukong_profile_identity());
    /* Available skills catalog (fs .md skills); NULL/empty -> section skipped. */
    {
        CHAR_T *skills = wukong_skill_build_summary();
        __append_section(&s, "Available skills", skills);
        tal_free(skills);
    }
    /* Long-term memory index (keyword -> ids); NULL/empty -> section skipped. */
    {
        CHAR_T *mem = wukong_memory_build_index();
        __append_section(&s, "Memory", mem);
        tal_free(mem);
    }
    /* Rolling summary of compacted early history (note + text composed by
     * wukong_compact.c); heap string, must free. */
    {
        CHAR_T *body = wukong_session_build_summary(chat_id);
        __append_section(&s, "Conversation summary", body);
        if (body) tal_free(body);
    }
    return s;   /* NULL on alloc failure */
}

STATIC ty_cJSON *__build_history_messages(CONST CHAR_T *chat_id)
{
    return wukong_session_get_messages(chat_id);
}

STATIC CHAR_T *__build_tools_json(VOID_T)
{
    return NULL;   /* M2a: no tools */
}

/* deep-copy each item of src into dst (caller keeps src ownership) */
STATIC VOID_T __append_msg_dup(ty_cJSON *dst, ty_cJSON *src)
{
    if (!dst || !src) return;
    INT_T n = ty_cJSON_GetArraySize(src);
    for (INT_T i = 0; i < n; i++) {
        ty_cJSON *dup = ty_cJSON_Duplicate(ty_cJSON_GetArrayItem(src, i), 1);
        if (dup) ty_cJSON_AddItemToArray(dst, dup);
    }
}

OPERATE_RET wukong_context_build(CONST CHAR_T *chat_id, CONST CHAR_T *user_text,
                                 ty_cJSON *runtime_messages, WUKONG_LLM_REQ_T *out)
{
    (VOID_T)user_text;   /* current user turn arrives via session history */
    if (!out) return OPRT_INVALID_PARM;

    out->system_prompt = NULL;
    out->messages = NULL;
    out->tools_json = NULL;

    out->system_prompt = __build_system_prompt(chat_id);
    out->messages = ty_cJSON_CreateArray();
    if (!out->system_prompt || !out->messages) {
        wukong_context_free(out);
        return OPRT_MALLOC_FAILED;
    }

    ty_cJSON *hist = __build_history_messages(chat_id);   /* prior turns, may be NULL */
    if (hist) { __append_msg_dup(out->messages, hist); ty_cJSON_Delete(hist); }


    if (runtime_messages) __append_msg_dup(out->messages, runtime_messages);  /* M2b tool history */

    out->tools_json = __build_tools_json();   /* M2a NULL */
    return OPRT_OK;
}

VOID_T wukong_context_free(WUKONG_LLM_REQ_T *out)
{
    if (!out) return;
    if (out->system_prompt) { tal_free((VOID_T *)out->system_prompt); out->system_prompt = NULL; }
    if (out->messages) { ty_cJSON_Delete(out->messages); out->messages = NULL; }
    if (out->tools_json) { tal_free((VOID_T *)out->tools_json); out->tools_json = NULL; }
}
