#ifndef __UI_SVC_MUSIC_H__
#define __UI_SVC_MUSIC_H__

#include <stdint.h>
#include "tuya_cloud_types.h"
#include "ty_cJSON.h"
#include "svc_ai_player.h"   /* AI_PLAYER_STATE_T */

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI_MUSIC_MODE_SEQUENCE = 0,
    UI_MUSIC_MODE_LIST_LOOP,
    UI_MUSIC_MODE_SINGLE_LOOP,
    UI_MUSIC_MODE_SHUFFLE,
    UI_MUSIC_MODE_MAX
} ui_music_mode_t;

typedef struct {
    AI_PLAYER_STATE_T state;        /* STOPPED / PLAYING / PAUSED */
    char song_name[128];
    char artist[128];
} ui_music_status_t;

/* Page-registered callback; invoked on the UI thread when status/data changes. */
typedef void (*ui_svc_music_cb_t)(const ui_music_status_t *st);

/* Async playlist delivery; invoked on the UI thread. Receiver takes ownership
 * of @list (must ty_cJSON_Delete it); NULL when the fetch failed / list empty. */
typedef void (*ui_svc_music_list_cb_t)(ty_cJSON *list);

void ui_svc_music_init(void);                  /* subscribe events; set default mode */
void ui_svc_music_set_cb(ui_svc_music_cb_t cb);/* NULL to unregister */
void ui_svc_music_get_status(ui_music_status_t *out);

void ui_svc_music_play_pause(void);
void ui_svc_music_next(void);
void ui_svc_music_prev(void);

/* Auto-play on page open: PLAYING → no-op; PAUSED → resume; STOPPED → resume
 * current / play first playlist item; empty playlist → cloud default browse.
 * Fully non-blocking; results arrive via the status callback. */
void ui_svc_music_autoplay(void);

/* Stop music playback (BG player only; leaves TTS/FG untouched). Used when
 * navigating away from the music pages. Status updates to STOPPED via events. */
void ui_svc_music_stop(void);

OPERATE_RET ui_svc_music_list(ty_cJSON **out); /* caller must ty_cJSON_Delete(*out) */

/* Fetch the playlist on WORKQ_SYSTEM and deliver it to @cb on the UI thread.
 * The playlist fetch takes the playback mutex, which the backend worker can
 * hold for seconds during a URL refresh — never call the sync variant from the
 * UI thread on a hot path. Single slot: the latest @cb wins. */
void ui_svc_music_list_async(ui_svc_music_list_cb_t cb);
void        ui_svc_music_play_id(int id);
void        ui_svc_music_remove_id(int id);
int         ui_svc_music_current_id(void);

ui_music_mode_t ui_svc_music_mode_get(void);
void            ui_svc_music_mode_set(ui_music_mode_t mode);

/* 0-100; returns -1 when length is unknown (e.g. live stream) or no player. */
int ui_svc_music_progress_percent(void);

#ifdef __cplusplus
}
#endif

#endif /* __UI_SVC_MUSIC_H__ */
