#include "ui_svc_transcribe.h"
#include "ui_svc_recording.h"  /* 领域 API + UI_REC_MD5_HEX_LEN */
#include "ui_svc_fs.h"         /* ui_fs_path / UI_FS_RECORDING[_TRANSCRIBE] */
#include "ui_app.h"            /* ui_app_async_call */

#include <string.h>
#include <stdio.h>
#include <stdint.h>

#include "tal_memory.h"
#include "tal_semaphore.h"
#include "tal_thread.h"
#include "tal_system.h"
#include "tal_workq_service.h" /* WORKQ_SYSTEM, tal_workq_schedule */
#include "tal_hash.h"          /* tal_md5_* — 搭便车算 md5（中断恢复的孤儿等） */
#include "tkl_fs.h"
#include "ty_cJSON.h"
#include "uni_log.h"

#include "wukong_ai_agent.h"
#include "wukong_ai_mode.h"    /* wukong_ai_device_mode_switch / AI_DEVICE_MODE_RECORD */
#include "tuya_ai_protocol.h"  /* AI_STREAM_END / AI_STREAM_ONE */
#include "tuya_ai_biz.h"       /* AI_BIZ_*_INFO_T */
#include "tuya_ai_http.h"      /* tuya_ai_http_dld_file */
#include "tuya_iot_internal_api.h" /* iot_httpc_common_post_simple */

/* ---------------------------------------------------------------------------
 * Constants
 * --------------------------------------------------------------------------- */
#define REC_NAME_MAX             64
#define REC_PATH_MAX             128
#define REC_FILENAME_MAX         48
#define REC_MD5_HEX_LEN          UI_REC_MD5_HEX_LEN
#define REC_ITEM_NUM_MAX         20

#define REC_UPLOAD_CHUNK_SIZE    (6 * 1024)
#define REC_UPLOAD_PROMPT        "转写并总结下刚才上传的音频"

#define REC_POLL_API             "m.wearable.audio.device.transcribe.result"
#define REC_POLL_API_VER         "2.0"
#define REC_POLL_INTERVAL_MS     5000    /* 有处理中条目时的复查间隔 */
#define REC_POLL_IDLE_MS         30000   /* 无待处理时的空闲心跳（被上传完成提前唤醒） */
#define REC_DLD_TIMEOUT_MS       5000
#define REC_POLL_THREAD_STACK    (1024 * 20)
#define REC_POLL_POST_BUF_SIZE   1024

/* ---------------------------------------------------------------------------
 * State
 * --------------------------------------------------------------------------- */
/* 上传泵上下文（WORKQ_SYSTEM 自驱动，单实例：同一时刻只允许一个上传） */
typedef struct {
    TUYA_FILE       fp;
    uint8_t        *buf;
    uint64_t        file_len;
    uint64_t        total_sent;
    int             target_id;
    int             last_pct;
    volatile BOOL_T active;
    volatile BOOL_T abort;
    BOOL_T          started;   /* 首个 work 已切入 RECORD + input_start */
    /* 搭便车 md5：仅当条目 md5 为零（中断恢复的孤儿 / 采集时哈希失败）时启用。
     * 正常录制的条目 md5 已在 file_close_and_save 时 finalize，这里 ctx=NULL、
     * 零额外开销。读循环里 update，EOF 时 finalize 并写回。 */
    TKL_HASH_HANDLE md5_ctx;
    BOOL_T          md5_failed;
    BOOL_T          md5_pending;   /* ctx 非空，EOF 时需 finalize+写回 */
} upload_ctx_t;

/* 同步下载上下文（仅轮询线程使用，单飞行） */
typedef struct {
    TUYA_FILE  fp;
    SEM_HANDLE done_sem;
    BOOL_T     received_end;
    BOOL_T     write_failed;
} dld_ctx_t;

static upload_ctx_t            s_upload = { .target_id = -1 };
static dld_ctx_t               s_dld = {0};

/* RECORD 模式持有：上传首 tick 切入 RECORD（删闲聊会话→上传音频走录音会话，
 * 云端据此建转写任务），并**保持到离开转写页**才恢复——这点关键：云端那一轮的
 * 响应在 input_stop 之后约 1~2s 才回来，必须仍处于 RECORD 语境才会静默完成转写；
 * 过早切回会让响应被闲聊/多模态 agent 接管、并打断转写（与旧 view 行为对齐）。
 * s_mode_held 跨多次上传/重试只保存一次原模式。独立于 s_upload，cleanup 不清它。 */
static AI_DEVICE_MODE_E        s_mode_before = AI_DEVICE_MODE_CHAT;
static BOOL_T                  s_mode_held = FALSE;

/* 录音设备模式 getter（项目无公共头，沿用 view 层/recording 服务的 extern 方式） */
extern AI_DEVICE_MODE_E tuya_ai_toy_device_mode_get(void);

static THREAD_HANDLE           s_poll_thread = NULL;
static SEM_HANDLE              s_poll_sem = NULL;
static volatile BOOL_T         s_poll_run = FALSE;

static ui_svc_transcribe_cb_t  s_cb = NULL;

/* ---------------------------------------------------------------------------
 * Forward declarations
 * --------------------------------------------------------------------------- */
static void        upload_work(void *arg);
static void        upload_cleanup(void);
static OPERATE_RET dld_recv_cb(AI_BIZ_ATTR_INFO_T *attr, AI_BIZ_HEAD_INFO_T *head,
                               VOID *data, VOID *usr_data);
static OPERATE_RET sync_dld_file(const char *url, const char *dst, UINT_T timeout_ms);
static OPERATE_RET poll_post_and_get_items(char md5_hex[][REC_MD5_HEX_LEN + 1],
                                           int count, ty_cJSON **out_result, ty_cJSON **out_items);
static OPERATE_RET poll_download_round(ty_cJSON *item, const char *md5_hex, int target_id,
                                       char *t_file, char *s_file);
static void        poll_handle_item(ty_cJSON *item, const int *ids,
                                    char md5_hex[][REC_MD5_HEX_LEN + 1], int count);
static int         poll_round_once(void);
static void        poll_thread(PVOID_T args);

/* ---------------------------------------------------------------------------
 * Status snapshot + UI-thread notify
 * --------------------------------------------------------------------------- */
static ui_transcribe_phase_t compute_phase(int id, int *out_pct)
{
    if (out_pct) *out_pct = -1;

    /* 正在上传该条目 → UPLOADING，附带当前百分比。 */
    if (s_upload.active && s_upload.target_id == id) {
        if (out_pct) {
            int pct = 0;
            if (s_upload.file_len > 0) {
                uint64_t raw = s_upload.total_sent * 100 / s_upload.file_len;
                pct = (raw > 100) ? 100 : (int)raw;
            }
            *out_pct = pct;
        }
        return UI_TRANSCRIBE_PHASE_UPLOADING;
    }

    if (!ui_svc_recording_md5_available(id)) {
        /* md5 待计算（中断恢复的孤儿 / 采集时哈希失败）：仍允许上传，上传泵
         * 会在读文件时顺带算出 md5 再进入 PROCESSING。这里按 transcribe_status
         * 兜底——NOT_UPLOADED 时落到 NOT_UPLOADED（可转写）。 */
        if (ui_svc_recording_transcribe_status(id) == UI_TRANSCRIBE_NOT_UPLOADED) {
            return UI_TRANSCRIBE_PHASE_NOT_UPLOADED;
        }
        return UI_TRANSCRIBE_PHASE_UNAVAILABLE;
    }
    switch (ui_svc_recording_transcribe_status(id)) {
    case UI_TRANSCRIBE_PROCESSING: return UI_TRANSCRIBE_PHASE_PROCESSING;
    case UI_TRANSCRIBE_DONE:       return UI_TRANSCRIBE_PHASE_DONE;
    case UI_TRANSCRIBE_FAILED:     return UI_TRANSCRIBE_PHASE_FAILED;
    case UI_TRANSCRIBE_NOT_UPLOADED:
    default:                       return UI_TRANSCRIBE_PHASE_NOT_UPLOADED;
    }
}

/* Runs on the UI thread (via ui_app_async_call); recomputes the phase fresh so
 * progress / status reflect the latest state at delivery time. */
static void notify_async_cb(void *arg)
{
    int id = (int)(uintptr_t)arg;
    if (s_cb != NULL) {
        ui_transcribe_status_snapshot_t snap;
        ui_svc_transcribe_get_status(id, &snap);
        s_cb(&snap);
    }
}

static void notify(int id)
{
    ui_app_async_call(notify_async_cb, (void *)(uintptr_t)id);
}

void ui_svc_transcribe_get_status(int id, ui_transcribe_status_snapshot_t *out)
{
    if (out == NULL) return;
    out->id = id;
    out->phase = compute_phase(id, &out->upload_percent);
}

void ui_svc_transcribe_set_cb(ui_svc_transcribe_cb_t cb) { s_cb = cb; }

/* ---------------------------------------------------------------------------
 * Upload pump (WORKQ_SYSTEM, self-rescheduling — off the UI thread)
 * --------------------------------------------------------------------------- */
static void upload_cleanup(void)
{
    if (s_upload.buf != NULL) tal_free(s_upload.buf);
    if (s_upload.fp != NULL)  tkl_fclose(s_upload.fp);
    /* md5_ctx 仅在本次会话内有效；失败/正常收尾都释放。 */
    if (s_upload.md5_ctx != NULL) {
        tal_md5_free(s_upload.md5_ctx);
        s_upload.md5_ctx = NULL;
    }
    memset(&s_upload, 0, sizeof(s_upload));
    s_upload.target_id = -1;
}

/* 失败/中止收尾：若已 input_start 则停输入，清理并通知。
 * 注意：不在此恢复设备模式——RECORD 模式由转写页生命周期持有，
 * 统一在离开页面时经 ui_svc_transcribe_release_mode() 恢复（见 .h 说明）。 */
static void upload_finish_fail(int id)
{
    if (s_upload.started) wukong_ai_agent_input_stop();
    upload_cleanup();
    notify(id);   /* 回退到原 phase（NOT_UPLOADED），可重试 */
}

static void upload_work(void *arg)
{
    int read_len = 0;
    int id = s_upload.target_id;
    (void)arg;

    if (!s_upload.active) return;
    if (s_upload.abort) {
        PR_INFO("transcribe upload: aborted id=%d", id);
        upload_finish_fail(id);
        return;
    }
    if (s_upload.fp == NULL || s_upload.buf == NULL) {
        upload_finish_fail(id);
        return;
    }

    /* 首个 work：持有 RECORD 模式（删闲聊会话→上传音频走录音会话，云端才据此
     * 建转写任务），再开输入会话。在 WORKQ 上做，避免阻塞 UI。模式保持到离开
     * 转写页才恢复（见 s_mode_held 说明）；跨重试只切一次。 */
    if (!s_upload.started) {
        if (!s_mode_held) {
            s_mode_before = tuya_ai_toy_device_mode_get();
            s_mode_held = TRUE;
            if (s_mode_before != AI_DEVICE_MODE_RECORD) {
                wukong_ai_agent_chat_break(NULL);
                if (wukong_ai_device_mode_switch(AI_DEVICE_MODE_RECORD) != OPRT_OK) {
                    PR_WARN("transcribe upload: switch to RECORD mode failed");
                }
            }
        }
        wukong_ai_agent_input_start(TRUE);
        s_upload.started = TRUE;
        /* fall through：本次 work 立即泵首块 */
    }

    read_len = tkl_fread(s_upload.buf, REC_UPLOAD_CHUNK_SIZE, s_upload.fp);
    if (read_len > 0) {
        /* 搭便车 md5：与落盘字节一致（fread 读的就是当初 fwrite 的字节）。
         * update 失败置 md5_failed，后续不再 update，EOF 时不写回。 */
        if (s_upload.md5_pending && !s_upload.md5_failed) {
            if (tal_md5_update_ret(s_upload.md5_ctx, s_upload.buf, (size_t)read_len) != OPRT_OK) {
                PR_ERR("transcribe upload: md5 update failed (id=%d, no cloud link)", id);
                s_upload.md5_failed = TRUE;
            }
        }
        if (wukong_ai_agent_send_file(s_upload.buf, (UINT_T)read_len) != OPRT_OK) {
            PR_ERR("transcribe upload: send failed sent=%lu/%lu",
                   (unsigned long)s_upload.total_sent, (unsigned long)s_upload.file_len);
            upload_finish_fail(id);
            return;
        }
        s_upload.total_sent += (uint64_t)read_len;

        int pct = 0;
        if (s_upload.file_len > 0) {
            uint64_t raw = s_upload.total_sent * 100 / s_upload.file_len;
            pct = (raw > 100) ? 100 : (int)raw;
        }
        if (pct != s_upload.last_pct) {
            s_upload.last_pct = pct;
            notify(id);
        }
        /* 自驱动泵下一块；让出 WORKQ 与其他 work 协作。 */
        tal_workq_schedule(WORKQ_SYSTEM, upload_work, NULL);
        return;
    }

    /* EOF — 上传完成。若搭便车 md5 进行中，先 finalize 并写回条目，再
     * mark_uploaded：保证进入 PROCESSING 态时 md5 已落定，轮询/anti-ABA 一致。
     * md5 失败（init/update/finish/set 任意一步，含初始化失败）则不 mark_uploaded：
     * 调用方收到 NOT_UPLOADED 回退、可重试；避免进入一个永远查不到结果的
     * PROCESSING 态。上传字节已发完，但云端无关联键无法建转写任务，等价于
     * 旧逻辑里哈希失败的后果。
     * 注意 md5_failed 判断故意提到 md5_pending 之外：init 失败时 md5_pending
     * 仍为 FALSE（pending 只在 init 成功时才置），若包在 pending 块里会被跳过，
     * 落到 mark_uploaded 违反"失败不 mark"的契约。 */
    if (s_upload.md5_pending && !s_upload.md5_failed) {
        uint8_t md5[16] = {0};
        if (tal_md5_finish_ret(s_upload.md5_ctx, md5) != OPRT_OK) {
            PR_ERR("transcribe upload: md5 finish failed (id=%d)", id);
            s_upload.md5_failed = TRUE;
        } else if (ui_svc_recording_set_md5(id, md5) != OPRT_OK) {
            PR_ERR("transcribe upload: set_md5 failed (id=%d)", id);
            s_upload.md5_failed = TRUE;
        } else {
            PR_INFO("transcribe upload: id=%d md5 computed & saved", id);
        }
    }
    if (s_upload.md5_failed) {
        PR_WARN("transcribe upload: id=%d md5 failed, not marking uploaded (retryable)", id);
        if (s_upload.started) wukong_ai_agent_input_stop();
        upload_cleanup();
        notify(id);   /* 回退到 NOT_UPLOADED，可重试 */
        return;
    }

    PR_INFO("transcribe upload: complete id=%d total=%lu", id, (unsigned long)s_upload.total_sent);
    ui_svc_recording_mark_uploaded(id);
    wukong_ai_agent_send_text(REC_UPLOAD_PROMPT);
    wukong_ai_agent_input_stop();
    upload_cleanup();
    notify(id);
    if (s_poll_sem != NULL) tal_semaphore_post(s_poll_sem);
}

OPERATE_RET ui_svc_transcribe_upload(int id)
{
    char name[REC_NAME_MAX] = {0};
    char path[REC_PATH_MAX] = {0};
    uint64_t len = 0;
    BOOL_T md5_ok = FALSE;
    TUYA_FILE fp = NULL;
    uint8_t *buf = NULL;

    if (s_upload.active) {
        PR_WARN("transcribe upload: already in progress");
        return OPRT_COM_ERROR;
    }
    if (ui_svc_recording_get_upload_info(id, name, sizeof(name), &len, &md5_ok) != OPRT_OK) {
        PR_WARN("transcribe upload: id=%d not found", id);
        return OPRT_COM_ERROR;
    }
    /* md5 不可用不再是硬拒绝：条目 md5 为零（中断恢复的孤儿 / 采集时哈希失败）
     * 时，上传泵在 fread 循环里顺带算出 md5，EOF 后写回再 mark_uploaded。
     * 云端关联键是本地 md5，不在上传字节里，所以"算出来晚于上传开始"无影响。 */
    (void)md5_ok;
    if (ui_fs_path(path, sizeof(path), UI_FS_RECORDING, name) != OPRT_OK) {
        return OPRT_COM_ERROR;
    }
    fp = tkl_fopen(path, "rb");
    if (fp == NULL) {
        PR_ERR("transcribe upload: open %s failed", path);
        return OPRT_COM_ERROR;
    }
    buf = (uint8_t *)tal_malloc(REC_UPLOAD_CHUNK_SIZE);
    if (buf == NULL) {
        tkl_fclose(fp);
        return OPRT_MALLOC_FAILED;
    }

    memset(&s_upload, 0, sizeof(s_upload));
    s_upload.fp        = fp;
    s_upload.buf       = buf;
    s_upload.file_len  = len;
    s_upload.target_id = id;
    s_upload.last_pct  = -1;
    s_upload.active    = TRUE;
    s_upload.started   = FALSE;

    /* 搭便车 md5：仅 md5 不可用时启用。正常录制条目 md5 已 finalize，跳过。
     * 初始化失败不阻断上传——置 md5_failed，EOF 时不写回、不 mark_uploaded，
     * 上传本身仍完成；只是这条无法关联云端转写（同旧逻辑里哈希失败的后果）。 */
    if (!md5_ok) {
        OPERATE_RET rt = tal_md5_create_init(&s_upload.md5_ctx);
        if (rt != OPRT_OK || s_upload.md5_ctx == NULL) {
            PR_ERR("transcribe upload: md5 ctx create failed: %d (id=%d, no cloud link)", rt, id);
            s_upload.md5_ctx = NULL;
            s_upload.md5_failed = TRUE;
        } else if (tal_md5_starts_ret(s_upload.md5_ctx) != OPRT_OK) {
            PR_ERR("transcribe upload: md5 starts failed (id=%d, no cloud link)", id);
            tal_md5_free(s_upload.md5_ctx);
            s_upload.md5_ctx = NULL;
            s_upload.md5_failed = TRUE;
        } else {
            s_upload.md5_pending = TRUE;
            PR_INFO("transcribe upload: id=%d md5 pending, will compute during upload", id);
        }
    }

    /* 模式切入 + input_start 放到首个 upload_work（WORKQ）里做，不阻塞 UI 线程。 */
    PR_INFO("transcribe upload: start id=%d %s len=%lu", id, path, (unsigned long)len);
    notify(id);   /* UPLOADING 0% */

    if (tal_workq_schedule(WORKQ_SYSTEM, upload_work, NULL) != OPRT_OK) {
        PR_ERR("transcribe upload: schedule failed");
        upload_cleanup();
        return OPRT_COM_ERROR;
    }
    return OPRT_OK;
}

/* ---------------------------------------------------------------------------
 * Synchronous file download (sem + timeout) — only the poll thread calls this
 * --------------------------------------------------------------------------- */
static OPERATE_RET dld_recv_cb(AI_BIZ_ATTR_INFO_T *attr, AI_BIZ_HEAD_INFO_T *head,
                               VOID *data, VOID *usr_data)
{
    (void)attr;
    (void)usr_data;
    if (head == NULL) return OPRT_OK;

    if (s_dld.fp != NULL && data != NULL && head->len > 0) {
        int n = tkl_fwrite(data, (int)head->len, s_dld.fp);
        if (n != (int)head->len) {
            s_dld.write_failed = TRUE;
            PR_ERR("transcribe dld: write expect=%u got=%d", (unsigned)head->len, n);
        }
    }
    if (head->stream_flag == AI_STREAM_END || head->stream_flag == AI_STREAM_ONE) {
        s_dld.received_end = TRUE;
        if (s_dld.done_sem != NULL) tal_semaphore_post(s_dld.done_sem);
    }
    return OPRT_OK;
}

static OPERATE_RET sync_dld_file(const char *url, const char *dst, UINT_T timeout_ms)
{
    OPERATE_RET rt = OPRT_OK;

    if (url == NULL || url[0] == '\0' || dst == NULL || dst[0] == '\0') {
        return OPRT_INVALID_PARM;
    }
    memset(&s_dld, 0, sizeof(s_dld));
    rt = tal_semaphore_create_init(&s_dld.done_sem, 0, 1);
    if (rt != OPRT_OK) {
        PR_ERR("transcribe dld: sem create failed %d", rt);
        return rt;
    }
    s_dld.fp = tkl_fopen(dst, "wb");
    if (s_dld.fp == NULL) {
        PR_ERR("transcribe dld: open dst failed %s", dst);
        tal_semaphore_release(s_dld.done_sem);
        s_dld.done_sem = NULL;
        return OPRT_COM_ERROR;
    }

    rt = tuya_ai_http_dld_file((char *)url, dld_recv_cb);
    if (rt == OPRT_OK) {
        /* dld_file 失败不回调 cb，sem 超时是唯一的失败探测。 */
        rt = tal_semaphore_wait(s_dld.done_sem, timeout_ms);
    }

    if (s_dld.fp != NULL) { tkl_fclose(s_dld.fp); s_dld.fp = NULL; }
    tal_semaphore_release(s_dld.done_sem);
    s_dld.done_sem = NULL;

    if (rt != OPRT_OK || !s_dld.received_end || s_dld.write_failed) {
        PR_WARN("transcribe dld: failed url=%s rt=%d end=%d wf=%d",
                url, rt, (int)s_dld.received_end, (int)s_dld.write_failed);
        tkl_fs_remove(dst);
        return (rt != OPRT_OK) ? rt : OPRT_COM_ERROR;
    }
    return OPRT_OK;
}

/* ---------------------------------------------------------------------------
 * Poll: POST md5 list, parse results, download done files, apply via recording
 * --------------------------------------------------------------------------- */
static OPERATE_RET poll_post_and_get_items(char md5_hex[][REC_MD5_HEX_LEN + 1],
                                           int count, ty_cJSON **out_result, ty_cJSON **out_items)
{
    char post_body[REC_POLL_POST_BUF_SIZE];
    ty_cJSON *result = NULL;
    ty_cJSON *items = NULL;
    OPERATE_RET rt = OPRT_OK;
    int offset = 0;
    int i = 0;

    *out_result = NULL;
    *out_items  = NULL;

    offset = snprintf(post_body, sizeof(post_body), "{\"md5List\":[");
    for (i = 0; i < count; i++) {
        if (offset >= (int)sizeof(post_body) - (REC_MD5_HEX_LEN + 8)) break;
        offset += snprintf(post_body + offset, sizeof(post_body) - offset,
                           "%s\"%s\"", (i > 0) ? "," : "", md5_hex[i]);
    }
    if (offset < (int)sizeof(post_body) - 2) {
        snprintf(post_body + offset, sizeof(post_body) - offset, "]}");
    }

    rt = iot_httpc_common_post_simple((char *)REC_POLL_API, (char *)REC_POLL_API_VER,
                                      post_body, NULL, &result);
    if (rt != OPRT_OK) {
        PR_WARN("transcribe poll: http failed rt=%d (offline?, retry)", rt);
        if (result != NULL) ty_cJSON_Delete(result);
        return rt;
    }

    /* iot_httpc_common_post_simple 一般已剥掉 result 包裹，但两种形态都容忍。 */
    if (result != NULL) {
        if (ty_cJSON_IsArray(result)) {
            items = result;
        } else {
            ty_cJSON *sub = ty_cJSON_GetObjectItem(result, "result");
            if (sub != NULL && ty_cJSON_IsArray(sub)) items = sub;
        }
    }
    if (items == NULL) {
        PR_WARN("transcribe poll: response missing result array");
        if (result != NULL) ty_cJSON_Delete(result);
        return OPRT_COM_ERROR;
    }

    *out_result = result;
    *out_items  = items;
    return OPRT_OK;
}

/* Download the cloud-advertised transcribe / summary files. Per-round atomic:
 * a subset of URLs is allowed, but a partial failure rolls back what we wrote. */
static OPERATE_RET poll_download_round(ty_cJSON *item, const char *md5_hex, int target_id,
                                       char *t_file, char *s_file)
{
    ty_cJSON *t_url_j = ty_cJSON_GetObjectItem(item, "transcribeFileUrl");
    ty_cJSON *s_url_j = ty_cJSON_GetObjectItem(item, "summaryFileUrl");
    const char *t_url = t_url_j ? ty_cJSON_GetStringValue(t_url_j) : NULL;
    const char *s_url = s_url_j ? ty_cJSON_GetStringValue(s_url_j) : NULL;
    BOOL_T have_t = (t_url != NULL && t_url[0] != '\0');
    BOOL_T have_s = (s_url != NULL && s_url[0] != '\0');
    char t_base[REC_FILENAME_MAX] = {0};
    char s_base[REC_FILENAME_MAX] = {0};
    char t_path[REC_PATH_MAX] = {0};
    char s_path[REC_PATH_MAX] = {0};
    OPERATE_RET drt = OPRT_OK;
    BOOL_T t_ok = FALSE;

    if (!have_t && !have_s) {
        PR_WARN("transcribe poll: id=%d status=1 but both urls missing — retry", target_id);
        return OPRT_INVALID_PARM;
    }

    if (have_t) {
        snprintf(t_base, sizeof(t_base), "%s.txt", md5_hex);
        if (ui_fs_path(t_path, sizeof(t_path), UI_FS_RECORDING_TRANSCRIBE, t_base) != OPRT_OK) {
            return OPRT_COM_ERROR;
        }
        drt = sync_dld_file(t_url, t_path, REC_DLD_TIMEOUT_MS);
        if (drt != OPRT_OK) {
            PR_WARN("transcribe poll: id=%d transcribe dld failed %d (retry)", target_id, drt);
            return drt;
        }
        t_ok = TRUE;
    }
    if (have_s) {
        snprintf(s_base, sizeof(s_base), "%s_summary.txt", md5_hex);
        if (ui_fs_path(s_path, sizeof(s_path), UI_FS_RECORDING_TRANSCRIBE, s_base) != OPRT_OK) {
            if (t_ok) tkl_fs_remove(t_path);
            return OPRT_COM_ERROR;
        }
        drt = sync_dld_file(s_url, s_path, REC_DLD_TIMEOUT_MS);
        if (drt != OPRT_OK) {
            PR_WARN("transcribe poll: id=%d summary dld failed %d (rollback, retry)", target_id, drt);
            if (t_ok) tkl_fs_remove(t_path);
            return drt;
        }
    }

    if (have_t) snprintf(t_file, REC_FILENAME_MAX, "%s", t_base);
    if (have_s) snprintf(s_file, REC_FILENAME_MAX, "%s", s_base);
    return OPRT_OK;
}

/* Status policy: 0=processing(skip), 1=done(download+apply), null/2=fail. */
static void poll_handle_item(ty_cJSON *item, const int *ids,
                             char md5_hex[][REC_MD5_HEX_LEN + 1], int count)
{
    ty_cJSON *md5_j = NULL, *st_j = NULL;
    const char *resp_md5 = NULL;
    int target_id = -1;
    int status = -1;   /* sentinel: null / missing / non-numeric */
    int k = 0;

    if (item == NULL) return;
    md5_j = ty_cJSON_GetObjectItem(item, "md5");
    resp_md5 = md5_j ? ty_cJSON_GetStringValue(md5_j) : NULL;
    if (resp_md5 == NULL) return;

    for (k = 0; k < count; k++) {
        if (strcmp(md5_hex[k], resp_md5) == 0) { target_id = ids[k]; break; }
    }
    if (target_id < 0) return;

    st_j = ty_cJSON_GetObjectItem(item, "status");
    if (st_j != NULL && !ty_cJSON_IsNull(st_j) && ty_cJSON_IsNumber(st_j)) {
        status = st_j->valueint;
    }
    PR_INFO("transcribe poll: id=%d md5=%s status=%d", target_id, resp_md5, status);

    switch (status) {
    case 0:
        return;   /* processing — leave PROCESSING */
    case 1: {
        char t_file[REC_FILENAME_MAX] = {0};
        char s_file[REC_FILENAME_MAX] = {0};
        if (poll_download_round(item, resp_md5, target_id, t_file, s_file) != OPRT_OK) {
            return;   /* dld failed / no urls — retry next round */
        }
        if (ui_svc_recording_apply_transcribe_result(target_id, resp_md5,
                                                     UI_TRANSCRIBE_DONE, t_file, s_file) == OPRT_OK) {
            notify(target_id);
        }
        return;
    }
    case -1:
    case 2:
        if (ui_svc_recording_apply_transcribe_result(target_id, resp_md5,
                                                     UI_TRANSCRIBE_FAILED, NULL, NULL) == OPRT_OK) {
            notify(target_id);
        }
        return;
    default:
        PR_WARN("transcribe poll: id=%d unknown status=%d, skip", target_id, status);
        return;
    }
}

/* One full round; returns the number of pending entries (so the thread knows
 * whether to keep short-polling or fall back to the idle heartbeat). */
static int poll_round_once(void)
{
    int ids[REC_ITEM_NUM_MAX];
    char md5_hex[REC_ITEM_NUM_MAX][REC_MD5_HEX_LEN + 1];
    int count = 0;
    ty_cJSON *result = NULL, *items = NULL;
    int n = 0, i = 0;

    if (ui_svc_recording_collect_pending(ids, md5_hex, &count) != OPRT_OK || count == 0) {
        return 0;
    }
    if (poll_post_and_get_items(md5_hex, count, &result, &items) != OPRT_OK) {
        return count;   /* keep polling — transient failure */
    }

    n = ty_cJSON_GetArraySize(items);
    PR_DEBUG("transcribe poll: response items=%d (sent=%d)", n, count);
    for (i = 0; i < n; i++) {
        poll_handle_item(ty_cJSON_GetArrayItem(items, i), ids, md5_hex, count);
    }
    ty_cJSON_Delete(result);
    return count;
}

static void poll_thread(PVOID_T args)
{
    (void)args;
    PR_INFO("transcribe poll: thread enter");
    while (s_poll_run) {
        int pending = poll_round_once();
        UINT_T wait = (pending > 0) ? REC_POLL_INTERVAL_MS : REC_POLL_IDLE_MS;
        if (s_poll_sem != NULL) {
            tal_semaphore_wait(s_poll_sem, wait);   /* 被上传完成提前唤醒；否则超时 */
        } else {
            tal_system_sleep(wait);
        }
    }
    PR_INFO("transcribe poll: thread exit");
}

/* ---------------------------------------------------------------------------
 * Lifecycle
 * --------------------------------------------------------------------------- */
void ui_svc_transcribe_init(void)
{
    THREAD_CFG_T cfg = {0};

    if (s_poll_thread != NULL) return;

    if (tal_semaphore_create_init(&s_poll_sem, 0, 1) != OPRT_OK) {
        PR_ERR("transcribe: poll sem create failed");
        s_poll_sem = NULL;
        return;
    }
    s_poll_run = TRUE;
    cfg.stackDepth = REC_POLL_THREAD_STACK;
    cfg.priority   = THREAD_PRIO_2;
    cfg.thrdname   = "rec_transcribe";
#if defined(ENABLE_EXT_RAM) && (ENABLE_EXT_RAM == 1)
    cfg.psram_mode = 1;
#endif
    if (tal_thread_create_and_start(&s_poll_thread, NULL, NULL,
                                    poll_thread, NULL, &cfg) != OPRT_OK) {
        PR_ERR("transcribe: poll thread create failed");
        s_poll_run = FALSE;
        tal_semaphore_release(s_poll_sem);
        s_poll_sem = NULL;
        s_poll_thread = NULL;
        return;
    }
    PR_INFO("transcribe: service started");
}

void ui_svc_transcribe_release_mode(void)
{
    if (s_mode_held) {
        if (s_mode_before != AI_DEVICE_MODE_RECORD) {
            wukong_ai_device_mode_switch(s_mode_before);
        }
        s_mode_held = FALSE;
        PR_INFO("transcribe: record mode released");
    }
}

void ui_svc_transcribe_deinit(void)
{
    s_upload.abort = TRUE;
    ui_svc_transcribe_release_mode();
    s_poll_run = FALSE;
    if (s_poll_sem != NULL) tal_semaphore_post(s_poll_sem);  /* 唤醒以便退出循环 */
    if (s_poll_thread != NULL) {
        tal_thread_delete(s_poll_thread);
        s_poll_thread = NULL;
    }
    if (s_poll_sem != NULL) {
        tal_semaphore_release(s_poll_sem);
        s_poll_sem = NULL;
    }
}
