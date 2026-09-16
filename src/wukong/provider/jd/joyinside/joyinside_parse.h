/**
 * @file joyinside_parse.h
 * @brief joyinside parse module
 * @version 0.2
 * @date 2025-12-15
 */

#ifndef __JOYINSIDE_PARSE_H__
#define __JOYINSIDE_PARSE_H__

#include "tuya_cloud_types.h"
#include "ty_cJSON.h"
#include "joyinside_biz.h"

/* Output callbacks — registered by the JD provider */
typedef struct {
    OPERATE_RET (*on_tts)(BYTE_T *data, UINT_T len, BOOL_T is_start, BOOL_T is_end);
    OPERATE_RET (*on_text)(ty_cJSON *root, BOOL_T eof);
    OPERATE_RET (*on_event)(JD_EVENT_TYPE_E type);
    VOID_T (*on_output_stop)(BOOL_T force);
} JD_OUTPUT_CBS_T;

VOID_T joyinside_parse_set_output_cbs(CONST JD_OUTPUT_CBS_T *cbs);

OPERATE_RET joyinside_parse_content(ty_cJSON *root);

#endif // __JOYINSIDE_PARSE_H__
