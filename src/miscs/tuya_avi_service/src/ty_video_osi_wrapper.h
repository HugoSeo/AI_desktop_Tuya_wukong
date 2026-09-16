#ifndef __TY_VIDEO_OSI_WRAPPER_H__
#define __TY_VIDEO_OSI_WRAPPER_H__

#include <stdint.h>
#include <stddef.h>

/* Must match vendor/T5/t5_os/ap/components/media_service/bk_private/video_osi_wrapper.h */
typedef struct {
    void *(*malloc)(size_t size);
    void *(*zalloc)(size_t num, size_t size);
    void *(*realloc)(void *old_mem, size_t size);
    void *(*psram_malloc)(size_t size);
    void *(*psram_zalloc)(size_t num, size_t size);
    void *(*psram_realloc)(void *old_mem, size_t size);
    void (*free)(void *ptr);
    void *(*memcpy)(void *out, const void *in, uint32_t n);
    void (*memcpy_word)(void *out, const void *in, uint32_t n);
    void (*log_write)(int level, char *tag, const char *fmt, ...);
    void (*osi_assert)(uint8_t expr, char *expr_s, const char *func);
    uint32_t (*get_time)(void);
    int (*f_open)(void **fp, const void *path, uint8_t mode);
    int (*f_close)(void *fp);
    int (*f_write)(void *fp, const void *buff, uint32_t btw, uint32_t *bw);
    int (*f_read)(void *fp, const void *buff, uint32_t btr, uint32_t *br);
    int (*f_lseek)(void *fp, uint32_t ofs, uint32_t whence);
    int (*f_tell)(void *fp);
    int (*f_size)(void *fp);
    uint32_t (*get_avi_index_start_addr)(void);
    uint32_t (*get_avi_index_count)(void);
} bk_video_osi_funcs_t;

extern int video_osi_funcs_init(void *config);
extern bk_video_osi_funcs_t *bk_get_video_osi_funcs(void);

int ty_video_osi_funcs_init(void);

#endif
