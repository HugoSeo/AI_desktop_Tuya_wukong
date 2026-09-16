#ifndef __TY_AVI_PLAYLIST_H__
#define __TY_AVI_PLAYLIST_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TY_AVI_PLAYLIST_MAX_FILES   32
#define TY_AVI_PLAYLIST_PATH_MAX    192

/** Replace the in-memory list with absolute paths supplied by the app FS. */
OPERATE_RET ty_avi_playlist_replace(const char *const paths[], uint16_t count);
uint16_t ty_avi_playlist_count(void);
const char *ty_avi_playlist_get(uint16_t index);
INT16_T ty_avi_playlist_current_index(void);
void ty_avi_playlist_dump(void);

#ifdef __cplusplus
}
#endif

#endif
