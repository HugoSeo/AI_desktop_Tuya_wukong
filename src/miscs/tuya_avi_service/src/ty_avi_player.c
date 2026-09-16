/**
 * @file ty_avi_player.c
 * @brief Video-only AVI decoder with RGB565 frame callbacks.
 *
 * Storage mount/list ownership and display/audio ownership stay outside this
 * module. The caller supplies an absolute file path and consumes decoded
 * frames, making the engine safe to embed in the Wukong Demo UI.
 */

#include "ty_avi_player.h"
#include "avilib.h"
#include "ty_video_osi_wrapper.h"
#include "avi_port.h"

#include "tal_image_jpeg_codec.h"
#include "tal_memory.h"
#include "tal_mutex.h"
#include "tal_semaphore.h"
#include "tal_thread.h"
#include "uni_log.h"

#include <string.h>

#define TAG "ty_avi_player"
#define TY_AVI_JPEG_MAX_BYTES (256u * 1024u)
#define TY_AVI_VIEW_MAX_PX    320u
#define TY_AVI_AUDIO_CHUNK_MAX_BYTES (4u * 1024u)
#define TY_AVI_AUDIO_LOOKAHEAD_MS     20u
#define TY_AVI_PLAY_STAT_WINDOW_MS 2000u
/* Consecutive hardware-decode failures that disarm the path for the session. */
#define TY_AVI_HW_DECODE_FAIL_RUN_MAX 3u

typedef struct {
    avi_t *avi;
    MUTEX_HANDLE avi_mutex;
    MUTEX_HANDLE ctrl_mutex;
    SEM_HANDLE exit_sem;
    THREAD_HANDLE video_th;
    TY_AVI_PLAYER_FRAME_CB frame_cb;
    TY_AVI_PLAYER_EVENT_CB event_cb;
    TY_AVI_PLAYER_AUDIO_CB audio_cb;
    void *user_data;
    volatile uint8_t running;
    volatile uint8_t paused;
    volatile uint32_t seek_seq;
    uint16_t source_width;
    uint16_t source_height;
    uint16_t max_width;
    uint16_t max_height;
    uint32_t fps_milli;
    uint32_t total_frames;
    volatile uint32_t frame_index;
    volatile uint32_t position_ms;
    uint32_t duration_ms;
    uint32_t play_start_ms;
    uint32_t playback_base_ms;
    /* Audio is optional and paced off the same player_elapsed_ms() clock as
     * video, independently of frame delivery — see player_sync_audio(). */
    uint8_t has_audio;
    uint32_t audio_bytes_per_ms;
    uint32_t audio_total_bytes;
    uint32_t audio_bytes_delivered;
    uint32_t audio_skip_bytes;
    /* Hardware JPEG decode: platform decoder -> YUV422 -> caller's DMA2D hook
     * -> RGB565. The decoder emits native size only, so both scratches are
     * source-sized and allocated once per session rather than per frame. */
    TY_AVI_PLAYER_YUV2RGB_CB yuv2rgb;
    uint8_t hw_codec_inited;
    uint8_t hw_decode;
    uint8_t hw_fail_run;
    uint8_t *hw_yuv;
    uint8_t *hw_rgb;
} ty_avi_player_ctx_t;

static ty_avi_player_ctx_t s_play;

static uint32_t player_now_ms(void)
{
    return (uint32_t)sys_port.get_tick();
}

static uint32_t player_elapsed_ms(void)
{
    uint32_t elapsed;

    if (s_play.paused || !s_play.running) {
        return s_play.position_ms;
    }
    elapsed = s_play.playback_base_ms + (player_now_ms() - s_play.play_start_ms);
    return elapsed > s_play.duration_ms ? s_play.duration_ms : elapsed;
}

static uint32_t player_frame_pts(uint32_t index)
{
    if (s_play.total_frames == 0 || s_play.duration_ms == 0) return 0;
    return (uint32_t)((uint64_t)index * s_play.duration_ms / s_play.total_frames);
}

static void player_fit_size(uint16_t src_w, uint16_t src_h,
                            uint16_t *dst_w, uint16_t *dst_h)
{
    uint32_t w = src_w;
    uint32_t h = src_h;
    uint32_t max_w = s_play.max_width ? s_play.max_width : TY_AVI_VIEW_MAX_PX;
    uint32_t max_h = s_play.max_height ? s_play.max_height : TY_AVI_VIEW_MAX_PX;

    if (w > max_w) {
        h = h * max_w / w;
        w = max_w;
    }
    if (h > max_h) {
        w = w * max_h / h;
        h = max_h;
    }
    if (w == 0) w = 1;
    if (h == 0) h = 1;
    *dst_w = (uint16_t)w;
    *dst_h = (uint16_t)h;
}

/* Overlap-safe forward compaction: every selected source pixel is at or after
 * its destination when dst <= src on both axes. */
static void player_downscale_rgb565(uint8_t *buf,
                                    uint16_t src_w, uint16_t src_h,
                                    uint16_t dst_w, uint16_t dst_h)
{
    uint16_t *pixels;
    uint32_t x_step;
    uint32_t y_step;
    uint32_t y_fp = 0;

    if (!buf || !src_w || !src_h || !dst_w || !dst_h ||
        (src_w == dst_w && src_h == dst_h)) {
        return;
    }

    pixels = (uint16_t *)buf;
    x_step = ((uint32_t)src_w << 16) / dst_w;
    y_step = ((uint32_t)src_h << 16) / dst_h;
    for (uint16_t dy = 0; dy < dst_h; dy++, y_fp += y_step) {
        uint16_t *dst = pixels + (uint32_t)dy * dst_w;
        const uint16_t *src = pixels + (y_fp >> 16) * src_w;
        uint32_t x_fp = 0;
        for (uint16_t dx = 0; dx < dst_w; dx++, x_fp += x_step) {
            dst[dx] = src[x_fp >> 16];
        }
    }
}

/* Same nearest-neighbour reduction between two distinct buffers, so the
 * hardware path can scale straight out of its reusable scratch into the small
 * per-frame buffer that is handed to the callback. */
static void player_scale_rgb565_to(const uint8_t *src_buf,
                                   uint16_t src_w, uint16_t src_h,
                                   uint8_t *dst_buf,
                                   uint16_t dst_w, uint16_t dst_h)
{
    const uint16_t *src_px = (const uint16_t *)src_buf;
    uint16_t *dst_px = (uint16_t *)dst_buf;
    uint32_t x_step;
    uint32_t y_step;
    uint32_t y_fp = 0;

    if (src_w == dst_w && src_h == dst_h) {
        memcpy(dst_buf, src_buf, (uint32_t)dst_w * dst_h * 2u);
        return;
    }

    x_step = ((uint32_t)src_w << 16) / dst_w;
    y_step = ((uint32_t)src_h << 16) / dst_h;
    for (uint16_t dy = 0; dy < dst_h; dy++, y_fp += y_step) {
        uint16_t *dst = dst_px + (uint32_t)dy * dst_w;
        const uint16_t *src = src_px + (y_fp >> 16) * src_w;
        uint32_t x_fp = 0;
        for (uint16_t dx = 0; dx < dst_w; dx++, x_fp += x_step) {
            dst[dx] = src[x_fp >> 16];
        }
    }
}

static void player_hw_decode_release(void)
{
    if (s_play.hw_yuv) {
        tal_psram_free(s_play.hw_yuv);
        s_play.hw_yuv = NULL;
    }
    if (s_play.hw_rgb) {
        tal_psram_free(s_play.hw_rgb);
        s_play.hw_rgb = NULL;
    }
    if (s_play.hw_codec_inited) {
        tkl_jpeg_codec_deinit();
        s_play.hw_codec_inited = 0;
    }
    s_play.hw_decode = 0;
}

/**
 * @brief Arm the hardware JPEG decode path for this session.
 *
 * tkl_jpeg_codec_convert() only dispatches to the hardware block for YUV422
 * output on a width%32 / height%8 geometry, and it always decodes at native
 * size — the 480x480 MJPEG this Demo records satisfies both. Anything else
 * (missing hook, odd geometry, codec init or scratch alloc failure) leaves
 * hw_decode == 0 and playback silently uses the software codec, so this is
 * never fatal.
 */
static void player_hw_decode_arm(void)
{
    uint32_t bytes;

    if (!s_play.yuv2rgb) {
        PR_NOTICE("AVI hw jpeg decode off: no colour-space hook supplied");
        return;
    }
    if ((s_play.source_width % 32u) != 0u || (s_play.source_height % 8u) != 0u) {
        PR_NOTICE("AVI hw jpeg decode off: %ux%u is not 32x8 aligned",
                  s_play.source_width, s_play.source_height);
        return;
    }
    if (tkl_jpeg_codec_init() != OPRT_OK) {
        PR_WARN("AVI hw jpeg codec init failed, using software decode");
        return;
    }
    s_play.hw_codec_inited = 1;

    bytes = (uint32_t)s_play.source_width * s_play.source_height * 2u;
    s_play.hw_yuv = (uint8_t *)tal_psram_malloc(bytes);
    s_play.hw_rgb = (uint8_t *)tal_psram_malloc(bytes);
    if (!s_play.hw_yuv || !s_play.hw_rgb) {
        PR_WARN("AVI hw jpeg scratch alloc failed (2x%uB), using software decode", bytes);
        player_hw_decode_release();
        return;
    }

    s_play.hw_decode = 1;
    PR_NOTICE("AVI hw jpeg decode armed: %ux%u scratch=2x%uKB",
              s_play.source_width, s_play.source_height, bytes / 1024u);
}

static OPERATE_RET player_decode_frame_hw(const uint8_t *jpeg, uint32_t jpeg_len,
                                          TY_AVI_PLAYER_FRAME_T *frame)
{
    TKL_JPEG_CODEC_INFO_T info = {0};
    uint16_t dst_w;
    uint16_t dst_h;

    /* Header parse only; fills in_size/out_width/out_height for convert(). */
    if (tkl_jpeg_codec_img_info_get((uint8_t *)jpeg, jpeg_len, &info) != OPRT_OK) {
        return OPRT_COM_ERROR;
    }
    /* The scratches are sized from the AVI stream header. A frame disagreeing
     * with it would overrun them, so hand it to the software path instead. */
    if (info.out_width != s_play.source_width ||
        info.out_height != s_play.source_height) {
        return OPRT_COM_ERROR;
    }
    if (tkl_jpeg_codec_convert((uint8_t *)jpeg, s_play.hw_yuv, &info,
                               JPEG_DEC_OUT_YUV422) != OPRT_OK) {
        return OPRT_COM_ERROR;
    }
    /* Same geometry in and out: DMA2D crops rather than scales, so the
     * reduction has to stay in player_scale_rgb565_to() below. */
    if (s_play.yuv2rgb(s_play.hw_yuv, info.out_width, info.out_height,
                       s_play.hw_rgb, info.out_width, info.out_height) != OPRT_OK) {
        return OPRT_COM_ERROR;
    }

    player_fit_size(info.out_width, info.out_height, &dst_w, &dst_h);
    frame->data = (uint8_t *)tal_psram_malloc((uint32_t)dst_w * dst_h * 2u);
    if (!frame->data) return OPRT_MALLOC_FAILED;

    player_scale_rgb565_to(s_play.hw_rgb, info.out_width, info.out_height,
                           frame->data, dst_w, dst_h);
    frame->width = dst_w;
    frame->height = dst_h;
    return OPRT_OK;
}

static OPERATE_RET player_decode_frame_sw(const uint8_t *jpeg, uint32_t jpeg_len,
                                          TY_AVI_PLAYER_FRAME_T *frame)
{
    TAL_IMAGE_JPEG_INFO_T info = {0};
    TAL_IMAGE_JPEG_OUTPUT_T out = {0};
    uint16_t dst_w;
    uint16_t dst_h;
    uint16_t dec_w;
    uint16_t dec_h;
    uint32_t bytes;

    if (!jpeg || jpeg_len < 2 || !frame || jpeg[0] != 0xff || jpeg[1] != 0xd8) {
        return OPRT_INVALID_PARM;
    }
    if (tal_image_jpeg_get_info(jpeg, jpeg_len, &info) != OPRT_OK ||
        info.width == 0 || info.height == 0) {
        return OPRT_COM_ERROR;
    }

    player_fit_size(info.width, info.height, &dst_w, &dst_h);
    dec_w = info.width;
    dec_h = info.height;
    if (tal_image_jpeg_calc_scaled_size(info.width, info.height, dst_w, dst_h,
                                        &dec_w, &dec_h) != OPRT_OK) {
        return OPRT_COM_ERROR;
    }

    bytes = (uint32_t)dec_w * dec_h * 2u;
    frame->data = (uint8_t *)tal_psram_malloc(bytes);
    if (!frame->data) return OPRT_MALLOC_FAILED;

    out.out_buf = frame->data;
    out.out_buf_size = bytes;
    out.out_width = dec_w;
    out.out_height = dec_h;
    if (tal_image_jpeg_decode_rgb565(jpeg, jpeg_len, &out) != OPRT_OK) {
        ty_avi_player_frame_free(frame->data);
        frame->data = NULL;
        return OPRT_COM_ERROR;
    }

    player_downscale_rgb565(frame->data, dec_w, dec_h, dst_w, dst_h);
    frame->width = dst_w;
    frame->height = dst_h;
    return OPRT_OK;
}

/**
 * @brief Decode one MJPEG frame to RGB565, hardware first.
 *
 * An isolated hardware failure falls back to software for THAT frame only — one
 * odd frame must not disarm the path for the rest of the clip. A *run* of them
 * does disarm it, because the hardware convert waits on a 500ms semaphore
 * before reporting failure: retrying that per frame would be slower than just
 * decoding in software. @a used_hw reports which path produced the frame so the
 * stat window can show the split.
 */
static OPERATE_RET player_decode_frame(const uint8_t *jpeg, uint32_t jpeg_len,
                                       TY_AVI_PLAYER_FRAME_T *frame,
                                       BOOL_T *used_hw)
{
    if (!jpeg || jpeg_len < 2 || !frame || jpeg[0] != 0xff || jpeg[1] != 0xd8) {
        return OPRT_INVALID_PARM;
    }
    if (used_hw) *used_hw = FALSE;

    if (s_play.hw_decode) {
        OPERATE_RET hw_rt = player_decode_frame_hw(jpeg, jpeg_len, frame);

        if (hw_rt == OPRT_OK) {
            s_play.hw_fail_run = 0;
            if (used_hw) *used_hw = TRUE;
            return OPRT_OK;
        }
        if (frame->data) {
            ty_avi_player_frame_free(frame->data);
            frame->data = NULL;
        }
        /* OOM is memory pressure, not a hardware/convert fault: the software
         * path's decode buffer isn't smaller, so it would fail right alongside
         * the hardware one. Don't let a transient allocation failure burn
         * down the fail-run count and permanently disarm hw decode for the
         * rest of the session. */
        if (hw_rt != OPRT_MALLOC_FAILED &&
            ++s_play.hw_fail_run >= TY_AVI_HW_DECODE_FAIL_RUN_MAX) {
            PR_WARN("AVI hw jpeg decode failed %u frames in a row, "
                    "falling back to software for this session",
                    s_play.hw_fail_run);
            s_play.hw_decode = 0;
        }
    }
    return player_decode_frame_sw(jpeg, jpeg_len, frame);
}

/* Pulls and delivers audio chunks up to the current playback clock. Runs on
 * the video thread (only file I/O under avi_mutex + a callback — no hardware
 * access happens here, so it can't stall on a blocking DAC write; see
 * TY_AVI_PLAYER_AUDIO_CB's doc for why that's the caller's job instead). */
static void player_sync_audio(uint8_t *scratch, uint32_t seek_seq)
{
    uint64_t target_bytes;
    uint32_t skip;
    long len;

    if (!s_play.has_audio || !s_play.audio_cb || !scratch) return;

    /* Prime one 20ms DAC frame ahead. Without this, the first PCM block is
     * only delivered after the first ~100ms JPEG decode and audio starts one
     * frame behind video for the whole clip. Chunk rounding supplies the
     * remaining bounded lookahead for 100ms recording batches. */
    target_bytes = (uint64_t)(player_elapsed_ms() + TY_AVI_AUDIO_LOOKAHEAD_MS) *
                   s_play.audio_bytes_per_ms;
    if (s_play.audio_total_bytes > 0 && target_bytes > s_play.audio_total_bytes) {
        target_bytes = s_play.audio_total_bytes;
    }
    while (s_play.audio_bytes_delivered < target_bytes) {
        if (!s_play.running || seek_seq != s_play.seek_seq) return;

        tal_mutex_lock(s_play.avi_mutex);
        len = AVI_read_next_audio_chunk(s_play.avi, (char *)scratch, (long)TY_AVI_AUDIO_CHUNK_MAX_BYTES);
        tal_mutex_unlock(s_play.avi_mutex);
        if (len <= 0) return; /* no more audio in the file (short/silent tail) */

        if (seek_seq != s_play.seek_seq) return; /* a seek landed while we were reading */
        s_play.audio_bytes_delivered += (uint32_t)len;
        skip = s_play.audio_skip_bytes < (uint32_t)len ?
               s_play.audio_skip_bytes : (uint32_t)len;
        s_play.audio_skip_bytes -= skip;
        if (skip == (uint32_t)len) continue;

        /* pause() takes the same mutex, so once it returns no audio callback
         * can still be enqueueing into an output queue being torn down. */
        tal_mutex_lock(s_play.ctrl_mutex);
        if (!s_play.running || s_play.paused || seek_seq != s_play.seek_seq) {
            tal_mutex_unlock(s_play.ctrl_mutex);
            return;
        }
        s_play.audio_cb(scratch + skip, (uint32_t)len - skip, s_play.user_data);
        tal_mutex_unlock(s_play.ctrl_mutex);
    }
}

/* Caller holds avi_mutex. Position audio by byte time rather than by a
 * proportional chunk index; the latter drifts whenever audio contains a
 * short tail or variable-sized chunks. */
static void player_seek_audio_locked(uint32_t time_ms)
{
    uint64_t target = (uint64_t)time_ms * s_play.audio_bytes_per_ms;
    uint64_t chunk_off = 0;
    long total_chunks = AVI_audio_chunks(s_play.avi);
    long lo = 0;
    long hi = total_chunks;

    if (s_play.audio_total_bytes > 0 && target > s_play.audio_total_bytes) {
        target = s_play.audio_total_bytes;
    }

    if (total_chunks <= 0) {
        s_play.audio_bytes_delivered = (uint32_t)target;
        s_play.audio_skip_bytes = 0;
        return;
    }

    /* upper_bound(chunk_start <= target) - 1 */
    while (lo < hi) {
        long mid = lo + (hi - lo) / 2;
        uint64_t off = 0;

        if (AVI_audio_byte_offset_of_chunk(s_play.avi, mid, &off) == 0 &&
            off <= target) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    lo = lo > 0 ? lo - 1 : 0;
    if (AVI_audio_byte_offset_of_chunk(s_play.avi, lo, &chunk_off) != 0) {
        chunk_off = 0;
        lo = 0;
    }
    AVI_set_audio_read_chunk(s_play.avi, lo, NULL);
    s_play.audio_bytes_delivered = chunk_off > UINT32_MAX ?
                                   UINT32_MAX : (uint32_t)chunk_off;
    s_play.audio_skip_bytes = target > chunk_off ?
                              (target - chunk_off > UINT32_MAX ?
                               UINT32_MAX : (uint32_t)(target - chunk_off)) : 0;
}

static void player_wait_until(uint8_t *audio_scratch, uint32_t pts_ms, uint32_t seek_seq)
{
    while (s_play.running && seek_seq == s_play.seek_seq) {
        if (s_play.paused) {
            sys_port.sleep(20);
            continue;
        }
        player_sync_audio(audio_scratch, seek_seq);
        if (player_elapsed_ms() >= pts_ms) return;
        sys_port.sleep(10);
    }
}

static long player_read_frame(uint8_t *jpeg, uint32_t max_bytes)
{
    long len;

    tal_mutex_lock(s_play.avi_mutex);
    len = AVI_read_next_video_frame(s_play.avi, (char *)jpeg, (long)max_bytes);
    tal_mutex_unlock(s_play.avi_mutex);
    return len;
}

static void player_emit_event(TY_AVI_PLAYER_EVENT_E event)
{
    TY_AVI_PLAYER_EVENT_CB cb = s_play.event_cb;
    if (cb) cb(event, s_play.user_data);
}

static void player_video_thread(void *arg)
{
    uint8_t *jpeg = NULL;
    uint8_t *audio_scratch = NULL;
    uint32_t delivered = 0;
    uint32_t stat_start_position_ms = player_elapsed_ms();
    uint32_t stat_seek_seq = s_play.seek_seq;
    uint32_t stat_delivered = 0;
    uint32_t stat_decode_count = 0;
    uint32_t stat_decode_total_ms = 0;
    uint32_t stat_decode_max_ms = 0;
    uint32_t stat_decode_hw = 0;
    uint32_t stat_decode_sw = 0;
    uint32_t stat_late_frames = 0;
    uint32_t clock_primed_seq = UINT32_MAX;
    uint32_t frame_interval_ms = s_play.fps_milli ?
                                 (1000000u + s_play.fps_milli - 1u) /
                                 s_play.fps_milli : 0;
    BOOL_T natural_end = FALSE;
    BOOL_T fatal_error = FALSE;
    (void)arg;

    jpeg = (uint8_t *)tal_psram_malloc(TY_AVI_JPEG_MAX_BYTES);
    if (!jpeg) {
        fatal_error = TRUE;
        goto out;
    }
    if (s_play.has_audio) {
        audio_scratch = (uint8_t *)tal_psram_malloc(TY_AVI_AUDIO_CHUNK_MAX_BYTES);
        if (!audio_scratch) {
            /* Non-fatal: fall back to video-only rather than aborting playback. */
            PR_WARN("AVI audio scratch alloc failed, playing video-only");
            s_play.has_audio = 0;
        }
    }

    while (s_play.running) {
        TY_AVI_PLAYER_FRAME_T frame = {0};
        uint32_t seq = s_play.seek_seq;
        uint32_t index = s_play.frame_index;
        uint32_t pts = player_frame_pts(index);
        uint32_t decode_start_ms;
        uint32_t decode_ms;
        BOOL_T used_hw = FALSE;
        long len;

        if (seq != stat_seek_seq) {
            stat_start_position_ms = player_elapsed_ms();
            stat_seek_seq = seq;
            stat_delivered = 0;
            stat_decode_count = 0;
            stat_decode_total_ms = 0;
            stat_decode_max_ms = 0;
            stat_decode_hw = 0;
            stat_decode_sw = 0;
            stat_late_frames = 0;
        }

        if (index >= s_play.total_frames) {
            /* The final video frame PTS is one interval before duration.
             * Keep the shared clock/audio feeder alive through the declared
             * end instead of cutting the last PCM block immediately at EOF. */
            player_wait_until(audio_scratch, s_play.duration_ms, seq);
            if (!s_play.running) break;
            if (seq != s_play.seek_seq) continue;
            player_sync_audio(audio_scratch, seq);
            natural_end = TRUE;
            break;
        }

        len = player_read_frame(jpeg, TY_AVI_JPEG_MAX_BYTES);
        if (len <= 0) {
            if (seq != s_play.seek_seq) continue;
            PR_ERR("AVI frame read failed index=%u/%u ret=%ld",
                   index, s_play.total_frames, len);
            fatal_error = TRUE;
            break;
        }
        if (seq != s_play.seek_seq) continue;

        /* Advance over frames that are already more than one complete frame
         * late before spending another ~100ms decoding them. This keeps video
         * near the audio clock instead of rendering an ever-growing backlog. */
        if (clock_primed_seq == seq && frame_interval_ms > 0 &&
            player_elapsed_ms() > pts + frame_interval_ms) {
            stat_late_frames++;
            s_play.frame_index = index + 1;
            player_sync_audio(audio_scratch, seq);
            continue;
        }

        frame.index = index;
        frame.pts_ms = pts;
        decode_start_ms = player_now_ms();
        if (player_decode_frame(jpeg, (uint32_t)len, &frame, &used_hw) != OPRT_OK) {
            decode_ms = player_now_ms() - decode_start_ms;
            stat_decode_count++;
            stat_decode_total_ms += decode_ms;
            if (decode_ms > stat_decode_max_ms) stat_decode_max_ms = decode_ms;
            PR_WARN("AVI frame decode failed index=%u len=%ld", index, len);
            s_play.frame_index = index + 1;
            continue;
        }
        decode_ms = player_now_ms() - decode_start_ms;
        stat_decode_count++;
        stat_decode_total_ms += decode_ms;
        if (decode_ms > stat_decode_max_ms) stat_decode_max_ms = decode_ms;
        if (used_hw) {
            stat_decode_hw++;
        } else {
            stat_decode_sw++;
        }
        if (!s_play.running || seq != s_play.seek_seq) {
            ty_avi_player_frame_free(frame.data);
            continue;
        }
        /* Start (and re-start after seek) the media clock only after the first
         * frame has been decoded. Previously the clock ran during that decode,
         * putting audio/video roughly one decode interval behind immediately. */
        if (clock_primed_seq != seq) {
            s_play.play_start_ms = player_now_ms();
            clock_primed_seq = seq;
            stat_start_position_ms = player_elapsed_ms();
        }

        player_wait_until(audio_scratch, pts, seq);
        if (!s_play.running || seq != s_play.seek_seq || s_play.paused) {
            ty_avi_player_frame_free(frame.data);
            continue;
        }
        if (frame_interval_ms > 0 &&
            player_elapsed_ms() > pts + frame_interval_ms) {
            stat_late_frames++;
        }

        s_play.frame_index = index + 1;
        s_play.position_ms = pts;
        delivered++;
        stat_delivered++;
        if (s_play.frame_cb) {
            s_play.frame_cb(&frame, s_play.user_data);
        } else {
            ty_avi_player_frame_free(frame.data);
        }
        /* Decode+callback above can take a while; top up audio immediately
         * rather than waiting for the next player_wait_until() tick. */
        player_sync_audio(audio_scratch, seq);

        uint32_t stat_elapsed_ms = player_elapsed_ms() -
                                   stat_start_position_ms;
        if (stat_elapsed_ms >= TY_AVI_PLAY_STAT_WINDOW_MS) {
            uint32_t fps_x10 = stat_delivered * 10000u / stat_elapsed_ms;
            uint32_t decode_avg_ms = stat_decode_count ?
                                     stat_decode_total_ms / stat_decode_count : 0;

            PR_NOTICE("avi play stat fps=%u.%u decode=%u/%ums hw=%u sw=%u late=%u frame=%u pos=%ums",
                      fps_x10 / 10u, fps_x10 % 10u, decode_avg_ms,
                      stat_decode_max_ms, stat_decode_hw, stat_decode_sw,
                      stat_late_frames, s_play.frame_index,
                      s_play.position_ms);
            stat_start_position_ms = player_elapsed_ms();
            stat_delivered = 0;
            stat_decode_count = 0;
            stat_decode_total_ms = 0;
            stat_decode_max_ms = 0;
            stat_decode_hw = 0;
            stat_decode_sw = 0;
            stat_late_frames = 0;
        }
    }

out:
    if (jpeg) tal_psram_free(jpeg);
    if (audio_scratch) tal_psram_free(audio_scratch);
    if (natural_end) {
        s_play.position_ms = s_play.duration_ms;
    } else {
        s_play.position_ms = player_elapsed_ms();
    }
    s_play.running = 0;

    if (fatal_error || (natural_end && delivered == 0)) {
        player_emit_event(TY_AVI_PLAYER_EVENT_ERROR);
    } else if (natural_end) {
        player_emit_event(TY_AVI_PLAYER_EVENT_COMPLETE);
    }
    tal_semaphore_post(s_play.exit_sem);
}

static void player_release_resources(void)
{
    /* Safe here: the decode thread is the only user of the hw scratches and
     * both callers of this function have already joined it (or never started
     * it), so nothing can be mid-convert. */
    player_hw_decode_release();
    if (s_play.avi) {
        AVI_close(s_play.avi);
        s_play.avi = NULL;
    }
    if (s_play.avi_mutex) {
        tal_mutex_release(s_play.avi_mutex);
        s_play.avi_mutex = NULL;
    }
    if (s_play.ctrl_mutex) {
        tal_mutex_release(s_play.ctrl_mutex);
        s_play.ctrl_mutex = NULL;
    }
    if (s_play.exit_sem) {
        tal_semaphore_release(s_play.exit_sem);
        s_play.exit_sem = NULL;
    }
}

OPERATE_RET ty_avi_player_start(const TY_AVI_PLAYER_CFG_T *cfg)
{
    THREAD_CFG_T thread_cfg = {
        .priority = THREAD_PRIO_2,
        .stackDepth = 8192,
        .thrdname = "ty_avi_play",
    };
    double fps;

    if (!cfg || !cfg->file_path || cfg->file_path[0] != '/' || !cfg->frame_cb) {
        return OPRT_INVALID_PARM;
    }
    if (s_play.avi || s_play.video_th) return OPRT_COM_ERROR;

    memset(&s_play, 0, sizeof(s_play));
    s_play.frame_cb = cfg->frame_cb;
    s_play.event_cb = cfg->event_cb;
    s_play.audio_cb = cfg->audio_cb;
    s_play.user_data = cfg->user_data;
    s_play.yuv2rgb = cfg->yuv422_to_rgb565;
    s_play.max_width = cfg->max_width ? cfg->max_width : TY_AVI_VIEW_MAX_PX;
    s_play.max_height = cfg->max_height ? cfg->max_height : TY_AVI_VIEW_MAX_PX;

    if (ty_video_osi_funcs_init() != 0) goto failed;
    s_play.avi = AVI_open_input_file(cfg->file_path, 1, AVI_MEM_PSRAM);
    if (!s_play.avi) {
        PR_ERR("AVI open failed: %s", cfg->file_path);
        goto failed;
    }

    s_play.source_width = (uint16_t)AVI_video_width(s_play.avi);
    s_play.source_height = (uint16_t)AVI_video_height(s_play.avi);
    s_play.total_frames = (uint32_t)AVI_video_frames(s_play.avi);
    fps = AVI_video_frame_rate(s_play.avi);
    if (!s_play.source_width || !s_play.source_height || !s_play.total_frames || fps <= 0.1) {
        PR_ERR("AVI invalid stream: %ux%u frames=%u fps=%.2f",
               s_play.source_width, s_play.source_height, s_play.total_frames, fps);
        goto failed;
    }
    s_play.fps_milli = (uint32_t)(fps * 1000.0 + 0.5);
    s_play.duration_ms = (uint32_t)((uint64_t)s_play.total_frames * 1000000u /
                                    s_play.fps_milli);

    if (cfg->audio_cb) {
        long afmt   = AVI_audio_format(s_play.avi);
        long achans = AVI_audio_channels(s_play.avi);
        long arate  = AVI_audio_rate(s_play.avi);
        long abits  = AVI_audio_bits(s_play.avi);
        long abytes = AVI_audio_bytes(s_play.avi);
        uint32_t bytes_per_sample = (uint32_t)((abits / 8) * (achans > 0 ? achans : 0));

        /* The shared Wukong DAC is configured once as 16kHz/16-bit/mono.
         * Passing any other raw track through would change pitch/duration and
         * eventually overflow the bounded audio queue. */
        if (afmt == WAVE_FORMAT_PCM && achans == 1 && arate == 16000 &&
            abits == 16 && bytes_per_sample == 2) {
            s_play.audio_bytes_per_ms = ((uint32_t)arate * bytes_per_sample) / 1000u;
            s_play.has_audio = s_play.audio_bytes_per_ms ? 1 : 0;
            s_play.audio_total_bytes = abytes > 0 ?
                                       ((uint64_t)abytes > UINT32_MAX ?
                                        UINT32_MAX : (uint32_t)abytes) : 0;
        } else if (achans > 0 || arate > 0 || abits > 0 || abytes > 0) {
            PR_WARN("AVI audio unsupported: fmt=0x%lx %ldHz/%ldbit/%ldch; playing video-only",
                    afmt, arate, abits, achans);
        }
        if (!s_play.has_audio) {
            PR_NOTICE("AVI has no usable audio track, playing video-only");
        } else if (s_play.audio_total_bytes > 0) {
            uint32_t audio_duration_ms = s_play.audio_total_bytes /
                                         s_play.audio_bytes_per_ms;
            uint32_t av_delta_ms = audio_duration_ms > s_play.duration_ms ?
                                   audio_duration_ms - s_play.duration_ms :
                                   s_play.duration_ms - audio_duration_ms;

            PR_NOTICE("AVI audio track: bytes=%u duration=%ums video=%ums delta=%ums",
                      s_play.audio_total_bytes, audio_duration_ms,
                      s_play.duration_ms, av_delta_ms);
            if (av_delta_ms > 200u) {
                PR_WARN("AVI source track durations differ by %ums; short track will end in silence",
                        av_delta_ms);
            }
        }
    }

    /* Geometry is known and validated by now, so the scratches can be sized
     * exactly once for the whole session. */
    player_hw_decode_arm();

    if (tal_mutex_create_init(&s_play.avi_mutex) != OPRT_OK ||
        tal_mutex_create_init(&s_play.ctrl_mutex) != OPRT_OK ||
        tal_semaphore_create_init(&s_play.exit_sem, 0, 1) != OPRT_OK) {
        goto failed;
    }

    s_play.running = 1;
    s_play.paused = cfg->start_paused ? 1 : 0;
    s_play.play_start_ms = player_now_ms();
    if (tal_thread_create_and_start(&s_play.video_th, NULL, NULL,
                                    player_video_thread, NULL, &thread_cfg) != OPRT_OK) {
        s_play.running = 0;
        goto failed;
    }

    PR_NOTICE("AVI play started: %s %ux%u frames=%u fps=%.2f duration=%ums",
              cfg->file_path, s_play.source_width, s_play.source_height,
              s_play.total_frames, fps, s_play.duration_ms);
    return OPRT_OK;

failed:
    player_release_resources();
    memset(&s_play, 0, sizeof(s_play));
    return OPRT_COM_ERROR;
}

OPERATE_RET ty_avi_player_stop(void)
{
    if (!s_play.avi && !s_play.video_th) return OPRT_OK;

    s_play.running = 0;
    s_play.paused = 0;
    s_play.seek_seq++;
    if (s_play.video_th && s_play.exit_sem) {
        tal_semaphore_wait(s_play.exit_sem, SEM_WAIT_FOREVER);
        tal_thread_delete(s_play.video_th);
        s_play.video_th = NULL;
    }
    player_release_resources();
    PR_NOTICE("AVI play stopped");
    memset(&s_play, 0, sizeof(s_play));
    return OPRT_OK;
}

OPERATE_RET ty_avi_player_pause(void)
{
    if (!s_play.running || s_play.paused || !s_play.ctrl_mutex) return OPRT_OK;
    tal_mutex_lock(s_play.ctrl_mutex);
    s_play.position_ms = player_elapsed_ms();
    s_play.playback_base_ms = s_play.position_ms;
    s_play.paused = 1;
    tal_mutex_unlock(s_play.ctrl_mutex);
    return OPRT_OK;
}

OPERATE_RET ty_avi_player_resume(void)
{
    if (!s_play.running || !s_play.paused || !s_play.ctrl_mutex) return OPRT_OK;
    tal_mutex_lock(s_play.ctrl_mutex);
    s_play.play_start_ms = player_now_ms();
    s_play.paused = 0;
    tal_mutex_unlock(s_play.ctrl_mutex);
    return OPRT_OK;
}

OPERATE_RET ty_avi_player_seek(uint32_t time_ms)
{
    uint32_t frame;

    if (!s_play.running || !s_play.avi || !s_play.ctrl_mutex) return OPRT_COM_ERROR;
    if (time_ms > s_play.duration_ms) time_ms = s_play.duration_ms;
    frame = s_play.duration_ms ?
            (uint32_t)((uint64_t)time_ms * s_play.total_frames / s_play.duration_ms) : 0;
    if (frame >= s_play.total_frames) frame = s_play.total_frames - 1;

    tal_mutex_lock(s_play.ctrl_mutex);
    s_play.seek_seq++;
    tal_mutex_lock(s_play.avi_mutex);
    if (AVI_set_video_read_index(s_play.avi, (long)frame, NULL) != 0) {
        tal_mutex_unlock(s_play.avi_mutex);
        tal_mutex_unlock(s_play.ctrl_mutex);
        return OPRT_COM_ERROR;
    }
    if (s_play.has_audio) {
        player_seek_audio_locked(time_ms);
    }
    tal_mutex_unlock(s_play.avi_mutex);
    s_play.frame_index = frame;
    s_play.position_ms = time_ms;
    s_play.playback_base_ms = time_ms;
    s_play.play_start_ms = player_now_ms();
    tal_mutex_unlock(s_play.ctrl_mutex);
    return OPRT_OK;
}

OPERATE_RET ty_avi_player_fast_forward(uint32_t delta_ms)
{
    uint32_t pos = ty_avi_player_get_position_ms();
    uint32_t target = pos + delta_ms;
    if (target < pos || target > s_play.duration_ms) target = s_play.duration_ms;
    return ty_avi_player_seek(target);
}

OPERATE_RET ty_avi_player_rewind(uint32_t delta_ms)
{
    uint32_t pos = ty_avi_player_get_position_ms();
    return ty_avi_player_seek(delta_ms >= pos ? 0 : pos - delta_ms);
}

uint32_t ty_avi_player_get_position_ms(void)
{
    return player_elapsed_ms();
}

uint32_t ty_avi_player_get_duration_ms(void)
{
    return s_play.duration_ms;
}

BOOL_T ty_avi_player_is_running(void)
{
    return s_play.running ? TRUE : FALSE;
}

BOOL_T ty_avi_player_is_paused(void)
{
    return s_play.paused ? TRUE : FALSE;
}

BOOL_T ty_avi_player_has_audio(void)
{
    return s_play.has_audio ? TRUE : FALSE;
}

void ty_avi_player_frame_free(void *buf)
{
    if (buf) tal_psram_free(buf);
}
