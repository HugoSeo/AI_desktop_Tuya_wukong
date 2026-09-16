/**
 * @file ty_avi_playlist.c
 * @brief In-memory AVI playlist populated by the application filesystem.
 *
 * Directory enumeration, mount ownership and file validation deliberately do
 * not live here. Wukong's ui_svc_fs supplies the absolute paths.
 */

#include "ty_avi_playlist.h"

#include "uni_log.h"

#include <string.h>

#define TAG "ty_avi_playlist"

typedef struct {
    char paths[TY_AVI_PLAYLIST_MAX_FILES][TY_AVI_PLAYLIST_PATH_MAX];
    uint16_t count;
    INT16_T current_idx;
} ty_avi_playlist_ctx_t;

static ty_avi_playlist_ctx_t s_pl = { .current_idx = -1 };

OPERATE_RET ty_avi_playlist_replace(const char *const paths[], uint16_t count)
{
    if (count > TY_AVI_PLAYLIST_MAX_FILES || (count > 0 && !paths)) {
        return OPRT_INVALID_PARM;
    }

    memset(&s_pl, 0, sizeof(s_pl));
    s_pl.current_idx = -1;
    for (uint16_t i = 0; i < count; i++) {
        if (!paths[i] || paths[i][0] != '/' ||
            strlen(paths[i]) >= TY_AVI_PLAYLIST_PATH_MAX) {
            memset(&s_pl, 0, sizeof(s_pl));
            s_pl.current_idx = -1;
            return OPRT_INVALID_PARM;
        }
        strncpy(s_pl.paths[i], paths[i], TY_AVI_PLAYLIST_PATH_MAX - 1);
        s_pl.count++;
    }
    return OPRT_OK;
}

uint16_t ty_avi_playlist_count(void)
{
    return s_pl.count;
}

const char *ty_avi_playlist_get(uint16_t index)
{
    return index < s_pl.count ? s_pl.paths[index] : NULL;
}

INT16_T ty_avi_playlist_current_index(void)
{
    return s_pl.current_idx;
}

void ty_avi_playlist_dump(void)
{
    for (uint16_t i = 0; i < s_pl.count; i++) {
        PR_NOTICE("AVI playlist [%u] %s%s", i, s_pl.paths[i],
                  s_pl.current_idx == (INT16_T)i ? " *" : "");
    }
}
