#include "ui_svc_recording.h"
#include "ui_app.h"            /* ui_app_async_call */
#include <string.h>
#include <stdio.h>

#include "tal_memory.h"
#include "tal_mutex.h"
#include "tal_time_service.h"
#include "tal_system.h"
#include "tal_hash.h"          /* tal_md5_* — 边录边算转写关联键 */
#include "tal_semaphore.h"     /* 写盘线程唤醒/退出同步 */
#include "tal_thread.h"        /* 专职 SD 写盘线程 */
#include "tal_workq_service.h" /* WORKQ_SYSTEM, tal_workq_schedule */
#include "tuya_list.h"
#include "tkl_fs.h"
#include "ty_cJSON.h"
#include "uni_log.h"
#include "base_event.h"        /* EVENT_WUKONG_STORAGE_READY 订阅（延迟索引加载） */

#include "wukong_ai_mode.h"
#include "wukong_ai_agent.h"
#include "wukong_audio_player.h"
#include "wukong_audio_input.h"
#include "svc_ai_player.h"
#include "ui_svc_music.h"      /* ui_svc_music_stop — auto-next-aware music stop */
#include "ui_svc_fs.h"         /* unified /tuyaos filesystem */
#include "tuya_app_config.h"   /* APP_OPUS_ENCODER_BITRATE — recovery duration estimate */

/* 录音设备模式 getter（项目无公共头，沿用 view 层的 extern 方式） */
extern AI_DEVICE_MODE_E tuya_ai_toy_device_mode_get(void);

/* Files live under the unified fs: <UI_FS_ROOT>/recording/ (see ui_svc_fs.h).
 * Paths are built at use-time via ui_fs_path(UI_FS_RECORDING, name). */
#define REC_INDEX_NAME    "recording_list.json"
#define REC_ITEM_NUM_MAX  20
#define REC_NAME_MAX      64
#define REC_PATH_MAX      128
#define REC_FILENAME_MAX  48   /* 转写结果 basename：<md5hex>.txt / <md5hex>_summary.txt */
#define REC_MD5_HEX_LEN   UI_REC_MD5_HEX_LEN

/* 上传 100% 后延迟首次轮询的冷却窗口（云端需要时间生成结果）。 */
#define REC_POLL_FIRST_DELAY_MS  10000

/* Crash-safety knobs. */
#define REC_INDEX_TMP_NAME   "recording_list.json.tmp" /* atomic index write staging */
#define REC_FSYNC_BYTES      8192   /* flush+fsync the capture file every ~8KB (~4s @16kbps) */
#define REC_MD5_READ_CHUNK   2048   /* read buffer for recovery md5 recompute */

/* Decoupled SD writer. The mic pipeline upstream only buffers ~0.9s of frames
 * and silently drops on overflow, so fwrite/fsync latency must never run on
 * the audio callback: the callback just queues bytes here and a dedicated
 * thread drains them to the SD card. 32KB ≈ 16s @16kbps CBR of stall slack. */
#define REC_WRING_SIZE       (32 * 1024)
#define REC_WR_CHUNK         2048   /* writer drain granularity per fwrite */
#define REC_WR_THREAD_STACK  4096
#define REC_WR_EXIT_WAIT_MS  10000  /* final drain (≤32KB) + fsync headroom */
#define REC_WR_DROP_LOG_STEP 16384  /* re-log dropped-bytes warning every 16KB */
#ifndef APP_OPUS_ENCODER_BITRATE
#define APP_OPUS_ENCODER_BITRATE 16000  /* fallback if the app config macro is unavailable */
#endif

typedef struct {
    int         id;
    char        name[REC_NAME_MAX];
    uint64_t    len;
    uint32_t    duration_sec;
    POSIX_TM_S  create_time;
    uint8_t     md5[16];                          /* 云端转写关联键；全零=未生成 */
    int         transcribe_status;                /* ui_transcribe_status_t */
    char        transcribe_filename[REC_FILENAME_MAX]; /* 转写结果 basename */
    char        summary_filename[REC_FILENAME_MAX];    /* 总结结果 basename */
    uint32_t    poll_not_before_tick;             /* 上传后冷却到期 tick；仅内存 */
    LIST_HEAD   node;
} rec_item_t;

typedef struct {
    uint32_t     num;
    LIST_HEAD    head;
    MUTEX_HANDLE mutex;
    BOOL_T       inited;
    BOOL_T       loaded;
} rec_list_t;

static rec_list_t s_list = {0};

/* 采集会话状态 */
static TUYA_FILE        s_fp = NULL;
static char             s_cur_name[REC_NAME_MAX] = {0};
static uint64_t         s_file_size = 0;
static uint32_t         s_sync_accum = 0;   /* bytes written since last flush+fsync */
static uint16_t         s_fallback_idx = 0;
static BOOL_T           s_running = FALSE;
static uint32_t         s_start_tick = 0;
static BOOL_T           s_session_active = FALSE;
static AI_DEVICE_MODE_E s_mode_before = AI_DEVICE_MODE_CHAT;

/* 流式 MD5 会话：file_open 起、写盘线程按落盘字节喂、file_close_and_save 收。
 * sticky-fail：一次失败只污染当前会话，finalize 返回全零摘要。 */
static TKL_HASH_HANDLE  s_md5_ctx = NULL;
static BOOL_T           s_md5_failed = FALSE;

/* SD 写盘线程 + 环形缓冲。音频回调只入环（不做任何文件 IO），写盘线程独自
 * 消费；线程随采集会话 start/stop 创建与回收。
 *
 * 会话态整体打包进堆上的 per-session ctx，线程只通过入参持有自己的 ctx，
 * 从不解引用全局指针：join 超时（SD 卡死 >REC_WR_EXIT_WAIT_MS）时整个 ctx
 * （含专属信号量、环）随僵尸线程一起泄漏废弃，下一会话全新分配——僵尸解卡
 * 后 run 恒为 FALSE、exited post 落在无人复用的信号量上，不会窜进新会话
 * （review !164 指出的复活/陈旧 post 两个隐患）。只有 s_wring_mutex 常驻，
 * 守护 s_wr_ctx 发布指针与 ctx 内的环字段，供音频回调入环。 */
typedef struct {
    volatile BOOL_T run;         /* cleared by writer_stop */
    volatile BOOL_T abandoned;   /* join 超时置位：僵尸不得再碰 s_fp/md5 */
    SEM_HANDLE      wake;        /* data available / exit poke */
    SEM_HANDLE      exited;      /* writer finished final drain */
    uint8_t        *ring;        /* REC_WRING_SIZE bytes */
    uint32_t        head;        /* read index */
    uint32_t        used;        /* bytes queued */
    uint32_t        dropped;     /* ring-full bytes this session */
    uint32_t        drop_logged; /* last dropped value we warned at */
} rec_writer_ctx_t;

static MUTEX_HANDLE      s_wring_mutex = NULL;  /* everlasting, created once */
static rec_writer_ctx_t *s_wr_ctx = NULL;       /* published session; NULL = none */
static THREAD_HANDLE     s_wr_thread = NULL;

/* Async mode-restore on close (keeps the slow backend teardown off the UI
 * thread so the page transition isn't blocked). s_close_gen invalidates a
 * pending restore when a newer open/close supersedes it. */
static AI_DEVICE_MODE_E   s_restore_mode = AI_DEVICE_MODE_CHAT;
static volatile uint32_t  s_close_gen = 0;
static BOOL_T             s_restore_pending = FALSE;

/* 状态快照 + 回调 */
static ui_svc_recording_cb_t s_cb = NULL;
static ui_recording_status_t s_status = {
    .cap_state = UI_REC_CAP_IDLE, .play_state = AI_PLAYER_STOPPED, .play_id = -1,
};

/* 前置声明 */
static OPERATE_RET list_init(void);
static void        list_load(void);
static void        list_save(void);
static void        list_clear_locked(void);
static int         list_alloc_id_locked(void);
static OPERATE_RET list_delete_by_id(int id);
static void        compose_datetime_str(const POSIX_TM_S *tm, char *buf, uint32_t size);

/* 采集辅助（实现见持久化函数之后、公共 API 之前） */
static void        make_filename(char *buf, uint32_t size);
static OPERATE_RET file_open(void);
static void        file_close_and_save(void);
static OPERATE_RET file_write_and_sync(const void *data, int len, rec_writer_ctx_t *ctx);
static OPERATE_RET writer_start(void);
static void        writer_stop(void);
static OPERATE_RET input_audio_cb(AI_AUDIO_CODEC_TYPE codec, void *data, int len);
static OPERATE_RET register_audio_cb(void);
static void        capture_restore_apply(void);
static void        capture_restore_work_cb(void *arg);

/* MD5 流式会话 + hex 互转（移植自 src/view/runtime/ui_record_runtime.c） */
static void        md5_to_hex(const uint8_t md5[16], char hex[REC_MD5_HEX_LEN + 1]);
static OPERATE_RET hex_to_md5(const char *hex, uint8_t md5[16]);
static BOOL_T      md5_is_zero(const uint8_t md5[16]);
static void        md5_session_begin(void);
static void        md5_session_update(const void *data, int len);
static void        md5_session_finalize(uint8_t out_md5[16]);
static void        md5_session_abort(void);
static OPERATE_RET md5_compute_file(const char *path, uint8_t out_md5[16]); /* recovery: hash on-disk file */

/* Crash recovery: reconcile the on-disk recording dir with the index. */
static void        list_recover_orphans(void);
static void        recover_orphans_work_cb(void *arg);   /* async wrapper: keep heavy md5 off the boot path */

/* ---------------------------------------------------------------------------
 * Recording list: JSON persistence
 *   Adapted from src/view/runtime/ui_record_runtime.c, stripping all
 *   md5 / transcribe / summary / upload / poll concerns. Stored at
 *   <UI_FS_ROOT>/recording/recording_list.json (see ui_svc_fs.h); fields:
 *   id/name/len/duration/year/mon/mday/hour/min/sec.
 * --------------------------------------------------------------------------- */

/* Free every node in the list (caller must hold the list mutex). */
static void list_clear_locked(void)
{
    LIST_HEAD *pos = NULL;
    LIST_HEAD *next = NULL;

    tuya_list_for_each_safe(pos, next, &s_list.head) {
        rec_item_t *rec = tuya_list_entry(pos, rec_item_t, node);
        if (rec == NULL) {
            continue;
        }
        tuya_list_del(&rec->node);
        tal_free(rec);
    }
    s_list.num = 0;
}

/* Read the on-disk JSON index into the in-memory list (lazy, once). */
static void list_load(void)
{
    TUYA_FILE fp = NULL;
    ty_cJSON *root = NULL;
    ty_cJSON *arr = NULL;
    char *buf = NULL;
    int file_size = 0;
    int i = 0;
    char info_path[REC_PATH_MAX];

    if (s_list.inited == FALSE) {
        return;
    }

    tal_mutex_lock(s_list.mutex);
    if (s_list.loaded == TRUE) {
        tal_mutex_unlock(s_list.mutex);
        return;
    }
    s_list.loaded = TRUE;
    tal_mutex_unlock(s_list.mutex);

    if (ui_fs_path(info_path, sizeof(info_path), UI_FS_RECORDING, REC_INDEX_NAME) != OPRT_OK) {
        return;
    }

    if (tkl_faccess(info_path, 0) != 0) {
        return;
    }

    file_size = tkl_fgetsize(info_path);
    if (file_size <= 0) {
        PR_WARN("recording: invalid json size: %d", file_size);
        return;
    }

    buf = (char *)tal_malloc(file_size + 1);
    if (buf == NULL) {
        PR_ERR("recording: alloc json buffer failed");
        return;
    }
    memset(buf, 0, file_size + 1);

    fp = tkl_fopen(info_path, "rb");
    if (fp == NULL) {
        PR_ERR("recording: open json failed");
        goto __exit;
    }
    if (tkl_fread(buf, file_size, fp) != file_size) {
        PR_ERR("recording: read json failed");
        goto __exit;
    }

    root = ty_cJSON_Parse(buf);
    if ((root == NULL) || (ty_cJSON_IsObject(root) == FALSE)) {
        PR_ERR("recording: parse json failed");
        goto __exit;
    }

    arr = ty_cJSON_GetObjectItem(root, "list");
    if ((arr == NULL) || (ty_cJSON_IsArray(arr) == FALSE)) {
        PR_WARN("recording: missing list array");
        goto __exit;
    }

    tal_mutex_lock(s_list.mutex);
    list_clear_locked();

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
        rec_item_t *item = NULL;

        if ((item_json == NULL) || (ty_cJSON_IsObject(item_json) == FALSE)) {
            continue;
        }

        item = (rec_item_t *)tal_malloc(sizeof(rec_item_t));
        if (item == NULL) {
            PR_ERR("recording: alloc item failed");
            break;
        }
        memset(item, 0, sizeof(rec_item_t));

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

        item->id = (id_j != NULL && ty_cJSON_IsNumber(id_j)) ? id_j->valueint : i;
        if (name_j != NULL && ty_cJSON_GetStringValue(name_j) != NULL) {
            snprintf(item->name, sizeof(item->name), "%s", ty_cJSON_GetStringValue(name_j));
        }
        item->len          = (len_j != NULL && ty_cJSON_IsNumber(len_j)) ? (uint64_t)len_j->valuedouble : 0;
        item->duration_sec = (dur_j != NULL && ty_cJSON_IsNumber(dur_j)) ? (uint32_t)dur_j->valueint : 0;
        item->create_time.tm_year = (y_j  != NULL && ty_cJSON_IsNumber(y_j))  ? y_j->valueint  : 0;
        item->create_time.tm_mon  = (mo_j != NULL && ty_cJSON_IsNumber(mo_j)) ? mo_j->valueint : 0;
        item->create_time.tm_mday = (md_j != NULL && ty_cJSON_IsNumber(md_j)) ? md_j->valueint : 0;
        item->create_time.tm_hour = (h_j  != NULL && ty_cJSON_IsNumber(h_j))  ? h_j->valueint  : 0;
        item->create_time.tm_min  = (mi_j != NULL && ty_cJSON_IsNumber(mi_j)) ? mi_j->valueint : 0;
        item->create_time.tm_sec  = (s_j  != NULL && ty_cJSON_IsNumber(s_j))  ? s_j->valueint  : 0;

        /* 转写相关字段（legacy JSON 缺字段时：md5 留零、status 默认 -1）。 */
        {
            ty_cJSON *md5_j = ty_cJSON_GetObjectItem(item_json, "md5");
            ty_cJSON *ts_j  = ty_cJSON_GetObjectItem(item_json, "transcribe_status");
            ty_cJSON *tf_j  = ty_cJSON_GetObjectItem(item_json, "transcribe_filename");
            ty_cJSON *sf_j  = ty_cJSON_GetObjectItem(item_json, "summary_filename");
            if (md5_j != NULL && ty_cJSON_GetStringValue(md5_j) != NULL) {
                hex_to_md5(ty_cJSON_GetStringValue(md5_j), item->md5);
            }
            item->transcribe_status =
                (ts_j != NULL && ty_cJSON_IsNumber(ts_j)) ? ts_j->valueint
                                                          : UI_TRANSCRIBE_NOT_UPLOADED;
            if (tf_j != NULL && ty_cJSON_GetStringValue(tf_j) != NULL) {
                snprintf(item->transcribe_filename, sizeof(item->transcribe_filename),
                         "%s", ty_cJSON_GetStringValue(tf_j));
            }
            if (sf_j != NULL && ty_cJSON_GetStringValue(sf_j) != NULL) {
                snprintf(item->summary_filename, sizeof(item->summary_filename),
                         "%s", ty_cJSON_GetStringValue(sf_j));
            }
        }

        tuya_list_add_tail(&item->node, &s_list.head);
        s_list.num++;
    }
    tal_mutex_unlock(s_list.mutex);

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

/* Serialize the in-memory list back to the on-disk JSON index.
 * Holds s_list.mutex across the entire operation (cJSON dump +
 * PrintUnformatted + fs IO) to serialize concurrent writers — fopen("wb")
 * truncates, so two unsynchronized writers would corrupt the JSON. */
static void list_save(void)
{
    ty_cJSON *root = NULL;
    ty_cJSON *arr = NULL;
    LIST_HEAD *pos = NULL;
    char *json_str = NULL;
    TUYA_FILE fp = NULL;
    int write_size = 0;
    char info_path[REC_PATH_MAX];
    char tmp_path[REC_PATH_MAX];

    if (s_list.inited == FALSE) {
        return;
    }
    /* The boot-time index load is deferred until storage.ready (see
     * ui_svc_recording_init). A save that sneaks in after the mount but before
     * the deferred load ran would truncate-write the index with the (empty)
     * in-memory list, destroying it — refuse to persist until the load has
     * happened. Any entry skipped here still has its audio file on disk, so
     * next boot's orphan recovery re-adopts it. */
    if (s_list.loaded == FALSE) {
        PR_WARN("recording: save before index load; skipped to protect on-disk index");
        return;
    }

    root = ty_cJSON_CreateObject();
    if (root == NULL) {
        PR_ERR("recording: create root json failed");
        return;
    }

    arr = ty_cJSON_CreateArray();
    if (arr == NULL) {
        PR_ERR("recording: create list json failed");
        goto __exit;
    }

    tal_mutex_lock(s_list.mutex);
    ty_cJSON_AddNumberToObject(root, "num", s_list.num);
    ty_cJSON_AddItemToObject(root, "list", arr);
    arr = NULL;

    tuya_list_for_each(pos, &s_list.head) {
        rec_item_t *rec = tuya_list_entry(pos, rec_item_t, node);
        ty_cJSON *item_json = NULL;

        if (rec == NULL) {
            continue;
        }

        item_json = ty_cJSON_CreateObject();
        if (item_json == NULL) {
            tal_mutex_unlock(s_list.mutex);
            PR_ERR("recording: create item json failed");
            goto __exit;
        }

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
        {
            char md5_hex[REC_MD5_HEX_LEN + 1] = {0};
            md5_to_hex(rec->md5, md5_hex);
            ty_cJSON_AddStringToObject(item_json, "md5", md5_hex);
            ty_cJSON_AddNumberToObject(item_json, "transcribe_status", rec->transcribe_status);
            ty_cJSON_AddStringToObject(item_json, "transcribe_filename", rec->transcribe_filename);
            ty_cJSON_AddStringToObject(item_json, "summary_filename", rec->summary_filename);
        }
        ty_cJSON_AddItemToArray(ty_cJSON_GetObjectItem(root, "list"), item_json);
    }

    json_str = ty_cJSON_PrintUnformatted(root);
    if (json_str == NULL) {
        tal_mutex_unlock(s_list.mutex);
        PR_ERR("recording: print json failed");
        goto __exit;
    }

    /* Stage to a temp file, fsync it, then rename it onto the index.
     * NOTE: the backing FS is FatFs, whose f_rename FAILS with FR_EXIST when the
     * destination already exists (it does NOT replace like POSIX). So we must
     * remove the old index before the rename — otherwise every save after the
     * first silently fails, the index never updates, and crash-recovery re-scans
     * (and re-md5s) every on-disk recording on EVERY boot. Trade-off: a power loss
     * in the tiny window between remove and rename leaves no index, which
     * list_recover_orphans() simply rebuilds on next boot. */
    if (ui_fs_path(tmp_path, sizeof(tmp_path), UI_FS_RECORDING, REC_INDEX_TMP_NAME) != OPRT_OK ||
        ui_fs_path(info_path, sizeof(info_path), UI_FS_RECORDING, REC_INDEX_NAME) != OPRT_OK) {
        tal_mutex_unlock(s_list.mutex);
        PR_ERR("recording: build index path failed");
        goto __exit;
    }

    fp = tkl_fopen(tmp_path, "wb");
    if (fp == NULL) {
        tal_mutex_unlock(s_list.mutex);
        PR_ERR("recording: open json tmp write failed");
        goto __exit;
    }

    write_size = (int)strlen(json_str);
    if (tkl_fwrite(json_str, write_size, fp) != write_size) {
        PR_ERR("recording: write json failed");
        tkl_fclose(fp); fp = NULL;
        tkl_fs_remove(tmp_path);
    } else {
        tkl_fflush(fp);
        tkl_fsync(tkl_fileno(fp));
        tkl_fclose(fp); fp = NULL;
        tkl_fs_remove(info_path);   /* FatFs f_rename won't overwrite — drop old index first */
        if (tkl_fs_rename(tmp_path, info_path) != 0) {
            PR_ERR("recording: index rename failed");
            tkl_fs_remove(tmp_path);
        }
    }
    tal_mutex_unlock(s_list.mutex);

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

/* Lazily initialize the in-memory list (mutex, head, JSON load). */
static OPERATE_RET list_init(void)
{
    OPERATE_RET rt = OPRT_OK;

    if (s_list.inited == TRUE) {
        return OPRT_OK;
    }

    INIT_LIST_HEAD(&s_list.head);
    s_list.num = 0;
    TUYA_CALL_ERR_RETURN(tal_mutex_create_init(&s_list.mutex));
    s_list.inited = TRUE;

#if defined(WUKONG_STORAGE_ENABLE) && (WUKONG_STORAGE_ENABLE == 1)
    /* Index load + orphan recovery are deferred to the storage.ready path (see
     * ui_svc_recording_init()): the volume mounts asynchronously on
     * WORKQ_SYSTEM, so loading here would race the mount — tkl_faccess fails
     * on the unmounted volume, s_list.loaded latches TRUE, the index never
     * loads this boot, and orphan recovery would then rewrite it with
     * estimated metadata (real durations/timestamps/md5 lost). */
#else
    /* Storage backend NONE: no mount ever happens; the immediate load attempt
     * fails fast in ui_fs_path() and the list simply stays empty. */
    list_load();
    /* Orphan recovery md5-hashes whole on-disk files; a large interrupted recording
     * (e.g. a multi-minute .opus) can block for tens of seconds. ui_svc_recording_init()
     * runs synchronously inside ui_services_init() -> ui_app_init(), BEFORE ui_port_start(),
     * so doing it inline delays the first UI frame by exactly that long. Run it off the
     * boot/first-frame path on WORKQ_SYSTEM; recovered recordings appear in the list shortly
     * after (list mutations are mutex-guarded). Fall back to inline if scheduling fails. */
    if (tal_workq_schedule(WORKQ_SYSTEM, recover_orphans_work_cb, NULL) != OPRT_OK) {
        PR_ERR("recording: schedule orphan recovery failed; running inline");
        list_recover_orphans();
    }
#endif
    return rt;
}

/* Approximate seconds from an opus byte length at the configured encoder bitrate.
 * Only used for interrupted (recovered) recordings whose exact tick duration was
 * lost; the encoder may be VBR so this is a rough estimate. */
static uint32_t rec_estimate_duration(uint64_t bytes)
{
    uint32_t bps = APP_OPUS_ENCODER_BITRATE;
    if (bps == 0) bps = 16000;
    return (uint32_t)((bytes * 8ULL) / bps);
}

/* True if a recording with this leaf name is already in the list. Caller holds the mutex. */
static BOOL_T rec_name_in_list_locked(const char *name)
{
    LIST_HEAD *pos = NULL;
    tuya_list_for_each(pos, &s_list.head) {
        rec_item_t *r = tuya_list_entry(pos, rec_item_t, node);
        if (r != NULL && strcmp(r->name, name) == 0) {
            return TRUE;
        }
    }
    return FALSE;
}

/* Parse "REC_YYYYMMDD_HHMMSS.opus" into create_time, humanized like list_load
 * stores it (tm_year = full year, tm_mon = 1-based). OPRT_OK on a clean parse. */
static OPERATE_RET rec_parse_name_time(const char *name, POSIX_TM_S *tm)
{
    int y, mo, d, h, mi, s;
    if (name == NULL || tm == NULL) {
        return OPRT_INVALID_PARM;
    }
    if (sscanf(name, "REC_%4d%2d%2d_%2d%2d%2d", &y, &mo, &d, &h, &mi, &s) != 6) {
        return OPRT_COM_ERROR;
    }
    memset(tm, 0, sizeof(*tm));
    tm->tm_year = y; tm->tm_mon = mo; tm->tm_mday = d;
    tm->tm_hour = h; tm->tm_min  = mi; tm->tm_sec  = s;
    return OPRT_OK;
}

/* Crash recovery. Because there is no per-file "complete" marker, the on-disk
 * directory is the source of truth: any REC_*.opus present but missing from the
 * index is an interrupted capture — re-register it (size, name-derived time,
 * estimated duration). md5 is left zero here and recomputed lazily at upload
 * time (the only consumer) — a multi-minute interrupted recording otherwise
 * blocks boot for tens of seconds on a full-file hash. Conversely, index entries
 * whose audio file has vanished are dropped. Runs once, right after list_load(). */
static void list_recover_orphans(void)
{
    char dir_path[REC_PATH_MAX];
    TUYA_DIR d = NULL;
    TUYA_FILEINFO info;
    BOOL_T changed = FALSE;
    LIST_HEAD *pos = NULL, *next = NULL;

    if (ui_fs_path(dir_path, sizeof(dir_path), UI_FS_RECORDING, NULL) != OPRT_OK) {
        return;
    }
    if (tkl_dir_open(dir_path, &d) != 0 || d == NULL) {
        return;
    }

    while (tkl_dir_read(d, &info) == 0) {
        const char *nm = NULL;
        BOOL_T is_reg = FALSE;
        char full[REC_PATH_MAX];
        INT_T sz;
        rec_item_t *item = NULL;
        size_t nlen;
        int id;

        if (tkl_dir_name(info, &nm) != 0 || nm == NULL) {
            break;   /* no resolvable name -> avoid spinning */
        }
        if (nm[0] == '.') {
            continue;
        }
        tkl_dir_is_regular(info, &is_reg);
        if (!is_reg) {
            continue;                                   /* skip the transcribe/ subdir */
        }
        nlen = strlen(nm);
        if (strncmp(nm, "REC_", 4) != 0 || nlen < 5 ||
            strcmp(nm + nlen - 5, ".opus") != 0) {
            continue;                                   /* not a capture file (e.g. the index json) */
        }

        tal_mutex_lock(s_list.mutex);
        if (rec_name_in_list_locked(nm)) {              /* already known -> not an orphan */
            tal_mutex_unlock(s_list.mutex);
            continue;
        }
        tal_mutex_unlock(s_list.mutex);

        if (ui_fs_path(full, sizeof(full), UI_FS_RECORDING, nm) != OPRT_OK) {
            continue;
        }
        sz = tkl_fgetsize(full);
        if (sz <= 0) {                                  /* empty stub -> drop the file */
            tkl_fs_remove(full);
            continue;
        }

        item = (rec_item_t *)tal_malloc(sizeof(rec_item_t));
        if (item == NULL) {
            continue;
        }
        memset(item, 0, sizeof(rec_item_t));
        snprintf(item->name, sizeof(item->name), "%s", nm);
        item->len              = (uint64_t)sz;
        item->duration_sec     = rec_estimate_duration(item->len);   /* approximate */
        /* md5 left zero — recomputed lazily at upload time (see header). */
        item->transcribe_status = UI_TRANSCRIBE_NOT_UPLOADED;
        if (rec_parse_name_time(nm, &item->create_time) != OPRT_OK) {
            POSIX_TM_S now;
            memset(&now, 0, sizeof(now));
            if (tal_time_get_local_time_custom(0, &now) == OPRT_OK) {
                item->create_time.tm_year = now.tm_year + 1900;
                item->create_time.tm_mon  = now.tm_mon + 1;
                item->create_time.tm_mday = now.tm_mday;
                item->create_time.tm_hour = now.tm_hour;
                item->create_time.tm_min  = now.tm_min;
                item->create_time.tm_sec  = now.tm_sec;
            }
        }

        tal_mutex_lock(s_list.mutex);
        if (s_list.num >= REC_ITEM_NUM_MAX && !tuya_list_empty(&s_list.head)) {
            rec_item_t *oldest = tuya_list_entry(s_list.head.next, rec_item_t, node);
            if (oldest != NULL) {
                char del[REC_PATH_MAX];
                if (ui_fs_path(del, sizeof(del), UI_FS_RECORDING, oldest->name) == OPRT_OK) {
                    tkl_fs_remove(del);
                }
                tuya_list_del(&oldest->node);
                tal_free(oldest);
                s_list.num--;
            }
        }
        id = list_alloc_id_locked();
        if (id < 0) {
            tal_mutex_unlock(s_list.mutex);
            tal_free(item);
            continue;
        }
        item->id = id;
        tuya_list_add_tail(&item->node, &s_list.head);
        s_list.num++;
        tal_mutex_unlock(s_list.mutex);
        changed = TRUE;
        PR_INFO("recording: recovered orphan %s size=%ld dur~%us",
                nm, (long)sz, (unsigned)item->duration_sec);
    }
    tkl_dir_close(d);

    /* Drop index entries whose audio file is gone (stale after an aborted save). */
    tal_mutex_lock(s_list.mutex);
    tuya_list_for_each_safe(pos, next, &s_list.head) {
        rec_item_t *r = tuya_list_entry(pos, rec_item_t, node);
        char full[REC_PATH_MAX];
        if (r == NULL) {
            continue;
        }
        if (ui_fs_path(full, sizeof(full), UI_FS_RECORDING, r->name) != OPRT_OK) {
            continue;
        }
        if (tkl_fgetsize(full) <= 0) {
            PR_WARN("recording: drop stale entry (file gone) %s", r->name);
            tuya_list_del(&r->node);
            tal_free(r);
            if (s_list.num > 0) {
                s_list.num--;
            }
            changed = TRUE;
        }
    }
    tal_mutex_unlock(s_list.mutex);

    if (changed) {
        list_save();
    }
}

/* WORKQ_SYSTEM wrapper so orphan recovery (heavy md5 over on-disk files) runs off
 * the boot/first-frame path. See list_init(). */
static void recover_orphans_work_cb(void *arg)
{
    (void)arg;
    list_recover_orphans();
}

/* Pick the lowest unused id in [0, REC_ITEM_NUM_MAX). Caller holds the mutex.
 * Returns the free id, or -1 when the list is full. */
static int list_alloc_id_locked(void)
{
    LIST_HEAD *pos = NULL;
    BOOL_T used[REC_ITEM_NUM_MAX] = {FALSE};
    int id = 0;

    tuya_list_for_each(pos, &s_list.head) {
        rec_item_t *rec = tuya_list_entry(pos, rec_item_t, node);
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

/* Drop a recording entry by id and remove the underlying file.
 * Returns OPRT_OK when removed, OPRT_COM_ERROR otherwise. */
static OPERATE_RET list_delete_by_id(int id)
{
    LIST_HEAD *pos = NULL;
    LIST_HEAD *next = NULL;
    OPERATE_RET rt = OPRT_COM_ERROR;
    char del_path[REC_PATH_MAX];

    if (list_init() != OPRT_OK) {
        return OPRT_COM_ERROR;
    }

    tal_mutex_lock(s_list.mutex);
    tuya_list_for_each_safe(pos, next, &s_list.head) {
        rec_item_t *rec = tuya_list_entry(pos, rec_item_t, node);
        if (rec == NULL) {
            continue;
        }
        if (rec->id != id) {
            continue;
        }

        if (ui_fs_path(del_path, sizeof(del_path), UI_FS_RECORDING, rec->name) == OPRT_OK) {
            tkl_fs_remove(del_path);
        }
        /* 同步删除云端转写结果文件（转写文本 / 总结文本）。 */
        if (rec->transcribe_filename[0] != '\0' &&
            ui_fs_path(del_path, sizeof(del_path), UI_FS_RECORDING_TRANSCRIBE,
                       rec->transcribe_filename) == OPRT_OK) {
            tkl_fs_remove(del_path);
        }
        if (rec->summary_filename[0] != '\0' &&
            ui_fs_path(del_path, sizeof(del_path), UI_FS_RECORDING_TRANSCRIBE,
                       rec->summary_filename) == OPRT_OK) {
            tkl_fs_remove(del_path);
        }

        tuya_list_del(&rec->node);
        tal_free(rec);
        if (s_list.num > 0) {
            s_list.num--;
        }
        rt = OPRT_OK;
        break;
    }
    tal_mutex_unlock(s_list.mutex);

    if (rt == OPRT_OK) {
        list_save();
    }
    return rt;
}

/* Format a stored POSIX_TM_S into "YYYY-MM-DD HH:MM:SS" for the UI list.
 * create_time is stored already humanized (tm_year is the full year, tm_mon
 * is 1-based) by file_close_and_save()/list_load(), so no +1900/+1 here. */
static void compose_datetime_str(const POSIX_TM_S *tm, char *buf, uint32_t size)
{
    if (!tm || !buf || size == 0) return;
    snprintf(buf, size, "%04d-%02d-%02d %02d:%02d:%02d",
             tm->tm_year, tm->tm_mon, tm->tm_mday,
             tm->tm_hour, tm->tm_min, tm->tm_sec);
}

/* ---------------------------------------------------------------------------
 * Capture: file + audio callback helpers
 *   Adapted from src/view/runtime/ui_record_runtime.c, stripping all
 *   md5 / transcribe concerns. Duration is a single continuous window
 *   (now - s_start_tick); there is no pause/resume here.
 * --------------------------------------------------------------------------- */

/* Build the .opus filename from wall-clock time, with a counter fallback. */
static void make_filename(char *buf, uint32_t size)
{
    POSIX_TM_S tm;
    memset(&tm, 0, sizeof(tm));
    if (tal_time_get_local_time_custom(0, &tm) == OPRT_OK) {
        snprintf(buf, size, "REC_%04d%02d%02d_%02d%02d%02d.opus",
                 tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                 tm.tm_hour, tm.tm_min, tm.tm_sec);
    } else {
        snprintf(buf, size, "REC_%04u.opus", (unsigned)s_fallback_idx);
        s_fallback_idx = (s_fallback_idx + 1) % 10000;
    }
}

/* Open a fresh capture file under <UI_FS_ROOT>/recording/, resetting size. */
static OPERATE_RET file_open(void)
{
    char filepath[REC_PATH_MAX];
    if (s_fp != NULL) { tkl_fclose(s_fp); s_fp = NULL; }
    make_filename(s_cur_name, sizeof(s_cur_name));
    if (ui_fs_path(filepath, sizeof(filepath), UI_FS_RECORDING, s_cur_name) != OPRT_OK) {
        PR_ERR("recording: build file path failed");
        return OPRT_COM_ERROR;
    }
    s_fp = tkl_fopen(filepath, "wb");
    if (s_fp == NULL) { PR_ERR("recording: open file failed"); return OPRT_COM_ERROR; }
    s_file_size = 0;
    s_sync_accum = 0;
    md5_session_begin();   /* 与磁盘写入同步累计哈希 */
    PR_INFO("recording: capture to %s", s_cur_name);
    return OPRT_OK;
}

/* Close the capture file and persist a list entry. Evicts the oldest
 * entry when the list is full. Duration is computed from the single
 * (now - s_start_tick) window. */
static void file_close_and_save(void)
{
    rec_item_t *item = NULL, *oldest = NULL;
    POSIX_TM_S tm;
    char del_path[REC_PATH_MAX];
    int id = -1;

    if (s_fp != NULL) { tkl_fclose(s_fp); s_fp = NULL; }
    if (s_file_size == 0 || s_cur_name[0] == '\0') {
        md5_session_abort(); s_cur_name[0] = '\0'; s_file_size = 0; return;
    }
    if (list_init() != OPRT_OK) { md5_session_abort(); s_cur_name[0] = '\0'; s_file_size = 0; return; }

    item = (rec_item_t *)tal_malloc(sizeof(rec_item_t));
    if (item == NULL) { md5_session_abort(); s_cur_name[0] = '\0'; s_file_size = 0; return; }
    memset(item, 0, sizeof(rec_item_t));
    memset(&tm, 0, sizeof(tm));

    tal_mutex_lock(s_list.mutex);
    if (s_list.num >= REC_ITEM_NUM_MAX && !tuya_list_empty(&s_list.head)) {
        oldest = tuya_list_entry(s_list.head.next, rec_item_t, node);
        if (oldest) {
            if (ui_fs_path(del_path, sizeof(del_path), UI_FS_RECORDING, oldest->name) == OPRT_OK) {
                tkl_fs_remove(del_path);
            }
            if (oldest->transcribe_filename[0] != '\0' &&
                ui_fs_path(del_path, sizeof(del_path), UI_FS_RECORDING_TRANSCRIBE,
                           oldest->transcribe_filename) == OPRT_OK) {
                tkl_fs_remove(del_path);
            }
            if (oldest->summary_filename[0] != '\0' &&
                ui_fs_path(del_path, sizeof(del_path), UI_FS_RECORDING_TRANSCRIBE,
                           oldest->summary_filename) == OPRT_OK) {
                tkl_fs_remove(del_path);
            }
            tuya_list_del(&oldest->node);
            tal_free(oldest);
            s_list.num--;
        }
    }
    id = list_alloc_id_locked();
    if (id < 0) { tal_mutex_unlock(s_list.mutex); tal_free(item); md5_session_abort(); s_cur_name[0]='\0'; s_file_size=0; return; }

    item->id = id;
    snprintf(item->name, sizeof(item->name), "%s", s_cur_name);
    item->len = s_file_size;
    /* 收尾哈希；失败则 md5 留全零（未生成），转写态置「未上传」。 */
    md5_session_finalize(item->md5);
    item->transcribe_status = UI_TRANSCRIBE_NOT_UPLOADED;
    item->duration_sec = (uint32_t)((tal_system_get_tick_count() - s_start_tick) / 1000);
    tal_time_get_local_time_custom(0, &tm);
    item->create_time.tm_year = tm.tm_year + 1900;
    item->create_time.tm_mon  = tm.tm_mon + 1;
    item->create_time.tm_mday = tm.tm_mday;
    item->create_time.tm_hour = tm.tm_hour;
    item->create_time.tm_min  = tm.tm_min;
    item->create_time.tm_sec  = tm.tm_sec;
    tuya_list_add_tail(&item->node, &s_list.head);
    s_list.num++;
    tal_mutex_unlock(s_list.mutex);

    list_save();
    PR_INFO("recording: saved %s size=%lu dur=%us",
            item->name, (unsigned long)item->len, (unsigned)item->duration_sec);
    s_cur_name[0] = '\0';
    s_file_size = 0;
}

/* Append bytes to the capture file (lazy-open) with hashing and periodic
 * fsync. Runs on the writer thread (ctx = its session); the inline fallback
 * path (writer failed to start) passes ctx = NULL. Works on a local fp copy:
 * an abandoned session NULLs s_fp mid-flight and the leaked-but-valid handle
 * must keep absorbing the one in-flight write. */
static OPERATE_RET file_write_and_sync(const void *data, int len, rec_writer_ctx_t *ctx)
{
    TUYA_FILE fp = NULL;
    int written = 0;
    /* Checked before the lazy open too: an abandoned zombie must not re-open
     * a fresh file after writer_stop already NULLed s_fp. */
    if (ctx != NULL && ctx->abandoned) return OPRT_COM_ERROR;
    if (s_fp == NULL && file_open() != OPRT_OK) return OPRT_COM_ERROR;
    fp = s_fp;

    written = tkl_fwrite((void *)data, len, fp);
    if (written != len) { PR_ERR("recording: write failed %d/%d", written, len); return OPRT_COM_ERROR; }
    /* Session abandoned while we were blocked in fwrite: the bytes went to the
     * leaked orphan file (fine, recovery re-hashes from disk), but s_file_size
     * and the md5 ctx may already belong to a NEW session — don't touch them. */
    if (ctx != NULL && ctx->abandoned) return OPRT_COM_ERROR;
    s_file_size += (uint64_t)len;
    md5_session_update(data, len);   /* 哈希与落盘字节一一对应 */

    /* Periodically force data to flash so a power loss costs at most ~REC_FSYNC_BYTES
     * (a few seconds), not the whole session's buffered tail. */
    s_sync_accum += (uint32_t)len;
    if (s_sync_accum >= REC_FSYNC_BYTES) {
        tkl_fflush(fp);
        tkl_fsync(tkl_fileno(fp));
        s_sync_accum = 0;
    }
    return OPRT_OK;
}

/* Drain the ring to the SD card in REC_WR_CHUNK slices. Copies out under the
 * lock, writes outside it, so the audio callback is never blocked behind file
 * IO. On a write failure the remaining backlog is left in the ring — we bail
 * out and retry on the next wake instead of spinning on a broken card. An
 * abandoned ctx just discards its backlog. */
static void writer_drain(rec_writer_ctx_t *ctx, uint8_t *chunk, uint32_t chunk_size)
{
    for (;;) {
        uint32_t n = 0;
        tal_mutex_lock(s_wring_mutex);
        if (ctx->abandoned) {
            ctx->used = 0;
        } else if (ctx->used > 0) {
            uint32_t first;
            n = (ctx->used > chunk_size) ? chunk_size : ctx->used;
            first = REC_WRING_SIZE - ctx->head;
            if (first > n) first = n;
            memcpy(chunk, ctx->ring + ctx->head, first);
            if (n > first) memcpy(chunk + first, ctx->ring, n - first);
            ctx->head = (ctx->head + n) % REC_WRING_SIZE;
            ctx->used -= n;
        }
        tal_mutex_unlock(s_wring_mutex);
        if (n == 0) return;
        if (file_write_and_sync(chunk, (int)n, ctx) != OPRT_OK) return;
    }
}

static void rec_writer_thread(PVOID_T args)
{
    rec_writer_ctx_t *ctx = (rec_writer_ctx_t *)args;
    uint8_t  fallback[256];
    uint8_t *chunk = (uint8_t *)tal_malloc(REC_WR_CHUNK);
    uint32_t chunk_size = REC_WR_CHUNK;

    if (chunk == NULL) {
        PR_WARN("recording: writer chunk alloc failed, using small stack buffer");
        chunk = fallback;
        chunk_size = sizeof(fallback);
    }
    PR_INFO("recording: writer thread enter");
    while (ctx->run) {
        /* Timed wait doubles as a missed-wakeup safety net (binary sem). */
        tal_semaphore_wait(ctx->wake, 500);
        writer_drain(ctx, chunk, chunk_size);
    }
    writer_drain(ctx, chunk, chunk_size);   /* final drain after stop */
    if (chunk != fallback) tal_free(chunk);
    PR_INFO("recording: writer thread exit (dropped=%u B%s)",
            (unsigned)ctx->dropped, ctx->abandoned ? ", abandoned" : "");
    /* On an abandoned ctx this posts to a semaphore nobody will ever wait on
     * again (leaked with the ctx) — it cannot pollute a newer session. */
    tal_semaphore_post(ctx->exited);
}

/* Free a ctx and everything it owns. Only for a ctx no live thread can touch:
 * either the thread was joined, or creation failed before it started. */
static void writer_ctx_free(rec_writer_ctx_t *ctx)
{
    if (ctx == NULL) return;
    if (ctx->wake   != NULL) tal_semaphore_release(ctx->wake);
    if (ctx->exited != NULL) tal_semaphore_release(ctx->exited);
    if (ctx->ring   != NULL) tal_free(ctx->ring);
    tal_free(ctx);
}

/* Bring up the per-session ctx (ring + sems) + writer thread. On any failure
 * everything is torn back down and an error returned — input_audio_cb then
 * degrades to inline writes (pre-writer behaviour) instead of losing the
 * session. */
static OPERATE_RET writer_start(void)
{
    rec_writer_ctx_t *ctx = NULL;
    THREAD_CFG_T cfg = {0};

    if (s_wr_thread != NULL) return OPRT_OK;

    if (s_wring_mutex == NULL &&
        tal_mutex_create_init(&s_wring_mutex) != OPRT_OK) {
        s_wring_mutex = NULL;
        return OPRT_COM_ERROR;
    }

    ctx = (rec_writer_ctx_t *)tal_malloc(sizeof(rec_writer_ctx_t));
    if (ctx == NULL) return OPRT_MALLOC_FAILED;
    memset(ctx, 0, sizeof(rec_writer_ctx_t));
    if (tal_semaphore_create_init(&ctx->wake, 0, 1) != OPRT_OK ||
        tal_semaphore_create_init(&ctx->exited, 0, 1) != OPRT_OK) {
        writer_ctx_free(ctx);
        return OPRT_COM_ERROR;
    }
    ctx->ring = (uint8_t *)tal_malloc(REC_WRING_SIZE);
    if (ctx->ring == NULL) {
        PR_ERR("recording: ring alloc %u failed", (unsigned)REC_WRING_SIZE);
        writer_ctx_free(ctx);
        return OPRT_MALLOC_FAILED;
    }

    ctx->run = TRUE;
    cfg.stackDepth = REC_WR_THREAD_STACK;
    cfg.priority   = THREAD_PRIO_2;
    cfg.thrdname   = "rec_sd_writer";
#if defined(ENABLE_EXT_RAM) && (ENABLE_EXT_RAM == 1)
    cfg.psram_mode = 1;
#endif
    if (tal_thread_create_and_start(&s_wr_thread, NULL, NULL,
                                    rec_writer_thread, ctx, &cfg) != OPRT_OK) {
        PR_ERR("recording: writer thread create failed");
        s_wr_thread = NULL;
        writer_ctx_free(ctx);
        return OPRT_COM_ERROR;
    }

    /* Publish last: the audio callback only sees a fully-initialized ctx. */
    tal_mutex_lock(s_wring_mutex);
    s_wr_ctx = ctx;
    tal_mutex_unlock(s_wring_mutex);
    return OPRT_OK;
}

/* Stop the writer: unpublish the ctx, poke the thread awake, wait for the
 * final drain, then reclaim thread + ctx. Must run after s_running is cleared
 * (no new pushes) and before file_close_and_save (writer feeds s_fp/md5 while
 * alive). On join timeout the whole session is abandoned power-loss-style:
 * ctx/thread/file handle/md5 ctx all leak, the on-disk file stays un-indexed
 * and next boot's orphan recovery adopts it. */
static void writer_stop(void)
{
    rec_writer_ctx_t *ctx = NULL;

    if (s_wr_thread == NULL) return;

    tal_mutex_lock(s_wring_mutex);
    ctx = s_wr_ctx;
    s_wr_ctx = NULL;             /* audio callback stops feeding this session */
    tal_mutex_unlock(s_wring_mutex);
    if (ctx == NULL) return;

    ctx->run = FALSE;
    tal_semaphore_post(ctx->wake);
    if (tal_semaphore_wait(ctx->exited, REC_WR_EXIT_WAIT_MS) != OPRT_OK) {
        /* Writer wedged inside fwrite/fsync. Poison the ctx so on un-stick it
         * discards its backlog and exits without touching the session globals
         * again, and leak everything it can still reach: ctx + sems + ring +
         * thread handle + file handle + md5 ctx (NULLed, NOT freed — the
         * zombie may be one instruction away from md5_session_update). */
        tal_mutex_lock(s_wring_mutex);
        ctx->abandoned = TRUE;
        tal_mutex_unlock(s_wring_mutex);
        s_wr_thread = NULL;
        s_fp = NULL;             /* file_open must not fclose the zombie's fp */
        s_md5_ctx = NULL;        /* md5_session_begin must not free it either */
        s_md5_failed = FALSE;
        s_cur_name[0] = '\0';    /* no index entry: orphan recovery owns it */
        s_file_size = 0;
        s_sync_accum = 0;
        PR_ERR("recording: writer exit timeout, session abandoned (file recovers as orphan on next boot)");
        return;
    }
    tal_thread_delete(s_wr_thread);
    s_wr_thread = NULL;

    if (ctx->dropped > 0) {
        PR_WARN("recording: session dropped %u B at ring (SD too slow)", (unsigned)ctx->dropped);
    }
    writer_ctx_free(ctx);
}

/* Audio data sink: queue OPUS packets for the SD writer thread. Runs on the
 * audio pipeline thread whose upstream queue only holds ~0.9s — so this path
 * must never touch the filesystem. */
static OPERATE_RET input_audio_cb(AI_AUDIO_CODEC_TYPE codec, void *data, int len)
{
    BOOL_T queued = FALSE, dropped = FALSE;
    uint32_t drop_total = 0, drop_logged = 0;

    if (codec != AUDIO_CODEC_OPUS) {
        PR_ERR("recording: non-OPUS codec %u rejected", (unsigned)codec);
        return OPRT_NOT_SUPPORTED;
    }
    if (data == NULL || len <= 0) return OPRT_OK;
    if (s_running == FALSE) return OPRT_OK;

    if (s_wring_mutex != NULL) {
        tal_mutex_lock(s_wring_mutex);
        if (s_wr_ctx != NULL) {
            rec_writer_ctx_t *ctx = s_wr_ctx;
            if (REC_WRING_SIZE - ctx->used >= (uint32_t)len) {
                uint32_t tail = (ctx->head + ctx->used) % REC_WRING_SIZE;
                uint32_t first = REC_WRING_SIZE - tail;
                if (first > (uint32_t)len) first = (uint32_t)len;
                memcpy(ctx->ring + tail, data, first);
                if ((uint32_t)len > first) {
                    memcpy(ctx->ring, (const uint8_t *)data + first, (uint32_t)len - first);
                }
                ctx->used += (uint32_t)len;
                queued = TRUE;
            } else {
                ctx->dropped += (uint32_t)len;
                drop_total  = ctx->dropped;
                drop_logged = ctx->drop_logged;
                if (drop_logged == 0 || drop_total - drop_logged >= REC_WR_DROP_LOG_STEP) {
                    ctx->drop_logged = drop_total;
                    dropped = TRUE;   /* log outside the lock */
                }
            }
            /* Post inside the lock: ctx cannot be freed while we hold it
             * (writer_stop unpublishes under this mutex before joining). */
            if (queued) tal_semaphore_post(ctx->wake);
        }
        tal_mutex_unlock(s_wring_mutex);
    }
    if (queued) return OPRT_OK;
    if (dropped) {
        /* First drop and every REC_WR_DROP_LOG_STEP after: the old silent-loss
         * failure mode must be loud. */
        PR_WARN("recording: ring full, dropped %u B total", (unsigned)drop_total);
        return OPRT_OK;
    }
    /* ctx unpublished but a writer session exists/existed this capture: we are
     * in writer_stop teardown — drop rather than race file_close_and_save. */
    if (s_wr_thread != NULL) return OPRT_OK;
    /* Writer unavailable (start failed): inline write, the pre-writer path. */
    return file_write_and_sync(data, len, NULL);
}

/* Register the capture audio callback with the AI mode layer. */
static OPERATE_RET register_audio_cb(void)
{
    AI_RECORD_HANDLE_T handle = {0};
    handle.input_audio = input_audio_cb;
    return wukong_ai_record_handle_set(&handle);
}

/* ---------------------------------------------------------------------------
 * MD5 streaming session + hex helpers
 *   Ported from src/view/runtime/ui_record_runtime.c. The digest is the cloud
 *   transcription correlation key; all-zero means "未生成" (hash failed / legacy
 *   entry) and gates upload off.
 * --------------------------------------------------------------------------- */

static void md5_to_hex(const uint8_t md5[16], char hex[REC_MD5_HEX_LEN + 1])
{
    int i = 0;
    for (i = 0; i < 16; i++) {
        snprintf(hex + i * 2, 3, "%02x", md5[i]);
    }
    hex[REC_MD5_HEX_LEN] = '\0';
}

/* Parse 32 hex chars into 16 bytes. On any malformed input md5[] is left
 * untouched (caller pre-zeros), so a parse failure reads as "未生成". */
static OPERATE_RET hex_to_md5(const char *hex, uint8_t md5[16])
{
    int i = 0, hi = 0, lo = 0;
    char c = 0;
    if (hex == NULL || strlen(hex) != REC_MD5_HEX_LEN) {
        return OPRT_INVALID_PARM;
    }
    for (i = 0; i < 16; i++) {
        c = hex[i * 2];
        if (c >= '0' && c <= '9') hi = c - '0';
        else if (c >= 'a' && c <= 'f') hi = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') hi = c - 'A' + 10;
        else return OPRT_INVALID_PARM;
        c = hex[i * 2 + 1];
        if (c >= '0' && c <= '9') lo = c - '0';
        else if (c >= 'a' && c <= 'f') lo = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') lo = c - 'A' + 10;
        else return OPRT_INVALID_PARM;
        md5[i] = (uint8_t)((hi << 4) | lo);
    }
    return OPRT_OK;
}

static BOOL_T md5_is_zero(const uint8_t md5[16])
{
    int i = 0;
    if (md5 == NULL) return TRUE;
    for (i = 0; i < 16; i++) {
        if (md5[i] != 0) return FALSE;
    }
    return TRUE;
}

static void md5_session_begin(void)
{
    OPERATE_RET rt = OPRT_OK;
    if (s_md5_ctx != NULL) {        /* stale ctx from an aborted session */
        tal_md5_free(s_md5_ctx);
        s_md5_ctx = NULL;
    }
    s_md5_failed = FALSE;
    rt = tal_md5_create_init(&s_md5_ctx);
    if (rt != OPRT_OK) {
        PR_ERR("recording md5: create_init failed: %d", rt);
        s_md5_ctx = NULL; s_md5_failed = TRUE; return;
    }
    rt = tal_md5_starts_ret(s_md5_ctx);
    if (rt != OPRT_OK) {
        PR_ERR("recording md5: starts_ret failed: %d", rt);
        tal_md5_free(s_md5_ctx); s_md5_ctx = NULL; s_md5_failed = TRUE;
    }
}

static void md5_session_update(const void *data, int len)
{
    OPERATE_RET rt = OPRT_OK;
    if (s_md5_ctx == NULL || s_md5_failed == TRUE) return;
    if (data == NULL || len <= 0) return;
    rt = tal_md5_update_ret(s_md5_ctx, (const uint8_t *)data, (size_t)len);
    if (rt != OPRT_OK) {
        PR_ERR("recording md5: update_ret failed: %d (len=%d)", rt, len);
        s_md5_failed = TRUE;
    }
}

/* Finalize and tear down; out_md5 zeroed on any failure. */
static void md5_session_finalize(uint8_t out_md5[16])
{
    OPERATE_RET rt = OPRT_OK;
    if (out_md5 == NULL) { md5_session_abort(); return; }
    memset(out_md5, 0, 16);
    if (s_md5_ctx == NULL) return;
    if (s_md5_failed == TRUE) {
        PR_ERR("recording md5: session failed — entry kept with zero digest");
        tal_md5_free(s_md5_ctx); s_md5_ctx = NULL; s_md5_failed = FALSE; return;
    }
    rt = tal_md5_finish_ret(s_md5_ctx, out_md5);
    if (rt != OPRT_OK) {
        PR_ERR("recording md5: finish_ret failed: %d — zero digest", rt);
        memset(out_md5, 0, 16);
    }
    tal_md5_free(s_md5_ctx); s_md5_ctx = NULL; s_md5_failed = FALSE;
}

static void md5_session_abort(void)
{
    if (s_md5_ctx != NULL) { tal_md5_free(s_md5_ctx); s_md5_ctx = NULL; }
    s_md5_failed = FALSE;
}

/* Recompute MD5 over an on-disk recording file (crash-recovery path). The live
 * session hashes exactly the bytes written to disk, so re-reading the surviving
 * file reproduces the identical digest for its content. out_md5 is zeroed on any
 * error. */
static OPERATE_RET md5_compute_file(const char *path, uint8_t out_md5[16])
{
    TKL_HASH_HANDLE ctx = NULL;
    TUYA_FILE fp = NULL;
    uint8_t *buf = NULL;
    int rd = 0;
    OPERATE_RET rt = OPRT_COM_ERROR;

    if (out_md5 != NULL) {
        memset(out_md5, 0, 16);
    }
    if (path == NULL || out_md5 == NULL) {
        return OPRT_INVALID_PARM;
    }

    fp = tkl_fopen(path, "rb");
    if (fp == NULL) {
        return OPRT_COM_ERROR;
    }
    buf = (uint8_t *)tal_malloc(REC_MD5_READ_CHUNK);
    if (buf == NULL) {
        tkl_fclose(fp);
        return OPRT_MALLOC_FAILED;
    }

    if (tal_md5_create_init(&ctx) != OPRT_OK || ctx == NULL) {
        goto __out;
    }
    if (tal_md5_starts_ret(ctx) != OPRT_OK) {
        goto __out;
    }
    while ((rd = tkl_fread(buf, REC_MD5_READ_CHUNK, fp)) > 0) {
        if (tal_md5_update_ret(ctx, buf, (size_t)rd) != OPRT_OK) {
            goto __out;
        }
        if (rd < REC_MD5_READ_CHUNK) {
            break;   /* short read == EOF */
        }
    }
    if (tal_md5_finish_ret(ctx, out_md5) != OPRT_OK) {
        memset(out_md5, 0, 16);
        goto __out;
    }
    rt = OPRT_OK;

__out:
    if (ctx != NULL) {
        tal_md5_free(ctx);
    }
    tal_free(buf);
    tkl_fclose(fp);
    return rt;
}

/* ---------------------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------------------- */

#if defined(WUKONG_STORAGE_ENABLE) && (WUKONG_STORAGE_ENABLE == 1)
/* Deferred boot load: index load + orphan recovery once the volume is up.
 * Called from two places — the base_event callback path and the
 * ui_svc_recording_init() catch-up path below — both of which run on
 * WORKQ_SYSTEM, so the two invocations are serialized on a single worker
 * thread and the bare s_boot_load_done check is a sufficient idempotency
 * guard (same convention as ui_svc_fs.c / wukong_picture.c).
 *
 * Ordering vs ui_fs: ui_services_init() calls ui_fs_init() before
 * ui_svc_recording_init(), and base_event dispatches subscribers in
 * subscription order (the catch-up path likewise queues behind ui_fs's on the
 * same worker), so by the time this runs the ui_fs standard tree has already
 * been built. */
static bool s_boot_load_done = false;

static OPERATE_RET __rec_on_storage_ready(void *data)
{
    (void)data;
    if (s_boot_load_done) {
        return OPRT_OK;
    }
    s_boot_load_done = true;

    if (list_init() != OPRT_OK) {
        return OPRT_COM_ERROR;
    }
    list_load();
    /* Orphan recovery md5-hashes whole on-disk files (can take tens of
     * seconds for a large interrupted recording) — schedule it as its own
     * work item instead of blocking this callback, mirroring the pre-branch
     * boot flow. It MUST run after list_load(), otherwise indexed recordings
     * would be re-adopted as orphans with estimated metadata. */
    if (tal_workq_schedule(WORKQ_SYSTEM, recover_orphans_work_cb, NULL) != OPRT_OK) {
        PR_ERR("recording: schedule orphan recovery failed; running inline");
        list_recover_orphans();
    }
    return OPRT_OK;
}

/* Trampoline: tal_workq_schedule() calls back with void(*)(void*), whereas the
 * event-subscribe callback returns OPERATE_RET — bridge the two so the
 * catch-up path lands on WORKQ_SYSTEM too instead of running inline on
 * whatever thread calls ui_svc_recording_init(). */
static void __rec_catchup_on_workq(void *data)
{
    (void)data;
    __rec_on_storage_ready(NULL);
}
#endif /* WUKONG_STORAGE_ENABLE */

void ui_svc_recording_init(void)
{
    list_init();
#if defined(WUKONG_STORAGE_ENABLE) && (WUKONG_STORAGE_ENABLE == 1)
    /* Subscribe first, then catch up if storage won the race (mirrors
     * ui_svc_fs.c). Do NOT run __rec_on_storage_ready() inline here: this
     * function runs on the boot thread while the event callback runs on
     * WORKQ_SYSTEM; hopping onto WORKQ_SYSTEM serializes the two. */
    if (ty_subscribe_event(EVENT_WUKONG_STORAGE_READY, "ui_rec",
                           __rec_on_storage_ready, SUBSCRIBE_TYPE_NORMAL) != OPRT_OK) {
        PR_ERR("recording: subscribe storage.ready failed; index will not load");
        return;
    }
    if (wukong_storage_ready()) {
        if (tal_workq_schedule(WORKQ_SYSTEM, __rec_catchup_on_workq, NULL) != OPRT_OK) {
            PR_WARN("recording: catch-up schedule failed, index may not load");
        }
    }
#endif
}

void ui_svc_recording_set_cb(ui_svc_recording_cb_t cb) { s_cb = cb; }

void ui_svc_recording_get_status(ui_recording_status_t *out)
{
    if (!out) return;
    if (s_status.cap_state == UI_REC_CAP_RECORDING) {
        s_status.cap_elapsed_sec =
            (uint32_t)((tal_system_get_tick_count() - s_start_tick) / 1000);
    }
    *out = s_status;
}

OPERATE_RET ui_svc_recording_remove_id(int id)
{
    PR_DEBUG("recording: delete id=%d", id);
    return list_delete_by_id(id);
}

/* Backend teardown when leaving recording: break any chat and restore the
 * device mode that was active before recording. Slow (mode switch re-inits the
 * previous mode), so it runs off the UI thread via WORKQ.
 *
 * Deliberately does NOT stop the audio player: the capture page never plays
 * anything, and by the time this deferred work runs the next page (e.g. music)
 * may have already started its own playback — a stop here would kill it. */
static void capture_restore_apply(void)
{
    wukong_ai_agent_chat_break(NULL);
    wukong_ai_device_mode_switch(s_restore_mode);
    s_restore_pending = FALSE;
}

static void capture_restore_work_cb(void *arg)
{
    uint32_t gen = (uint32_t)(uintptr_t)arg;
    /* Superseded by a newer open (which kept RECORD mode) or close — skip. */
    if (gen != s_close_gen) return;
    capture_restore_apply();
}

OPERATE_RET ui_svc_recording_capture_open(void)
{
    OPERATE_RET rt;
    if (list_init() != OPRT_OK) return OPRT_COM_ERROR;
    if (register_audio_cb() != OPRT_OK) {
        PR_ERR("recording: register audio cb failed");
    }

    /* A prior close's async mode-restore may still be pending: cancel it and
     * keep the original s_mode_before (the device is still in RECORD, so
     * re-reading the mode now would capture RECORD as the restore target). */
    s_close_gen++;
    if (s_restore_pending) {
        s_restore_pending = FALSE;          /* device stays in RECORD; keep s_mode_before */
        s_session_active = TRUE;
    } else if (s_session_active == FALSE) {
        s_mode_before = tuya_ai_toy_device_mode_get();
        s_session_active = TRUE;
    }

    /* Stop music the auto-next-aware way BEFORE switching device mode. A bare
     * wukong_audio_player_stop() (or a stop triggered by the mode switch) fires
     * a STOPPED event that, with auto-next still enabled, makes the music
     * controller advance to the next track — so music silently resumes.
     * ui_svc_music_stop() disables auto-next first, then stops the BG player.
     * (on_route_change does the same on entry as a general safety net, but
     * doing it here guarantees it lands before the mode switch.) */
#if defined(UI_FEATURE_MUSIC) && UI_FEATURE_MUSIC
    ui_svc_music_stop();
#endif
    wukong_ai_agent_chat_break(NULL);
    rt = wukong_ai_device_mode_switch(AI_DEVICE_MODE_RECORD);
    s_running = FALSE;
    s_status.cap_state = UI_REC_CAP_IDLE;
    s_status.cap_elapsed_sec = 0;
    PR_INFO("recording: open (mode_before=%d)", (int)s_mode_before);
    return rt;
}

void ui_svc_recording_capture_close(void)
{
    /* Fast path on the UI thread: stop the mic and persist the file so nothing
     * is lost, then return immediately. */
    s_running = FALSE;
    wukong_audio_input_wakeup_set(FALSE);
    writer_stop();          /* drain + join before touching s_fp/md5 */
    file_close_and_save();
    s_status.cap_state = UI_REC_CAP_IDLE;
    s_status.cap_elapsed_sec = 0;

    /* Defer the slow backend restore (player stop + chat break + device mode
     * switch) to WORKQ so the page transition isn't blocked. */
    if (s_session_active == TRUE) {
        s_session_active = FALSE;
        s_restore_mode = s_mode_before;
        s_restore_pending = TRUE;
        uint32_t gen = ++s_close_gen;
        if (tal_workq_schedule(WORKQ_SYSTEM, capture_restore_work_cb,
                               (void *)(uintptr_t)gen) != OPRT_OK) {
            PR_WARN("recording: schedule restore failed, doing it inline");
            capture_restore_apply();
        }
    }
    PR_INFO("recording: close (deferred restore)");
}

OPERATE_RET ui_svc_recording_capture_start(void)
{
    /* Non-fatal: input_audio_cb degrades to inline SD writes without it. */
    if (writer_start() != OPRT_OK) {
        PR_ERR("recording: SD writer unavailable, capturing with inline writes");
    }
    s_start_tick = (uint32_t)tal_system_get_tick_count();
    s_running = TRUE;
    s_status.cap_state = UI_REC_CAP_RECORDING;
    s_status.cap_elapsed_sec = 0;
    return wukong_audio_input_wakeup_set(TRUE);
}

void ui_svc_recording_capture_stop(void)
{
    s_running = FALSE;
    wukong_audio_input_wakeup_set(FALSE);
    writer_stop();          /* drain + join before touching s_fp/md5 */
    file_close_and_save();
    s_status.cap_state = UI_REC_CAP_IDLE;
}

/* ---------------------------------------------------------------------------
 * Playback
 *   Adapted from src/view/runtime/ui_record_runtime.c (play / pause / resume /
 *   stop). Playback uses the BG player; progress mirrors ui_svc_music.c.
 * --------------------------------------------------------------------------- */

/* Push the current snapshot to the registered page callback. Play controls run
 * on the UI thread (invoked from page handlers), so a direct call is safe. */
static void notify_status(void)
{
    if (s_cb) s_cb(&s_status);
}

void ui_svc_recording_play_id(int id)
{
    LIST_HEAD *pos = NULL;
    char name[REC_NAME_MAX] = {0};
    char filepath[REC_PATH_MAX] = {0};
    BOOL_T found = FALSE;

    if (list_init() != OPRT_OK) return;
    tal_mutex_lock(s_list.mutex);
    tuya_list_for_each(pos, &s_list.head) {
        rec_item_t *r = tuya_list_entry(pos, rec_item_t, node);
        if (r == NULL || r->id != id) continue;
        snprintf(name, sizeof(name), "%s", r->name);
        found = TRUE; break;
    }
    tal_mutex_unlock(s_list.mutex);
    if (!found) { PR_WARN("recording play: id=%d not found", id); return; }

    if (ui_fs_path(filepath, sizeof(filepath), UI_FS_RECORDING, name) != OPRT_OK) {
        PR_ERR("recording play: build path failed id=%d", id);
        return;
    }
    wukong_audio_player_stop(AI_PLAYER_BG);
    /* Reset before the attempt so a failed start can't leave a stale
     * "playing" item showing in the UI. */
    s_status.play_state = AI_PLAYER_STOPPED;
    s_status.play_id = -1;
    if (wukong_audio_play_local(filepath, "录音", NULL, AI_AUDIO_CODEC_OPUS, 0) == OPRT_OK) {
        s_status.play_state = AI_PLAYER_PLAYING;
        s_status.play_id = id;
    } else {
        PR_ERR("recording play: play_local failed id=%d", id);
    }
    notify_status();
}

void ui_svc_recording_play_pause(void)
{
    if (wukong_audio_player_pause() == OPRT_OK) {
        s_status.play_state = AI_PLAYER_PAUSED;
        notify_status();
    }
}

void ui_svc_recording_play_resume(void)
{
    if (wukong_audio_player_resume() == OPRT_OK) {
        s_status.play_state = AI_PLAYER_PLAYING;
        notify_status();
    }
}

void ui_svc_recording_play_stop(void)
{
    wukong_audio_player_stop(AI_PLAYER_BG);
    s_status.play_state = AI_PLAYER_STOPPED;
    s_status.play_id = -1;
    notify_status();
}

int ui_svc_recording_play_current_id(void) { return s_status.play_id; }

OPERATE_RET ui_svc_recording_play_progress_bytes(uint32_t *off, uint32_t *len)
{
    UINT_T o = 0, l = 0;
    OPERATE_RET rt;
    if (off == NULL || len == NULL) return OPRT_INVALID_PARM;
    rt = wukong_audio_player_get_progress(&o, &l);
    if (rt != OPRT_OK) return rt;
    if (l == 0) return OPRT_COM_ERROR;   /* length unknown (e.g. live stream) */
    if (o > l) o = l;
    *off = (uint32_t)o;
    *len = (uint32_t)l;
    return OPRT_OK;
}

/* ---------------------------------------------------------------------------
 * Async list fetch
 *   Mirrors ui_svc_music.c's WORKQ_SYSTEM → ui_app_async_call hand-off, but
 *   builds the ty_cJSON array from our in-memory s_list (no backend call).
 * --------------------------------------------------------------------------- */
static ui_svc_recording_list_cb_t s_list_cb = NULL;

/* UI thread: hand the built list to the receiver (ownership transfers); with
 * no receiver left, free it so nothing leaks. */
static void list_deliver_ui_cb(void *data)
{
    ty_cJSON *list = (ty_cJSON *)data;
    if (s_list_cb) s_list_cb(list);
    else if (list) ty_cJSON_Delete(list);
}

/* WORKQ_SYSTEM：在锁外构造 ty_cJSON 数组，避免 UI 线程持表锁 */
static void list_fetch_work_cb(void *data)
{
    (void)data;
    LIST_HEAD *pos = NULL;
    ty_cJSON *arr = ty_cJSON_CreateArray();
    if (arr == NULL) { ui_app_async_call(list_deliver_ui_cb, NULL); return; }

    if (list_init() == OPRT_OK) {
        tal_mutex_lock(s_list.mutex);
        /* 存储序是旧→新（尾插、淘汰头部），展示要最新在前：倒着遍历。
         * tuya_list 无反向宏，直接走 prev 链。 */
        for (pos = s_list.head.prev; pos != &s_list.head; pos = pos->prev) {
            rec_item_t *r = tuya_list_entry(pos, rec_item_t, node);
            char dt[32] = {0};
            ty_cJSON *o = NULL;
            if (r == NULL) continue;
            o = ty_cJSON_CreateObject();
            if (o == NULL) continue;
            compose_datetime_str(&r->create_time, dt, sizeof(dt));
            ty_cJSON_AddNumberToObject(o, "id", r->id);
            ty_cJSON_AddStringToObject(o, "name", r->name);
            ty_cJSON_AddStringToObject(o, "datetime", dt);
            ty_cJSON_AddNumberToObject(o, "duration", (double)r->duration_sec);
            ty_cJSON_AddItemToArray(arr, o);
        }
        tal_mutex_unlock(s_list.mutex);
    }
    ui_app_async_call(list_deliver_ui_cb, arr);
}

void ui_svc_recording_list_async(ui_svc_recording_list_cb_t cb)
{
    s_list_cb = cb;
    if (tal_workq_schedule(WORKQ_SYSTEM, list_fetch_work_cb, NULL) != OPRT_OK) {
        PR_ERR("recording svc: schedule list fetch failed");
        ui_app_async_call(list_deliver_ui_cb, NULL);
    }
}

/* ---------------------------------------------------------------------------
 * Transcribe-workflow domain API (called by ui_svc_transcribe)
 *   Data ownership stays here: these self-lock the list mutex and persist as
 *   needed, so the transcribe workflow never touches item memory or the JSON.
 * --------------------------------------------------------------------------- */

OPERATE_RET ui_svc_recording_collect_pending(int *ids,
                                             char md5_hex[][REC_MD5_HEX_LEN + 1],
                                             int *count)
{
    LIST_HEAD *pos = NULL;
    int n = 0;
    uint32_t now = (uint32_t)tal_system_get_tick_count();

    if (ids == NULL || md5_hex == NULL || count == NULL) return OPRT_INVALID_PARM;
    *count = 0;
    if (list_init() != OPRT_OK) return OPRT_COM_ERROR;

    tal_mutex_lock(s_list.mutex);
    tuya_list_for_each(pos, &s_list.head) {
        rec_item_t *rec = tuya_list_entry(pos, rec_item_t, node);
        if (rec == NULL) continue;
        if (rec->transcribe_status != UI_TRANSCRIBE_PROCESSING) continue;
        if (md5_is_zero(rec->md5)) continue;
        /* 上传后冷却未到不查；无符号差强转有符号自然处理 tick 回绕。 */
        if ((int32_t)(now - rec->poll_not_before_tick) < 0) continue;
        if (n >= REC_ITEM_NUM_MAX) break;
        ids[n] = rec->id;
        md5_to_hex(rec->md5, md5_hex[n]);
        n++;
    }
    tal_mutex_unlock(s_list.mutex);

    *count = n;
    return OPRT_OK;
}

void ui_svc_recording_mark_uploaded(int id)
{
    LIST_HEAD *pos = NULL;
    BOOL_T changed = FALSE;

    if (list_init() != OPRT_OK) return;
    tal_mutex_lock(s_list.mutex);
    tuya_list_for_each(pos, &s_list.head) {
        rec_item_t *rec = tuya_list_entry(pos, rec_item_t, node);
        if (rec == NULL || rec->id != id) continue;
        rec->transcribe_status   = UI_TRANSCRIBE_PROCESSING;
        rec->poll_not_before_tick =
            (uint32_t)tal_system_get_tick_count() + REC_POLL_FIRST_DELAY_MS;
        changed = TRUE;
        break;
    }
    tal_mutex_unlock(s_list.mutex);
    if (changed) list_save();
}

OPERATE_RET ui_svc_recording_apply_transcribe_result(int id, const char *md5_hex,
                                                     ui_transcribe_status_t st,
                                                     const char *t_file,
                                                     const char *s_file)
{
    LIST_HEAD *pos = NULL;
    BOOL_T applied = FALSE;
    BOOL_T dropped = FALSE;

    if (list_init() != OPRT_OK) return OPRT_COM_ERROR;

    tal_mutex_lock(s_list.mutex);
    tuya_list_for_each(pos, &s_list.head) {
        rec_item_t *rec = tuya_list_entry(pos, rec_item_t, node);
        char cur_hex[REC_MD5_HEX_LEN + 1] = {0};
        if (rec == NULL || rec->id != id) continue;
        md5_to_hex(rec->md5, cur_hex);
        /* anti-ABA：id 可回收复用，md5 变了说明条目在 HTTP 往返期间被替换。 */
        if (md5_hex == NULL || strcmp(cur_hex, md5_hex) != 0) {
            dropped = TRUE;
            break;
        }
        rec->transcribe_status = (int)st;
        if (st == UI_TRANSCRIBE_DONE) {
            if (t_file != NULL) {
                snprintf(rec->transcribe_filename, sizeof(rec->transcribe_filename), "%s", t_file);
            }
            if (s_file != NULL) {
                snprintf(rec->summary_filename, sizeof(rec->summary_filename), "%s", s_file);
            }
        }
        applied = TRUE;
        break;
    }
    tal_mutex_unlock(s_list.mutex);

    if (dropped) {
        /* 已下载文件成了绑定到失效 md5 的孤儿，清掉。 */
        char p[REC_PATH_MAX];
        if (t_file != NULL && t_file[0] != '\0' &&
            ui_fs_path(p, sizeof(p), UI_FS_RECORDING_TRANSCRIBE, t_file) == OPRT_OK) {
            tkl_fs_remove(p);
        }
        if (s_file != NULL && s_file[0] != '\0' &&
            ui_fs_path(p, sizeof(p), UI_FS_RECORDING_TRANSCRIBE, s_file) == OPRT_OK) {
            tkl_fs_remove(p);
        }
        return OPRT_COM_ERROR;
    }
    if (!applied) return OPRT_COM_ERROR;
    list_save();
    return OPRT_OK;
}

ui_transcribe_status_t ui_svc_recording_transcribe_status(int id)
{
    LIST_HEAD *pos = NULL;
    int st = UI_TRANSCRIBE_NOT_UPLOADED;

    if (list_init() != OPRT_OK) return UI_TRANSCRIBE_NOT_UPLOADED;
    tal_mutex_lock(s_list.mutex);
    tuya_list_for_each(pos, &s_list.head) {
        rec_item_t *rec = tuya_list_entry(pos, rec_item_t, node);
        if (rec == NULL || rec->id != id) continue;
        st = rec->transcribe_status;
        break;
    }
    tal_mutex_unlock(s_list.mutex);
    return (ui_transcribe_status_t)st;
}

BOOL_T ui_svc_recording_md5_available(int id)
{
    LIST_HEAD *pos = NULL;
    BOOL_T ok = FALSE;

    if (list_init() != OPRT_OK) return FALSE;
    tal_mutex_lock(s_list.mutex);
    tuya_list_for_each(pos, &s_list.head) {
        rec_item_t *rec = tuya_list_entry(pos, rec_item_t, node);
        if (rec == NULL || rec->id != id) continue;
        ok = md5_is_zero(rec->md5) ? FALSE : TRUE;
        break;
    }
    tal_mutex_unlock(s_list.mutex);
    return ok;
}

/* Write back a recording's md5 (the upload pump computes it lazily while reading
 * the file for upload). Persists the index. md5 == NULL or all-zero is rejected
 * (no point persisting "still pending"). */
OPERATE_RET ui_svc_recording_set_md5(int id, const uint8_t md5[16])
{
    LIST_HEAD *pos = NULL;
    BOOL_T changed = FALSE;

    if (md5 == NULL || md5_is_zero(md5)) return OPRT_INVALID_PARM;
    if (list_init() != OPRT_OK) return OPRT_COM_ERROR;

    tal_mutex_lock(s_list.mutex);
    tuya_list_for_each(pos, &s_list.head) {
        rec_item_t *rec = tuya_list_entry(pos, rec_item_t, node);
        if (rec == NULL || rec->id != id) continue;
        if (memcmp(rec->md5, md5, 16) != 0) {
            memcpy(rec->md5, md5, 16);
            changed = TRUE;
        }
        break;
    }
    tal_mutex_unlock(s_list.mutex);

    if (changed) list_save();
    return changed ? OPRT_OK : OPRT_COM_ERROR;
}

OPERATE_RET ui_svc_recording_get_upload_info(int id, char *name, uint32_t name_size,
                                             uint64_t *out_len, BOOL_T *out_md5_ok)
{
    LIST_HEAD *pos = NULL;
    BOOL_T found = FALSE;

    if (list_init() != OPRT_OK) return OPRT_COM_ERROR;
    tal_mutex_lock(s_list.mutex);
    tuya_list_for_each(pos, &s_list.head) {
        rec_item_t *rec = tuya_list_entry(pos, rec_item_t, node);
        if (rec == NULL || rec->id != id) continue;
        if (name != NULL && name_size > 0) snprintf(name, name_size, "%s", rec->name);
        if (out_len != NULL) *out_len = rec->len;
        if (out_md5_ok != NULL) *out_md5_ok = md5_is_zero(rec->md5) ? FALSE : TRUE;
        found = TRUE;
        break;
    }
    tal_mutex_unlock(s_list.mutex);
    return found ? OPRT_OK : OPRT_COM_ERROR;
}

BOOL_T ui_svc_recording_get_name(int id, char *buf, uint32_t size)
{
    LIST_HEAD *pos = NULL;
    BOOL_T found = FALSE;

    if (buf == NULL || size == 0) return FALSE;
    buf[0] = '\0';
    if (list_init() != OPRT_OK) return FALSE;
    tal_mutex_lock(s_list.mutex);
    tuya_list_for_each(pos, &s_list.head) {
        rec_item_t *rec = tuya_list_entry(pos, rec_item_t, node);
        if (rec == NULL || rec->id != id) continue;
        snprintf(buf, size, "%s", rec->name);
        found = TRUE;
        break;
    }
    tal_mutex_unlock(s_list.mutex);
    return found;
}

/* ---------------------------------------------------------------------------
 * Transcribe-result text read API (for the transcribe page reading card)
 *   List lookup is mutex-guarded; the kernel fs calls happen outside the lock.
 *   The returned handle is owned by the caller (close via _close_file).
 * --------------------------------------------------------------------------- */

BOOL_T ui_svc_recording_has_file(int id, ui_rec_file_kind_t kind)
{
    LIST_HEAD *pos = NULL;
    BOOL_T has = FALSE;

    if (list_init() != OPRT_OK) return FALSE;
    tal_mutex_lock(s_list.mutex);
    tuya_list_for_each(pos, &s_list.head) {
        rec_item_t *rec = tuya_list_entry(pos, rec_item_t, node);
        if (rec == NULL || rec->id != id) continue;
        if (kind == UI_REC_FILE_TRANSCRIBE)      has = (rec->transcribe_filename[0] != '\0');
        else if (kind == UI_REC_FILE_SUMMARY)    has = (rec->summary_filename[0] != '\0');
        break;
    }
    tal_mutex_unlock(s_list.mutex);
    return has;
}

OPERATE_RET ui_svc_recording_open_file(int id, ui_rec_file_kind_t kind,
                                       TUYA_FILE *out_fp, uint32_t *out_size)
{
    LIST_HEAD *pos = NULL;
    BOOL_T found = FALSE;
    char basename[REC_FILENAME_MAX] = {0};
    char path[REC_PATH_MAX] = {0};
    TUYA_FILE fp = NULL;
    int file_size = 0;

    if (out_fp == NULL || out_size == NULL) return OPRT_INVALID_PARM;
    *out_fp = NULL; *out_size = 0;
    if (list_init() != OPRT_OK) return OPRT_COM_ERROR;

    tal_mutex_lock(s_list.mutex);
    tuya_list_for_each(pos, &s_list.head) {
        rec_item_t *rec = tuya_list_entry(pos, rec_item_t, node);
        if (rec == NULL || rec->id != id) continue;
        if (kind == UI_REC_FILE_TRANSCRIBE && rec->transcribe_filename[0] != '\0') {
            snprintf(basename, sizeof(basename), "%s", rec->transcribe_filename); found = TRUE;
        } else if (kind == UI_REC_FILE_SUMMARY && rec->summary_filename[0] != '\0') {
            snprintf(basename, sizeof(basename), "%s", rec->summary_filename); found = TRUE;
        }
        break;
    }
    tal_mutex_unlock(s_list.mutex);

    if (!found) return OPRT_COM_ERROR;
    if (ui_fs_path(path, sizeof(path), UI_FS_RECORDING_TRANSCRIBE, basename) != OPRT_OK) {
        return OPRT_COM_ERROR;
    }
    file_size = tkl_fgetsize(path);
    if (file_size < 0) { PR_ERR("reading: fgetsize failed %s rt=%d", path, file_size); return OPRT_COM_ERROR; }
    fp = tkl_fopen(path, "rb");
    if (fp == NULL) { PR_ERR("reading: fopen failed %s", path); return OPRT_COM_ERROR; }
    *out_fp = fp; *out_size = (uint32_t)file_size;
    return OPRT_OK;
}

int ui_svc_recording_read_at(TUYA_FILE fp, uint32_t offset, char *buf, uint32_t size)
{
    if (fp == NULL || buf == NULL || size == 0) return -1;
    if (tkl_fseek(fp, (int64_t)offset, 0 /* SEEK_SET */) != 0) {
        PR_ERR("reading: fseek failed off=%u", (unsigned)offset);
        return -1;
    }
    return tkl_fread(buf, (int)size, fp);
}

void ui_svc_recording_close_file(TUYA_FILE fp)
{
    if (fp != NULL) tkl_fclose(fp);
}
