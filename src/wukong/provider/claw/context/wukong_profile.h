#pragma once
#include "tuya_cloud_types.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Agent self-profile sections. SOUL/USER read from fs (wukong_storage record 层,
 * SOUL.md/USER.md) with built-in English default + first-boot seed; IDENTITY is
 * a compile-time macro. Returned pointers are module-owned (cached) — do NOT free. */
CONST CHAR_T *wukong_profile_soul(VOID_T);      /* SOUL.md 人格:性格/语气/边界 */
CONST CHAR_T *wukong_profile_user(VOID_T);      /* USER.md 用户画像:偏好/称呼/习惯 */
CONST CHAR_T *wukong_profile_identity(VOID_T);  /* 设备身份(编译期宏,不走 fs) */

/* Drop the cached SOUL/USER so the next getter re-reads fs (call after a
 * write to claw/SOUL.md or claw/USER.md). */
/* Rewrite one profile file ("soul" | "user") with the full new content and
 * refresh the cache (write-then-commit: a failed flush keeps the old text). */
OPERATE_RET wukong_profile_update(CONST CHAR_T *target, CONST CHAR_T *content);

/* Restore one profile file ("soul" | "user") to the built-in default (deletes
 * the file; the next read re-seeds it). */
OPERATE_RET wukong_profile_reset(CONST CHAR_T *target);

VOID_T wukong_profile_reload(VOID_T);
#ifdef __cplusplus
}
#endif
