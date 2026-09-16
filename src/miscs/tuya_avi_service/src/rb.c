#include "rb.h"
#include "avi_port.h"
#include "tuya_cloud_types.h"
#include <string.h>

static inline uint32_t min_u32(uint32_t a, uint32_t b) { return a < b ? a : b; }

static uint32_t roundup_pow_of_two(uint32_t len)
{
    uint32_t i, mask;
    if ((len & (len - 1)) == 0) return len;
    for (i = 31; i > 0; i--) {
        mask = 1u << i;
        if (len & mask) break;
    }
    return mask << 1;
}

int avi_rb_init(struct ringbuffer *r, uint32_t len, uint32_t esize)
{
    if (!r || !len || !esize) return -1;
    r->esize = esize;
    r->in = r->out = 0;
    r->size = roundup_pow_of_two(esize * len);
    r->data = (uint8_t *)sys_port.psram_malloc(r->size);
    if (!r->data) return -1;
    r->mask = r->size - 1;
    return 0;
}

void avi_rb_deinit(struct ringbuffer *r)
{
    if (!r) return;
    if (r->data) sys_port.psram_free(r->data);
    r->data = NULL;
    r->in = r->out = 0;
}

void avi_rb_clean(struct ringbuffer *r)
{
    if (r) r->in = r->out = 0;
}

uint32_t avi_rb_unused(struct ringbuffer *r)
{
    return r->size - (r->in - r->out);
}

uint32_t avi_rb_in(struct ringbuffer *r, const uint8_t *buf, uint32_t len)
{
    uint32_t l, left = avi_rb_unused(r);
    len = min_u32(len, left);
    l = min_u32(len, r->size - (r->in & r->mask));
    memcpy(r->data + (r->in & r->mask), buf, l);
    memcpy(r->data, buf + l, len - l);
    r->in += len;
    return len;
}

uint32_t avi_rb_out(struct ringbuffer *r, void *buf, uint32_t len)
{
    uint32_t l, avail = avi_rb_avail(r);
    len = min_u32(len, avail);
    l = min_u32(len, r->size - (r->out & r->mask));
    memcpy(buf, r->data + (r->out & r->mask), l);
    memcpy(buf + l, r->data, len - l);
    r->out += len;
    return len;
}

uint32_t avi_rb_avail(struct ringbuffer *r)
{
    return r->in - r->out;
}
