
#include "ty_frame_buff.h"
#include "tkl_memory.h"

void *ty_frame_buff_malloc(RAM_TYPE_E type, UINT32_T size)
{
    ty_frame_buffer_t *frame_buff = NULL;
    UINT32_T len = sizeof(ty_frame_buffer_t) + size;
    if(type == TYPE_SRAM) {
        frame_buff = tkl_system_malloc(len);
        frame_buff->type = TYPE_SRAM;
    }else {
        frame_buff = tkl_system_psram_malloc(len);
        frame_buff->type = TYPE_PSRAM;
    }

    if(frame_buff) {
        frame_buff->frame = (UINT8_T *)frame_buff + sizeof(ty_frame_buffer_t);
        frame_buff->len = size;
    }

    return frame_buff;
}

OPERATE_RET ty_frame_buff_free(void *frame_buff)
{
    ty_frame_buffer_t *tmp = (ty_frame_buffer_t *)(frame_buff);
    if(tmp->type == TYPE_SRAM) {
        tkl_system_free(frame_buff);
    }else {
        tkl_system_psram_free(frame_buff);
    }

    frame_buff = NULL;

    return 0;
}
