/**
 * @file claw_config.c
 * @brief 读 claw/wukong.json → 解析容错 + 逐项校验回落 → 只读单例;缺键增量补齐回写。
 */
#include "claw_config.h"
#include "wukong_storage.h"
#include "ty_cJSON.h"
#include "uni_log.h"
#include <stdio.h>
#include <string.h>

#define CLAW_CFG_NS   "claw"
#define CLAW_CFG_NAME "wukong.json"

INT_T claw_config_clamp(INT_T v, INT_T lo, INT_T hi, INT_T dflt)
{
    return (v >= lo && v <= hi) ? v : dflt;
}

STATIC CLAW_CONFIG_T *s_cfg = NULL;

/* 懒分配填默认:get 可能早于 load 被调(memory_init 先读),不能返回 NULL。 */
STATIC OPERATE_RET __ensure_cfg(VOID_T)
{
    if (s_cfg == NULL) {
        s_cfg = (CLAW_CONFIG_T *)wukong_claw_malloc(sizeof(*s_cfg));
        if (s_cfg == NULL) {
            return OPRT_MALLOC_FAILED;
        }
        CLAW_CONFIG_T d = {
            CLAW_CFG_COMPACT_TRIGGER, CLAW_CFG_COMPACT_TRIGGER / CLAW_CFG_COMPACT_KEEP_RATIO,
            CLAW_CFG_SEND_TOKENS, CLAW_CFG_AGENT_MAX_ITER,
            CLAW_CFG_SESSION_MSG_CHARS, CLAW_CFG_SESSION_FILE_MAX, CLAW_CFG_MEM_MAX,
        };
        *s_cfg = d;
    }
    return OPRT_OK;
}

CONST CLAW_CONFIG_T *claw_config_get(VOID_T)
{
    (VOID_T)__ensure_cfg();
    return s_cfg;
}

/* obj[key] 为 number 时返回其值,否则 dflt。 */
STATIC INT_T __cfg_int(ty_cJSON *obj, CONST CHAR_T *key, INT_T dflt)
{
    ty_cJSON *it = obj ? ty_cJSON_GetObjectItem(obj, key) : NULL;
    return (it && ty_cJSON_IsNumber(it)) ? it->valueint : dflt;
}

/* root[name] 对象,缺则新建并挂上(标记需回写)。 */
STATIC ty_cJSON *__ensure_obj(ty_cJSON *root, CONST CHAR_T *name, BOOL_T *dirty)
{
    ty_cJSON *o = ty_cJSON_GetObjectItem(root, name);
    if (o == NULL) {
        o = ty_cJSON_CreateObject();
        ty_cJSON_AddItemToObject(root, name, o);
        *dirty = TRUE;
    }
    return o;
}

/* obj 缺 key 才补默认(已有键原值不动,标记需回写)。 */
STATIC VOID_T __merge_key(ty_cJSON *obj, CONST CHAR_T *key, INT_T dflt, BOOL_T *dirty)
{
    if (ty_cJSON_GetObjectItem(obj, key) == NULL) {
        ty_cJSON_AddNumberToObject(obj, key, dflt);
        *dirty = TRUE;
    }
}

OPERATE_RET claw_config_load(VOID_T)
{
    if (__ensure_cfg() != OPRT_OK) {
        return OPRT_MALLOC_FAILED;
    }
    CLAW_CONFIG_T tmp = {                 /* 默认起点 */
        CLAW_CFG_COMPACT_TRIGGER, CLAW_CFG_COMPACT_TRIGGER / CLAW_CFG_COMPACT_KEEP_RATIO,
        CLAW_CFG_SEND_TOKENS, CLAW_CFG_AGENT_MAX_ITER,
        CLAW_CFG_SESSION_MSG_CHARS, CLAW_CFG_SESSION_FILE_MAX, CLAW_CFG_MEM_MAX,
    };
    BYTE_T *buf = NULL; UINT_T len = 0;
    if (wukong_storage_read(CLAW_CFG_NS, CLAW_CFG_NAME, &buf, &len) == OPRT_OK
        && buf != NULL && len > 0) {
        ty_cJSON *root = ty_cJSON_Parse((CONST CHAR_T *)buf);
        if (root != NULL) {
            ty_cJSON *ses = ty_cJSON_GetObjectItem(root, "session");
            ty_cJSON *agt = ty_cJSON_GetObjectItem(root, "agent");
            INT_T trig  = __cfg_int(ses, "compact_trigger",   CLAW_CFG_COMPACT_TRIGGER);
            INT_T ratio = __cfg_int(ses, "compact_keep_ratio", CLAW_CFG_COMPACT_KEEP_RATIO);
            INT_T sendt = __cfg_int(ses, "send_tokens",       CLAW_CFG_SEND_TOKENS);
            INT_T iter  = __cfg_int(agt, "max_tool_iterations", CLAW_CFG_AGENT_MAX_ITER);
            tmp.compact_trigger = claw_config_clamp(trig,  10, 500,  CLAW_CFG_COMPACT_TRIGGER);
            ratio               = claw_config_clamp(ratio, 1,  10,   CLAW_CFG_COMPACT_KEEP_RATIO);
            tmp.compact_keep    = tmp.compact_trigger / ratio;   /* ratio>=1 已保证 */
            tmp.send_tokens     = claw_config_clamp(sendt, 256, 32000, CLAW_CFG_SEND_TOKENS);
            tmp.agent_max_iter  = claw_config_clamp(iter,  1,   50,   CLAW_CFG_AGENT_MAX_ITER);

            ty_cJSON *mem = ty_cJSON_GetObjectItem(root, "memory");
            INT_T mchars = __cfg_int(ses, "session_msg_chars", CLAW_CFG_SESSION_MSG_CHARS);
            INT_T fmax   = __cfg_int(ses, "session_file_max",  CLAW_CFG_SESSION_FILE_MAX);
            INT_T mmax   = __cfg_int(mem, "mem_max",           CLAW_CFG_MEM_MAX);
            tmp.session_msg_chars = claw_config_clamp(mchars, 256, 32768,   CLAW_CFG_SESSION_MSG_CHARS);
            tmp.session_file_max  = claw_config_clamp(fmax,   16384, 1048576, CLAW_CFG_SESSION_FILE_MAX);
            tmp.mem_max           = claw_config_clamp(mmax,   10, CLAW_FIXED_MEM_CEIL, CLAW_CFG_MEM_MAX);

            /* 增量合并迁移:旧文件缺新键则补默认回写,已有键原值保留;补齐后幂等不再回写。 */
            BOOL_T dirty = FALSE;
            ty_cJSON *ses_o = __ensure_obj(root, "session", &dirty);
            __merge_key(ses_o, "compact_trigger",    CLAW_CFG_COMPACT_TRIGGER,    &dirty);
            __merge_key(ses_o, "compact_keep_ratio", CLAW_CFG_COMPACT_KEEP_RATIO, &dirty);
            __merge_key(ses_o, "send_tokens",        CLAW_CFG_SEND_TOKENS,        &dirty);
            __merge_key(ses_o, "session_msg_chars",  CLAW_CFG_SESSION_MSG_CHARS,  &dirty);
            __merge_key(ses_o, "session_file_max",   CLAW_CFG_SESSION_FILE_MAX,   &dirty);
            __merge_key(__ensure_obj(root, "memory", &dirty), "mem_max", CLAW_CFG_MEM_MAX, &dirty);
            __merge_key(__ensure_obj(root, "agent", &dirty), "max_tool_iterations", CLAW_CFG_AGENT_MAX_ITER, &dirty);
            if (dirty) {
                CHAR_T *out = ty_cJSON_PrintUnformatted(root);
                if (out != NULL) {
                    if (wukong_storage_write(CLAW_CFG_NS, CLAW_CFG_NAME,
                                             (CONST BYTE_T *)out, (UINT_T)strlen(out)) == OPRT_OK) {
                        PR_NOTICE("claw_config: wukong.json migrated (missing keys added)");
                    } else {
                        PR_WARN("claw_config: wukong.json migrate write failed");
                    }
                    ty_cJSON_FreeBuffer(out);
                } else {
                    PR_WARN("claw_config: wukong.json migrate print failed");
                }
            }

            ty_cJSON_Delete(root);
        } else {
            PR_WARN("claw_config: wukong.json parse failed, use defaults");
        }
    } else {
        /* 文件不存在 → first-boot 写默认模板(用宏值拼,保证一致);之后用户 SD 卡编辑,claw 只读。
           坏 JSON 走上面分支、不覆盖用户文件。 */
        CHAR_T seed[384];
        int n = snprintf(seed, sizeof(seed),
            "{\n  \"version\": \"1.0\",\n"
            "  \"session\": { \"compact_trigger\": %d, \"compact_keep_ratio\": %d, \"send_tokens\": %d,"
            " \"session_msg_chars\": %d, \"session_file_max\": %d },\n"
            "  \"memory\": { \"mem_max\": %d },\n"
            "  \"agent\": { \"max_tool_iterations\": %d }\n}\n",
            CLAW_CFG_COMPACT_TRIGGER, CLAW_CFG_COMPACT_KEEP_RATIO, CLAW_CFG_SEND_TOKENS,
            CLAW_CFG_SESSION_MSG_CHARS, CLAW_CFG_SESSION_FILE_MAX, CLAW_CFG_MEM_MAX,
            CLAW_CFG_AGENT_MAX_ITER);
        if (n > 0 && wukong_storage_write(CLAW_CFG_NS, CLAW_CFG_NAME,
                                          (CONST BYTE_T *)seed, (UINT_T)n) == OPRT_OK) {
            PR_NOTICE("claw_config: wukong.json seeded with defaults");
        } else {
            PR_WARN("claw_config: wukong.json seed failed, use defaults");
        }
    }
    if (buf != NULL) wukong_storage_free(buf);
    *s_cfg = tmp;   /* 一次性替换单例(非逐字段,避免读交错) */
    return OPRT_OK;
}
