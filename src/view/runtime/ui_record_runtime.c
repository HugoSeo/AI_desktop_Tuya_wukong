/**
 * @file ui_record_runtime.c
 * @brief Recording functional-layer implementation for the view UI runtime
 *
 * Mirrors the recording flow originally implemented in the desktop board
 * (desk_func_record.c) but rewritten in the view code style: dispatch
 * drives this runtime, the runtime owns the on-disk list / file handles
 * and bridges to the wukong audio / agent / mode subsystems. UI screens
 * (ui_record.c / ui_record_list.c) stay decoupled and observe state
 * changes through ui_record_list_replace_begin/push/commit() and
 * ui_record_list_set_upload_progress().
 *
 * @version 1.0
 * @date 2026-05-13
 * @copyright Copyright (c) Tuya Inc.
 */
#include "ui_record_runtime.h"
#include "ui_common.h"

#include "wukong_ai_mode.h"
#include "wukong_ai_agent.h"
#include "wukong_audio_player.h"
#include "wukong_audio_input.h"
#include "svc_ai_player.h"
#include "tuya_ai_protocol.h"
#include "tuya_ai_biz.h"
#include "tuya_ai_http.h"
#include "tuya_iot_internal_api.h"

#include "tal_memory.h"
#include "tal_mutex.h"
#include "tal_semaphore.h"
#include "tal_thread.h"
#include "tal_time_service.h"
#include "tal_system.h"
#include "tal_hash.h"
#include "tuya_list.h"
#include "tkl_fs.h"
#include "ty_cJSON.h"
#include "lvgl.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* External getter (no public header in current project layout) */
extern AI_DEVICE_MODE_E tuya_ai_toy_device_mode_get(VOID_T);

/* ---------------------------------------------------------------------------
 * Macros
 * --------------------------------------------------------------------------- */
#define REC_STORE_DIR            "/t5_fs/tmp/record"
#define REC_INFO_PATH            "/t5_fs/tmp/record/record_list.json"
#define REC_TRANSCRIBE_DIR       "/t5_fs/tmp/record/transcribe"

#define REC_ITEM_NUM_MAX         20
#define REC_NAME_MAX             64
#define REC_PATH_MAX             128
#define REC_FILENAME_MAX         48

#define REC_UPLOAD_CHUNK_SIZE    (6 * 1024)
#define REC_UPLOAD_TIMER_MS      20
#define REC_UPLOAD_PROMPT        "转写并总结下刚才上传的音频"

#define REC_MD5_HEX_LEN          32

#define REC_POLL_API             "m.wearable.audio.device.transcribe.result"
#define REC_POLL_API_VER         "2.0"
#define REC_POLL_INTERVAL_MS     5000
#define REC_POLL_FIRST_DELAY_MS  10000  //文件上传后延迟10秒请求
#define REC_DLD_TIMEOUT_MS       5000
#define REC_POLL_THREAD_STACK    (1024 * 20)
#define REC_POLL_POST_BUF_SIZE   1024

/* ---------------------------------------------------------------------------
 * Type definitions
 * --------------------------------------------------------------------------- */
typedef struct {
    INT_T        id;
    CHAR_T       name[REC_NAME_MAX];
    UINT64_T     len;
    UINT32_T     duration_sec;
    POSIX_TM_S   create_time;
    UINT8_T      md5[16];
    INT_T        transcribe_status;  /* -1/0/1/2 — see ADR-0002 */
    CHAR_T       transcribe_filename[REC_FILENAME_MAX];  /* basename, see ADR-0003 */
    CHAR_T       summary_filename[REC_FILENAME_MAX];     /* basename, see ADR-0003 */
    UINT32_T     poll_not_before_tick;  /* 上传 100% 后冷却到期 tick；in-memory only，重启回 0 表示立即可查 */
    LIST_HEAD    list_node;
} REC_ITEM_T;

typedef struct {
    UINT32_T     num;
    LIST_HEAD    head;
    MUTEX_HANDLE mutex;
    BOOL_T       inited;
    BOOL_T       loaded;
} REC_LIST_T;

typedef struct {
    TUYA_FILE    fp;
    UINT8_T     *read_buf;
    UINT64_T     file_len;
    UINT64_T     total_sent;
    INT_T        target_id;
} REC_UPLOAD_CTX_T;

typedef struct {
    TUYA_FILE    fp;
    SEM_HANDLE   done_sem;
    BOOL_T       received_end;
    BOOL_T       write_failed;
} REC_DLD_CTX_T;

typedef struct {
    INT_T   id;
    CHAR_T  md5_hex[REC_MD5_HEX_LEN + 1];   /* anti-ABA, see ADR-0003 */
    INT_T   new_status;
    CHAR_T  transcribe_filename[REC_FILENAME_MAX];
    CHAR_T  summary_filename[REC_FILENAME_MAX];
} REC_POLL_PLAN_T;

/* ---------------------------------------------------------------------------
 * File scope variables
 * --------------------------------------------------------------------------- */
STATIC REC_LIST_T          s_rec_list = {0};

STATIC TUYA_FILE           s_rec_fp = NULL;
STATIC CHAR_T              s_rec_cur_name[REC_NAME_MAX] = {0};
STATIC UINT64_T            s_rec_file_size = 0;
STATIC UINT16_T            s_rec_fallback_idx = 0;

STATIC TKL_HASH_HANDLE     s_rec_md5_ctx = NULL;
STATIC BOOL_T              s_rec_md5_failed = FALSE;

STATIC BOOL_T              s_rec_running = FALSE;
STATIC UINT32_T            s_rec_elapsed_ms = 0;
STATIC UINT32_T            s_rec_resume_tick = 0;

STATIC BOOL_T              s_rec_session_active = FALSE;
STATIC AI_DEVICE_MODE_E    s_rec_mode_before = AI_DEVICE_MODE_CHAT;

STATIC lv_timer_t         *s_upload_timer = NULL;
STATIC REC_UPLOAD_CTX_T    s_upload_ctx = {0};

STATIC THREAD_HANDLE       s_poll_thread = NULL;
STATIC REC_DLD_CTX_T       s_dld_ctx = {0};

/* ---------------------------------------------------------------------------
 * Forward declarations
 * --------------------------------------------------------------------------- */
STATIC OPERATE_RET __rec_list_init(VOID_T);
STATIC VOID_T      __rec_list_load(VOID_T);
STATIC VOID_T      __rec_list_save(VOID_T);
STATIC VOID_T      __rec_list_clear_locked(VOID_T);
STATIC INT_T       __rec_list_alloc_id_locked(VOID_T);
STATIC OPERATE_RET __rec_list_delete_by_id(INT_T id);

STATIC VOID_T      __rec_make_filename(CHAR_T *buf, UINT32_T size);
STATIC OPERATE_RET __rec_file_open(VOID_T);
STATIC VOID_T      __rec_file_close_and_save(VOID_T);

STATIC OPERATE_RET __rec_input_audio_cb(AI_AUDIO_CODEC_TYPE codec, VOID *data, INT_T len);
STATIC OPERATE_RET __rec_register_audio_cb(VOID_T);

STATIC VOID_T      __rec_upload_stop(VOID_T);
STATIC VOID_T      __rec_upload_timer_cb(lv_timer_t *timer);
STATIC OPERATE_RET __rec_set_transcribe_status(INT_T id, INT_T status);

STATIC VOID_T      __rec_compose_datetime_str(CONST POSIX_TM_S *tm, CHAR_T *buf, UINT32_T size);
STATIC VOID_T      __rec_accumulate_elapsed_locked(VOID_T);

STATIC VOID_T      __rec_md5_to_hex(CONST UINT8_T md5[16], CHAR_T hex[33]);
STATIC OPERATE_RET __rec_hex_to_md5(CONST CHAR_T *hex, UINT8_T md5[16]);
STATIC BOOL_T      __rec_md5_is_zero(CONST UINT8_T md5[16]);
STATIC VOID_T      __rec_md5_session_begin(VOID_T);
STATIC VOID_T      __rec_md5_session_update(CONST VOID *data, INT_T len);
STATIC VOID_T      __rec_md5_session_finalize(UINT8_T out_md5[16]);
STATIC VOID_T      __rec_md5_session_abort(VOID_T);

STATIC OPERATE_RET __rec_dld_recv_cb(AI_BIZ_ATTR_INFO_T *attr,
                                     AI_BIZ_HEAD_INFO_T *head,
                                     VOID *data, VOID *usr_data);
STATIC OPERATE_RET __rec_sync_dld_file(CONST CHAR_T *url,
                                       CONST CHAR_T *dst_path,
                                       UINT_T timeout_ms);
STATIC VOID        __rec_poll_thread(PVOID_T args);
STATIC VOID        __rec_poll_refresh_async(VOID *arg);

/* ---------------------------------------------------------------------------
 * Recording list: JSON persistence
 * --------------------------------------------------------------------------- */

/**
 * @brief Free every node in the list (caller must hold the list mutex)
 * @return none
 */
STATIC VOID_T __rec_list_clear_locked(VOID_T)
{
    LIST_HEAD *pos = NULL;
    LIST_HEAD *next = NULL;

    tuya_list_for_each_safe(pos, next, &s_rec_list.head) {
        REC_ITEM_T *rec = tuya_list_entry(pos, REC_ITEM_T, list_node);
        if (rec == NULL) {
            continue;
        }
        tuya_list_del(&rec->list_node);
        tal_free(rec);
    }
    s_rec_list.num = 0;
}

/**
 * @brief Read the on-disk JSON index into the in-memory list (lazy, once)
 * @return none
 */
STATIC VOID_T __rec_list_load(VOID_T)
{
    TUYA_FILE fp = NULL;
    ty_cJSON *root = NULL;
    ty_cJSON *arr = NULL;
    CHAR_T *buf = NULL;
    INT_T file_size = 0;
    INT_T i = 0;

    if (s_rec_list.inited == FALSE) {
        return;
    }

    tal_mutex_lock(s_rec_list.mutex);
    if (s_rec_list.loaded == TRUE) {
        tal_mutex_unlock(s_rec_list.mutex);
        return;
    }
    s_rec_list.loaded = TRUE;
    tal_mutex_unlock(s_rec_list.mutex);

    if (tkl_faccess(REC_INFO_PATH, 0) != 0) {
        return;
    }

    file_size = tkl_fgetsize(REC_INFO_PATH);
    if (file_size <= 0) {
        PR_WARN("record: invalid json size: %d", file_size);
        return;
    }

    buf = (CHAR_T *)tal_malloc(file_size + 1);
    if (buf == NULL) {
        PR_ERR("record: alloc json buffer failed");
        return;
    }
    memset(buf, 0, file_size + 1);

    fp = tkl_fopen(REC_INFO_PATH, "rb");
    if (fp == NULL) {
        PR_ERR("record: open json failed");
        goto __exit;
    }
    if (tkl_fread(buf, file_size, fp) != file_size) {
        PR_ERR("record: read json failed");
        goto __exit;
    }

    root = ty_cJSON_Parse(buf);
    if ((root == NULL) || (ty_cJSON_IsObject(root) == FALSE)) {
        PR_ERR("record: parse json failed");
        goto __exit;
    }

    arr = ty_cJSON_GetObjectItem(root, "list");
    if ((arr == NULL) || (ty_cJSON_IsArray(arr) == FALSE)) {
        PR_WARN("record: missing list array");
        goto __exit;
    }

    tal_mutex_lock(s_rec_list.mutex);
    __rec_list_clear_locked();

    for (i = 0; i < ty_cJSON_GetArraySize(arr) && i < REC_ITEM_NUM_MAX; i++) {
        ty_cJSON *item_json = ty_cJSON_GetArrayItem(arr, i);
        ty_cJSON *id_j = NULL;
        ty_cJSON *name_j = NULL;
        ty_cJSON *len_j = NULL;
        ty_cJSON *dur_j = NULL;
        ty_cJSON *y_j = NULL;
        ty_cJSON *mo_j = NULL;
        ty_cJSON *md_j = NULL;
        ty_cJSON *h_j = NULL;
        ty_cJSON *mi_j = NULL;
        ty_cJSON *s_j = NULL;
        ty_cJSON *md5_j = NULL;
        ty_cJSON *ts_j = NULL;
        ty_cJSON *tf_j = NULL;
        ty_cJSON *sf_j = NULL;
        REC_ITEM_T *item = NULL;

        if ((item_json == NULL) || (ty_cJSON_IsObject(item_json) == FALSE)) {
            continue;
        }

        item = (REC_ITEM_T *)tal_malloc(sizeof(REC_ITEM_T));
        if (item == NULL) {
            PR_ERR("record: alloc item failed");
            break;
        }
        memset(item, 0, sizeof(REC_ITEM_T));

        id_j   = ty_cJSON_GetObjectItem(item_json, "id");
        name_j = ty_cJSON_GetObjectItem(item_json, "name");
        len_j  = ty_cJSON_GetObjectItem(item_json, "len");
        dur_j  = ty_cJSON_GetObjectItem(item_json, "duration");
        y_j    = ty_cJSON_GetObjectItem(item_json, "year");
        mo_j   = ty_cJSON_GetObjectItem(item_json, "mon");
        md_j   = ty_cJSON_GetObjectItem(item_json, "mday");
        h_j    = ty_cJSON_GetObjectItem(item_json, "hour");
        mi_j   = ty_cJSON_GetObjectItem(item_json, "min");
        s_j    = ty_cJSON_GetObjectItem(item_json, "sec");
        md5_j  = ty_cJSON_GetObjectItem(item_json, "md5");
        ts_j   = ty_cJSON_GetObjectItem(item_json, "transcribe_status");
        tf_j   = ty_cJSON_GetObjectItem(item_json, "transcribe_filename");
        sf_j   = ty_cJSON_GetObjectItem(item_json, "summary_filename");

        item->id = (id_j != NULL && ty_cJSON_IsNumber(id_j)) ? id_j->valueint : i;
        if (name_j != NULL && ty_cJSON_GetStringValue(name_j) != NULL) {
            snprintf(item->name, sizeof(item->name), "%s", ty_cJSON_GetStringValue(name_j));
        }
        item->len          = (len_j != NULL && ty_cJSON_IsNumber(len_j)) ? (UINT64_T)len_j->valuedouble : 0;
        item->duration_sec = (dur_j != NULL && ty_cJSON_IsNumber(dur_j)) ? (UINT32_T)dur_j->valueint : 0;
        item->create_time.tm_year = (y_j  != NULL && ty_cJSON_IsNumber(y_j))  ? y_j->valueint  : 0;
        item->create_time.tm_mon  = (mo_j != NULL && ty_cJSON_IsNumber(mo_j)) ? mo_j->valueint : 0;
        item->create_time.tm_mday = (md_j != NULL && ty_cJSON_IsNumber(md_j)) ? md_j->valueint : 0;
        item->create_time.tm_hour = (h_j  != NULL && ty_cJSON_IsNumber(h_j))  ? h_j->valueint  : 0;
        item->create_time.tm_min  = (mi_j != NULL && ty_cJSON_IsNumber(mi_j)) ? mi_j->valueint : 0;
        item->create_time.tm_sec  = (s_j  != NULL && ty_cJSON_IsNumber(s_j))  ? s_j->valueint  : 0;

        /* md5 missing (legacy json) or malformed ⇒ leave the 16 zero bytes
         * already set by memset above; the entry will be treated as
         * "未生成" per ADR-0001 and won't gate local playback. */
        if (md5_j != NULL && ty_cJSON_GetStringValue(md5_j) != NULL) {
            __rec_hex_to_md5(ty_cJSON_GetStringValue(md5_j), item->md5);
        }

        /* transcribe_status missing (legacy json) ⇒ default to -1
         * 未上传, per ADR-0002. */
        item->transcribe_status =
            (ts_j != NULL && ty_cJSON_IsNumber(ts_j)) ? ts_j->valueint : -1;

        /* transcribe_filename / summary_filename missing (pre-ADR-0003 json)
         * ⇒ leave as empty strings; runtime will re-fetch when status flips
         * to 1 next time. */
        if (tf_j != NULL && ty_cJSON_GetStringValue(tf_j) != NULL) {
            snprintf(item->transcribe_filename, sizeof(item->transcribe_filename),
                     "%s", ty_cJSON_GetStringValue(tf_j));
        }
        if (sf_j != NULL && ty_cJSON_GetStringValue(sf_j) != NULL) {
            snprintf(item->summary_filename, sizeof(item->summary_filename),
                     "%s", ty_cJSON_GetStringValue(sf_j));
        }

        tuya_list_add_tail(&item->list_node, &s_rec_list.head);
        s_rec_list.num++;
    }
    tal_mutex_unlock(s_rec_list.mutex);

__exit:
    if (fp != NULL) {
        tkl_fclose(fp);
    }
    if (root != NULL) {
        ty_cJSON_Delete(root);
    }
    if (buf != NULL) {
        tal_free(buf);
    }
}

/**
 * @brief Serialize the in-memory list back to the on-disk JSON index
 * @return none
 * @note  Holds s_rec_list.mutex across the *entire* operation (cJSON dump
 *        + PrintUnformatted + fs IO). This is wider than strictly needed
 *        but is required to serialize concurrent writers — the LVGL thread
 *        (record stop / upload-100% transitions) and the poll thread (apply
 *        plans) can both reach this function. fopen("wb") truncates, so two
 *        unsynchronized writers would interleave bytes and corrupt the JSON.
 *        The wider critical section is intentional, see ADR-0003.
 */
STATIC VOID_T __rec_list_save(VOID_T)
{
    ty_cJSON *root = NULL;
    ty_cJSON *arr = NULL;
    LIST_HEAD *pos = NULL;
    CHAR_T *json_str = NULL;
    TUYA_FILE fp = NULL;
    INT_T write_size = 0;

    if (s_rec_list.inited == FALSE) {
        return;
    }

    root = ty_cJSON_CreateObject();
    if (root == NULL) {
        PR_ERR("record: create root json failed");
        return;
    }

    arr = ty_cJSON_CreateArray();
    if (arr == NULL) {
        PR_ERR("record: create list json failed");
        goto __exit;
    }

    tal_mutex_lock(s_rec_list.mutex);
    ty_cJSON_AddNumberToObject(root, "num", s_rec_list.num);
    ty_cJSON_AddItemToObject(root, "list", arr);
    arr = NULL;

    tuya_list_for_each(pos, &s_rec_list.head) {
        REC_ITEM_T *rec = tuya_list_entry(pos, REC_ITEM_T, list_node);
        ty_cJSON *item_json = NULL;
        CHAR_T md5_hex[33] = {0};

        if (rec == NULL) {
            continue;
        }

        item_json = ty_cJSON_CreateObject();
        if (item_json == NULL) {
            tal_mutex_unlock(s_rec_list.mutex);
            PR_ERR("record: create item json failed");
            goto __exit;
        }

        __rec_md5_to_hex(rec->md5, md5_hex);

        ty_cJSON_AddNumberToObject(item_json, "id",       rec->id);
        ty_cJSON_AddStringToObject(item_json, "name",     rec->name);
        ty_cJSON_AddNumberToObject(item_json, "len",      (double)rec->len);
        ty_cJSON_AddNumberToObject(item_json, "duration", (double)rec->duration_sec);
        ty_cJSON_AddNumberToObject(item_json, "year",     rec->create_time.tm_year);
        ty_cJSON_AddNumberToObject(item_json, "mon",      rec->create_time.tm_mon);
        ty_cJSON_AddNumberToObject(item_json, "mday",     rec->create_time.tm_mday);
        ty_cJSON_AddNumberToObject(item_json, "hour",     rec->create_time.tm_hour);
        ty_cJSON_AddNumberToObject(item_json, "min",      rec->create_time.tm_min);
        ty_cJSON_AddNumberToObject(item_json, "sec",      rec->create_time.tm_sec);
        ty_cJSON_AddStringToObject(item_json, "md5",      md5_hex);
        ty_cJSON_AddNumberToObject(item_json, "transcribe_status",
                                   rec->transcribe_status);
        ty_cJSON_AddStringToObject(item_json, "transcribe_filename",
                                   rec->transcribe_filename);
        ty_cJSON_AddStringToObject(item_json, "summary_filename",
                                   rec->summary_filename);
        ty_cJSON_AddItemToArray(ty_cJSON_GetObjectItem(root, "list"), item_json);
    }

    json_str = ty_cJSON_PrintUnformatted(root);
    if (json_str == NULL) {
        tal_mutex_unlock(s_rec_list.mutex);
        PR_ERR("record: print json failed");
        goto __exit;
    }

    tkl_fs_mkdir(REC_STORE_DIR);

    fp = tkl_fopen(REC_INFO_PATH, "wb");
    if (fp == NULL) {
        tal_mutex_unlock(s_rec_list.mutex);
        PR_ERR("record: open json write failed");
        goto __exit;
    }

    write_size = (INT_T)strlen(json_str);
    if (tkl_fwrite(json_str, write_size, fp) != write_size) {
        PR_ERR("record: write json failed");
    }
    tal_mutex_unlock(s_rec_list.mutex);

__exit:
    if (fp != NULL) {
        tkl_fclose(fp);
    }
    if (json_str != NULL) {
        ty_cJSON_FreeBuffer(json_str);
    }
    if (arr != NULL) {
        ty_cJSON_Delete(arr);
    }
    if (root != NULL) {
        ty_cJSON_Delete(root);
    }
}

/**
 * @brief Lazily initialize the in-memory list (mutex, head, JSON load)
 * @return OPRT_OK on success
 */
STATIC OPERATE_RET __rec_list_init(VOID_T)
{
    OPERATE_RET rt = OPRT_OK;

    if (s_rec_list.inited == TRUE) {
        return OPRT_OK;
    }

    INIT_LIST_HEAD(&s_rec_list.head);
    s_rec_list.num = 0;
    TUYA_CALL_ERR_RETURN(tal_mutex_create_init(&s_rec_list.mutex));
    s_rec_list.inited = TRUE;

    __rec_list_load();
    return rt;
}

/**
 * @brief Pick the lowest unused id in [0, REC_ITEM_NUM_MAX)
 * @return free id, or -1 when the list is full
 * @note Caller must hold the list mutex.
 */
STATIC INT_T __rec_list_alloc_id_locked(VOID_T)
{
    LIST_HEAD *pos = NULL;
    BOOL_T used[REC_ITEM_NUM_MAX] = {FALSE};
    INT_T id = 0;

    tuya_list_for_each(pos, &s_rec_list.head) {
        REC_ITEM_T *rec = tuya_list_entry(pos, REC_ITEM_T, list_node);
        if (rec == NULL) {
            continue;
        }
        if ((rec->id >= 0) && (rec->id < REC_ITEM_NUM_MAX)) {
            used[rec->id] = TRUE;
        }
    }

    for (id = 0; id < REC_ITEM_NUM_MAX; id++) {
        if (used[id] == FALSE) {
            return id;
        }
    }
    return -1;
}

/**
 * @brief Drop a recording entry by id and remove the underlying file
 * @param[in] id record id
 * @return OPRT_OK when removed, OPRT_COM_ERROR otherwise
 */
STATIC OPERATE_RET __rec_list_delete_by_id(INT_T id)
{
    LIST_HEAD *pos = NULL;
    LIST_HEAD *next = NULL;
    OPERATE_RET rt = OPRT_COM_ERROR;
    CHAR_T del_path[REC_PATH_MAX];

    if (__rec_list_init() != OPRT_OK) {
        return OPRT_COM_ERROR;
    }

    tal_mutex_lock(s_rec_list.mutex);
    tuya_list_for_each_safe(pos, next, &s_rec_list.head) {
        REC_ITEM_T *rec = tuya_list_entry(pos, REC_ITEM_T, list_node);
        if (rec == NULL) {
            continue;
        }
        if (rec->id != id) {
            continue;
        }

        snprintf(del_path, sizeof(del_path), "%s/%s", REC_STORE_DIR, rec->name);
        tkl_fs_remove(del_path);

        /* Per ADR-0003 §"并发安全补丁 — 隐患 2": when deleting a recording,
         * also clean up its transcribe / summary text files (if any) so the
         * /t5_fs/tmp/record/transcribe/ directory does not leak orphan files
         * across the device's lifetime. Reads happen *before* tal_free(rec). */
        if (rec->transcribe_filename[0] != '\0') {
            snprintf(del_path, sizeof(del_path), "%s/%s",
                     REC_TRANSCRIBE_DIR, rec->transcribe_filename);
            tkl_fs_remove(del_path);
        }
        if (rec->summary_filename[0] != '\0') {
            snprintf(del_path, sizeof(del_path), "%s/%s",
                     REC_TRANSCRIBE_DIR, rec->summary_filename);
            tkl_fs_remove(del_path);
        }

        tuya_list_del(&rec->list_node);
        tal_free(rec);
        if (s_rec_list.num > 0) {
            s_rec_list.num--;
        }
        rt = OPRT_OK;
        break;
    }
    tal_mutex_unlock(s_rec_list.mutex);

    if (rt == OPRT_OK) {
        __rec_list_save();
    }
    return rt;
}

/* ---------------------------------------------------------------------------
 * Recording file: open / write / close
 * --------------------------------------------------------------------------- */

/**
 * @brief Compose a deterministic OPUS filename from local time
 * @param[out] buf output buffer
 * @param[in] size output buffer size in bytes
 * @return none
 * @note Falls back to a rolling index when local time is unavailable so
 *       captures never collide silently.
 */
STATIC VOID_T __rec_make_filename(CHAR_T *buf, UINT32_T size)
{
    POSIX_TM_S tm;
    OPERATE_RET rt = OPRT_OK;

    memset(&tm, 0, sizeof(POSIX_TM_S));
    rt = tal_time_get_local_time_custom(0, &tm);
    if (rt == OPRT_OK) {
        snprintf(buf, size, "REC_%04d%02d%02d_%02d%02d%02d.opus",
                 tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                 tm.tm_hour, tm.tm_min, tm.tm_sec);
    } else {
        snprintf(buf, size, "REC_%04u.opus", (unsigned)s_rec_fallback_idx);
        s_rec_fallback_idx = (s_rec_fallback_idx + 1) % 10000;
    }
}

/**
 * @brief Open a fresh recording file under REC_STORE_DIR (OPUS-only)
 * @return OPRT_OK on success, OPRT_COM_ERROR otherwise
 * @note Aborts any pre-existing file/md5 session before opening the new
 *       one. Initializes a streaming MD5 context which __rec_input_audio_cb
 *       will feed in lock-step with each disk write — see ADR-0001.
 */
STATIC OPERATE_RET __rec_file_open(VOID_T)
{
    CHAR_T filepath[REC_PATH_MAX];

    if (s_rec_fp != NULL) {
        tkl_fclose(s_rec_fp);
        s_rec_fp = NULL;
        __rec_md5_session_abort();
    }

    tkl_fs_mkdir("/t5_fs/tmp");
    tkl_fs_mkdir(REC_STORE_DIR);

    __rec_make_filename(s_rec_cur_name, sizeof(s_rec_cur_name));
    snprintf(filepath, sizeof(filepath), "%s/%s", REC_STORE_DIR, s_rec_cur_name);

    s_rec_fp = tkl_fopen(filepath, "wb");
    if (s_rec_fp == NULL) {
        PR_ERR("record: open file failed");
        return OPRT_COM_ERROR;
    }

    s_rec_file_size = 0;
    __rec_md5_session_begin();

    PR_INFO("record: capture to %s (codec=opus)", s_rec_cur_name);
    return OPRT_OK;
}

/**
 * @brief Close the active capture file and persist a new list entry
 * @return none
 * @note When the captured payload is empty the file is dropped without
 *       creating a list entry. When the list reaches REC_ITEM_NUM_MAX
 *       the oldest entry is evicted (and its file removed) before adding
 *       the new one. The persisted duration comes from s_rec_elapsed_ms.
 *       The streaming MD5 context (fed by __rec_input_audio_cb) is
 *       finalized here on success or aborted on early-exit paths.
 */
STATIC VOID_T __rec_file_close_and_save(VOID_T)
{
    REC_ITEM_T *item = NULL;
    REC_ITEM_T *oldest = NULL;
    POSIX_TM_S tm;
    CHAR_T del_path[REC_PATH_MAX];
    UINT8_T md5_buf[16] = {0};
    INT_T id = -1;

    if (s_rec_fp != NULL) {
        tkl_fclose(s_rec_fp);
        s_rec_fp = NULL;
    }

    if (s_rec_file_size == 0 || s_rec_cur_name[0] == '\0') {
        __rec_md5_session_abort();
        s_rec_cur_name[0] = '\0';
        s_rec_file_size = 0;
        return;
    }

    if (__rec_list_init() != OPRT_OK) {
        __rec_md5_session_abort();
        s_rec_cur_name[0] = '\0';
        s_rec_file_size = 0;
        return;
    }

    /* Pull the streaming-accumulated digest out of the active session.
     * On any prior update failure (sticky) or finalize failure md5_buf
     * stays zeroed; the entry is still kept (per ADR-0001) so local
     * playback works, but cloud transcription lookup is impossible for
     * this recording. */
    __rec_md5_session_finalize(md5_buf);

    item = (REC_ITEM_T *)tal_malloc(sizeof(REC_ITEM_T));
    if (item == NULL) {
        PR_ERR("record: alloc item failed");
        s_rec_cur_name[0] = '\0';
        s_rec_file_size = 0;
        return;
    }
    memset(item, 0, sizeof(REC_ITEM_T));
    memset(&tm, 0, sizeof(POSIX_TM_S));

    tal_mutex_lock(s_rec_list.mutex);

    if (s_rec_list.num >= REC_ITEM_NUM_MAX && !tuya_list_empty(&s_rec_list.head)) {
        oldest = tuya_list_entry(s_rec_list.head.next, REC_ITEM_T, list_node);
        if (oldest != NULL) {
            snprintf(del_path, sizeof(del_path), "%s/%s", REC_STORE_DIR, oldest->name);
            tkl_fs_remove(del_path);
            /* ADR-0003 §"并发安全补丁 — 隐患 2": evicting the oldest entry
             * must also drop its transcribe / summary files, otherwise the
             * transcribe/ dir leaks orphans every time the list rotates. */
            if (oldest->transcribe_filename[0] != '\0') {
                snprintf(del_path, sizeof(del_path), "%s/%s",
                         REC_TRANSCRIBE_DIR, oldest->transcribe_filename);
                tkl_fs_remove(del_path);
            }
            if (oldest->summary_filename[0] != '\0') {
                snprintf(del_path, sizeof(del_path), "%s/%s",
                         REC_TRANSCRIBE_DIR, oldest->summary_filename);
                tkl_fs_remove(del_path);
            }
            tuya_list_del(&oldest->list_node);
            tal_free(oldest);
            s_rec_list.num--;
        }
    }

    id = __rec_list_alloc_id_locked();
    if (id < 0) {
        tal_mutex_unlock(s_rec_list.mutex);
        tal_free(item);
        s_rec_cur_name[0] = '\0';
        s_rec_file_size = 0;
        return;
    }

    item->id = id;
    snprintf(item->name, sizeof(item->name), "%s", s_rec_cur_name);
    item->len          = s_rec_file_size;
    item->duration_sec = s_rec_elapsed_ms / 1000;
    memcpy(item->md5, md5_buf, sizeof(item->md5));
    item->transcribe_status = -1;  /* 未上传 — see ADR-0002 */

    tal_time_get_local_time_custom(0, &tm);
    item->create_time.tm_year = tm.tm_year + 1900;
    item->create_time.tm_mon  = tm.tm_mon + 1;
    item->create_time.tm_mday = tm.tm_mday;
    item->create_time.tm_hour = tm.tm_hour;
    item->create_time.tm_min  = tm.tm_min;
    item->create_time.tm_sec  = tm.tm_sec;

    tuya_list_add_tail(&item->list_node, &s_rec_list.head);
    s_rec_list.num++;

    tal_mutex_unlock(s_rec_list.mutex);

    __rec_list_save();

    {
        CHAR_T md5_hex[33] = {0};
        __rec_md5_to_hex(item->md5, md5_hex);
        PR_INFO("record: saved %s (size=%lu, dur=%us, md5=%s)",
                s_rec_cur_name, (unsigned long)s_rec_file_size,
                (unsigned)item->duration_sec, md5_hex);
    }

    s_rec_cur_name[0] = '\0';
    s_rec_file_size = 0;
}

/* ---------------------------------------------------------------------------
 * MD5 helpers — see docs/adr/0001-recording-md5-byte-range.md
 * --------------------------------------------------------------------------- */

/**
 * @brief Render a 16-byte MD5 digest as 32 lowercase hex chars + NUL
 */
STATIC VOID_T __rec_md5_to_hex(CONST UINT8_T md5[16], CHAR_T hex[33])
{
    INT_T i = 0;
    for (i = 0; i < 16; i++) {
        snprintf(hex + i * 2, 3, "%02x", md5[i]);
    }
    hex[REC_MD5_HEX_LEN] = '\0';
}

/**
 * @brief Parse a 32-char hex string into a 16-byte MD5 digest
 * @return OPRT_OK iff exactly 32 valid hex chars; on any failure the
 *         caller-supplied md5 buffer is left untouched (caller is
 *         expected to pre-zero it so a parse failure ⇒ "未生成").
 */
STATIC OPERATE_RET __rec_hex_to_md5(CONST CHAR_T *hex, UINT8_T md5[16])
{
    INT_T i = 0;
    INT_T hi = 0;
    INT_T lo = 0;
    CHAR_T c = 0;

    if (hex == NULL || strlen(hex) != REC_MD5_HEX_LEN) {
        return OPRT_INVALID_PARM;
    }
    for (i = 0; i < 16; i++) {
        c = hex[i * 2];
        if (c >= '0' && c <= '9') {
            hi = c - '0';
        } else if (c >= 'a' && c <= 'f') {
            hi = c - 'a' + 10;
        } else if (c >= 'A' && c <= 'F') {
            hi = c - 'A' + 10;
        } else {
            return OPRT_INVALID_PARM;
        }
        c = hex[i * 2 + 1];
        if (c >= '0' && c <= '9') {
            lo = c - '0';
        } else if (c >= 'a' && c <= 'f') {
            lo = c - 'a' + 10;
        } else if (c >= 'A' && c <= 'F') {
            lo = c - 'A' + 10;
        } else {
            return OPRT_INVALID_PARM;
        }
        md5[i] = (UINT8_T)((hi << 4) | lo);
    }
    return OPRT_OK;
}

/**
 * @brief Check whether a 16-byte md5 buffer is all-zero
 * @return TRUE iff every byte is 0; used as the "未生成 / 无法关联云端转写"
 *         predicate per ADR-0001 (hash failure leaves the digest at 16
 *         zero bytes; legacy JSON without an md5 field also lands here).
 */
STATIC BOOL_T __rec_md5_is_zero(CONST UINT8_T md5[16])
{
    INT_T i = 0;
    if (md5 == NULL) {
        return TRUE;
    }
    for (i = 0; i < 16; i++) {
        if (md5[i] != 0) {
            return FALSE;
        }
    }
    return TRUE;
}

/**
 * @brief Begin a streaming MD5 session bound to the active capture file
 *
 * Called by __rec_file_open after a fresh file is created. Subsequent
 * __rec_input_audio_cb invocations feed bytes to this context in lock-step
 * with the disk write; __rec_file_close_and_save (success path) calls
 * finalize. Failure modes during begin set the sticky-fail flag so update
 * becomes a no-op and finalize returns the all-zero digest.
 */
STATIC VOID_T __rec_md5_session_begin(VOID_T)
{
    OPERATE_RET rt = OPRT_OK;

    if (s_rec_md5_ctx != NULL) {
        /* Stale context from an aborted prior session — clear it first */
        tal_md5_free(s_rec_md5_ctx);
        s_rec_md5_ctx = NULL;
    }
    s_rec_md5_failed = FALSE;

    rt = tal_md5_create_init(&s_rec_md5_ctx);
    if (rt != OPRT_OK) {
        PR_ERR("record md5: create_init failed: %d", rt);
        s_rec_md5_ctx = NULL;
        s_rec_md5_failed = TRUE;
        return;
    }
    rt = tal_md5_starts_ret(s_rec_md5_ctx);
    if (rt != OPRT_OK) {
        PR_ERR("record md5: starts_ret failed: %d", rt);
        tal_md5_free(s_rec_md5_ctx);
        s_rec_md5_ctx = NULL;
        s_rec_md5_failed = TRUE;
        return;
    }
}

/**
 * @brief Feed one audio chunk to the active streaming MD5 session
 * @note  No-op when the session never started or has already failed; the
 *        sticky-fail flag ensures one bad chunk poisons only this session,
 *        not subsequent recordings.
 */
STATIC VOID_T __rec_md5_session_update(CONST VOID *data, INT_T len)
{
    OPERATE_RET rt = OPRT_OK;

    if (s_rec_md5_ctx == NULL || s_rec_md5_failed == TRUE) {
        return;
    }
    if (data == NULL || len <= 0) {
        return;
    }

    rt = tal_md5_update_ret(s_rec_md5_ctx, (CONST UINT8_T *)data, (size_t)len);
    if (rt != OPRT_OK) {
        PR_ERR("record md5: update_ret failed: %d (len=%d)", rt, len);
        s_rec_md5_failed = TRUE;
    }
}

/**
 * @brief Finalize and tear down the streaming MD5 session
 * @param[out] out_md5 16-byte digest; zeroed on any failure so callers can
 *                     keep the entry as "未生成" without an extra branch.
 */
STATIC VOID_T __rec_md5_session_finalize(UINT8_T out_md5[16])
{
    OPERATE_RET rt = OPRT_OK;

    if (out_md5 == NULL) {
        __rec_md5_session_abort();
        return;
    }
    memset(out_md5, 0, 16);

    if (s_rec_md5_ctx == NULL) {
        return;
    }
    if (s_rec_md5_failed == TRUE) {
        PR_ERR("record md5: session already failed — entry kept with zero digest");
        tal_md5_free(s_rec_md5_ctx);
        s_rec_md5_ctx = NULL;
        s_rec_md5_failed = FALSE;
        return;
    }

    rt = tal_md5_finish_ret(s_rec_md5_ctx, out_md5);
    if (rt != OPRT_OK) {
        PR_ERR("record md5: finish_ret failed: %d — entry kept with zero digest", rt);
        memset(out_md5, 0, 16);
    }

    tal_md5_free(s_rec_md5_ctx);
    s_rec_md5_ctx = NULL;
    s_rec_md5_failed = FALSE;
}

/**
 * @brief Tear down the streaming MD5 session without producing a digest
 * @note  Used on early-exit paths (empty capture, list init failure, file
 *        re-open mid-session) where no list entry will be created.
 */
STATIC VOID_T __rec_md5_session_abort(VOID_T)
{
    if (s_rec_md5_ctx != NULL) {
        tal_md5_free(s_rec_md5_ctx);
        s_rec_md5_ctx = NULL;
    }
    s_rec_md5_failed = FALSE;
}

/* ---------------------------------------------------------------------------
 * AI audio input callback registration
 * --------------------------------------------------------------------------- */

/**
 * @brief Audio-input callback invoked by the AI record mode (OPUS-only)
 * @param[in] codec codec the data buffer is encoded in; must be
 *                  AUDIO_CODEC_OPUS — anything else is rejected with
 *                  PR_ERR (the recording feature has been narrowed to
 *                  OPUS, see ADR-0001).
 * @param[in] data pointer to encoded audio data
 * @param[in] len length of the audio data in bytes
 * @return OPRT_OK on success, error code otherwise
 * @note Silently drops packets when the runtime is paused / stopped so
 *       the underlying file pointer never receives stray writes. On the
 *       happy path, every byte that hits disk is also fed to the active
 *       streaming MD5 session in lock-step (see __rec_md5_session_update).
 */
STATIC OPERATE_RET __rec_input_audio_cb(AI_AUDIO_CODEC_TYPE codec, VOID *data, INT_T len)
{
    INT_T written = 0;

    if (codec != AUDIO_CODEC_OPUS) {
        PR_ERR("record: non-OPUS codec %u rejected (recording is OPUS-only)",
               (unsigned)codec);
        return OPRT_NOT_SUPPORTED;
    }
    if (data == NULL || len <= 0) {
        return OPRT_OK;
    }
    if (s_rec_running == FALSE) {
        return OPRT_OK;
    }

    if (s_rec_fp == NULL) {
        if (__rec_file_open() != OPRT_OK) {
            return OPRT_COM_ERROR;
        }
    }

    written = tkl_fwrite(data, len, s_rec_fp);
    if (written != len) {
        PR_ERR("record: write audio failed (expect=%d, got=%d)", len, written);
        return OPRT_COM_ERROR;
    }

    /* Stream the same bytes into the active MD5 session — order matches
     * disk so md5(stored bytes) == md5(file). */
    __rec_md5_session_update(data, len);

    s_rec_file_size += (UINT64_T)len;
    return OPRT_OK;
}

/**
 * @brief Register the runtime's audio callback with the AI mode layer
 * @return OPRT_OK on success
 */
STATIC OPERATE_RET __rec_register_audio_cb(VOID_T)
{
    AI_RECORD_HANDLE_T handle = {0};
    handle.input_audio = __rec_input_audio_cb;
    return wukong_ai_record_handle_set(&handle);
}

/* ---------------------------------------------------------------------------
 * Helpers: datetime formatting and elapsed accumulator
 * --------------------------------------------------------------------------- */

/**
 * @brief Render a POSIX_TM_S into "YYYY-MM-DD HH:MM:SS" for the UI list
 * @param[in] tm time-of-day struct populated by tal_time_get_local_time_custom
 * @param[out] buf destination buffer
 * @param[in] size destination buffer size in bytes
 * @return none
 */
STATIC VOID_T __rec_compose_datetime_str(CONST POSIX_TM_S *tm, CHAR_T *buf, UINT32_T size)
{
    if (tm == NULL || buf == NULL || size == 0) {
        return;
    }
    snprintf(buf, size, "%04d-%02d-%02d %02d:%02d:%02d",
             tm->tm_year, tm->tm_mon, tm->tm_mday,
             tm->tm_hour, tm->tm_min, tm->tm_sec);
}

/**
 * @brief Fold the active capture window into s_rec_elapsed_ms
 * @return none
 * @note Caller is responsible for ensuring s_rec_running is currently TRUE.
 *       Uses tal_system_get_tick_count() which wraps after ~49 days; the
 *       unsigned subtraction yields the correct delta across a single wrap.
 */
STATIC VOID_T __rec_accumulate_elapsed_locked(VOID_T)
{
    UINT32_T now = (UINT32_T)tal_system_get_tick_count();
    s_rec_elapsed_ms += (now - s_rec_resume_tick);
    s_rec_resume_tick = now;
}

/* ---------------------------------------------------------------------------
 * Upload pipeline (chunked send to AI agent)
 * --------------------------------------------------------------------------- */

/**
 * @brief Mutate the persisted transcribe_status of one entry by id
 * @param[in] id record id
 * @param[in] status new value (-1/0/1/2)
 * @return OPRT_OK if the entry exists and the status was actually changed
 *         (and the json was rewritten); OPRT_COM_ERROR if the id does not
 *         exist; OPRT_OK with no-save when the value is already up-to-date.
 * @note Acquires the list mutex internally; do NOT call while holding it.
 *       __rec_list_save acquires the same mutex on its own. See ADR-0002.
 */
STATIC OPERATE_RET __rec_set_transcribe_status(INT_T id, INT_T status)
{
    LIST_HEAD *pos = NULL;
    BOOL_T found = FALSE;
    BOOL_T changed = FALSE;

    if (__rec_list_init() != OPRT_OK) {
        return OPRT_COM_ERROR;
    }

    tal_mutex_lock(s_rec_list.mutex);
    tuya_list_for_each(pos, &s_rec_list.head) {
        REC_ITEM_T *rec = tuya_list_entry(pos, REC_ITEM_T, list_node);
        if (rec == NULL || rec->id != id) {
            continue;
        }
        found = TRUE;
        if (rec->transcribe_status != status) {
            rec->transcribe_status = status;
            changed = TRUE;
        }
        break;
    }
    tal_mutex_unlock(s_rec_list.mutex);

    if (found == FALSE) {
        return OPRT_COM_ERROR;
    }
    if (changed == TRUE) {
        __rec_list_save();
    }
    return OPRT_OK;
}

/**
 * @brief Tear down the upload timer and release the read buffer / file
 * @return none
 */
STATIC VOID_T __rec_upload_stop(VOID_T)
{
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

    ui_record_list_set_upload_progress(0);
}

/**
 * @brief LVGL timer that pumps one chunk per tick into the AI agent
 * @param[in] timer LVGL timer handle (unused, state lives in s_upload_ctx)
 * @return none
 */
STATIC VOID_T __rec_upload_timer_cb(lv_timer_t *timer)
{
    INT_T read_len = 0;
    OPERATE_RET rt = OPRT_OK;
    UINT8_T pct = 0;

    (VOID_T)timer;

    if (s_upload_ctx.fp == NULL || s_upload_ctx.read_buf == NULL) {
        __rec_upload_stop();
        wukong_ai_agent_input_stop();
        return;
    }

    read_len = tkl_fread(s_upload_ctx.read_buf, REC_UPLOAD_CHUNK_SIZE, s_upload_ctx.fp);
    if (read_len > 0) {
        rt = wukong_ai_agent_send_file(s_upload_ctx.read_buf, (UINT_T)read_len);
        if (rt != OPRT_OK) {
            PR_ERR("record upload: send failed ret=%d sent=%lu/%lu",
                   rt,
                   (unsigned long)s_upload_ctx.total_sent,
                   (unsigned long)s_upload_ctx.file_len);
            __rec_upload_stop();
            wukong_ai_agent_input_stop();
            return;
        }
        s_upload_ctx.total_sent += (UINT64_T)read_len;

        if (s_upload_ctx.file_len > 0) {
            UINT64_T raw = s_upload_ctx.total_sent * 100 / s_upload_ctx.file_len;
            pct = (raw > 100) ? 100 : (UINT8_T)raw;
        }
        ui_record_list_set_upload_progress(pct);
        return;
    }

    ui_record_list_set_upload_progress(100);
    PR_INFO("record upload: complete, total=%lu", (unsigned long)s_upload_ctx.total_sent);

    /* -1 → 0 边沿：上传 100% 视作"已上传"，转写状态进入"处理中"并落盘
     * （ADR-0002）。先 set + save 再触发两路 UI 刷新，避免 UI 读到旧值。
     * 0 → 1/2 接入点 TODO（云端转写回执到达时迁移）。 */
    {
        INT_T target_id = s_upload_ctx.target_id;
        if (__rec_set_transcribe_status(target_id, 0) == OPRT_OK) {
            LIST_HEAD *pos = NULL;
            tal_mutex_lock(s_rec_list.mutex);
            tuya_list_for_each(pos, &s_rec_list.head) {
                REC_ITEM_T *rec = tuya_list_entry(pos, REC_ITEM_T, list_node);
                if (rec != NULL && rec->id == target_id) {
                    rec->poll_not_before_tick =
                        (UINT32_T)tal_system_get_tick_count() + REC_POLL_FIRST_DELAY_MS;
                    break;
                }
            }
            tal_mutex_unlock(s_rec_list.mutex);
            ui_record_list_set_play_status(0);
            ui_record_runtime_refresh_ui_list();
        }
    }

    __rec_upload_stop();
    wukong_ai_agent_send_text(REC_UPLOAD_PROMPT);
    wukong_ai_agent_input_stop();
}

/* ---------------------------------------------------------------------------
 * Public API: session lifecycle
 * --------------------------------------------------------------------------- */

/**
 * @brief Enter recording mode and prepare the runtime for a new session
 * @return OPRT_OK on success
 */
OPERATE_RET ui_record_runtime_open(VOID_T)
{
    OPERATE_RET rt = OPRT_OK;

    TUYA_CALL_ERR_RETURN(__rec_list_init());
    TUYA_CALL_ERR_LOG(__rec_register_audio_cb());

    if (s_rec_session_active == FALSE) {
        s_rec_mode_before    = tuya_ai_toy_device_mode_get();
        s_rec_session_active = TRUE;
    }

    wukong_audio_player_stop(AI_PLAYER_ALL);
    wukong_ai_agent_chat_break(NULL);
    rt = wukong_ai_device_mode_switch(AI_DEVICE_MODE_RECORD);

    s_rec_running     = FALSE;
    s_rec_elapsed_ms  = 0;
    s_rec_resume_tick = 0;

    PR_INFO("record: open (mode_before=%d)", (int)s_rec_mode_before);
    return rt;
}

/**
 * @brief Leave recording mode and release per-session resources
 * @return none
 */
VOID_T ui_record_runtime_close(VOID_T)
{
    PR_INFO("record: close");

    s_rec_running = FALSE;
    wukong_audio_input_wakeup_set(FALSE);

    __rec_upload_stop();
    __rec_file_close_and_save();

    wukong_audio_player_stop(AI_PLAYER_ALL);
    wukong_ai_agent_chat_break(NULL);

    if (s_rec_session_active == TRUE) {
        wukong_ai_device_mode_switch(s_rec_mode_before);
        s_rec_session_active = FALSE;
    }

    s_rec_elapsed_ms  = 0;
    s_rec_resume_tick = 0;
}

/* ---------------------------------------------------------------------------
 * Public API: capture control
 * --------------------------------------------------------------------------- */

/**
 * @brief Begin recording: reset elapsed counter and enable the audio input
 * @return OPRT_OK on success
 */
OPERATE_RET ui_record_runtime_start(VOID_T)
{
    PR_DEBUG("record: start");

    s_rec_elapsed_ms  = 0;
    s_rec_resume_tick = (UINT32_T)tal_system_get_tick_count();
    s_rec_running     = TRUE;
    return wukong_audio_input_wakeup_set(TRUE);
}

/**
 * @brief Pause an in-progress recording without closing the file
 * @return none
 */
VOID_T ui_record_runtime_pause(VOID_T)
{
    PR_DEBUG("record: pause");

    if (s_rec_running == TRUE) {
        __rec_accumulate_elapsed_locked();
        s_rec_running = FALSE;
    }
    wukong_audio_input_wakeup_set(FALSE);
}

/**
 * @brief Resume a paused recording, re-enabling the audio input
 * @return none
 */
VOID_T ui_record_runtime_resume(VOID_T)
{
    PR_DEBUG("record: resume");

    s_rec_resume_tick = (UINT32_T)tal_system_get_tick_count();
    s_rec_running     = TRUE;
    wukong_audio_input_wakeup_set(TRUE);
}

/**
 * @brief Stop the current recording, close the file and persist the entry
 * @return none
 */
VOID_T ui_record_runtime_stop(VOID_T)
{
    PR_DEBUG("record: stop");

    if (s_rec_running == TRUE) {
        __rec_accumulate_elapsed_locked();
        s_rec_running = FALSE;
    }
    wukong_audio_input_wakeup_set(FALSE);

    __rec_file_close_and_save();

    s_rec_elapsed_ms  = 0;
    s_rec_resume_tick = 0;
}

/* ---------------------------------------------------------------------------
 * Public API: list / playback / upload / delete
 * --------------------------------------------------------------------------- */

/**
 * @brief Read the persisted recording list and push it to the UI screen
 * @return none
 */
VOID_T ui_record_runtime_refresh_ui_list(VOID_T)
{
    LIST_HEAD *pos = NULL;
    UI_RECORD_ITEM_T item;
    UINT32_T n = 0;

    if (__rec_list_init() != OPRT_OK) {
        ui_record_list_replace_begin();
        ui_record_list_replace_commit();
        return;
    }

    ui_record_list_replace_begin();

    tal_mutex_lock(s_rec_list.mutex);
    tuya_list_for_each(pos, &s_rec_list.head) {
        REC_ITEM_T *rec = tuya_list_entry(pos, REC_ITEM_T, list_node);
        if (rec == NULL) {
            continue;
        }
        if (n >= REC_ITEM_NUM_MAX) {
            break;
        }

        memset(&item, 0, sizeof(item));
        item.id = rec->id;
        snprintf(item.name, sizeof(item.name), "%s", "录音");
        __rec_compose_datetime_str(&rec->create_time, item.datetime_str,
                                   sizeof(item.datetime_str));
        item.duration_sec      = rec->duration_sec;
        item.transcribe_status = rec->transcribe_status;
        item.md5_unavailable   = __rec_md5_is_zero(rec->md5);

        if (ui_record_list_replace_push(&item) != OPRT_OK) {
            break;
        }
        n++;
    }
    tal_mutex_unlock(s_rec_list.mutex);

    ui_record_list_replace_commit();
}

/**
 * @brief Delete a recording entry by id (removes the file too)
 * @param[in] id record id
 * @return OPRT_OK on success
 */
OPERATE_RET ui_record_runtime_delete(INT_T id)
{
    PR_DEBUG("record: delete id=%d", id);
    return __rec_list_delete_by_id(id);
}

/**
 * @brief Start local playback of a recording entry by id
 * @param[in] id record id
 * @return OPRT_OK on success
 */
OPERATE_RET ui_record_runtime_play(INT_T id)
{
    LIST_HEAD *pos = NULL;
    CHAR_T name[REC_NAME_MAX] = {0};
    CHAR_T filepath[REC_PATH_MAX] = {0};
    BOOL_T found = FALSE;

    if (__rec_list_init() != OPRT_OK) {
        return OPRT_COM_ERROR;
    }

    tal_mutex_lock(s_rec_list.mutex);
    tuya_list_for_each(pos, &s_rec_list.head) {
        REC_ITEM_T *rec = tuya_list_entry(pos, REC_ITEM_T, list_node);
        if (rec == NULL || rec->id != id) {
            continue;
        }
        snprintf(name, sizeof(name), "%s", rec->name);
        found = TRUE;
        break;
    }
    tal_mutex_unlock(s_rec_list.mutex);

    if (found == FALSE) {
        PR_WARN("record play: id=%d not found", id);
        return OPRT_COM_ERROR;
    }

    snprintf(filepath, sizeof(filepath), "%s/%s", REC_STORE_DIR, name);

    PR_INFO("record play: %s codec=opus", filepath);
    wukong_audio_player_stop(AI_PLAYER_BG);
    return wukong_audio_play_local(filepath, "录音", NULL, AI_AUDIO_CODEC_OPUS, 0);
}

/**
 * @brief Pause the current playback without releasing decoder context
 * @return OPRT_OK on success, error code otherwise
 */
OPERATE_RET ui_record_runtime_play_pause(VOID_T)
{
    PR_DEBUG("record play: pause");
    return wukong_audio_player_pause();
}

/**
 * @brief Resume the playback suspended by ui_record_runtime_play_pause()
 * @return OPRT_OK on success, error code otherwise
 */
OPERATE_RET ui_record_runtime_play_resume(VOID_T)
{
    PR_DEBUG("record play: resume");
    return wukong_audio_player_resume();
}

/**
 * @brief Stop the current playback and release the BG audio player
 * @return OPRT_OK on success, error code otherwise
 */
OPERATE_RET ui_record_runtime_play_stop(VOID_T)
{
    PR_DEBUG("record play: stop");
    return wukong_audio_player_stop(AI_PLAYER_BG);
}

/**
 * @brief Upload a recording entry to the AI agent in fixed-size chunks
 * @param[in] id record id
 * @return OPRT_OK when the upload pipeline starts successfully
 */
OPERATE_RET ui_record_runtime_upload(INT_T id)
{
    LIST_HEAD *pos = NULL;
    CHAR_T name[REC_NAME_MAX] = {0};
    CHAR_T filepath[REC_PATH_MAX] = {0};
    UINT64_T file_len = 0;
    BOOL_T md5_unavailable = TRUE;
    TUYA_FILE fp = NULL;
    UINT8_T *read_buf = NULL;

    if (s_upload_timer != NULL) {
        PR_WARN("record upload: already in progress");
        return OPRT_COM_ERROR;
    }

    if (__rec_list_init() != OPRT_OK) {
        return OPRT_COM_ERROR;
    }

    tal_mutex_lock(s_rec_list.mutex);
    tuya_list_for_each(pos, &s_rec_list.head) {
        REC_ITEM_T *rec = tuya_list_entry(pos, REC_ITEM_T, list_node);
        if (rec == NULL || rec->id != id) {
            continue;
        }
        snprintf(name, sizeof(name), "%s", rec->name);
        file_len = rec->len;
        md5_unavailable = __rec_md5_is_zero(rec->md5);
        break;
    }
    tal_mutex_unlock(s_rec_list.mutex);

    if (name[0] == '\0') {
        PR_WARN("record upload: id=%d not found", id);
        return OPRT_COM_ERROR;
    }

    /* md5 全零 ⇒ 永远无法关联云端转写（ADR-0001 / ADR-0002）。runtime
     * 层兜底拒绝；UI 层应已根据 md5_unavailable 把上传按钮 disable，
     * 这里走到说明绕过了 UI 校验。 */
    if (md5_unavailable == TRUE) {
        PR_WARN("record upload: id=%d md5 unavailable, refuse upload", id);
        return OPRT_NOT_SUPPORTED;
    }

    snprintf(filepath, sizeof(filepath), "%s/%s", REC_STORE_DIR, name);
    PR_INFO("record upload: %s size=%lu", filepath, (unsigned long)file_len);

    fp = tkl_fopen(filepath, "rb");
    if (fp == NULL) {
        PR_ERR("record upload: open failed");
        return OPRT_COM_ERROR;
    }

    read_buf = (UINT8_T *)tal_malloc(REC_UPLOAD_CHUNK_SIZE);
    if (read_buf == NULL) {
        PR_ERR("record upload: alloc read buf failed");
        tkl_fclose(fp);
        return OPRT_MALLOC_FAILED;
    }

    memset(&s_upload_ctx, 0, sizeof(s_upload_ctx));
    s_upload_ctx.fp         = fp;
    s_upload_ctx.read_buf   = read_buf;
    s_upload_ctx.file_len   = file_len;
    s_upload_ctx.total_sent = 0;
    s_upload_ctx.target_id  = id;

    /* The UI click handler already reveals the progress overlay and
     * primes it at 0%; calling set_upload_progress(0) here would hide
     * the container again, so the first explicit progress push comes
     * from __rec_upload_timer_cb. */
    wukong_ai_agent_input_start(TRUE);
    s_upload_timer = lv_timer_create(__rec_upload_timer_cb, REC_UPLOAD_TIMER_MS, NULL);
    if (s_upload_timer == NULL) {
        PR_ERR("record upload: create timer failed");
        __rec_upload_stop();
        wukong_ai_agent_input_stop();
        return OPRT_COM_ERROR;
    }

    return OPRT_OK;
}

/**
 * @brief Debug-print every field of a recording entry to PR_DEBUG
 * @param[in] id record id
 * @return none
 */
VOID_T ui_record_runtime_dump_info(INT_T id)
{
    LIST_HEAD *pos = NULL;
    BOOL_T found = FALSE;
    REC_ITEM_T snapshot;
    CHAR_T md5_hex[33] = {0};

    memset(&snapshot, 0, sizeof(snapshot));

    if (s_rec_list.inited == FALSE) {
        PR_DEBUG("record dump: list not initialized, id=%d", id);
        return;
    }

    tal_mutex_lock(s_rec_list.mutex);
    tuya_list_for_each(pos, &s_rec_list.head) {
        REC_ITEM_T *rec = tuya_list_entry(pos, REC_ITEM_T, list_node);
        if (rec == NULL || rec->id != id) {
            continue;
        }
        /* shallow copy under the mutex; list_node is unused after */
        snapshot = *rec;
        found = TRUE;
        break;
    }
    tal_mutex_unlock(s_rec_list.mutex);

    if (found == FALSE) {
        PR_DEBUG("record dump: id=%d not found", id);
        return;
    }

    __rec_md5_to_hex(snapshot.md5, md5_hex);
    PR_DEBUG("record dump: id=%d name=%s len=%lu duration=%us "
             "ts=%04d-%02d-%02d %02d:%02d:%02d md5=%s "
             "transcribe_status=%d transcribe_file=%s summary_file=%s",
             snapshot.id, snapshot.name,
             (unsigned long)snapshot.len,
             (unsigned)snapshot.duration_sec,
             snapshot.create_time.tm_year, snapshot.create_time.tm_mon,
             snapshot.create_time.tm_mday, snapshot.create_time.tm_hour,
             snapshot.create_time.tm_min, snapshot.create_time.tm_sec,
             md5_hex,
             snapshot.transcribe_status,
             snapshot.transcribe_filename,
             snapshot.summary_filename);
}

/* ---------------------------------------------------------------------------
 * Transcribe-result poll: synchronous wrapper around tuya_ai_http_dld_file
 * --------------------------------------------------------------------------- */

/**
 * @brief Stream callback for tuya_ai_http_dld_file — write chunks to s_dld_ctx.fp
 * @param[in] attr     biz attribute (unused)
 * @param[in] head     biz head — head->len = chunk length, head->stream_flag = stream phase
 * @param[in] data     chunk bytes
 * @param[in] usr_data unused (tuya_ai_http_dld_file hard-codes NULL — see ADR-0003)
 * @return OPRT_OK always (we never want to abort the framework's stream loop)
 * @note  Runs on WORKQ_SYSTEM thread, NOT on the poll thread. Synchronization
 *        with the poll thread is via s_dld_ctx.done_sem: poll thread waits,
 *        we post on AI_STREAM_END / AI_STREAM_ONE.
 */
STATIC OPERATE_RET __rec_dld_recv_cb(AI_BIZ_ATTR_INFO_T *attr,
                                     AI_BIZ_HEAD_INFO_T *head,
                                     VOID *data, VOID *usr_data)
{
    (VOID_T)attr;
    (VOID_T)usr_data;

    if (head == NULL) {
        return OPRT_OK;
    }

    if (s_dld_ctx.fp != NULL && data != NULL && head->len > 0) {
        INT_T n = tkl_fwrite(data, (INT_T)head->len, s_dld_ctx.fp);
        if (n != (INT_T)head->len) {
            s_dld_ctx.write_failed = TRUE;
            PR_ERR("record dld: write failed expect=%u got=%d",
                   (unsigned)head->len, n);
        }
    }

    if (head->stream_flag == AI_STREAM_END ||
        head->stream_flag == AI_STREAM_ONE) {
        s_dld_ctx.received_end = TRUE;
        if (s_dld_ctx.done_sem != NULL) {
            tal_semaphore_post(s_dld_ctx.done_sem);
        }
    }
    return OPRT_OK;
}

/**
 * @brief Synchronously download `url` to `dst_path` (sem + timeout wrapper)
 * @param[in] url        signed transient URL from cloud (must not be NULL/empty)
 * @param[in] dst_path   absolute destination path on local fs
 * @param[in] timeout_ms how long to wait for the download to complete
 * @return OPRT_OK on success; error otherwise (file is removed on any failure)
 * @note Single-threaded by design: only the poll thread calls this and only
 *       one in-flight download at a time, so the global s_dld_ctx is safe
 *       (per ADR-0003). On failure (timeout / write error / never received
 *       END marker / dld_file framework reported error), the partial file
 *       is removed so the next attempt starts clean.
 */
STATIC OPERATE_RET __rec_sync_dld_file(CONST CHAR_T *url,
                                       CONST CHAR_T *dst_path,
                                       UINT_T timeout_ms)
{
    OPERATE_RET rt = OPRT_OK;

    if (url == NULL || url[0] == '\0' || dst_path == NULL || dst_path[0] == '\0') {
        return OPRT_INVALID_PARM;
    }

    memset(&s_dld_ctx, 0, sizeof(s_dld_ctx));

    rt = tal_semaphore_create_init(&s_dld_ctx.done_sem, 0, 1);
    if (rt != OPRT_OK) {
        PR_ERR("record dld: sem create failed rt=%d", rt);
        return rt;
    }

    s_dld_ctx.fp = tkl_fopen(dst_path, "wb");
    if (s_dld_ctx.fp == NULL) {
        PR_ERR("record dld: open dst failed: %s", dst_path);
        tal_semaphore_release(s_dld_ctx.done_sem);
        s_dld_ctx.done_sem = NULL;
        return OPRT_COM_ERROR;
    }

    rt = tuya_ai_http_dld_file((CHAR_T *)url, __rec_dld_recv_cb);
    if (rt == OPRT_OK) {
        /* dld_file does NOT call cb on failure — sem timeout is our only
         * failure detector here (see ADR-0003). */
        rt = tal_semaphore_wait(s_dld_ctx.done_sem, timeout_ms);
    }

    if (s_dld_ctx.fp != NULL) {
        tkl_fclose(s_dld_ctx.fp);
        s_dld_ctx.fp = NULL;
    }
    tal_semaphore_release(s_dld_ctx.done_sem);
    s_dld_ctx.done_sem = NULL;

    if (rt != OPRT_OK || !s_dld_ctx.received_end || s_dld_ctx.write_failed) {
        PR_WARN("record dld: failed url=%s rt=%d end=%d write_fail=%d",
                url, rt, (INT_T)s_dld_ctx.received_end,
                (INT_T)s_dld_ctx.write_failed);
        tkl_fs_remove(dst_path);
        return (rt != OPRT_OK) ? rt : OPRT_COM_ERROR;
    }

    return OPRT_OK;
}

/* ---------------------------------------------------------------------------
 * Transcribe-result poll: thread main loop (5s tick, full-round serial)
 * ---------------------------------------------------------------------------
 * Architecture: __rec_poll_thread is a thin while/sleep shell over
 * __rec_poll_round_once. Round_once orchestrates 5 helpers, one per ADR-0003
 * step:
 *   1. __rec_poll_collect_pending     — short-lock scan: status==0 && md5!=0
 *   2. __rec_poll_post_and_get_items  — build body, POST, dump, locate items[]
 *   3. __rec_poll_plan_one (per item) — md5 reverse lookup + status switch
 *      └─ __rec_poll_download_round   — presence-based dld + per-round atomic
 *   4. __rec_poll_apply_plans         — short-lock apply + save + refresh
 * Status semantics (cloud `status` field, ADR-0003): null / 0=processing /
 * 1=done / 2=fail. We collapse {null, 2} → local 2 (Q2 decision).
 */

/**
 * @brief LVGL-thread bounce for ui_record_runtime_refresh_ui_list
 * @param[in] arg unused
 */
STATIC VOID __rec_poll_refresh_async(VOID *arg)
{
    (VOID_T)arg;
    ui_record_runtime_refresh_ui_list();
}

/**
 * @brief Collect (id, md5) pairs for entries pending a transcribe result
 * @param[out] ids       array of size REC_ITEM_NUM_MAX, filled up to *count
 * @param[out] md5_hex   matching md5 hex strings, one per id
 * @param[out] count     number of entries written; 0 means nothing pending
 * @return OPRT_OK on success
 * @note Self-locks s_rec_list.mutex. Filters: transcribe_status==0 (waiting
 *       on cloud), md5 non-zero (md5 is the lookup key — zero means capture
 *       failed, no point polling), and the per-entry 5s cooldown after
 *       upload 100% (poll_not_before_tick — see ADR-0003 补丁).
 */
STATIC OPERATE_RET __rec_poll_collect_pending(INT_T *ids,
                                              CHAR_T md5_hex[][REC_MD5_HEX_LEN + 1],
                                              INT_T *count)
{
    LIST_HEAD *pos = NULL;
    INT_T n = 0;
    UINT32_T now = (UINT32_T)tal_system_get_tick_count();

    *count = 0;

    tal_mutex_lock(s_rec_list.mutex);
    tuya_list_for_each(pos, &s_rec_list.head) {
        REC_ITEM_T *rec = tuya_list_entry(pos, REC_ITEM_T, list_node);
        if (rec == NULL) {
            continue;
        }
        if (rec->transcribe_status != 0) {
            continue;
        }
        if (__rec_md5_is_zero(rec->md5)) {
            continue;
        }
        /* 上传 100% 后 5s 内不向云端发起首查（ADR-0003 补丁），
         * 无符号差强转有符号自然处理 32-bit tick 回绕。 */
        if ((INT32_T)(now - rec->poll_not_before_tick) < 0) {
            continue;
        }
        if (n >= REC_ITEM_NUM_MAX) {
            break;
        }
        ids[n] = rec->id;
        __rec_md5_to_hex(rec->md5, md5_hex[n]);
        n++;
    }
    tal_mutex_unlock(s_rec_list.mutex);

    *count = n;
    return OPRT_OK;
}

/**
 * @brief POST {"md5List":[...]} and locate the response items array
 * @param[in]  md5_hex     hex strings to query, count entries
 * @param[in]  count       number of md5 strings (>0)
 * @param[out] out_result  top-level cJSON; caller must ty_cJSON_Delete on success
 * @param[out] out_items   array node inside *out_result (borrowed pointer)
 * @return OPRT_OK on success (both outputs valid); error otherwise
 * @note Failure paths set *out_result=NULL and *out_items=NULL so the caller
 *       can unconditionally `if (result) ty_cJSON_Delete(result)`. Double-
 *       probes for top-level array OR `.result` array — iot_httpc_common_
 *       post_simple normally strips the wrapper for thing.* APIs but we
 *       tolerate either shape.
 */
STATIC OPERATE_RET __rec_poll_post_and_get_items(CONST CHAR_T md5_hex[][REC_MD5_HEX_LEN + 1],
                                                 INT_T count,
                                                 ty_cJSON **out_result,
                                                 ty_cJSON **out_items)
{
    CHAR_T post_body[REC_POLL_POST_BUF_SIZE];
    ty_cJSON *result = NULL;
    ty_cJSON *items = NULL;
    OPERATE_RET rt = OPRT_OK;
    INT_T offset = 0;
    INT_T i = 0;

    *out_result = NULL;
    *out_items  = NULL;

    offset = snprintf(post_body, sizeof(post_body), "{\"md5List\":[");
    for (i = 0; i < count; i++) {
        if (offset >= (INT_T)sizeof(post_body) - (REC_MD5_HEX_LEN + 8)) {
            /* Defensive: should never trigger at REC_ITEM_NUM_MAX=20
             * (~700B) inside REC_POLL_POST_BUF_SIZE=1024 */
            break;
        }
        offset += snprintf(post_body + offset, sizeof(post_body) - offset,
                           "%s\"%s\"", (i > 0) ? "," : "", md5_hex[i]);
    }
    if (offset < (INT_T)sizeof(post_body) - 2) {
        snprintf(post_body + offset, sizeof(post_body) - offset, "]}");
    }

    rt = iot_httpc_common_post_simple((CHAR_T *)REC_POLL_API,
                                      (CHAR_T *)REC_POLL_API_VER,
                                      post_body, NULL, &result);
    if (rt != OPRT_OK) {
        PR_WARN("record poll: http failed rt=%d (likely offline, retry)", rt);
        if (result != NULL) {
            ty_cJSON_Delete(result);
        }
        return rt;
    }

    if (result != NULL) {
        CHAR_T *dump = ty_cJSON_PrintUnformatted(result);
        if (dump != NULL) {
            PR_DEBUG("record poll: response=%s", dump);
            ty_cJSON_FreeBuffer(dump);
        }
        dump = ty_cJSON_Print(result);
        if (dump != NULL) {
            PR_DEBUG("record poll: response (pretty)=\n%s", dump);
            ty_cJSON_FreeBuffer(dump);
        }
    }

    if (result != NULL) {
        if (ty_cJSON_IsArray(result)) {
            items = result;
        } else {
            ty_cJSON *sub = ty_cJSON_GetObjectItem(result, "result");
            if (sub != NULL && ty_cJSON_IsArray(sub)) {
                items = sub;
            }
        }
    }

    if (items == NULL) {
        PR_WARN("record poll: response missing result array");
        if (result != NULL) {
            ty_cJSON_Delete(result);
        }
        return OPRT_COM_ERROR;
    }

    *out_result = result;
    *out_items  = items;
    return OPRT_OK;
}

/**
 * @brief Synchronously fetch the cloud-advertised transcribe / summary files
 * @param[in]  item                   response cJSON object for one md5
 * @param[in]  md5_hex                md5 hex string (basename root)
 * @param[in]  target_id              local entry id (for logging only)
 * @param[out] transcribe_filename    REC_FILENAME_MAX buffer; set to "" if no transcribeFileUrl
 * @param[out] summary_filename       REC_FILENAME_MAX buffer; set to "" if no summaryFileUrl
 * @return OPRT_OK if every advertised URL landed (atomicity invariant);
 *         OPRT_INVALID_PARM if both URLs are missing (warn + retry next round);
 *         other errors mean partial download — files already rolled back.
 * @note Per ADR-0003 §"`status=1` 下载允许 URL 子集，但本轮内原子": cloud
 *       may advertise just transcribeFileUrl OR just summaryFileUrl. The
 *       output buffers are pre-zeroed by the caller; we only write to a
 *       slot when the corresponding URL is advertised AND its download
 *       succeeded. A partial failure (e.g. transcribe ok, summary fails)
 *       removes any files we already wrote so the next round retries from
 *       a clean state.
 */
STATIC OPERATE_RET __rec_poll_download_round(ty_cJSON *item,
                                             CONST CHAR_T *md5_hex,
                                             INT_T target_id,
                                             CHAR_T *transcribe_filename,
                                             CHAR_T *summary_filename)
{
    ty_cJSON *t_url_j = ty_cJSON_GetObjectItem(item, "transcribeFileUrl");
    ty_cJSON *s_url_j = ty_cJSON_GetObjectItem(item, "summaryFileUrl");
    CONST CHAR_T *t_url = (t_url_j != NULL) ? ty_cJSON_GetStringValue(t_url_j) : NULL;
    CONST CHAR_T *s_url = (s_url_j != NULL) ? ty_cJSON_GetStringValue(s_url_j) : NULL;
    BOOL_T have_t = (t_url != NULL && t_url[0] != '\0');
    BOOL_T have_s = (s_url != NULL && s_url[0] != '\0');
    CHAR_T t_basename[REC_FILENAME_MAX] = {0};
    CHAR_T s_basename[REC_FILENAME_MAX] = {0};
    CHAR_T t_path[REC_PATH_MAX] = {0};
    CHAR_T s_path[REC_PATH_MAX] = {0};
    OPERATE_RET drt = OPRT_OK;
    BOOL_T t_dld_ok = FALSE;

    PR_DEBUG("record poll: id=%d transcribeFileUrl=%s summaryFileUrl=%s",
             target_id,
             have_t ? t_url : "(null)",
             have_s ? s_url : "(null)");

    if (!have_t && !have_s) {
        PR_WARN("record poll: id=%d status=1 but both urls missing "
                "— keep local 0, retry", target_id);
        return OPRT_INVALID_PARM;
    }

    tkl_fs_mkdir(REC_TRANSCRIBE_DIR);

    if (have_t) {
        snprintf(t_basename, sizeof(t_basename), "%s.txt", md5_hex);
        snprintf(t_path, sizeof(t_path), "%s/%s",
                 REC_TRANSCRIBE_DIR, t_basename);
        drt = __rec_sync_dld_file(t_url, t_path, REC_DLD_TIMEOUT_MS);
        if (drt != OPRT_OK) {
            PR_WARN("record poll: id=%d transcribe dld failed rt=%d "
                    "(keep local 0, retry next round)", target_id, drt);
            return drt;
        }
        t_dld_ok = TRUE;
    }

    if (have_s) {
        snprintf(s_basename, sizeof(s_basename), "%s_summary.txt", md5_hex);
        snprintf(s_path, sizeof(s_path), "%s/%s",
                 REC_TRANSCRIBE_DIR, s_basename);
        drt = __rec_sync_dld_file(s_url, s_path, REC_DLD_TIMEOUT_MS);
        if (drt != OPRT_OK) {
            PR_WARN("record poll: id=%d summary dld failed rt=%d "
                    "(rollback partial files, retry next round)",
                    target_id, drt);
            if (t_dld_ok) {
                tkl_fs_remove(t_path);
            }
            return drt;
        }
    }

    if (have_t) {
        snprintf(transcribe_filename, REC_FILENAME_MAX, "%s", t_basename);
    }
    if (have_s) {
        snprintf(summary_filename, REC_FILENAME_MAX, "%s", s_basename);
    }
    return OPRT_OK;
}

/**
 * @brief Decide what to do for one cloud response item; fill *plan if action needed
 * @param[in]  item       response cJSON object
 * @param[in]  ids        local id array from collect_pending
 * @param[in]  md5_hex    matching md5 hex array
 * @param[in]  count      number of valid entries in ids/md5_hex
 * @param[out] plan       caller-allocated plan slot (zeroed on success)
 * @return OPRT_OK if a plan was written (caller advances plan_count);
 *         anything else = skip silently (still processing, unknown md5,
 *         download failed, …)
 * @note Status policy (ADR-0003 Q2):
 *         null / missing / 2 (fail) → new_status=2
 *         0 (processing)            → no plan
 *         1 (done)                  → download_round, then new_status=1
 *         anything else             → no plan (defensive)
 */
STATIC OPERATE_RET __rec_poll_plan_one(ty_cJSON *item,
                                       CONST INT_T *ids,
                                       CONST CHAR_T md5_hex[][REC_MD5_HEX_LEN + 1],
                                       INT_T count,
                                       REC_POLL_PLAN_T *plan)
{
    ty_cJSON *md5_j = NULL;
    ty_cJSON *st_j  = NULL;
    CONST CHAR_T *resp_md5 = NULL;
    INT_T target_id = -1;
    INT_T status = -1;     /* sentinel: null / missing / non-numeric */
    INT_T k = 0;

    if (item == NULL) {
        return OPRT_INVALID_PARM;
    }

    md5_j = ty_cJSON_GetObjectItem(item, "md5");
    resp_md5 = (md5_j != NULL) ? ty_cJSON_GetStringValue(md5_j) : NULL;
    if (resp_md5 == NULL) {
        return OPRT_INVALID_PARM;
    }
    for (k = 0; k < count; k++) {
        if (strcmp(md5_hex[k], resp_md5) == 0) {
            target_id = ids[k];
            break;
        }
    }
    if (target_id < 0) {
        return OPRT_INVALID_PARM;
    }

    st_j = ty_cJSON_GetObjectItem(item, "status");
    if (st_j != NULL && !ty_cJSON_IsNull(st_j) && ty_cJSON_IsNumber(st_j)) {
        status = st_j->valueint;
    }
    PR_INFO("record poll: id=%d md5=%s status=%d", target_id, resp_md5, status);

    memset(plan, 0, sizeof(*plan));
    plan->id = target_id;
    snprintf(plan->md5_hex, sizeof(plan->md5_hex), "%s", resp_md5);

    switch (status) {
    case 0:
        return OPRT_INVALID_PARM;   /* still processing — leave local 0 */
    case 1:
        if (__rec_poll_download_round(item, resp_md5, target_id,
                                      plan->transcribe_filename,
                                      plan->summary_filename) != OPRT_OK) {
            return OPRT_COM_ERROR;  /* dld failed / both urls missing — retry */
        }
        plan->new_status = 1;
        return OPRT_OK;
    case -1:                        /* null / missing / non-numeric */
    case 2:
        plan->new_status = 2;
        return OPRT_OK;
    default:
        PR_WARN("record poll: id=%d unknown status=%d, skip", target_id, status);
        return OPRT_INVALID_PARM;
    }
}

/**
 * @brief Apply a batch of plans to the in-memory list, persist, refresh UI
 * @param[in] plans       array of plans to commit
 * @param[in] plan_count  number of valid plans (>0)
 * @return none
 * @note Self-locks s_rec_list.mutex. Per-plan anti-ABA: ids are recyclable
 *       (deleted/evicted entries free the id, next stop() reuses it), so we
 *       match md5 alongside id. A mismatch means the entry was replaced
 *       while we were doing HTTP / sync download — drop the plan and remove
 *       any orphan files bound to the now-defunct md5.
 *       __rec_list_save() self-locks too, so we unlock before calling it
 *       (existing behavior — see ADR-0003 §"并发安全补丁").
 *       lv_async_call bounces the UI refresh onto the LVGL thread.
 */
STATIC VOID __rec_poll_apply_plans(CONST REC_POLL_PLAN_T *plans, INT_T plan_count)
{
    INT_T i = 0;

    tal_mutex_lock(s_rec_list.mutex);
    for (i = 0; i < plan_count; i++) {
        CONST REC_POLL_PLAN_T *p = &plans[i];
        LIST_HEAD *pos = NULL;
        tuya_list_for_each(pos, &s_rec_list.head) {
            REC_ITEM_T *rec = tuya_list_entry(pos, REC_ITEM_T, list_node);
            CHAR_T cur_hex[REC_MD5_HEX_LEN + 1] = {0};
            if (rec == NULL || rec->id != p->id) {
                continue;
            }
            __rec_md5_to_hex(rec->md5, cur_hex);
            if (strcmp(cur_hex, p->md5_hex) != 0) {
                PR_WARN("record poll: id=%d md5 changed during round "
                        "(was %s, now %s) — drop plan",
                        p->id, p->md5_hex, cur_hex);
                if (p->transcribe_filename[0] != '\0') {
                    CHAR_T orphan_path[REC_PATH_MAX];
                    snprintf(orphan_path, sizeof(orphan_path), "%s/%s",
                             REC_TRANSCRIBE_DIR, p->transcribe_filename);
                    tkl_fs_remove(orphan_path);
                }
                if (p->summary_filename[0] != '\0') {
                    CHAR_T orphan_path[REC_PATH_MAX];
                    snprintf(orphan_path, sizeof(orphan_path), "%s/%s",
                             REC_TRANSCRIBE_DIR, p->summary_filename);
                    tkl_fs_remove(orphan_path);
                }
                break;
            }
            rec->transcribe_status = p->new_status;
            if (p->new_status == 1) {
                snprintf(rec->transcribe_filename,
                         sizeof(rec->transcribe_filename),
                         "%s", p->transcribe_filename);
                snprintf(rec->summary_filename,
                         sizeof(rec->summary_filename),
                         "%s", p->summary_filename);
            }
            break;
        }
    }
    tal_mutex_unlock(s_rec_list.mutex);

    __rec_list_save();
    lv_async_call(__rec_poll_refresh_async, NULL);
}

/**
 * @brief One full polling round (collect → POST → plan → apply)
 * @return none
 * @note Each early-exit is a plain `return`; the caller drives sleep cadence.
 *       Skeleton matches ADR-0003 §"轮询线程主循环" 1:1.
 */
STATIC VOID __rec_poll_round_once(VOID_T)
{
    INT_T  ids[REC_ITEM_NUM_MAX];
    CHAR_T md5_hex[REC_ITEM_NUM_MAX][REC_MD5_HEX_LEN + 1];
    REC_POLL_PLAN_T plans[REC_ITEM_NUM_MAX];
    INT_T count = 0;
    INT_T plan_count = 0;
    ty_cJSON *result = NULL;
    ty_cJSON *items  = NULL;
    INT_T n = 0;
    INT_T i = 0;

    if (__rec_list_init() != OPRT_OK) {
        return;
    }
    if (__rec_poll_collect_pending(ids, md5_hex, &count) != OPRT_OK || count == 0) {
        return;
    }
    if (__rec_poll_post_and_get_items(md5_hex, count, &result, &items) != OPRT_OK) {
        return;
    }

    n = ty_cJSON_GetArraySize(items);
    PR_DEBUG("record poll: response items=%d (sent md5_count=%d)", n, count);
    for (i = 0; i < n && plan_count < REC_ITEM_NUM_MAX; i++) {
        ty_cJSON *item = ty_cJSON_GetArrayItem(items, i);
        if (__rec_poll_plan_one(item, ids, md5_hex, count,
                                &plans[plan_count]) == OPRT_OK) {
            plan_count++;
        }
    }
    ty_cJSON_Delete(result);

    if (plan_count > 0) {
        __rec_poll_apply_plans(plans, plan_count);
    }
}

/**
 * @brief Poll thread main loop — see ADR-0003 §"轮询线程主循环"
 * @param[in] args unused
 * @note Fire-and-forget: runs forever (no stop API). Each round_once is
 *       self-contained and idempotent on failure (next tick retries with
 *       fresh state), so the shell only needs to drive cadence.
 */
STATIC VOID __rec_poll_thread(PVOID_T args)
{
    (VOID_T)args;
    PR_INFO("record poll: thread enter");
    while (1) {
        __rec_poll_round_once();
        tal_system_sleep(REC_POLL_INTERVAL_MS);
    }
}

/**
 * @brief Spawn the transcribe-result poll thread (idempotent)
 * @return none
 */
VOID_T ui_record_runtime_poll_start(VOID_T)
{
    THREAD_CFG_T cfg = {0};
    OPERATE_RET rt = OPRT_OK;

    if (s_poll_thread != NULL) {
        return;
    }

    if (__rec_list_init() != OPRT_OK) {
        PR_ERR("record poll: list init failed, abort thread spawn");
        return;
    }

    cfg.stackDepth = REC_POLL_THREAD_STACK;
    cfg.priority   = THREAD_PRIO_2;
    cfg.thrdname   = "rec_poll";

    rt = tal_thread_create_and_start(&s_poll_thread, NULL, NULL,
                                     __rec_poll_thread, NULL, &cfg);
    if (rt != OPRT_OK) {
        PR_ERR("record poll: thread create failed rt=%d", rt);
        s_poll_thread = NULL;
        return;
    }
    PR_INFO("record poll: thread started (stack=%u)",
            (unsigned)REC_POLL_THREAD_STACK);
}

/* ===========================================================================
 * Reading-card file API
 *
 * Bridges the LVGL-thread reading card view to the on-disk transcribe /
 * summary text files. Three concerns are kept apart:
 *   - list lookup is mutex-guarded (consistent with the rest of this file)
 *   - kernel filesystem calls are made *outside* the mutex
 *   - the returned TUYA_FILE handle is owned by the caller; the runtime
 *     does not track it, so the LVGL reading state machine is responsible
 *     for closing it via ui_record_runtime_close_file()
 * =========================================================================== */

BOOL_T ui_record_runtime_has_file(INT_T id, UI_REC_FILE_KIND_T kind)
{
    LIST_HEAD *pos = NULL;
    BOOL_T has = FALSE;

    if (__rec_list_init() != OPRT_OK) {
        return FALSE;
    }

    tal_mutex_lock(s_rec_list.mutex);
    tuya_list_for_each(pos, &s_rec_list.head) {
        REC_ITEM_T *rec = tuya_list_entry(pos, REC_ITEM_T, list_node);
        if (rec == NULL || rec->id != id) {
            continue;
        }
        if (kind == UI_REC_FILE_TRANSCRIBE) {
            has = (rec->transcribe_filename[0] != '\0') ? TRUE : FALSE;
        } else if (kind == UI_REC_FILE_SUMMARY) {
            has = (rec->summary_filename[0] != '\0') ? TRUE : FALSE;
        }
        break;
    }
    tal_mutex_unlock(s_rec_list.mutex);

    return has;
}

OPERATE_RET ui_record_runtime_open_file(INT_T id,
                                        UI_REC_FILE_KIND_T kind,
                                        TUYA_FILE *out_fp,
                                        UINT32_T *out_size)
{
    LIST_HEAD *pos = NULL;
    BOOL_T found = FALSE;
    CHAR_T basename[REC_FILENAME_MAX] = {0};
    CHAR_T path[REC_PATH_MAX] = {0};
    TUYA_FILE fp = NULL;
    INT_T file_size = 0;

    if (out_fp == NULL || out_size == NULL) {
        return OPRT_INVALID_PARM;
    }
    *out_fp = NULL;
    *out_size = 0;

    if (__rec_list_init() != OPRT_OK) {
        return OPRT_COM_ERROR;
    }

    tal_mutex_lock(s_rec_list.mutex);
    tuya_list_for_each(pos, &s_rec_list.head) {
        REC_ITEM_T *rec = tuya_list_entry(pos, REC_ITEM_T, list_node);
        if (rec == NULL || rec->id != id) {
            continue;
        }
        if (kind == UI_REC_FILE_TRANSCRIBE && rec->transcribe_filename[0] != '\0') {
            strncpy(basename, rec->transcribe_filename, sizeof(basename) - 1);
            found = TRUE;
        } else if (kind == UI_REC_FILE_SUMMARY && rec->summary_filename[0] != '\0') {
            strncpy(basename, rec->summary_filename, sizeof(basename) - 1);
            found = TRUE;
        }
        break;
    }
    tal_mutex_unlock(s_rec_list.mutex);

    if (found == FALSE) {
        return OPRT_COM_ERROR;
    }

    snprintf(path, sizeof(path), "%s/%s", REC_TRANSCRIBE_DIR, basename);

    file_size = tkl_fgetsize(path);
    if (file_size < 0) {
        PR_ERR("reading: fgetsize failed path=%s rt=%d", path, file_size);
        return OPRT_COM_ERROR;
    }

    fp = tkl_fopen(path, "rb");
    if (fp == NULL) {
        PR_ERR("reading: fopen failed path=%s", path);
        return OPRT_COM_ERROR;
    }

    *out_fp = fp;
    *out_size = (UINT32_T)file_size;
    return OPRT_OK;
}

INT_T ui_record_runtime_read_at(TUYA_FILE fp,
                                UINT32_T offset,
                                CHAR_T *buf,
                                UINT32_T size)
{
    INT_T rt = 0;

    if (fp == NULL || buf == NULL || size == 0) {
        return -1;
    }

    rt = tkl_fseek(fp, (INT64_T)offset, 0 /* SEEK_SET */);
    if (rt != 0) {
        PR_ERR("reading: fseek failed offset=%u rt=%d", (unsigned)offset, rt);
        return -1;
    }

    return tkl_fread(buf, (INT_T)size, fp);
}

VOID_T ui_record_runtime_close_file(TUYA_FILE fp)
{
    if (fp == NULL) {
        return;
    }
    tkl_fclose(fp);
}
