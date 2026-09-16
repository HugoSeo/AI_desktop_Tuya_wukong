#include "ty_video_osi_wrapper.h"
#include "avilib_adp.h"
#include "avi_port.h"

#include "uni_log.h"
#include "tal_memory.h"

#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#define TAG "ty_video_osi"
#define TY_AVI_FILE_WRITE_CACHE_BYTES (32u * 1024u)

static uint8_t *s_avi_index_buf = NULL;

typedef struct {
    TUYA_FILE file;
    uint8_t *write_cache;
    uint32_t write_cached;
} ty_avi_file_ctx_t;

static void *malloc_wrapper(size_t size)
{
    return tal_malloc(size);
}

static void *zalloc_wrapper(size_t num, size_t size)
{
    size_t total = num * size;
    void *p = tal_malloc(total);
    if (p) {
        memset(p, 0, total);
    }
    return p;
}

static void *realloc_wrapper(void *old_mem, size_t size)
{
    return tal_realloc(old_mem, size);
}

static void *psram_malloc_wrapper(size_t size)
{
    return tal_psram_malloc(size);
}

static void *psram_zalloc_wrapper(size_t num, size_t size)
{
    size_t total = num * size;
    void *p = tal_psram_malloc(total);
    if (p) {
        memset(p, 0, total);
    }
    return p;
}

static void *psram_realloc_wrapper(void *old_mem, size_t size)
{
    return tal_psram_realloc(old_mem, size);
}

static void free_wrapper(void *ptr)
{
    tal_free(ptr);
}

static void *memcpy_wrapper(void *out, const void *in, uint32_t n)
{
    return memcpy(out, in, n);
}

static void memcpy_word_wrapper(void *out, const void *in, uint32_t n)
{
    memcpy(out, in, n);
}

static void assert_wrapper(uint8_t expr, char *expr_s, const char *func)
{
    if (!expr) {
        PR_ERR("assert failed: %s at %s", expr_s, func);
    }
}

static uint32_t get_time_wrapper(void)
{
    return (uint32_t)sys_port.get_tick();
}

static const char *mode_to_str(uint8_t mode)
{
    if (mode & 0x08) {
        return "wb+";
    }
    if (mode & 0x02) {
        return "wb";
    }
    if (mode & 0x01) {
        return "rb";
    }
    return "rb";
}

static int avi_file_flush(ty_avi_file_ctx_t *ctx)
{
    uint32_t written = 0;

    if (!ctx || !ctx->write_cache || ctx->write_cached == 0) return 0;
    while (written < ctx->write_cached) {
        INT_T n = file_opt_port.fwrite(ctx->write_cache + written,
                                       (INT_T)(ctx->write_cached - written),
                                       ctx->file);
        if (n <= 0) {
            if (written > 0) {
                ctx->write_cached -= written;
                memmove(ctx->write_cache, ctx->write_cache + written,
                        ctx->write_cached);
            }
            return -1;
        }
        written += (uint32_t)n;
    }
    ctx->write_cached = 0;
    return 0;
}

static int f_open_wrapper(void **fp, const void *path, uint8_t mode)
{
    ty_avi_file_ctx_t *ctx;
    TUYA_FILE file;

    if (!fp || !path) return -1;
    file = file_opt_port.fopen((const char *)path, mode_to_str(mode));
    if (file == NULL) {
        *fp = NULL;
        PR_ERR("avi fopen failed: %s", (const char *)path);
        return -1;
    }

    ctx = (ty_avi_file_ctx_t *)tal_malloc(sizeof(*ctx));
    if (!ctx) {
        file_opt_port.fclose(file);
        *fp = NULL;
        return -1;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->file = file;
    if (mode & (0x02u | 0x08u)) {
        ctx->write_cache = (uint8_t *)tal_psram_malloc(TY_AVI_FILE_WRITE_CACHE_BYTES);
        if (!ctx->write_cache) {
            PR_WARN("avi write cache alloc failed, using direct writes: %s",
                    (const char *)path);
        } else {
            PR_NOTICE("avi write cache enabled: %uKB",
                      TY_AVI_FILE_WRITE_CACHE_BYTES / 1024u);
        }
    }
    *fp = ctx;
    return 0;
}

static int f_close_wrapper(void *fp)
{
    ty_avi_file_ctx_t *ctx = (ty_avi_file_ctx_t *)fp;
    int flush_ret;
    int close_ret;

    if (!ctx) return -1;
    flush_ret = avi_file_flush(ctx);
    close_ret = file_opt_port.fclose(ctx->file);
    if (ctx->write_cache) tal_psram_free(ctx->write_cache);
    tal_free(ctx);
    return (flush_ret == 0 && close_ret == 0) ? 0 : -1;
}

static int f_write_wrapper(void *fp, const void *buff, uint32_t btw, uint32_t *bw)
{
    ty_avi_file_ctx_t *ctx = (ty_avi_file_ctx_t *)fp;
    const uint8_t *src = (const uint8_t *)buff;
    uint32_t copied = 0;

    if (!ctx || !buff || !bw) return -1;
    *bw = 0;
    if (!ctx->write_cache) {
        INT_T n = file_opt_port.fwrite((void *)buff, (INT_T)btw, ctx->file);
        if (n < 0) return -1;
        *bw = (uint32_t)n;
        return 0;
    }

    while (copied < btw) {
        uint32_t remaining = btw - copied;
        uint32_t space = TY_AVI_FILE_WRITE_CACHE_BYTES - ctx->write_cached;

        /* Large already-contiguous writes do not benefit from an extra copy. */
        if (ctx->write_cached == 0 && remaining >= TY_AVI_FILE_WRITE_CACHE_BYTES) {
            INT_T n = file_opt_port.fwrite((void *)(src + copied),
                                           (INT_T)remaining, ctx->file);
            if (n <= 0) return -1;
            copied += (uint32_t)n;
            continue;
        }

        uint32_t chunk = remaining < space ? remaining : space;
        memcpy(ctx->write_cache + ctx->write_cached, src + copied, chunk);
        ctx->write_cached += chunk;
        copied += chunk;
        if (ctx->write_cached == TY_AVI_FILE_WRITE_CACHE_BYTES &&
            avi_file_flush(ctx) != 0) {
            return -1;
        }
    }
    *bw = copied;
    return 0;
}

static int f_read_wrapper(void *fp, const void *buff, uint32_t btr, uint32_t *br)
{
    ty_avi_file_ctx_t *ctx = (ty_avi_file_ctx_t *)fp;
    INT_T n;

    if (!ctx || !buff || !br || avi_file_flush(ctx) != 0) return -1;
    n = file_opt_port.fread((void *)buff, (INT_T)btr, ctx->file);
    if (n < 0) {
        return -1;
    }
    *br = (uint32_t)n;
    return 0;
}

static int f_lseek_wrapper(void *fp, uint32_t ofs, uint32_t whence)
{
    ty_avi_file_ctx_t *ctx = (ty_avi_file_ctx_t *)fp;

    if (!ctx || avi_file_flush(ctx) != 0) return -1;
    return file_opt_port.fseek(ctx->file, (INT64_T)ofs, (INT_T)whence);
}

static int f_tell_wrapper(void *fp)
{
    ty_avi_file_ctx_t *ctx = (ty_avi_file_ctx_t *)fp;
    INT64_T pos;

    if (!ctx) return -1;
    pos = file_opt_port.ftell(ctx->file);
    if (pos < 0) return -1;
    return (int)(pos + ctx->write_cached);
}

static int f_size_wrapper(void *fp)
{
    int current;
    int size;

    if (!fp) return -1;
    current = f_tell_wrapper(fp);
    if (current < 0 || f_lseek_wrapper(fp, 0, SEEK_END) != 0) return -1;
    size = f_tell_wrapper(fp);
    if (f_lseek_wrapper(fp, (uint32_t)current, SEEK_SET) != 0) return -1;
    return size;
}

#define DEBUG_LOG_ENABLE 0
static void log_write_wrapper(int level, char *tag, const char *fmt, ...)
{
    if (!DEBUG_LOG_ENABLE) {
        return;
    }

    char buf[256];
    va_list ap;

    (void)level;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (tag && tag[0]) {
        PR_DEBUG("%s: %s", tag, buf);
    } else {
        PR_DEBUG("%s", buf);
    }
}

static uint32_t get_avi_index_start_addr_wrapper(void)
{
    if (s_avi_index_buf == NULL) {
        s_avi_index_buf = (uint8_t *)tal_psram_malloc(AVI_INDEX_COUNT * sizeof(uint32_t));
    }
    return (uint32_t)(uintptr_t)s_avi_index_buf;
}

static uint32_t get_avi_index_count_wrapper(void)
{
    return AVI_INDEX_COUNT;
}

static bk_video_osi_funcs_t s_video_osi_funcs = {
    .malloc = malloc_wrapper,
    .zalloc = zalloc_wrapper,
    .realloc = realloc_wrapper,
    .psram_malloc = psram_malloc_wrapper,
    .psram_zalloc = psram_zalloc_wrapper,
    .psram_realloc = psram_realloc_wrapper,
    .free = free_wrapper,
    .memcpy = memcpy_wrapper,
    .memcpy_word = memcpy_word_wrapper,
    .log_write = log_write_wrapper,
    .osi_assert = assert_wrapper,
    .get_time = get_time_wrapper,
    .f_open = f_open_wrapper,
    .f_close = f_close_wrapper,
    .f_write = f_write_wrapper,
    .f_read = f_read_wrapper,
    .f_lseek = f_lseek_wrapper,
    .f_tell = f_tell_wrapper,
    .f_size = f_size_wrapper,
    .get_avi_index_start_addr = get_avi_index_start_addr_wrapper,
    .get_avi_index_count = get_avi_index_count_wrapper,
};

int ty_video_osi_funcs_init(void)
{
    int ret = video_osi_funcs_init(&s_video_osi_funcs);
    bk_video_osi_funcs_t *funcs = bk_get_video_osi_funcs();

    if (ret != 0 || funcs == NULL || funcs->malloc == NULL || funcs->f_open == NULL) {
        PR_ERR("video osi init failed ret=%d funcs=%p malloc=%p f_open=%p",
               ret, funcs,
               funcs ? funcs->malloc : NULL,
               funcs ? funcs->f_open : NULL);
        return -1;
    }

    PR_NOTICE("video osi init ok, funcs=%p", funcs);
    return 0;
}
