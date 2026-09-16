#include "wukong_profile.h"
#include "wukong_storage.h"
#include "tuya_iot_config.h"
#include "tuya_app_config.h"
#include "tal_memory.h"
#include "mix_method.h"   /* mm_strdup */
#include <string.h>

#define WK_STR(x)  #x
#define WK_XSTR(x) WK_STR(x)

/* 内置英文默认:既是 fs 无内容时的回退,也是首次 seed 源 */
STATIC CONST CHAR_T *DEFAULT_SOUL =
    "You are Wukong, a witty and playful AI assistant living on an embedded device.\n"
    "You address the user warmly as 主人 (or 小主), keeping things light and\n"
    "humorous while genuinely helpful.\n"
    "\n"
    "- Reply in the user's language.\n"
    "- Keep replies concise.\n"
    "- Use tools when needed. Before calling tools, say in ONE short sentence\n"
    "  what you are going to check and why — a real plan, not a greeting. Then\n"
    "  call the tools.\n"
    "- Only rewrite your persona (profile_update) when the user explicitly\n"
    "  asks for it.";

STATIC CONST CHAR_T *DEFAULT_USER =
    "- Name: (not set)\n"
    "- Language: Chinese / English\n"
    "- Preferences: (learn from conversation)";

/* IDENTITY 保持编译期宏拼装,不走 fs、不缓存;模型名来自 CLAW_LLM_MODEL 宏
 * (与 wukong_provider_claw.c 的 local.model 同源),让 agent 能答"当前跑什么模型" */
STATIC CONST CHAR_T *IDENTITY =
    "Device: Wukong AI toy on a Tuya " TARGET_PLATFORM " chip (SDK " IOT_SDK_VER "), "
    WK_XSTR(TUYA_LCD_WIDTH_VAL) "x" WK_XSTR(TUYA_LCD_HEIGHT_VAL) " touchscreen, "
    "camera, microphone and speaker, Wi-Fi.\n"
    "LLM model: " CLAW_LLM_MODEL ".";

/* 模块拥有的缓存 */
STATIC CHAR_T *s_soul = NULL;
STATIC CHAR_T *s_user = NULL;

/* Storage namespace: profile files live at <root>/tuyaos/claw/profile/<name>. */
#define PROFILE_NS  "claw/profile"

/* Load claw/profile/<name> into *cache: use fs content if present; otherwise seed the
 * default (only on NOT_FOUND) and use the default. *cache is never left NULL
 * except on mm_strdup OOM. */
STATIC VOID_T __load(CHAR_T **cache, CONST CHAR_T *name, CONST CHAR_T *deflt)
{
    BYTE_T *buf = NULL;
    UINT_T  len = 0;
    OPERATE_RET rt = wukong_storage_read(PROFILE_NS, name, &buf, &len);

    /* Use fs only when it holds non-empty content; an existing-but-empty file
     * (rt OK, len 0) counts as "no content" and falls back to the default. */
    BOOL_T from_fs = (rt == OPRT_OK && buf != NULL && buf[0] != '\0');

    /* Seed the default only when the file is missing, so it persists next boot. */
    if (!from_fs && rt == OPRT_NOT_FOUND) {
        wukong_storage_write(PROFILE_NS, name, (CONST BYTE_T *)deflt, (UINT_T)strlen(deflt));
    }
    *cache = mm_strdup(from_fs ? (CONST CHAR_T *)buf : deflt);
    wukong_storage_free(buf);
}

CONST CHAR_T *wukong_profile_soul(VOID_T)
{
    if (s_soul == NULL) {
        __load(&s_soul, "SOUL.md", DEFAULT_SOUL);
    }
    return s_soul;
}

CONST CHAR_T *wukong_profile_user(VOID_T)
{
    if (s_user == NULL) {
        __load(&s_user, "USER.md", DEFAULT_USER);
    }
    return s_user;
}

CONST CHAR_T *wukong_profile_identity(VOID_T) { return IDENTITY; }

/* Resolve a profile target to its file name, cache slot and default text. */
STATIC OPERATE_RET __resolve(CONST CHAR_T *target, CONST CHAR_T **name,
                             CHAR_T ***cache, CONST CHAR_T **deflt)
{
    if (target == NULL) {
        return OPRT_INVALID_PARM;
    }
    if (strcmp(target, "soul") == 0) {
        *name = "SOUL.md";
        *cache = &s_soul;
        *deflt = DEFAULT_SOUL;
        return OPRT_OK;
    }
    if (strcmp(target, "user") == 0) {
        *name = "USER.md";
        *cache = &s_user;
        *deflt = DEFAULT_USER;
        return OPRT_OK;
    }
    return OPRT_INVALID_PARM;
}

OPERATE_RET wukong_profile_update(CONST CHAR_T *target, CONST CHAR_T *content)
{
    CONST CHAR_T *name = NULL;
    CHAR_T **cache = NULL;
    CONST CHAR_T *deflt = NULL;

    if (content == NULL || content[0] == '\0') {
        return OPRT_INVALID_PARM;
    }
    OPERATE_RET rt = __resolve(target, &name, &cache, &deflt);
    if (rt != OPRT_OK) {
        return rt;
    }
    /* Disk first; refresh the cache only after the write lands, so a failed
     * flush keeps serving the old text (no false "updated"). */
    rt = wukong_storage_write(PROFILE_NS, name, (CONST BYTE_T *)content, (UINT_T)strlen(content));
    if (rt != OPRT_OK) {
        return rt;
    }
    CHAR_T *dup = mm_strdup(content);
    if (dup == NULL) {
        return OPRT_MALLOC_FAILED;   /* disk is updated; next reload picks it up */
    }
    tal_free(*cache);
    *cache = dup;
    return OPRT_OK;
}

OPERATE_RET wukong_profile_reset(CONST CHAR_T *target)
{
    CONST CHAR_T *name = NULL;
    CHAR_T **cache = NULL;
    CONST CHAR_T *deflt = NULL;

    OPERATE_RET rt = __resolve(target, &name, &cache, &deflt);
    if (rt != OPRT_OK) {
        return rt;
    }
    (VOID_T)wukong_storage_delete(PROFILE_NS, name);
    tal_free(*cache);
    *cache = NULL;   /* next getter re-seeds the built-in default */
    return OPRT_OK;
}

VOID_T wukong_profile_reload(VOID_T)
{
    tal_free(s_soul); s_soul = NULL;
    tal_free(s_user); s_user = NULL;
}
