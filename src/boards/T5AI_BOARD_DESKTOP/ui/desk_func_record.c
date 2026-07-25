/**
 * @file desk_func_record.c
 * @brief Record screen UI with three-state management (default / recording / paused)
 * @version 1.0
 * @date 2025-03-18
 * @copyright Copyright (c) Tuya Inc.
 */
#include "desk_event_handle.h"
#include "desk_handle_ui.h"
#include "wukong_ai_agent.h"
#include "ty_cJSON.h"
#include "tal_time_service.h"
#include "wav_encode.h"
#include "svc_ai_player.h"

/* ---------------------------------------------------------------------------
 * Macros
 * --------------------------------------------------------------------------- */
#define RECORD_PCM_SAMPLE_RATE      16000
#define RECORD_PCM_CHANNELS         1
#define RECORD_PCM_BIT_DEPTH        16
#define RECORD_UPLOAD_READ_SIZE     (6 * 1024)

/* ---------------------------------------------------------------------------
 * File scope variables
 * --------------------------------------------------------------------------- */
static record_scr_res_t s_record_res = {0};
static RECORD_STATE_E   s_record_state = RECORD_STATE_DEFAULT;
static lv_timer_t      *s_record_timer = NULL;
static uint32_t         s_elapsed_seconds = 0;
static AI_DEVICE_MODE_E s_ai_mode_before_record;

static RECORD_AUDIO_LIST_HEAD_T s_record_list = {0};
static BOOL_T s_record_info_loaded = FALSE;
static TUYA_FILE s_record_fp = NULL;
static UINT64_T s_record_file_size = 0;
static char s_record_cur_name[64] = {0};
static UINT16_T s_record_fallback_idx = 0;
static AI_AUDIO_CODEC_TYPE s_record_codec_type = AUDIO_CODEC_PCM;

typedef struct {
    TUYA_FILE   fp;
    UINT8_T    *read_buf;
    UINT64_T    file_len;
    UINT64_T    total_sent;
} __RECORD_UPLOAD_CTX_T;

static lv_timer_t          *s_upload_timer = NULL;
static __RECORD_UPLOAD_CTX_T s_upload_ctx  = {0};

static int __record_info_list_create(void);
static void __record_info_json_write(void);
static void __record_file_close_and_save(void);
static void __record_list_create(void);
static void __record_play_destroy(lv_record_ui_t *ui);
static void __record_play_ui_create(void);
static void __record_play_content_refresh(void);
static void __record_play_toggle_event(lv_event_t *e);
static void __record_play_rewind_event(lv_event_t *e);
static void __record_play_forward_event(lv_event_t *e);
static void __record_play_back_event(lv_event_t *e);
static void __record_play_delete_event(lv_event_t *e);
static void __record_play_start_playback(void);
static void __record_play_stop_playback(void);
static void __record_play_timer_cb(lv_timer_t *timer);
static void __record_upload_timer_cb(lv_timer_t *timer);
static void __record_upload_stop(void);

static uint32_t s_record_list_ui_generation = 0;
static int      s_play_record_id = -1;
static char     s_play_record_name[64] = {0};
static UINT32_T s_play_duration = 0;
static POSIX_TM_S s_play_create_time = {0};

static lv_timer_t *s_play_timer = NULL;
static BOOL_T      s_play_is_playing = FALSE;
static UINT32_T    s_play_elapsed_ms = 0;
static UINT32_T    s_play_resume_tick = 0;

/* ---------------------------------------------------------------------------
 * Record list and file management
 * --------------------------------------------------------------------------- */
/**
 * @brief Clear all items in record list (must be called with lock held)
 * @return none
 */
static void __record_info_list_clear_locked(void)
{
    LIST_HEAD *pos = NULL;
    LIST_HEAD *next = NULL;

    tuya_list_for_each_safe(pos, next, &s_record_list.list_head)
    {
        RECORD_AUDIO_LIST_T *rec = tuya_list_entry(pos, RECORD_AUDIO_LIST_T, list_node);
        if (rec == NULL) {
            continue;
        }
        tuya_list_del(&rec->list_node);
        tal_free(rec);
    }
    s_record_list.num = 0;
}

/**
 * @brief Read record list from JSON file
 * @return none
 */
static void __record_info_json_read(void)
{
    TUYA_FILE fp = NULL;
    ty_cJSON *root = NULL;
    ty_cJSON *rec_list = NULL;
    char *json_buf = NULL;
    int file_size = 0;
    int i = 0;

    if (s_record_list.inited == FALSE) {
        return;
    }

    tal_mutex_lock(s_record_list.mutex);
    if (s_record_info_loaded == TRUE) {
        tal_mutex_unlock(s_record_list.mutex);
        return;
    }
    s_record_info_loaded = TRUE;
    tal_mutex_unlock(s_record_list.mutex);

    if (tkl_faccess(RECORD_INFO_SAVE_PATH, 0) != 0) {
        return;
    }

    file_size = tkl_fgetsize(RECORD_INFO_SAVE_PATH);
    if (file_size <= 0) {
        TAL_PR_WARN("[desk_record] invalid json file size: %d", file_size);
        return;
    }

    json_buf = (char *)tal_malloc(file_size + 1);
    if (json_buf == NULL) {
        TAL_PR_ERR("[desk_record] malloc json buffer failed");
        return;
    }

    memset(json_buf, 0, file_size + 1);
    fp = tkl_fopen(RECORD_INFO_SAVE_PATH, "rb");
    if (fp == NULL) {
        TAL_PR_ERR("[desk_record] open json failed: %s", RECORD_INFO_SAVE_PATH);
        goto __exit;
    }

    if (tkl_fread(json_buf, file_size, fp) != file_size) {
        TAL_PR_ERR("[desk_record] read json failed: %s", RECORD_INFO_SAVE_PATH);
        goto __exit;
    }

    root = ty_cJSON_Parse(json_buf);
    if ((root == NULL) || (ty_cJSON_IsObject(root) == FALSE)) {
        TAL_PR_ERR("[desk_record] parse json failed");
        goto __exit;
    }

    rec_list = ty_cJSON_GetObjectItem(root, "list");
    if ((rec_list == NULL) || (ty_cJSON_IsArray(rec_list) == FALSE)) {
        TAL_PR_WARN("[desk_record] invalid record list json");
        goto __exit;
    }

    tal_mutex_lock(s_record_list.mutex);
    __record_info_list_clear_locked();

    for (i = 0; i < ty_cJSON_GetArraySize(rec_list) && i < RECORD_INFO_ITEM_NUM; i++) {
        ty_cJSON *item_json = ty_cJSON_GetArrayItem(rec_list, i);
        ty_cJSON *id_json = NULL;
        ty_cJSON *name_json = NULL;
        ty_cJSON *len_json = NULL;
        ty_cJSON *duration_json = NULL;
        ty_cJSON *year_json = NULL;
        ty_cJSON *mon_json = NULL;
        ty_cJSON *mday_json = NULL;
        ty_cJSON *hour_json = NULL;
        ty_cJSON *min_json = NULL;
        ty_cJSON *sec_json = NULL;
        RECORD_AUDIO_LIST_T *item = NULL;

        if ((item_json == NULL) || (ty_cJSON_IsObject(item_json) == FALSE)) {
            continue;
        }

        item = (RECORD_AUDIO_LIST_T *)tal_malloc(sizeof(RECORD_AUDIO_LIST_T));
        if (item == NULL) {
            TAL_PR_ERR("[desk_record] malloc record item failed");
            break;
        }

        memset(item, 0, sizeof(RECORD_AUDIO_LIST_T));
        id_json       = ty_cJSON_GetObjectItem(item_json, "id");
        name_json     = ty_cJSON_GetObjectItem(item_json, "name");
        len_json      = ty_cJSON_GetObjectItem(item_json, "len");
        duration_json = ty_cJSON_GetObjectItem(item_json, "duration");
        year_json     = ty_cJSON_GetObjectItem(item_json, "year");
        mon_json  = ty_cJSON_GetObjectItem(item_json, "mon");
        mday_json = ty_cJSON_GetObjectItem(item_json, "mday");
        hour_json = ty_cJSON_GetObjectItem(item_json, "hour");
        min_json  = ty_cJSON_GetObjectItem(item_json, "min");
        sec_json  = ty_cJSON_GetObjectItem(item_json, "sec");

        item->id = (id_json != NULL && ty_cJSON_IsNumber(id_json)) ? id_json->valueint : i;
        if (name_json != NULL && ty_cJSON_GetStringValue(name_json) != NULL) {
            snprintf(item->name, sizeof(item->name), "%s", ty_cJSON_GetStringValue(name_json));
        }
        item->len = (len_json != NULL && ty_cJSON_IsNumber(len_json)) ? (UINT64_T)len_json->valuedouble : 0;
        item->duration = (duration_json != NULL && ty_cJSON_IsNumber(duration_json)) ? (UINT32_T)duration_json->valueint : 0;
        item->create_time.tm_year = (year_json != NULL && ty_cJSON_IsNumber(year_json)) ? year_json->valueint : 0;
        item->create_time.tm_mon  = (mon_json  != NULL && ty_cJSON_IsNumber(mon_json))  ? mon_json->valueint  : 0;
        item->create_time.tm_mday = (mday_json != NULL && ty_cJSON_IsNumber(mday_json)) ? mday_json->valueint : 0;
        item->create_time.tm_hour = (hour_json != NULL && ty_cJSON_IsNumber(hour_json)) ? hour_json->valueint : 0;
        item->create_time.tm_min  = (min_json  != NULL && ty_cJSON_IsNumber(min_json))  ? min_json->valueint  : 0;
        item->create_time.tm_sec  = (sec_json  != NULL && ty_cJSON_IsNumber(sec_json))  ? sec_json->valueint  : 0;

        tuya_list_add_tail(&item->list_node, &s_record_list.list_head);
        s_record_list.num++;
    }
    tal_mutex_unlock(s_record_list.mutex);

__exit:
    if (fp != NULL) {
        tkl_fclose(fp);
    }
    if (root != NULL) {
        ty_cJSON_Delete(root);
    }
    if (json_buf != NULL) {
        tal_free(json_buf);
    }
}

/**
 * @brief Write record list to JSON file
 * @return none
 */
static void __record_info_json_write(void)
{
    ty_cJSON *root = NULL;
    ty_cJSON *rec_list = NULL;
    LIST_HEAD *pos = NULL;
    char *json_str = NULL;
    TUYA_FILE fp = NULL;
    int write_size = 0;

    if (s_record_list.inited == FALSE) {
        return;
    }

    root = ty_cJSON_CreateObject();
    if (root == NULL) {
        TAL_PR_ERR("[desk_record] create root json failed");
        return;
    }

    rec_list = ty_cJSON_CreateArray();
    if (rec_list == NULL) {
        TAL_PR_ERR("[desk_record] create list json failed");
        goto __exit;
    }

    tal_mutex_lock(s_record_list.mutex);
    ty_cJSON_AddNumberToObject(root, "num", s_record_list.num);
    ty_cJSON_AddItemToObject(root, "list", rec_list);
    rec_list = NULL;

    tuya_list_for_each(pos, &s_record_list.list_head)
    {
        RECORD_AUDIO_LIST_T *rec = tuya_list_entry(pos, RECORD_AUDIO_LIST_T, list_node);
        ty_cJSON *item_json = NULL;

        if (rec == NULL) {
            continue;
        }

        item_json = ty_cJSON_CreateObject();
        if (item_json == NULL) {
            tal_mutex_unlock(s_record_list.mutex);
            TAL_PR_ERR("[desk_record] create item json failed");
            goto __exit;
        }

        ty_cJSON_AddNumberToObject(item_json, "id", rec->id);
        ty_cJSON_AddStringToObject(item_json, "name", rec->name);
        ty_cJSON_AddNumberToObject(item_json, "len", (double)rec->len);
        ty_cJSON_AddNumberToObject(item_json, "duration", (double)rec->duration);
        ty_cJSON_AddNumberToObject(item_json, "year", rec->create_time.tm_year);
        ty_cJSON_AddNumberToObject(item_json, "mon", rec->create_time.tm_mon);
        ty_cJSON_AddNumberToObject(item_json, "mday", rec->create_time.tm_mday);
        ty_cJSON_AddNumberToObject(item_json, "hour", rec->create_time.tm_hour);
        ty_cJSON_AddNumberToObject(item_json, "min", rec->create_time.tm_min);
        ty_cJSON_AddNumberToObject(item_json, "sec", rec->create_time.tm_sec);
        ty_cJSON_AddItemToArray(ty_cJSON_GetObjectItem(root, "list"), item_json);
    }
    tal_mutex_unlock(s_record_list.mutex);

    json_str = ty_cJSON_PrintUnformatted(root);
    if (json_str == NULL) {
        TAL_PR_ERR("[desk_record] print json failed");
        goto __exit;
    }

    tkl_fs_mkdir(RECORD_STORE_DIR);

    fp = tkl_fopen(RECORD_INFO_SAVE_PATH, "wb");
    if (fp == NULL) {
        TAL_PR_ERR("[desk_record] open json write failed: %s", RECORD_INFO_SAVE_PATH);
        goto __exit;
    }

    write_size = strlen(json_str);
    if (tkl_fwrite(json_str, write_size, fp) != write_size) {
        TAL_PR_ERR("[desk_record] write json failed: %s", RECORD_INFO_SAVE_PATH);
    }

__exit:
    if (fp != NULL) {
        tkl_fclose(fp);
    }
    if (json_str != NULL) {
        ty_cJSON_FreeBuffer(json_str);
    }
    if (rec_list != NULL) {
        ty_cJSON_Delete(rec_list);
    }
    if (root != NULL) {
        ty_cJSON_Delete(root);
    }
}

/**
 * @brief Initialize record info list (create + load from JSON)
 * @return OPRT_OK on success
 */
static int __record_info_list_create(void)
{
    if (s_record_list.inited) {
        return OPRT_OK;
    }

    OPERATE_RET rt = OPRT_OK;
    INIT_LIST_HEAD(&s_record_list.list_head);
    s_record_list.num = 0;
    TUYA_CALL_ERR_RETURN(tal_mutex_create_init(&s_record_list.mutex));
    s_record_list.inited = TRUE;
    __record_info_json_read();
    return OPRT_OK;
}

/**
 * @brief Allocate next free ID for a record item (must be called with lock held)
 * @return Free ID (0 ~ RECORD_INFO_ITEM_NUM-1), or -1 if full
 */
static int __record_info_list_alloc_id_locked(void)
{
    LIST_HEAD *pos = NULL;
    BOOL_T used[RECORD_INFO_ITEM_NUM] = {FALSE};
    int id = 0;

    tuya_list_for_each(pos, &s_record_list.list_head)
    {
        RECORD_AUDIO_LIST_T *rec = tuya_list_entry(pos, RECORD_AUDIO_LIST_T, list_node);
        if (rec == NULL) {
            continue;
        }
        if ((rec->id >= 0) && (rec->id < RECORD_INFO_ITEM_NUM)) {
            used[rec->id] = TRUE;
        }
    }

    for (id = 0; id < RECORD_INFO_ITEM_NUM; id++) {
        if (used[id] == FALSE) {
            return id;
        }
    }

    return -1;
}

/**
 * @brief Generate a recording filename based on local time and codec type
 * @param[out] name Output buffer for the filename
 * @param[in] name_size Size of the output buffer
 * @param[in] codec_type Audio codec type (AUDIO_CODEC_PCM -> .wav, AUDIO_CODEC_OPUS -> .opus)
 * @return none
 */
static void __record_generate_filename(char *name, size_t name_size, AI_AUDIO_CODEC_TYPE codec_type)
{
    POSIX_TM_S tm;
    OPERATE_RET rt;
    CONST char *ext = (codec_type == AUDIO_CODEC_OPUS) ? "opus" : "wav";

    memset(&tm, 0, sizeof(POSIX_TM_S));
    rt = tal_time_get_local_time_custom(0, &tm);
    if (rt == OPRT_OK) {
        snprintf(name, name_size, "REC_%04d%02d%02d_%02d%02d%02d.%s",
                 tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                 tm.tm_hour, tm.tm_min, tm.tm_sec, ext);
    } else {
        snprintf(name, name_size, "REC_%04u.%s", (unsigned)s_record_fallback_idx, ext);
        s_record_fallback_idx = (s_record_fallback_idx + 1) % 10000;
    }
}

/**
 * @brief Open a new recording file under RECORD_STORE_DIR
 * @param[in] codec_type Audio codec type (AUDIO_CODEC_PCM or AUDIO_CODEC_OPUS)
 * @return OPRT_OK on success
 */
static OPERATE_RET __record_file_open(AI_AUDIO_CODEC_TYPE codec_type)
{
    char filepath[128];

    if (s_record_fp != NULL) {
        tkl_fclose(s_record_fp);
        s_record_fp = NULL;
    }

    tkl_fs_mkdir("/t5_fs/tmp");
    tkl_fs_mkdir(RECORD_STORE_DIR);

    s_record_codec_type = codec_type;
    __record_generate_filename(s_record_cur_name, sizeof(s_record_cur_name), codec_type);
    snprintf(filepath, sizeof(filepath), "%s/%s", RECORD_STORE_DIR, s_record_cur_name);

    s_record_fp = tkl_fopen(filepath, "wb");
    if (s_record_fp == NULL) {
        TAL_PR_ERR("[desk_record] open record file failed: %s", filepath);
        return OPRT_COM_ERROR;
    }

    if (codec_type == AUDIO_CODEC_PCM) {
        uint8_t wav_head[WAV_HEAD_LEN];
        memset(wav_head, 0, sizeof(wav_head));
        app_get_wav_head(0, 1, RECORD_PCM_SAMPLE_RATE, RECORD_PCM_BIT_DEPTH, RECORD_PCM_CHANNELS, wav_head);
        if (tkl_fwrite(wav_head, WAV_HEAD_LEN, s_record_fp) != WAV_HEAD_LEN) {
            TAL_PR_ERR("[desk_record] write wav header failed");
            tkl_fclose(s_record_fp);
            s_record_fp = NULL;
            return OPRT_COM_ERROR;
        }
    }

    s_record_file_size = 0;
    TAL_PR_INFO("[desk_record] recording to: %s (codec=%u)", filepath, (unsigned)codec_type);
    return OPRT_OK;
}

/**
 * @brief Close current recording file and save entry to record list + JSON
 * @return none
 */
static void __record_file_close_and_save(void)
{
    RECORD_AUDIO_LIST_T *item = NULL;
    RECORD_AUDIO_LIST_T *oldest = NULL;
    POSIX_TM_S tm;
    char del_path[128];
    int id = -1;

    if (s_record_fp != NULL) {
        if (s_record_codec_type == AUDIO_CODEC_PCM) {
            uint8_t wav_head[WAV_HEAD_LEN];
            memset(wav_head, 0, sizeof(wav_head));
            app_get_wav_head((uint32_t)s_record_file_size, 1,
                             RECORD_PCM_SAMPLE_RATE, RECORD_PCM_BIT_DEPTH, RECORD_PCM_CHANNELS, wav_head);
            tkl_fseek(s_record_fp, 0, 0);
            tkl_fwrite(wav_head, WAV_HEAD_LEN, s_record_fp);
        }
        tkl_fclose(s_record_fp);
        s_record_fp = NULL;
    }

    if (s_record_file_size == 0 || s_record_cur_name[0] == '\0') {
        s_record_cur_name[0] = '\0';
        s_record_file_size = 0;
        return;
    }

    if (__record_info_list_create() != OPRT_OK) {
        s_record_cur_name[0] = '\0';
        s_record_file_size = 0;
        return;
    }

    item = (RECORD_AUDIO_LIST_T *)tal_malloc(sizeof(RECORD_AUDIO_LIST_T));
    if (item == NULL) {
        TAL_PR_ERR("[desk_record] malloc record item failed");
        s_record_cur_name[0] = '\0';
        s_record_file_size = 0;
        return;
    }

    memset(item, 0, sizeof(RECORD_AUDIO_LIST_T));
    memset(&tm, 0, sizeof(POSIX_TM_S));

    tal_mutex_lock(s_record_list.mutex);

    if (s_record_list.num >= RECORD_INFO_ITEM_NUM && !tuya_list_empty(&s_record_list.list_head)) {
        oldest = tuya_list_entry(s_record_list.list_head.next, RECORD_AUDIO_LIST_T, list_node);
        if (oldest != NULL) {
            snprintf(del_path, sizeof(del_path), "%s/%s", RECORD_STORE_DIR, oldest->name);
            tkl_fs_remove(del_path);
            tuya_list_del(&oldest->list_node);
            tal_free(oldest);
            s_record_list.num--;
        }
    }

    id = __record_info_list_alloc_id_locked();
    if (id < 0) {
        tal_mutex_unlock(s_record_list.mutex);
        tal_free(item);
        s_record_cur_name[0] = '\0';
        s_record_file_size = 0;
        return;
    }

    item->id = id;
    snprintf(item->name, sizeof(item->name), "%s", s_record_cur_name);
    item->len = s_record_file_size;
    item->duration = s_elapsed_seconds;

    tal_time_get_local_time_custom(0, &tm);
    item->create_time.tm_year = tm.tm_year + 1900;
    item->create_time.tm_mon  = tm.tm_mon + 1;
    item->create_time.tm_mday = tm.tm_mday;
    item->create_time.tm_hour = tm.tm_hour;
    item->create_time.tm_min  = tm.tm_min;
    item->create_time.tm_sec  = tm.tm_sec;

    tuya_list_add_tail(&item->list_node, &s_record_list.list_head);
    s_record_list.num++;

    tal_mutex_unlock(s_record_list.mutex);

    __record_info_json_write();

    TAL_PR_INFO("[desk_record] saved: %s, size=%lu", s_record_cur_name, (unsigned long)s_record_file_size);

    s_record_cur_name[0] = '\0';
    s_record_file_size = 0;
}

/* ---------------------------------------------------------------------------
 * Function implementations
 * --------------------------------------------------------------------------- */
/**
 * @brief Audio input callback for record mode
 * @param[in] codec_type Audio codec type (AUDIO_CODEC_PCM or AUDIO_CODEC_OPUS)
 * @param[in] data Pointer to audio data
 * @param[in] len  Length of the audio data in bytes
 * @return OPRT_OK on success
 * @note For AUDIO_CODEC_PCM, data is stored as WAV; for AUDIO_CODEC_OPUS, data is stored as raw .opus
 */
static OPERATE_RET __record_input_audio_cb(AI_AUDIO_CODEC_TYPE codec_type, VOID *data, INT_T len)
{
    INT_T written = 0;

    if (data == NULL || len <= 0) {
        return OPRT_OK;
    }

    if (s_record_fp == NULL) {
        if (__record_file_open(codec_type) != OPRT_OK) {
            return OPRT_COM_ERROR;
        }
    }

    TAL_PR_DEBUG("[desk_record] write audio: codec=%u, data=%p, len=%d", (unsigned)codec_type, data, len);

    written = tkl_fwrite(data, len, s_record_fp);
    if (written != len) {
        TAL_PR_ERR("[desk_record] write audio failed: expected=%d, written=%d", len, written);
        return OPRT_COM_ERROR;
    }

    s_record_file_size += (UINT64_T)len;
    return OPRT_OK;
}

/**
 * @brief Register record audio input callback to AI mode record handle
 * @return OPRT_OK on success
 */
OPERATE_RET desk_record_handle_register(VOID)
{
    AI_RECORD_HANDLE_T handle = {0};
    handle.input_audio = __record_input_audio_cb;
    return wukong_ai_record_handle_set(&handle);
}

/**
 * @brief Update the time display label with current elapsed time
 * @return none
 */
static void __record_update_time_display(void)
{
    lv_record_ui_t *ui = &getContent()->st_func_record;
    char time_str[16];
    uint32_t hours   = s_elapsed_seconds / 3600;
    uint32_t minutes = (s_elapsed_seconds % 3600) / 60;
    uint32_t seconds = s_elapsed_seconds % 60;

    snprintf(time_str, sizeof(time_str), "%02u:%02u.%02u", (unsigned)hours, (unsigned)minutes, (unsigned)seconds);
    lv_label_set_text(ui->record_time, time_str);
}

/**
 * @brief Timer callback to increment elapsed time and refresh display
 * @param[in] timer LVGL timer handle
 * @return none
 */
static void __record_timer_cb(lv_timer_t *timer)
{
    s_elapsed_seconds++;
    __record_update_time_display();
}

/**
 * @brief Transition to default (idle) state, reset timer and restore UI
 * @return none
 */
static void __record_switch_to_default(void)
{
    lv_record_ui_t *ui = &getContent()->st_func_record;

    s_record_state = RECORD_STATE_DEFAULT;
    wukong_audio_input_wakeup_set(FALSE);
    __record_file_close_and_save();
    s_elapsed_seconds = 0;

    if (s_record_timer != NULL) {
        lv_timer_del(s_record_timer);
        s_record_timer = NULL;
    }

    lv_img_set_src(ui->record_icon, &s_record_res.record_default_icon);
    lv_label_set_text(ui->record_time, RECORD_TIME_DEFAULT);
    lv_obj_clear_flag(ui->list_icon_obj, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui->list_label, LV_OBJ_FLAG_HIDDEN);
}

/**
 * @brief Transition to recording state, start timer and update UI
 * @return none
 */
static void __record_switch_to_recording(void)
{
    lv_record_ui_t *ui = &getContent()->st_func_record;

    s_record_state = RECORD_STATE_RECORDING;
    wukong_audio_input_wakeup_set(TRUE);
    lv_img_set_src(ui->record_icon, &s_record_res.record_recording_icon);
    lv_obj_add_flag(ui->list_icon_obj, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui->list_label, LV_OBJ_FLAG_HIDDEN);

    s_record_timer = lv_timer_create(__record_timer_cb, 1000, NULL);
}

/**
 * @brief Transition to paused state, stop timer and update icon
 * @return none
 */
static void __record_switch_to_paused(void)
{
    lv_record_ui_t *ui = &getContent()->st_func_record;

    s_record_state = RECORD_STATE_PAUSED;
    wukong_audio_input_wakeup_set(FALSE);
    lv_img_set_src(ui->record_icon, &s_record_res.record_pause_icon);

    if (s_record_timer != NULL) {
        lv_timer_del(s_record_timer);
        s_record_timer = NULL;
    }
}

/**
 * @brief Back button click handler, navigate to personal center
 * @param[in] e LVGL event object
 * @return none
 */
static void record_back_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED) {
        TAL_PR_DEBUG("[%s] clicked !!!!!!", __func__);

        lv_indev_wait_release(lv_indev_get_act());
        desk_handle_ui_back_to(DHUI_SCREEN_ID_PERSONAL_CENTER, LV_SCR_LOAD_ANIM_MOVE_RIGHT, DHUI_SWITCH_PERMANENT);
    }
}

/**
 * @brief List / finish button click handler, return to default state when recording or paused
 * @param[in] e LVGL event object
 * @return none
 */
static void record_list_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED) {
        TAL_PR_DEBUG("[%s] clicked, state=%d", __func__, s_record_state);

        if (s_record_state == RECORD_STATE_RECORDING || s_record_state == RECORD_STATE_PAUSED) {
            __record_switch_to_default();
        } else if (s_record_state == RECORD_STATE_DEFAULT) {
            desk_handle_ui_switch_to(DHUI_SCREEN_ID_RECORD_LIST, LV_SCR_LOAD_ANIM_FADE_ON, DHUI_SWITCH_PERMANENT);
        }
    }
}

/**
 * @brief Record button click handler, cycle state: default -> recording -> paused -> recording ...
 * @param[in] e LVGL event object
 * @return none
 */
static void record_handle_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED) {
        TAL_PR_DEBUG("[%s] clicked, state=%d", __func__, s_record_state);

        switch (s_record_state) {
        case RECORD_STATE_DEFAULT:
            __record_switch_to_recording();
            break;
        case RECORD_STATE_RECORDING:
            __record_switch_to_paused();
            break;
        case RECORD_STATE_PAUSED:
            __record_switch_to_recording();
            break;
        default:
            break;
        }
    }
}

/**
 * @brief Create title bar with back button and list/finish button
 * @return none
 */
static void record_title_create(void)
{
    lv_record_ui_t *ui = &getContent()->st_func_record;

    ui->title = lv_obj_create(ui->record_scr);
    lv_obj_remove_style_all(ui->title);
    lv_obj_set_size(ui->title, LV_HOR_RES, 50);
    lv_obj_set_pos(ui->title, 0, 0);
    lv_obj_set_style_bg_opa(ui->title, LV_OPA_TRANSP, 0);

    lv_obj_t *title_name = lv_label_create(ui->title);
    lv_label_set_text(title_name, "录音");
    lv_obj_set_style_text_font(title_name, &AlibabaPuHuiTi3_Regular18_Static, 0);
    lv_obj_set_style_text_color(title_name, lv_color_white(), 0);
    lv_obj_set_size(title_name, LV_SIZE_CONTENT, 20);
    lv_obj_align(title_name, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_align(title_name, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *back_btn = lv_btn_create(ui->title);
    lv_obj_remove_style_all(back_btn);
    lv_obj_set_size(back_btn, 50, 50);
    lv_obj_set_pos(back_btn, 0, 0);
    lv_obj_set_style_bg_opa(back_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_tag(back_btn, NULL);
    lv_obj_add_event_cb(back_btn, record_back_event, LV_EVENT_CLICKED, NULL);
    if (png_img_load(tuya_app_gui_get_picture_full_path(ICON_BACK_LEFT_24_24), &s_record_res.back_icon) == 0) {
        lv_obj_t *back_icon = lv_img_create(back_btn);
        lv_img_set_src(back_icon, &s_record_res.back_icon);
        lv_obj_align(back_icon, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_size(back_icon, 24, 24);
    }

    ui->list_btn = lv_btn_create(ui->title);
    lv_obj_remove_style_all(ui->list_btn);
    lv_obj_set_size(ui->list_btn, 50, 50);
    lv_obj_set_pos(ui->list_btn, 270, 0);
    lv_obj_set_style_bg_opa(ui->list_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_tag(ui->list_btn, NULL);
    lv_obj_add_event_cb(ui->list_btn, record_list_event, LV_EVENT_CLICKED, NULL);

    ui->list_icon_obj = lv_img_create(ui->list_btn);
    lv_obj_align(ui->list_icon_obj, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_size(ui->list_icon_obj, 24, 24);
    if (png_img_load(tuya_app_gui_get_picture_full_path(RECORD_LIST_ICON), &s_record_res.list_icon) == 0) {
        lv_img_set_src(ui->list_icon_obj, &s_record_res.list_icon);
    }

    ui->list_label = lv_label_create(ui->list_btn);
    lv_label_set_text(ui->list_label, "完成");
    lv_obj_set_style_text_font(ui->list_label, &AlibabaPuHuiTi3_Regular18_Static, 0);
    lv_obj_set_style_text_color(ui->list_label, lv_color_white(), 0);
    lv_obj_set_size(ui->list_label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(ui->list_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(ui->list_label, LV_OBJ_FLAG_HIDDEN);
}

/**
 * @brief Create content area with time display and record button
 * @return none
 */
static void record_content_create(void)
{
    lv_record_ui_t *ui = &getContent()->st_func_record;

    ui->content = lv_obj_create(ui->record_scr);
    lv_obj_set_size(ui->content, LV_HOR_RES, LV_VER_RES - 50);
    lv_obj_set_style_border_width(ui->content, 0, 0);
    lv_obj_set_pos(ui->content, 0, 50);
    lv_obj_set_scroll_dir(ui->content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(ui->content, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_all(ui->content, 0, 0);
    lv_obj_set_style_bg_opa(ui->content, LV_OPA_TRANSP, 0);

    ui->record_time = lv_label_create(ui->content);
    lv_label_set_text(ui->record_time, RECORD_TIME_DEFAULT);
    lv_obj_set_style_text_font(ui->record_time, &AlibabaPuHuiTi3_Regular40, 0);
    lv_obj_set_style_text_color(ui->record_time, lv_color_white(), 0);
    lv_obj_set_size(ui->record_time, LV_SIZE_CONTENT, 42);
    lv_obj_align(ui->record_time, LV_ALIGN_TOP_MID, 0, 30);
    lv_obj_set_style_text_align(ui->record_time, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *record_btn = lv_btn_create(ui->content);
    lv_obj_remove_style_all(record_btn);
    lv_obj_set_size(record_btn, 50, 50);
    lv_obj_align(record_btn, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_set_style_bg_opa(record_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_tag(record_btn, NULL);
    lv_obj_add_event_cb(record_btn, record_handle_event, LV_EVENT_CLICKED, NULL);

    png_img_load(tuya_app_gui_get_picture_full_path(RECORD_DEFAULT_ICON), &s_record_res.record_default_icon);
    png_img_load(tuya_app_gui_get_picture_full_path(RECORD_RECORDING_ICON), &s_record_res.record_recording_icon);
    png_img_load(tuya_app_gui_get_picture_full_path(RECORD_PAUSE_ICON), &s_record_res.record_pause_icon);

    ui->record_icon = lv_img_create(record_btn);
    lv_img_set_src(ui->record_icon, &s_record_res.record_default_icon);
    lv_obj_align(ui->record_icon, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_size(ui->record_icon, 50, 50);
}

/**
 * @brief Delete a record item by ID from the list and remove its file
 * @param[in] record_id ID of the record to delete
 * @return OPRT_OK on success
 */
static OPERATE_RET __record_info_list_delete_by_id(int record_id)
{
    LIST_HEAD *pos = NULL;
    LIST_HEAD *next = NULL;
    OPERATE_RET rt = OPRT_COM_ERROR;
    char del_path[128];

    if (__record_info_list_create() != OPRT_OK) {
        return OPRT_COM_ERROR;
    }

    tal_mutex_lock(s_record_list.mutex);
    tuya_list_for_each_safe(pos, next, &s_record_list.list_head)
    {
        RECORD_AUDIO_LIST_T *rec = tuya_list_entry(pos, RECORD_AUDIO_LIST_T, list_node);
        if (rec == NULL) {
            continue;
        }
        if (rec->id != record_id) {
            continue;
        }

        snprintf(del_path, sizeof(del_path), "%s/%s", RECORD_STORE_DIR, rec->name);
        tkl_fs_remove(del_path);
        tuya_list_del(&rec->list_node);
        tal_free(rec);
        if (s_record_list.num > 0) {
            s_record_list.num--;
        }
        rt = OPRT_OK;
        break;
    }
    tal_mutex_unlock(s_record_list.mutex);

    if (rt == OPRT_OK) {
        __record_info_json_write();
    }

    return rt;
}

/**
 * @brief Back button click handler for the record list screen
 * @param[in] e LVGL event object
 * @return none
 */
static void __record_list_back_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED) {
        desk_handle_ui_back(LV_SCR_LOAD_ANIM_FADE_ON, DHUI_SWITCH_PERMANENT);
    }
}

/**
 * @brief Delete button click handler for a record list item
 * @param[in] e LVGL event object
 * @return none
 */
static void __record_list_item_delete_event(lv_event_t *e)
{
    lv_indev_t *indev = lv_indev_get_act();
    int record_id = (int)(uintptr_t)lv_event_get_user_data(e);

    TAL_PR_INFO("[%s] delete record id: %d", __func__, record_id);

    if (__record_info_list_delete_by_id(record_id) != OPRT_OK) {
        TAL_PR_WARN("[%s] delete record failed, id: %d", __func__, record_id);
        return;
    }

    if (indev != NULL) {
        lv_indev_wait_release(indev);
    }

    __record_list_create();
}

/**
 * @brief Click handler for a record list item, switch to playback view
 * @param[in] e LVGL event object
 * @return none
 */
static void __record_list_item_clicked_event(lv_event_t *e)
{
    int record_id = (int)(uintptr_t)lv_event_get_user_data(e);
    lv_record_ui_t *ui = &getContent()->st_func_record;
    LIST_HEAD *pos = NULL;
    BOOL_T found = FALSE;

    TAL_PR_INFO("[%s] clicked record id: %d", __func__, record_id);

    if (__record_info_list_create() != OPRT_OK) {
        return;
    }

    tal_mutex_lock(s_record_list.mutex);
    tuya_list_for_each(pos, &s_record_list.list_head)
    {
        RECORD_AUDIO_LIST_T *rec = tuya_list_entry(pos, RECORD_AUDIO_LIST_T, list_node);
        if (rec == NULL || rec->id != record_id) {
            continue;
        }
        s_play_record_id = rec->id;
        snprintf(s_play_record_name, sizeof(s_play_record_name), "%s", rec->name);
        s_play_duration = rec->duration;
        memcpy(&s_play_create_time, &rec->create_time, sizeof(POSIX_TM_S));
        found = TRUE;
        break;
    }
    tal_mutex_unlock(s_record_list.mutex);

    if (found == FALSE) {
        return;
    }

    __record_play_content_refresh();
    lv_obj_add_flag(ui->record_list_cont, LV_OBJ_FLAG_HIDDEN);
}

/**
 * @brief Create a delete button inside a record list item
 * @param[in] parent Parent LVGL object
 * @param[in] record_id Record ID associated with this button
 * @return none
 */
static void __record_list_item_delete_btn_create(lv_obj_t *parent, int record_id)
{
    lv_obj_t *delete_btn = NULL;
    lv_obj_t *delete_label = NULL;

    if (parent == NULL) {
        return;
    }

    delete_btn = lv_btn_create(parent);
    lv_obj_remove_style_all(delete_btn);
    lv_obj_set_size(delete_btn, 52, 28);
    lv_obj_align(delete_btn, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_radius(delete_btn, 14, 0);
    lv_obj_set_style_bg_opa(delete_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(delete_btn, lv_color_hex(0x4B4D59), 0);
    lv_obj_set_style_border_width(delete_btn, 0, 0);
    lv_obj_add_event_cb(delete_btn, __record_list_item_delete_event, LV_EVENT_CLICKED, (void *)(uintptr_t)record_id);

    delete_label = lv_label_create(delete_btn);
    lv_obj_remove_style_all(delete_label);
    lv_label_set_text(delete_label, "删除");
    lv_obj_center(delete_label);
    lv_obj_set_style_text_font(delete_label, &AlibabaPuHuiTi3_Regular18_Static, 0);
    lv_obj_set_style_text_color(delete_label, lv_color_hex(0xF3E55D), 0);
}

/**
 * @brief Build the scrollable record list inside the list container
 * @return none
 */
static void __record_list_create(void)
{
    lv_record_ui_t *ui = &getContent()->st_func_record;
    LIST_HEAD *pos = NULL;
    BOOL_T has_record = FALSE;
    char time_str[64];

    lv_obj_clean(ui->record_list_cont);
    lv_obj_set_flex_flow(ui->record_list_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(ui->record_list_cont, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(ui->record_list_cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_ver(ui->record_list_cont, 12, 0);
    lv_obj_set_style_pad_hor(ui->record_list_cont, 20, 0);
    lv_obj_set_style_pad_row(ui->record_list_cont, 10, 0);
    lv_obj_set_style_pad_column(ui->record_list_cont, 0, 0);
    lv_obj_set_style_border_width(ui->record_list_cont, 0, 0);

    if (__record_info_list_create() != OPRT_OK) {
        return;
    }

    tal_mutex_lock(s_record_list.mutex);

    tuya_list_for_each(pos, &s_record_list.list_head)
    {
        RECORD_AUDIO_LIST_T *rec = tuya_list_entry(pos, RECORD_AUDIO_LIST_T, list_node);
        lv_obj_t *item_cont = NULL;
        lv_obj_t *name_label = NULL;
        lv_obj_t *date_label = NULL;

        if (rec == NULL) {
            continue;
        }

        has_record = TRUE;

        snprintf(time_str, sizeof(time_str), "%04d-%02d-%02d %02d:%02d:%02d",
                 rec->create_time.tm_year, rec->create_time.tm_mon, rec->create_time.tm_mday,
                 rec->create_time.tm_hour, rec->create_time.tm_min, rec->create_time.tm_sec);

        item_cont = lv_obj_create(ui->record_list_cont);
        lv_obj_remove_style_all(item_cont);
        lv_obj_set_size(item_cont, 280, 72);
        lv_obj_add_flag(item_cont, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_radius(item_cont, 16, 0);
        lv_obj_set_style_bg_opa(item_cont, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(item_cont, lv_color_hex(0x353740), 0);
        lv_obj_set_style_pad_left(item_cont, 16, 0);
        lv_obj_set_style_pad_right(item_cont, 16, 0);
        lv_obj_set_style_pad_top(item_cont, 12, 0);
        lv_obj_set_style_pad_bottom(item_cont, 12, 0);
        lv_obj_set_style_border_width(item_cont, 0, 0);
        lv_obj_set_scrollbar_mode(item_cont, LV_SCROLLBAR_MODE_OFF);
        lv_obj_set_scroll_dir(item_cont, LV_DIR_NONE);
        lv_obj_add_event_cb(item_cont, __record_list_item_clicked_event, LV_EVENT_CLICKED, (void *)(uintptr_t)rec->id);

        name_label = lv_label_create(item_cont);
        lv_obj_remove_style_all(name_label);
        lv_label_set_long_mode(name_label, LV_LABEL_LONG_DOT);
        lv_label_set_text(name_label, "录音");
        lv_obj_set_size(name_label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_pos(name_label, 0, 0);
        lv_obj_set_style_text_font(name_label, &AlibabaPuHuiTi3_Regular18_Static, 0);
        lv_obj_set_style_text_color(name_label, lv_color_white(), 0);
        lv_obj_set_style_text_align(name_label, LV_TEXT_ALIGN_LEFT, 0);

        date_label = lv_label_create(item_cont);
        lv_obj_remove_style_all(date_label);
        lv_label_set_long_mode(date_label, LV_LABEL_LONG_DOT);
        lv_label_set_text(date_label, time_str);
        lv_obj_set_size(date_label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_pos(date_label, 0, 28);
        lv_obj_set_style_text_font(date_label, &AlibabaPuHuiTi3_Regular18_Static, 0);
        lv_obj_set_style_text_color(date_label, lv_color_hex(0xB8BDDE), 0);
        lv_obj_set_style_text_align(date_label, LV_TEXT_ALIGN_LEFT, 0);

        __record_list_item_delete_btn_create(item_cont, rec->id);
    }

    tal_mutex_unlock(s_record_list.mutex);

    if (has_record == FALSE) {
        lv_obj_t *empty_label = lv_label_create(ui->record_list_cont);
        lv_obj_remove_style_all(empty_label);
        lv_label_set_text(empty_label, "暂无录音");
        lv_obj_set_size(empty_label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_text_font(empty_label, &AlibabaPuHuiTi3_Regular18_Static, 0);
        lv_obj_set_style_text_color(empty_label, lv_color_hex(0xB8BDDE), 0);
        lv_obj_set_style_text_align(empty_label, LV_TEXT_ALIGN_CENTER, 0);
    }
}

/**
 * @brief Helper to destroy playback content and reset related UI pointers
 * @param[in] ui Pointer to record UI struct
 * @return none
 */
static void __record_play_destroy(lv_record_ui_t *ui)
{
    __record_upload_stop();
    __record_play_stop_playback();
    wukong_audio_player_stop(AI_PLAYER_BG);

    if (ui->record_play_cont != NULL) {
        lv_obj_add_flag(ui->record_play_cont, LV_OBJ_FLAG_HIDDEN);
    }
    s_play_record_id = -1;
    s_play_elapsed_ms = 0;
}

/**
 * @brief Stop playback and release the progress timer
 * @return none
 */
static void __record_play_stop_playback(void)
{
    if (s_play_is_playing) {
        UINT32_T now = tal_system_get_tick_count();
        s_play_elapsed_ms += (now - s_play_resume_tick);
    }
    s_play_is_playing = FALSE;

    if (s_play_timer != NULL) {
        lv_timer_del(s_play_timer);
        s_play_timer = NULL;
    }
}

/**
 * @brief Start playing the current record file and begin progress tracking
 * @return none
 */
static void __record_play_start_playback(void)
{
    lv_record_ui_t *ui = &getContent()->st_func_record;
    char filepath[128];

    if (s_play_record_name[0] == '\0') {
        return;
    }

    __record_play_stop_playback();
    wukong_audio_player_stop(AI_PLAYER_BG);

    snprintf(filepath, sizeof(filepath), "%s/%s", RECORD_STORE_DIR, s_play_record_name);

    CONST char *ext = strrchr(s_play_record_name, '.');
    INT_T play_codec = AI_AUDIO_CODEC_WAV;
    if (ext != NULL && strcmp(ext, ".opus") == 0) {
        play_codec = AI_AUDIO_CODEC_OPUS;
    }

    TAL_PR_INFO("[desk_record] play local: %s, duration=%u, codec=%d", filepath, (unsigned)s_play_duration, play_codec);

    wukong_audio_play_local(filepath, "录音", NULL, play_codec, 0);

    s_play_is_playing = TRUE;
    s_play_elapsed_ms = 0;
    s_play_resume_tick = tal_system_get_tick_count();

    s_play_timer = lv_timer_create(__record_play_timer_cb, 500, NULL);

    if (s_record_res.play_playing_icon.data != NULL && ui->play_btn_icon != NULL) {
        lv_img_set_src(ui->play_btn_icon, &s_record_res.play_playing_icon);
    }
}

/**
 * @brief Progress timer callback, updates slider and time labels periodically
 * @param[in] timer LVGL timer handle
 * @return none
 */
static void __record_play_timer_cb(lv_timer_t *timer)
{
    lv_record_ui_t *ui = &getContent()->st_func_record;
    UINT32_T elapsed_ms;
    UINT32_T cur_sec;
    char time_str[16];

    if (s_play_is_playing == FALSE) {
        return;
    }

    elapsed_ms = s_play_elapsed_ms + (tal_system_get_tick_count() - s_play_resume_tick);
    cur_sec = elapsed_ms / 1000;

    if (cur_sec >= s_play_duration && s_play_duration > 0) {
        cur_sec = s_play_duration;
        s_play_is_playing = FALSE;
        s_play_elapsed_ms = s_play_duration * 1000;

        if (s_play_timer != NULL) {
            lv_timer_del(s_play_timer);
            s_play_timer = NULL;
        }

        if (s_record_res.play_pause_icon.data != NULL && ui->play_btn_icon != NULL) {
            lv_img_set_src(ui->play_btn_icon, &s_record_res.play_pause_icon);
        }
    }

    if (ui->play_slider != NULL) {
        lv_slider_set_value(ui->play_slider, (int32_t)cur_sec, LV_ANIM_OFF);
    }

    snprintf(time_str, sizeof(time_str), "%02u:%02u",
             (unsigned)(cur_sec / 60), (unsigned)(cur_sec % 60));
    if (ui->play_cur_time != NULL) {
        lv_label_set_text(ui->play_cur_time, time_str);
    }
}

/**
 * @brief Back button event handler in playback view, return to record list
 * @param[in] e LVGL event object
 * @return none
 */
static void __record_play_back_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_record_ui_t *ui = &getContent()->st_func_record;

    if (code == LV_EVENT_CLICKED) {
        __record_play_destroy(ui);
        lv_obj_clear_flag(ui->record_list_cont, LV_OBJ_FLAG_HIDDEN);
    }
}

/**
 * @brief Play/pause toggle button event handler
 * @param[in] e LVGL event object
 * @return none
 */
static void __record_play_toggle_event(lv_event_t *e)
{
    lv_record_ui_t *ui = &getContent()->st_func_record;

    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    TAL_PR_INFO("[%s] play/pause toggled, record_id=%d, playing=%d", __func__, s_play_record_id, s_play_is_playing);

    if (s_play_is_playing) {
        UINT32_T now = tal_system_get_tick_count();
        s_play_elapsed_ms += (now - s_play_resume_tick);
        s_play_is_playing = FALSE;
        wukong_audio_player_pause();

        if (s_record_res.play_pause_icon.data != NULL && ui->play_btn_icon != NULL) {
            lv_img_set_src(ui->play_btn_icon, &s_record_res.play_pause_icon);
        }
    } else {
        UINT32_T cur_sec = s_play_elapsed_ms / 1000;
        if ((cur_sec >= s_play_duration && s_play_duration > 0) ||
            (s_play_elapsed_ms == 0 && s_play_timer == NULL)) {
            __record_play_start_playback();
            return;
        }

        wukong_audio_player_resume();
        s_play_is_playing = TRUE;
        s_play_resume_tick = tal_system_get_tick_count();

        if (s_play_timer == NULL) {
            s_play_timer = lv_timer_create(__record_play_timer_cb, 500, NULL);
        }

        if (s_record_res.play_playing_icon.data != NULL && ui->play_btn_icon != NULL) {
            lv_img_set_src(ui->play_btn_icon, &s_record_res.play_playing_icon);
        }
    }
}

/**
 * @brief Rewind 15 seconds button event handler
 * @param[in] e LVGL event object
 * @return none
 */
static void __record_play_rewind_event(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    TAL_PR_INFO("[%s] rewind 15s, record_id=%d", __func__, s_play_record_id);
}

/**
 * @brief Forward 15 seconds button event handler
 * @param[in] e LVGL event object
 * @return none
 */
static void __record_play_forward_event(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    TAL_PR_INFO("[%s] forward 15s, record_id=%d", __func__, s_play_record_id);
}

/**
 * @brief Delete button event handler in playback view
 * @param[in] e LVGL event object
 * @return none
 */
static void __record_play_delete_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_record_ui_t *ui = &getContent()->st_func_record;
    lv_indev_t *indev = NULL;

    if (code != LV_EVENT_CLICKED) {
        return;
    }

    TAL_PR_INFO("[%s] delete record id: %d", __func__, s_play_record_id);

    if (__record_info_list_delete_by_id(s_play_record_id) != OPRT_OK) {
        TAL_PR_WARN("[%s] delete failed, id: %d", __func__, s_play_record_id);
        return;
    }

    indev = lv_indev_get_act();
    if (indev != NULL) {
        lv_indev_wait_release(indev);
    }

    __record_play_destroy(ui);
    lv_obj_clear_flag(ui->record_list_cont, LV_OBJ_FLAG_HIDDEN);
    __record_list_create();
}

/**
 * @brief Stop ongoing upload and release all upload resources
 * @return none
 */
static void __record_upload_stop(void)
{
    lv_record_ui_t *ui = &getContent()->st_func_record;

    if (s_upload_timer != NULL) {
        lv_timer_del(s_upload_timer);
        s_upload_timer = NULL;
    }
    if (s_upload_ctx.read_buf != NULL) {
        tal_free(s_upload_ctx.read_buf);
    }
    if (s_upload_ctx.fp != NULL) {
        tkl_fclose(s_upload_ctx.fp);
    }
    memset(&s_upload_ctx, 0, sizeof(s_upload_ctx));

    if (ui->upload_bar_cont != NULL) {
        lv_obj_add_flag(ui->upload_bar_cont, LV_OBJ_FLAG_HIDDEN);
    }
}

/**
 * @brief LVGL timer callback for chunked file upload (20ms interval)
 * @param[in] timer LVGL timer handle
 * @return none
 */
static void __record_upload_timer_cb(lv_timer_t *timer)
{
    lv_record_ui_t *ui = &getContent()->st_func_record;
    INT_T read_len = 0;
    OPERATE_RET rt = OPRT_OK;
    INT_T pct = 0;
    char pct_str[8];

    if (s_upload_ctx.fp == NULL || s_upload_ctx.read_buf == NULL) {
        __record_upload_stop();
        wukong_ai_agent_input_stop();
        return;
    }

    read_len = tkl_fread(s_upload_ctx.read_buf, RECORD_UPLOAD_READ_SIZE, s_upload_ctx.fp);
    if (read_len > 0) {
        rt = wukong_ai_agent_send_file(s_upload_ctx.read_buf, (UINT_T)read_len);
        if (rt != OPRT_OK) {
            TAL_PR_ERR("[upload] send failed, ret: %d, sent: %lu/%lu",
                       rt, (unsigned long)s_upload_ctx.total_sent,
                       (unsigned long)s_upload_ctx.file_len);
            __record_upload_stop();
            wukong_ai_agent_input_stop();
            return;
        }
        s_upload_ctx.total_sent += (UINT64_T)read_len;

        if (s_upload_ctx.file_len > 0) {
            pct = (INT_T)(s_upload_ctx.total_sent * 100 / s_upload_ctx.file_len);
            if (pct > 100) {
                pct = 100;
            }
        }
        if (ui->upload_bar != NULL) {
            lv_bar_set_value(ui->upload_bar, pct, LV_ANIM_OFF);
        }
        if (ui->upload_pct_label != NULL) {
            snprintf(pct_str, sizeof(pct_str), "%d%%", pct);
            lv_label_set_text(ui->upload_pct_label, pct_str);
        }

        TAL_PR_DEBUG("[upload] sent %lu/%lu bytes (%d%%)",
                     (unsigned long)s_upload_ctx.total_sent,
                     (unsigned long)s_upload_ctx.file_len, pct);
        return;
    }

    if (ui->upload_bar != NULL) {
        lv_bar_set_value(ui->upload_bar, 100, LV_ANIM_OFF);
    }
    if (ui->upload_pct_label != NULL) {
        lv_label_set_text(ui->upload_pct_label, "100%");
    }

    TAL_PR_INFO("[upload] complete, total: %lu bytes", (unsigned long)s_upload_ctx.total_sent);
    __record_upload_stop();
    wukong_ai_agent_send_text("将刚才上传的录音文件转换成文字并总结");
    wukong_ai_agent_input_stop();
}

/**
 * @brief Info button event handler, start chunked file upload via LVGL timer
 * @param[in] e LVGL event object
 * @return none
 */
static void __record_play_info_event(lv_event_t *e)
{
    lv_record_ui_t *ui = &getContent()->st_func_record;
    LIST_HEAD *pos = NULL;
    UINT64_T file_len = 0;
    char file_path[128] = {0};

    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    if (s_play_record_id < 0) {
        TAL_PR_WARN("[%s] no record selected", __func__);
        return;
    }

    if (s_upload_timer != NULL) {
        TAL_PR_WARN("[%s] upload already in progress", __func__);
        return;
    }

    if (s_record_list.inited && s_record_list.mutex != NULL) {
        tal_mutex_lock(s_record_list.mutex);
        tuya_list_for_each(pos, &s_record_list.list_head)
        {
            RECORD_AUDIO_LIST_T *rec = tuya_list_entry(pos, RECORD_AUDIO_LIST_T, list_node);
            if (rec != NULL && rec->id == s_play_record_id) {
                file_len = rec->len;
                break;
            }
        }
        tal_mutex_unlock(s_record_list.mutex);
    }

    snprintf(file_path, sizeof(file_path), "%s/%s", RECORD_STORE_DIR, s_play_record_name);

    TAL_PR_INFO("========== Record File Info ==========");
    TAL_PR_INFO("  Record ID   : %d", s_play_record_id);
    TAL_PR_INFO("  File Name   : %s", s_play_record_name);
    TAL_PR_INFO("  File Path   : %s", file_path);
    TAL_PR_INFO("  File Size   : %lu bytes", (unsigned long)file_len);
    TAL_PR_INFO("  Duration    : %u ms", s_play_duration);
    TAL_PR_INFO("  Create Time : %04d-%02d-%02d %02d:%02d:%02d",
                s_play_create_time.tm_year, s_play_create_time.tm_mon,
                s_play_create_time.tm_mday, s_play_create_time.tm_hour,
                s_play_create_time.tm_min, s_play_create_time.tm_sec);
    TAL_PR_INFO("  Playing     : %s", s_play_is_playing ? "YES" : "NO");
    TAL_PR_INFO("  Elapsed     : %u ms", s_play_elapsed_ms);
    TAL_PR_INFO("======================================");

    TUYA_FILE upload_fp = tkl_fopen(file_path, "rb");
    if (upload_fp == NULL) {
        TAL_PR_ERR("[%s] open file failed: %s", __func__, file_path);
        return;
    }

    UINT8_T *read_buf = (UINT8_T *)tal_malloc(RECORD_UPLOAD_READ_SIZE);
    if (read_buf == NULL) {
        TAL_PR_ERR("[%s] alloc read buffer failed", __func__);
        tkl_fclose(upload_fp);
        return;
    }

    memset(&s_upload_ctx, 0, sizeof(s_upload_ctx));
    s_upload_ctx.fp         = upload_fp;
    s_upload_ctx.read_buf   = read_buf;
    s_upload_ctx.file_len   = file_len;
    s_upload_ctx.total_sent = 0;

    if (ui->upload_bar != NULL) {
        lv_bar_set_value(ui->upload_bar, 0, LV_ANIM_OFF);
    }
    if (ui->upload_pct_label != NULL) {
        lv_label_set_text(ui->upload_pct_label, "0%");
    }
    if (ui->upload_bar_cont != NULL) {
        lv_obj_clear_flag(ui->upload_bar_cont, LV_OBJ_FLAG_HIDDEN);
    }

    wukong_ai_agent_input_start(TRUE);
    s_upload_timer = lv_timer_create(__record_upload_timer_cb, 20, NULL);
}

/**
 * @brief Create playback UI widgets once (called from setup_record_list_scr)
 * @return none
 * @note All dynamic data uses placeholders; call __record_play_content_refresh to update
 */
static void __record_play_ui_create(void)
{
    lv_record_ui_t *ui = &getContent()->st_func_record;
    lv_obj_t *card = NULL;
    lv_obj_t *btn = NULL;
    lv_obj_t *icon = NULL;

    card = lv_obj_create(ui->record_play_cont);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, 280, 170);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x353740), 0);
    lv_obj_set_style_pad_all(card, 16, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(card, LV_DIR_NONE);

    lv_obj_t *title_label = lv_label_create(card);
    lv_obj_remove_style_all(title_label);
    lv_label_set_text(title_label, "录音");
    lv_obj_set_style_text_font(title_label, &AlibabaPuHuiTi3_Regular18_Static, 0);
    lv_obj_set_style_text_color(title_label, lv_color_white(), 0);
    lv_obj_set_pos(title_label, 0, 0);

    btn = lv_btn_create(card);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, 40, 40);
    lv_obj_align(btn, LV_ALIGN_TOP_RIGHT, 8, -8);
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(btn, __record_play_back_event, LV_EVENT_CLICKED, NULL);
    if (OPRT_OK == png_img_load(tuya_app_gui_get_picture_full_path(ICON_BACK_TOP_24_24), &s_record_res.expand_icon)) {
        icon = lv_img_create(btn);
        lv_img_set_src(icon, &s_record_res.expand_icon);
        lv_obj_center(icon);
    }

    ui->play_date_label = lv_label_create(card);
    lv_obj_remove_style_all(ui->play_date_label);
    lv_label_set_text(ui->play_date_label, "");
    lv_obj_set_style_text_font(ui->play_date_label, &AlibabaPuHuiTi3_Regular16, 0);
    lv_obj_set_style_text_color(ui->play_date_label, lv_color_hex(0xB8BDDE), 0);
    lv_obj_set_pos(ui->play_date_label, 0, 26);

    ui->play_slider = lv_slider_create(card);
    lv_obj_set_size(ui->play_slider, 248, 6);
    lv_obj_set_pos(ui->play_slider, 0, 54);
    lv_slider_set_range(ui->play_slider, 0, 1);
    lv_slider_set_value(ui->play_slider, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(ui->play_slider, lv_color_hex(0x3D4A6B), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ui->play_slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(ui->play_slider, 3, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ui->play_slider, lv_color_hex(0xF3E55D), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(ui->play_slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(ui->play_slider, 3, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(ui->play_slider, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(ui->play_slider, 0, LV_PART_KNOB);

    ui->play_cur_time = lv_label_create(card);
    lv_obj_remove_style_all(ui->play_cur_time);
    lv_label_set_text(ui->play_cur_time, "00:00");
    lv_obj_set_style_text_font(ui->play_cur_time, &AlibabaPuHuiTi3_Regular16, 0);
    lv_obj_set_style_text_color(ui->play_cur_time, lv_color_hex(0xB8BDDE), 0);
    lv_obj_set_pos(ui->play_cur_time, 0, 66);

    ui->play_total_time = lv_label_create(card);
    lv_obj_remove_style_all(ui->play_total_time);
    lv_label_set_text(ui->play_total_time, "00:00");
    lv_obj_set_style_text_font(ui->play_total_time, &AlibabaPuHuiTi3_Regular16, 0);
    lv_obj_set_style_text_color(ui->play_total_time, lv_color_hex(0xB8BDDE), 0);
    lv_obj_align(ui->play_total_time, LV_ALIGN_TOP_RIGHT, 0, 66);

    png_img_load(tuya_app_gui_get_picture_full_path(RECORD_PLAY_PLAYING), &s_record_res.play_playing_icon);
    png_img_load(tuya_app_gui_get_picture_full_path(RECORD_PLAY_PAUSE), &s_record_res.play_pause_icon);
    btn = lv_btn_create(card);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, 40, 40);
    lv_obj_set_pos(btn, 104, 94);
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(btn, __record_play_toggle_event, LV_EVENT_CLICKED, NULL);
    if (s_record_res.play_playing_icon.data != NULL) {
        ui->play_btn_icon = lv_img_create(btn);
        lv_img_set_src(ui->play_btn_icon, &s_record_res.play_playing_icon);
        lv_obj_center(ui->play_btn_icon);
    }

    png_img_load(tuya_app_gui_get_picture_full_path(ICON_AI_CAMERA_ON), &s_record_res.ai_camera_icon);
    btn = lv_btn_create(card);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, 40, 40);
    lv_obj_set_pos(btn, -12, 94);
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(btn, __record_play_info_event, LV_EVENT_CLICKED, NULL);
    if (s_record_res.ai_camera_icon.data != NULL) {
        icon = lv_img_create(btn);
        lv_img_set_src(icon, &s_record_res.ai_camera_icon);
        lv_obj_center(icon);
    }

    png_img_load(tuya_app_gui_get_picture_full_path(RECORD_FAST_BACK), &s_record_res.fast_back_icon);
    btn = lv_btn_create(card);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, 26, 26);
    lv_obj_set_pos(btn, 58, 101);
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(btn, __record_play_rewind_event, LV_EVENT_CLICKED, NULL);
    if (s_record_res.fast_back_icon.data != NULL) {
        icon = lv_img_create(btn);
        lv_img_set_src(icon, &s_record_res.fast_back_icon);
        lv_obj_center(icon);
    }

    png_img_load(tuya_app_gui_get_picture_full_path(RECORD_FAST_FORWARD), &s_record_res.fast_forward_icon);
    btn = lv_btn_create(card);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, 26, 26);
    lv_obj_set_pos(btn, 164, 101);
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(btn, __record_play_forward_event, LV_EVENT_CLICKED, NULL);
    if (s_record_res.fast_forward_icon.data != NULL) {
        icon = lv_img_create(btn);
        lv_img_set_src(icon, &s_record_res.fast_forward_icon);
        lv_obj_center(icon);
    }

    png_img_load(tuya_app_gui_get_picture_full_path(RECORD_DELETE_24_24), &s_record_res.delete_icon);
    btn = lv_btn_create(card);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, 24, 24);
    lv_obj_set_pos(btn, 220, 102);
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(btn, __record_play_delete_event, LV_EVENT_CLICKED, NULL);
    if (s_record_res.delete_icon.data != NULL) {
        icon = lv_img_create(btn);
        lv_img_set_src(icon, &s_record_res.delete_icon);
        lv_obj_center(icon);
    }

    ui->upload_bar_cont = lv_obj_create(ui->record_play_cont);
    lv_obj_remove_style_all(ui->upload_bar_cont);
    lv_obj_set_size(ui->upload_bar_cont, 280, 36);
    // lv_obj_align(ui->upload_bar_cont, LV_ALIGN_TOP_MID, 0, 186);
    lv_obj_align(ui->upload_bar_cont, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_radius(ui->upload_bar_cont, 12, 0);
    lv_obj_set_style_bg_opa(ui->upload_bar_cont, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(ui->upload_bar_cont, lv_color_hex(0x000000), 0);
    lv_obj_set_style_pad_hor(ui->upload_bar_cont, 16, 0);
    lv_obj_set_style_pad_ver(ui->upload_bar_cont, 0, 0);
    lv_obj_set_style_border_width(ui->upload_bar_cont, 0, 0);
    lv_obj_set_scrollbar_mode(ui->upload_bar_cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(ui->upload_bar_cont, LV_DIR_NONE);
    lv_obj_add_flag(ui->upload_bar_cont, LV_OBJ_FLAG_HIDDEN);

    ui->upload_bar = lv_bar_create(ui->upload_bar_cont);
    lv_obj_set_size(ui->upload_bar, 200, 8);
    lv_obj_align(ui->upload_bar, LV_ALIGN_LEFT_MID, 0, 0);
    lv_bar_set_range(ui->upload_bar, 0, 100);
    lv_bar_set_value(ui->upload_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(ui->upload_bar, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ui->upload_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(ui->upload_bar, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ui->upload_bar, lv_color_hex(0xFFA500), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(ui->upload_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(ui->upload_bar, 4, LV_PART_INDICATOR);

    ui->upload_pct_label = lv_label_create(ui->upload_bar_cont);
    lv_obj_remove_style_all(ui->upload_pct_label);
    lv_label_set_text(ui->upload_pct_label, "0%");
    lv_obj_set_style_text_font(ui->upload_pct_label, &AlibabaPuHuiTi3_Regular16, 0);
    lv_obj_set_style_text_color(ui->upload_pct_label, lv_color_hex(0xFFA500), 0);
    lv_obj_align(ui->upload_pct_label, LV_ALIGN_RIGHT_MID, 0, 0);
}

/**
 * @brief Refresh playback view with current record data and show the container
 * @return none
 */
static void __record_play_content_refresh(void)
{
    lv_record_ui_t *ui = &getContent()->st_func_record;
    char date_str[64];
    char time_str[16];

    snprintf(date_str, sizeof(date_str), "%04d-%02d-%02d %02d:%02d:%02d",
             s_play_create_time.tm_year, s_play_create_time.tm_mon, s_play_create_time.tm_mday,
             s_play_create_time.tm_hour, s_play_create_time.tm_min, s_play_create_time.tm_sec);
    lv_label_set_text(ui->play_date_label, date_str);

    lv_slider_set_range(ui->play_slider, 0, (s_play_duration > 0) ? (int32_t)s_play_duration : 1);
    lv_slider_set_value(ui->play_slider, 0, LV_ANIM_OFF);

    lv_label_set_text(ui->play_cur_time, "00:00");

    snprintf(time_str, sizeof(time_str), "%02u:%02u",
             (unsigned)(s_play_duration / 60), (unsigned)(s_play_duration % 60));
    lv_label_set_text(ui->play_total_time, time_str);

    if (s_record_res.play_pause_icon.data != NULL && ui->play_btn_icon != NULL) {
        lv_img_set_src(ui->play_btn_icon, &s_record_res.play_pause_icon);
    }

    lv_obj_clear_flag(ui->record_play_cont, LV_OBJ_FLAG_HIDDEN);
}

/**
 * @brief Create and initialize the record list screen
 * @return none
 */
void setup_record_list_scr(void)
{
    lv_record_ui_t *ui = &getContent()->st_func_record;
    s_record_list_ui_generation++;

    ui->record_list_scr = lv_obj_create(NULL);
    lv_obj_set_size(ui->record_list_scr, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(ui->record_list_scr, lv_color_hex(0x25262A), 0);
    lv_obj_set_style_pad_all(ui->record_list_scr, 0, 0);
    lv_obj_set_scrollbar_mode(ui->record_list_scr, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(ui->record_list_scr, LV_DIR_NONE);

    ui->record_list_title = lv_obj_create(ui->record_list_scr);
    lv_obj_remove_style_all(ui->record_list_title);
    lv_obj_set_size(ui->record_list_title, LV_HOR_RES, 50);
    lv_obj_set_pos(ui->record_list_title, 0, 0);
    lv_obj_set_style_bg_opa(ui->record_list_title, LV_OPA_TRANSP, 0);

    lv_obj_t *title_name = lv_label_create(ui->record_list_title);
    lv_label_set_text(title_name, "录音文件");
    lv_obj_set_style_text_font(title_name, &AlibabaPuHuiTi3_Regular18_Static, 0);
    lv_obj_set_style_text_color(title_name, lv_color_white(), 0);
    lv_obj_set_size(title_name, LV_SIZE_CONTENT, 20);
    lv_obj_align(title_name, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_align(title_name, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *back_btn = lv_btn_create(ui->record_list_title);
    lv_obj_remove_style_all(back_btn);
    lv_obj_set_size(back_btn, 50, 50);
    lv_obj_set_pos(back_btn, 0, 0);
    lv_obj_set_style_bg_opa(back_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_tag(back_btn, NULL);
    lv_obj_add_event_cb(back_btn, __record_list_back_event, LV_EVENT_CLICKED, NULL);

    if (png_img_load(tuya_app_gui_get_picture_full_path(ICON_BACK_LEFT_24_24), &s_record_res.back_icon) == 0) {
        lv_obj_t *back_icon = lv_img_create(back_btn);
        lv_img_set_src(back_icon, &s_record_res.back_icon);
        lv_obj_set_pos(back_icon, 13, 13);
        lv_obj_set_size(back_icon, 24, 24);
    }

    ui->record_list_cont = lv_obj_create(ui->record_list_scr);
    lv_obj_remove_style_all(ui->record_list_cont);
    lv_obj_set_size(ui->record_list_cont, LV_HOR_RES, LV_VER_RES - 50);
    lv_obj_set_pos(ui->record_list_cont, 0, 50);
    lv_obj_set_style_bg_opa(ui->record_list_cont, LV_OPA_TRANSP, 0);

    ui->record_play_cont = lv_obj_create(ui->record_list_scr);
    lv_obj_remove_style_all(ui->record_play_cont);
    lv_obj_set_size(ui->record_play_cont, LV_HOR_RES, LV_VER_RES - 50);
    lv_obj_set_pos(ui->record_play_cont, 0, 50);
    lv_obj_set_style_bg_opa(ui->record_play_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_scrollbar_mode(ui->record_play_cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(ui->record_play_cont, LV_DIR_NONE);
    lv_obj_set_style_pad_all(ui->record_play_cont, 0, 0);
    lv_obj_add_flag(ui->record_play_cont, LV_OBJ_FLAG_HIDDEN);
    __record_play_ui_create();
    __record_list_create();
    lv_obj_update_layout(ui->record_list_scr);
    setDeskUIIndex(DESKUI_INDEX_RECORD_LIST);
}

/**
 * @brief Release all resources held by the record list screen
 * @return none
 */
void record_list_scr_res_clear(void)
{
    lv_record_ui_t *ui = &getContent()->st_func_record;

    __record_upload_stop();
    __record_play_stop_playback();
    wukong_audio_player_stop(AI_PLAYER_BG);

    s_record_list_ui_generation++;
    s_play_record_id = -1;
    s_play_elapsed_ms = 0;

    png_img_unload(&s_record_res.back_icon);
    png_img_unload(&s_record_res.play_playing_icon);
    png_img_unload(&s_record_res.play_pause_icon);
    png_img_unload(&s_record_res.fast_forward_icon);
    png_img_unload(&s_record_res.fast_back_icon);
    png_img_unload(&s_record_res.delete_icon);
    png_img_unload(&s_record_res.expand_icon);
    png_img_unload(&s_record_res.ai_camera_icon);
    memset(&s_record_res, 0, sizeof(record_scr_res_t));

    ui->record_list_scr = NULL;
    ui->record_list_title = NULL;
    ui->record_list_cont = NULL;
    ui->record_play_cont = NULL;
    ui->play_date_label = NULL;
    ui->play_slider = NULL;
    ui->play_cur_time = NULL;
    ui->play_total_time = NULL;
    ui->play_btn_icon = NULL;
    ui->upload_bar_cont = NULL;
    ui->upload_bar = NULL;
    ui->upload_pct_label = NULL;
}

/**
 * @brief Create and initialize the record screen
 * @return none
 */
void setup_record_scr(void)
{
    lv_record_ui_t *ui = &getContent()->st_func_record;

    ui->record_scr = lv_obj_create(NULL);
    lv_obj_set_size(ui->record_scr, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(ui->record_scr, lv_color_hex(0x25262A), 0);
    lv_obj_set_style_pad_all(ui->record_scr, 0, 0);
    lv_obj_set_scrollbar_mode(ui->record_scr, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(ui->record_scr, LV_DIR_NONE);
    record_title_create();
    record_content_create();
    lv_obj_update_layout(ui->record_scr);

    s_ai_mode_before_record = tuya_ai_toy_device_mode_get();
    wukong_audio_player_stop(AI_PLAYER_ALL);
    wukong_ai_agent_chat_break(NULL);
    wukong_ai_device_mode_switch(AI_DEVICE_MODE_RECORD);
}

/**
 * @brief Release all resources held by the record screen
 * @return none
 */
void record_scr_res_clear(void)
{
    TAL_PR_INFO("[%s] enter.", __func__);

    wukong_audio_input_wakeup_set(FALSE);
    __record_file_close_and_save();

    if (s_record_timer != NULL) {
        lv_timer_del(s_record_timer);
        s_record_timer = NULL;
    }
    s_record_state = RECORD_STATE_DEFAULT;
    s_elapsed_seconds = 0;

    wukong_audio_player_stop(AI_PLAYER_ALL);
    wukong_ai_agent_chat_break(NULL);
    setup_scr_chat_mode_suppress();
    wukong_ai_device_mode_switch(s_ai_mode_before_record);

    png_img_unload(&s_record_res.back_icon);
    png_img_unload(&s_record_res.list_icon);
    png_img_unload(&s_record_res.record_default_icon);
    png_img_unload(&s_record_res.record_recording_icon);
    png_img_unload(&s_record_res.record_pause_icon);
    memset(&s_record_res, 0, sizeof(record_scr_res_t));
}
