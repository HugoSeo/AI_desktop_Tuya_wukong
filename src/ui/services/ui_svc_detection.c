#include "ui_svc_detection.h"
#include "ui_app.h"              /* ui_app_async_call — marshal result to UI thread */
#include "tuya_app_config.h"     /* ENABLE_AI_MODE_DETECTION */
#include "tuya_cloud_types.h"
#include "tuya_iot_internal_api.h"  /* iot_httpc_common_post_simple */
#include "ty_cJSON.h"
#include "tal_time_service.h"
#include "tal_workq_service.h"
#include "tal_memory.h"          /* tal_malloc/free, tal_psram_malloc/free */
#include "uni_log.h"
/* AI base: download by URL + device secret key (svc_ai_basic) */
#include "tuya_ai_http.h"        /* tuya_ai_http_dld_image / _get_secret_key / _stop_dld, TUYA_AI_SECRET_KEY_LEN */
#include "tuya_ai_biz.h"         /* AI_BIZ_RECV_CB, AI_BIZ_HEAD_INFO_T */
#include "tuya_ai_protocol.h"    /* AI_STREAM_*, AI_IV_LEN, AI_GCM_TAG_LEN */
#include "tal_symmetry.h"        /* tal_aes_gcm_decode */
/* JPEG decode/scale (app misc, RGB565) */
#include "tal_image_jpeg_codec.h"
#include "tal_image_scale.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* Manual detection trigger lives in the detection device mode; only present
 * when that feature is compiled in. The list query below is independent of it. */
#if defined(ENABLE_AI_MODE_DETECTION) && (ENABLE_AI_MODE_DETECTION == 1)
extern VOID_T __on_get_detection_msg(VOID_T);
#endif

#define DETECTION_QUERY_RANGE_S   (3600 * 24)            /* last 24 hours */
#define DETECTION_MSG_LIST_API    "thing.ipc.ai.robot.msg.list"
#define DETECTION_MSG_LIST_VER    "1.0"

/* ---------------------------------------------------------------------------
 * File scope state (owned by the service; page reads via getters)
 * --------------------------------------------------------------------------- */
/* Item page: allocated by ui_svc_detection_acquire() when the detection page
 * opens, released by ui_svc_detection_release() when it closes. NULL means "not
 * held" (or the allocation failed) — every access site checks it.
 * NOTE: s_items is a pointer, so size the block via ITEMS_BYTES, never
 * sizeof(s_items) (which is 4). */
static ui_svc_detection_item_t *s_items;
#define ITEMS_BYTES  (sizeof(*s_items) * UI_SVC_DETECTION_PAGE_SIZE)

/* Deferred-release latch: release() cannot free while the workq thread is still
 * writing s_items, so it sets this and fetch_done_cb (UI thread) does the free. */
static bool s_free_pending = false;
static int  s_item_count   = 0;
static int  s_total_count  = 0;
static int  s_total_pages  = 1;
static int  s_current_page = 1;
static int  s_pending_page = 1;
static bool s_in_flight    = false;
static bool s_last_ok      = false;
static ui_svc_detection_cb_t s_cb = NULL;

/* Image view state (see the image section at the bottom for the contract). The
 * accumulation buffer s_dl_buf is owned by the download worker thread. */
static uint8_t *s_dl_buf  = NULL;
static uint32_t s_dl_cap  = 0;
static uint32_t s_dl_off  = 0;
static bool     s_dl_ok   = false;
static uint32_t s_img_seq = 0;
static bool     s_img_in_flight = false;
static ui_svc_detection_image_cb_t s_image_cb = NULL;

/* ---------------------------------------------------------------------------
 * Blocking HTTP query (workq thread only — must NOT touch LVGL)
 * --------------------------------------------------------------------------- */
static OPERATE_RET query_msg_list(int page_num)
{
    OPERATE_RET rt = OPRT_OK;
    ty_cJSON *result = NULL;
    char post_content[128] = {0};

    /* Runs on the workq thread. No backing store (page closed, or acquire
     * failed) → skip the whole query rather than write through a NULL. */
    if (s_items == NULL) {
        PR_ERR("detection: no item storage, query skipped");
        return OPRT_COM_ERROR;
    }

    TIME_T now = tal_time_get_posix();
    TIME_T start = now - DETECTION_QUERY_RANGE_S;

    snprintf(post_content, sizeof(post_content),
             "{\"startTime\":%ld,\"endTime\":%ld,\"pageNum\":%d,\"pageSize\":%d}",
             (long)start, (long)now, page_num, UI_SVC_DETECTION_PAGE_SIZE);

    rt = iot_httpc_common_post_simple(DETECTION_MSG_LIST_API, DETECTION_MSG_LIST_VER,
                                      post_content, NULL, &result);
    if (rt != OPRT_OK) {
        PR_ERR("detection: msg list request failed, rt=%d", rt);
        return rt;
    }

    if (result == NULL) {
        /* Empty but successful response: clear the page, keep one page. */
        memset(s_items, 0, ITEMS_BYTES);
        s_item_count = 0;
        s_total_pages = 1;
        return OPRT_OK;
    }

    memset(s_items, 0, ITEMS_BYTES);
    s_item_count = 0;

    ty_cJSON *total_count_json = ty_cJSON_GetObjectItem(result, "totalCount");
    if (total_count_json && ty_cJSON_GetStringValue(total_count_json)) {
        s_total_count = atoi(ty_cJSON_GetStringValue(total_count_json));
    } else if (total_count_json && ty_cJSON_IsNumber(total_count_json)) {
        s_total_count = total_count_json->valueint;
    } else {
        s_total_count = 0;
    }

    s_total_pages = (s_total_count + UI_SVC_DETECTION_PAGE_SIZE - 1) / UI_SVC_DETECTION_PAGE_SIZE;
    if (s_total_pages < 1) {
        s_total_pages = 1;
    }
    if (s_total_pages > UI_SVC_DETECTION_MAX_PAGE) {
        s_total_pages = UI_SVC_DETECTION_MAX_PAGE;
    }

    ty_cJSON *datas = ty_cJSON_GetObjectItem(result, "datas");
    if (datas && ty_cJSON_IsArray(datas)) {
        int count = ty_cJSON_GetArraySize(datas);
        if (count > UI_SVC_DETECTION_PAGE_SIZE) {
            count = UI_SVC_DETECTION_PAGE_SIZE;
        }
        for (int i = 0; i < count; i++) {
            ty_cJSON *item = ty_cJSON_GetArrayItem(datas, i);
            if (item == NULL) {
                continue;
            }
            ty_cJSON *title_j = ty_cJSON_GetObjectItem(item, "msgTitle");
            ty_cJSON *date_j  = ty_cJSON_GetObjectItem(item, "dateTime");
            ty_cJSON *pics_j  = ty_cJSON_GetObjectItem(item, "attachPics");

            if (title_j && ty_cJSON_GetStringValue(title_j)) {
                snprintf(s_items[i].title, sizeof(s_items[i].title),
                         "%s", ty_cJSON_GetStringValue(title_j));
            }
            if (date_j && ty_cJSON_GetStringValue(date_j)) {
                snprintf(s_items[i].datetime, sizeof(s_items[i].datetime),
                         "%s", ty_cJSON_GetStringValue(date_j));
            }
            /* Single URL string (see CONTEXT.md). Stored per item so a card tap
             * can fetch + decrypt it on demand without re-querying the list. */
            if (pics_j && ty_cJSON_GetStringValue(pics_j)) {
                snprintf(s_items[i].attachPics, sizeof(s_items[i].attachPics),
                         "%s", ty_cJSON_GetStringValue(pics_j));
            }
            s_item_count++;
        }
    }

    PR_INFO("detection: page %d totalCount=%d items=%d",
            page_num, s_total_count, s_item_count);
    ty_cJSON_Delete(result);
    return OPRT_OK;
}

/* ---------------------------------------------------------------------------
 * Async plumbing: workq fetch -> UI-thread completion
 * --------------------------------------------------------------------------- */
/* Free the item page. UI thread only, and only when no worker can be writing it. */
static void items_free(void)
{
    tal_free(s_items);
    s_items        = NULL;
    s_item_count   = 0;
    s_free_pending = false;
}

static void fetch_done_cb(void *arg)
{
    (void)arg;
    s_in_flight = false;

    /* The page closed while this query was in flight: the worker has finished
     * writing, so it is finally safe to drop the buffer. Discard the result. */
    if (s_free_pending) {
        items_free();
        return;
    }

    if (s_last_ok) {
        s_current_page = s_pending_page;
    }
    if (s_cb) {
        s_cb(s_last_ok);
    }
}

static void fetch_work_cb(void *data)
{
    (void)data;
    OPERATE_RET rt = query_msg_list(s_pending_page);
    s_last_ok = (rt == OPRT_OK);
    ui_app_async_call(fetch_done_cb, NULL);
}

/* ---------------------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------------------- */
void ui_svc_detection_init(void)
{
    /* The item page itself is not allocated here — the detection page acquires
     * it on open and releases it on close. */
    s_items        = NULL;
    s_free_pending = false;
    s_item_count   = 0;
    s_total_count  = 0;
    s_total_pages  = 1;
    s_current_page = 1;
    s_pending_page = 1;
    s_in_flight    = false;
    s_last_ok      = false;
    s_cb           = NULL;

    /* Image view state (no worker running at init — safe to set directly). */
    s_dl_buf        = NULL;
    s_dl_cap        = 0;
    s_dl_off        = 0;
    s_dl_ok         = false;
    s_img_seq       = 0;
    s_img_in_flight = false;
    s_image_cb      = NULL;
}

void ui_svc_detection_acquire(void)
{
    s_free_pending = false;   /* re-entered before a deferred free landed */
    if (s_items != NULL) {
        return;               /* already held */
    }
    s_items = tal_malloc(ITEMS_BYTES);
    if (s_items) {
        memset(s_items, 0, ITEMS_BYTES);
    } else {
        PR_ERR("detection: item page alloc %d bytes failed, list disabled",
               (int)ITEMS_BYTES);
    }
    s_item_count = 0;
}

void ui_svc_detection_release(void)
{
    s_cb = NULL;   /* no page to notify anymore */

    /* A workq query may still be writing s_items. Freeing now would leave it
     * writing into released memory, so defer to fetch_done_cb (UI thread),
     * which runs after the worker is done. */
    if (s_in_flight) {
        s_free_pending = true;
        return;
    }
    items_free();
}

void ui_svc_detection_set_cb(ui_svc_detection_cb_t cb)
{
    s_cb = cb;
}

void ui_svc_detection_fetch(int page_num)
{
    if (page_num < 1) {
        page_num = 1;
    }
    if (s_in_flight) {
        PR_DEBUG("detection: query already in flight, ignoring page=%d", page_num);
        return;
    }

    s_pending_page = page_num;
    s_in_flight = true;

    OPERATE_RET rt = tal_workq_schedule(WORKQ_SYSTEM, fetch_work_cb, NULL);
    if (rt != OPRT_OK) {
        PR_ERR("detection: workq schedule failed, rt=%d", rt);
        s_in_flight = false;
        s_last_ok = false;
        /* Called from the UI thread (page event) — notify failure directly so
         * the page can drop its loading indicator. */
        if (s_cb) {
            s_cb(false);
        }
    }
}

void ui_svc_detection_trigger(void)
{
#if defined(ENABLE_AI_MODE_DETECTION) && (ENABLE_AI_MODE_DETECTION == 1)
    PR_INFO("detection: manual trigger (summary)");
    __on_get_detection_msg();
#else
    PR_WARN("detection: manual trigger unavailable (ENABLE_AI_MODE_DETECTION off)");
#endif
}

const ui_svc_detection_item_t *ui_svc_detection_items(void)
{
    return s_items;
}

int ui_svc_detection_item_count(void)
{
    return s_item_count;
}

int ui_svc_detection_total_pages(void)
{
    return s_total_pages;
}

int ui_svc_detection_current_page(void)
{
    return s_current_page;
}

/* ===========================================================================
 * Detection record image: download (tuya_ai_http_dld_image) -> V3 decrypt
 * (device secret key + AES-128-GCM) -> JPEG decode to RGB565 -> UI thread.
 *
 * The download streams ~6KB chunks on a WORKQ_SYSTEM thread (inside
 * tuya_ai_http_dld_image); the recv cb accumulates them into one PSRAM blob,
 * then decrypts + decodes on that same worker thread (heavy work off the UI
 * thread) and marshals the RGB565 result back via ui_app_async_call.
 *
 * Concurrency: single-flight (the overlay covers the list, so only one image
 * loads at a time; a request while one is in flight is dropped). The accumulation
 * buffer s_dl_buf is allocated/written/freed ONLY on the worker thread — the UI
 * thread (cancel/request) only flips scalar flags + calls tuya_ai_http_stop_dld,
 * so there is no cross-thread free race. The recv cb context is a file-scope
 * static because tuya_ai_http hard-codes usr_data = NULL (ADR-0003).
 * Assumes detection viewing does not overlap an AI-session image download
 * (tuya_ai_http keeps global single-flight state — see docs/adr/0001).
 * =========================================================================== */

/* EN_PIC_INFO V3 self-describing header (little-endian; see docs/adr/0001 and the
 * AI 基座图片加密 V3 spec). Fields are NOT naturally aligned, so parse by byte
 * offset rather than casting a packed struct. */
#define EN_PIC_V3_VERSION       3u
#define EN_PIC_V3_HDR_LEN       64u   /* 4+16+4+1+4+2+8+1+24 */
#define EN_PIC_V3_OFF_VERSION   0u
#define EN_PIC_V3_OFF_IV        4u
#define EN_PIC_V3_OFF_SIZE      20u
#define EN_PIC_V3_OFF_ENC_LEN   25u

/* Hard caps (see plan): reject pathological downloads; bound decode memory. */
#define DET_IMG_DL_MAX          (2u * 1024u * 1024u)   /* encrypted blob */
#define DET_IMG_MAX_EDGE        480u                   /* RGB565 long-edge fit */

/* (s_dl_buf / s_img_* / s_image_cb are declared in the file-scope state block
 *  near the top so ui_svc_detection_init() can reset them.) */

static uint32_t __rd_u32le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Worker-thread only. */
static void __dl_reset(void)
{
    if (s_dl_buf) {
        tal_psram_free(s_dl_buf);
        s_dl_buf = NULL;
    }
    s_dl_cap = 0;
    s_dl_off = 0;
    s_dl_ok  = false;
}

/* UI thread (via ui_app_async_call): deliver result or release the orphan. */
static void __notify_image(void *p)
{
    ui_svc_detection_image_t *img = (ui_svc_detection_image_t *)p;
    if (s_image_cb) {
        s_image_cb(img);              /* receiver takes or drops img->data */
    } else if (img->data) {
        tal_psram_free(img->data);    /* overlay gone — release the orphan */
    }
    tal_free(img);
}

/* Worker thread: hand a result to the UI thread (ownership of @p data transfers). */
static void __post_result(bool ok, uint16_t w, uint16_t h, uint8_t *data, uint32_t seq)
{
    s_img_in_flight = false;          /* clear before handing off */

    ui_svc_detection_image_t *img = tal_malloc(sizeof(*img));
    if (img == NULL) {
        if (data) {
            tal_psram_free(data);
        }
        return;
    }
    img->ok     = ok;
    img->width  = w;
    img->height = h;
    img->data   = data;
    img->seq    = seq;
    ui_app_async_call(__notify_image, img);
}

static void __post_fail(void)
{
    __dl_reset();
    __post_result(false, 0, 0, NULL, s_img_seq);
}

/* Decrypt the assembled V3 blob -> JPEG bytes (PSRAM). NULL on error;
 * *out_len = JPEG length on success. */
static uint8_t *__v3_decrypt(const uint8_t *blob, uint32_t blob_len, uint32_t *out_len)
{
    if (blob_len <= EN_PIC_V3_HDR_LEN) {
        PR_ERR("detection: blob too small %u", blob_len);
        return NULL;
    }
    uint32_t version = __rd_u32le(blob + EN_PIC_V3_OFF_VERSION);
    if (version != EN_PIC_V3_VERSION) {
        PR_ERR("detection: unexpected pic version %u (want v3)", version);
        return NULL;
    }
    const uint8_t *iv = blob + EN_PIC_V3_OFF_IV;          /* 16 bytes */
    uint32_t size     = __rd_u32le(blob + EN_PIC_V3_OFF_SIZE);
    uint32_t enc_len  = __rd_u32le(blob + EN_PIC_V3_OFF_ENC_LEN);

    const uint8_t *payload = blob + EN_PIC_V3_HDR_LEN;
    uint32_t payload_len   = blob_len - EN_PIC_V3_HDR_LEN;

    /* encrypt_payload_len includes the trailing 16-byte GCM tag (Q2/A). */
    if (enc_len < (uint32_t)AI_GCM_TAG_LEN || enc_len > payload_len) {
        PR_ERR("detection: bad enc_len %u (payload %u)", enc_len, payload_len);
        return NULL;
    }
    uint32_t cipher_len = enc_len - AI_GCM_TAG_LEN;
    const uint8_t *tag  = payload + cipher_len;
    const uint8_t *tail = payload + enc_len;             /* unencrypted remainder */
    uint32_t tail_len   = payload_len - enc_len;

    UCHAR_T key[TUYA_AI_SECRET_KEY_LEN] = {0};
    if (tuya_ai_http_get_secret_key(key) != OPRT_OK) {
        PR_ERR("detection: get secret key failed");
        return NULL;
    }
    /* Defensive: an all-zero key means the key path was not provisioned
     * (e.g. AI_SUB_VERSION != 2) — decrypting would yield garbage. */
    bool key_ok = false;
    for (uint32_t i = 0; i < (uint32_t)TUYA_AI_SECRET_KEY_LEN; i++) {
        if (key[i] != 0) { key_ok = true; break; }
    }
    if (!key_ok) {
        PR_ERR("detection: secret key all-zero, abort decrypt");
        return NULL;
    }

    uint8_t *jpeg = tal_psram_malloc(cipher_len + tail_len);
    if (jpeg == NULL) {
        PR_ERR("detection: psram malloc jpeg %u failed", cipher_len + tail_len);
        return NULL;
    }

    UINT32_T plain_len = 0;
    OPERATE_RET rt = tal_aes_gcm_decode(key, TUYA_AI_SECRET_KEY_LEN,
                                        iv, AI_IV_LEN, NULL, 0,
                                        payload, cipher_len,
                                        jpeg, &plain_len,
                                        (UINT8_T *)tag, AI_GCM_TAG_LEN);
    if (rt != OPRT_OK) {
        PR_ERR("detection: gcm decode err %d", rt);
        tal_psram_free(jpeg);
        return NULL;
    }
    if (tail_len > 0) {
        memcpy(jpeg + plain_len, tail, tail_len);        /* append unencrypted tail */
    }
    uint32_t jpeg_len = plain_len + tail_len;

    /* Full-encryption case: trim trailing GCM padding to the real image size.
     * (With a plaintext tail we assume no padding; JPEG decoders stop at EOI
     * anyway, so a few extra bytes are harmless.) */
    if (tail_len == 0 && size > 0 && size <= jpeg_len) {
        jpeg_len = size;
    }
    *out_len = jpeg_len;
    return jpeg;
}

/* Decode JPEG (PSRAM) -> RGB565 (PSRAM), scaled to fit DET_IMG_MAX_EDGE. */
static uint8_t *__decode_fit(const uint8_t *jpeg, uint32_t jpeg_len,
                             uint16_t *out_w, uint16_t *out_h)
{
    TAL_IMAGE_JPEG_INFO_T info = {0};
    if (tal_image_jpeg_get_info(jpeg, jpeg_len, &info) != OPRT_OK ||
        info.width == 0 || info.height == 0) {
        PR_ERR("detection: jpeg get_info failed");
        return NULL;
    }

    uint16_t long_edge = (info.width >= info.height) ? info.width : info.height;
    if (long_edge > DET_IMG_MAX_EDGE) {
        /* Downscale preserving aspect ratio. */
        uint16_t tw, th;
        if (info.width >= info.height) {
            tw = DET_IMG_MAX_EDGE;
            th = (uint16_t)((uint32_t)info.height * DET_IMG_MAX_EDGE / info.width);
        } else {
            th = DET_IMG_MAX_EDGE;
            tw = (uint16_t)((uint32_t)info.width * DET_IMG_MAX_EDGE / info.height);
        }
        if (tw == 0) { tw = 1; }
        if (th == 0) { th = 1; }

        TAL_IMAGE_JPEG_SCALE_IN_T in = {0};
        in.method     = TAL_IMAGE_SCALE_MTH_BILINEAR;
        in.mode       = TAL_IMAGE_SCALE_MODE_SIZE;
        in.data       = jpeg;
        in.size       = jpeg_len;
        in.out_width  = tw;
        in.out_height = th;

        TAL_IMAGE_SCALE_OUT_T sout = {0};
        if (tal_image_jpeg_scale_rgb565(&in, &sout) != OPRT_OK || sout.buf == NULL) {
            PR_ERR("detection: jpeg scale failed");
            return NULL;
        }
        /* Copy into a tal_psram buffer so the page frees through one path. */
        uint8_t *rgb = tal_psram_malloc(sout.size);
        if (rgb) {
            memcpy(rgb, sout.buf, sout.size);
            *out_w = sout.width;
            *out_h = sout.height;
        }
        tal_image_scale_buf_free(&sout);
        return rgb;
    }

    /* Fits — decode at native size. */
    uint32_t size = (uint32_t)info.width * info.height * 2;
    uint8_t *rgb = tal_psram_malloc(size);
    if (rgb == NULL) {
        PR_ERR("detection: psram malloc rgb %u failed", size);
        return NULL;
    }
    TAL_IMAGE_JPEG_OUTPUT_T out = {0};
    out.out_buf      = rgb;
    out.out_buf_size = size;
    out.out_width    = info.width;
    out.out_height   = info.height;
    if (tal_image_jpeg_decode_rgb565(jpeg, jpeg_len, &out) != OPRT_OK) {
        PR_ERR("detection: jpeg decode failed");
        tal_psram_free(rgb);
        return NULL;
    }
    *out_w = info.width;
    *out_h = info.height;
    return rgb;
}

/* Worker thread: decrypt + decode the accumulated blob, then post the result. */
static void __img_finalize(void)
{
    if (!s_dl_ok || s_dl_buf == NULL || s_dl_off == 0) {
        __post_fail();
        return;
    }
    uint32_t jpeg_len = 0;
    uint8_t *jpeg = __v3_decrypt(s_dl_buf, s_dl_off, &jpeg_len);
    __dl_reset();                       /* free the encrypted blob early */
    if (jpeg == NULL) {
        __post_result(false, 0, 0, NULL, s_img_seq);
        return;
    }

    uint16_t w = 0, h = 0;
    uint8_t *rgb = __decode_fit(jpeg, jpeg_len, &w, &h);
    tal_psram_free(jpeg);
    if (rgb == NULL) {
        __post_result(false, 0, 0, NULL, s_img_seq);
        return;
    }
    __post_result(true, w, h, rgb, s_img_seq);
}

/* tuya_ai_http_dld_image stream callback (worker thread; usr_data == NULL). */
static OPERATE_RET __img_recv_cb(AI_BIZ_ATTR_INFO_T *attr, AI_BIZ_HEAD_INFO_T *head,
                                 VOID *data, VOID *usr_data)
{
    (void)attr;
    (void)usr_data;
    if (!s_img_in_flight) {
        __dl_reset();                   /* cancelled — free on the worker thread */
        return OPRT_COM_ERROR;          /* abort the dld loop */
    }
    uint8_t flag = head->stream_flag;

    if (flag == AI_STREAM_START || flag == AI_STREAM_ONE) {
        __dl_reset();
        uint32_t total = head->total_len;
        if (total == 0 || total > DET_IMG_DL_MAX) {
            PR_ERR("detection: image size %u out of range", total);
            __post_fail();
            return OPRT_COM_ERROR;
        }
        s_dl_buf = tal_psram_malloc(total);
        if (s_dl_buf == NULL) {
            PR_ERR("detection: psram malloc dl %u failed", total);
            __post_fail();
            return OPRT_COM_ERROR;
        }
        s_dl_cap = total;
        s_dl_off = 0;
        s_dl_ok  = true;
    }

    if (s_dl_ok && data && head->len > 0) {
        if (s_dl_off + head->len <= s_dl_cap) {
            memcpy(s_dl_buf + s_dl_off, data, head->len);
            s_dl_off += head->len;
        } else {
            PR_ERR("detection: dl overflow off=%u len=%u cap=%u",
                   s_dl_off, head->len, s_dl_cap);
            s_dl_ok = false;
        }
    }

    if (flag == AI_STREAM_END || flag == AI_STREAM_ONE) {
        __img_finalize();
    }
    return s_dl_ok ? OPRT_OK : OPRT_COM_ERROR;
}

void ui_svc_detection_set_image_cb(ui_svc_detection_image_cb_t cb)
{
    s_image_cb = cb;
}

void ui_svc_detection_image_request(const char *url, uint32_t seq)
{
    if (url == NULL || url[0] == '\0') {
        PR_WARN("detection: empty image url");
        return;
    }
    if (s_img_in_flight) {
        PR_DEBUG("detection: image already in flight, ignoring");
        return;
    }
    s_img_seq = seq;
    s_img_in_flight = true;

    OPERATE_RET rt = tuya_ai_http_dld_image((CHAR_T *)url, __img_recv_cb);
    if (rt != OPRT_OK) {
        PR_ERR("detection: dld_image schedule failed %d", rt);
        s_img_in_flight = false;
        /* Called from the UI thread (card tap) — notify failure directly so the
         * overlay can drop its spinner. */
        ui_svc_detection_image_t img = { .ok = false, .seq = seq };
        if (s_image_cb) {
            s_image_cb(&img);
        }
    }
}

void ui_svc_detection_image_cancel(void)
{
    /* Only flip the flag + stop the network; the worker frees s_dl_buf on its
     * next callback (sees !in_flight) or in finalize. UI thread never frees it. */
    if (s_img_in_flight) {
        s_img_in_flight = false;
        tuya_ai_http_stop_dld();
    }
}

void ui_svc_detection_free_rgb565(void *buf)
{
    if (buf) {
        tal_psram_free(buf);
    }
}
