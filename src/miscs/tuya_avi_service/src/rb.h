#ifndef __TY_AVI_RB_H__
#define __TY_AVI_RB_H__

#include <stdint.h>

#define __DYNAMIC_MALLOC__

struct ringbuffer {
    uint32_t in;
    uint32_t out;
    uint32_t mask;
    uint32_t size;
    uint32_t esize;
    uint8_t *data;
};

int avi_rb_init(struct ringbuffer *r, uint32_t len, uint32_t esize);
void avi_rb_deinit(struct ringbuffer *r);
void avi_rb_clean(struct ringbuffer *r);
uint32_t avi_rb_in(struct ringbuffer *r, const uint8_t *buf, uint32_t len);
uint32_t avi_rb_out(struct ringbuffer *r, void *buf, uint32_t len);
uint32_t avi_rb_avail(struct ringbuffer *r);
uint32_t avi_rb_unused(struct ringbuffer *r);

#endif
