#pragma once
#include "tuya_cloud_types.h"
#ifdef __cplusplus
extern "C" {
#endif

#define WUKONG_SKILL_ID_MAX     32
#define WUKONG_SKILL_DESC_MAX   128
#define WUKONG_SKILL_MAX        16
#define WUKONG_SKILL_BODY_MAX   8192

/* Scan claw/skills/<id>/ directories (each holding a SKILL.md manifest) into
 * an in-memory {id, summary} catalog; a directory with no SKILL.md is
 * skipped. Safe to call when no fs/skills exist (catalog stays empty). Also
 * self-manages the skill member's lifecycle: on first call it registers the
 * read_skill tool and subscribes to storage.ready (both once — repeat calls
 * via wukong_skill_reload() only rescan) so a later-mounted volume triggers
 * its own rescan without a provider-side proxy. */
OPERATE_RET wukong_skill_init(VOID_T);
/* "- <id>: <summary>\n" x N (list body only, no header); NULL when empty.
 * Caller frees with tal_free. */
CHAR_T     *wukong_skill_build_summary(VOID_T);
/* Read claw/skills/<id>/SKILL.md body (frontmatter stripped). *out_text heap,
 * caller frees. OPRT_NOT_FOUND if missing, OPRT_INVALID_PARM on bad/unsafe id. */
OPERATE_RET wukong_skill_read(CONST CHAR_T *id, CHAR_T **out_text);
/* Drop the catalog and re-scan (after skills change). */
VOID_T      wukong_skill_reload(VOID_T);

#ifdef __cplusplus
}
#endif
