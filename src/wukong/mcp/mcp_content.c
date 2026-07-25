/**
 * @file mcp_content.c
 * @brief MCP content builder helpers and base64 utility
 * @version 3.0.0
 *
 * @copyright Copyright (c) 2025 Tuya Inc. All Rights Reserved.
 */

#include "mcp_content.h"

#include "tal_memory.h"
#include "utilities/uni_base64.h"

/* ========================================================================== */
/*                        Content Builder Helpers                             */
/* ========================================================================== */

ty_cJSON *mcp_content_make_text(CONST CHAR_T *text)
{
    ty_cJSON *item;

    if (!text)
        return NULL;

    item = ty_cJSON_CreateObject();
    if (!item)
        return NULL;

    ty_cJSON_AddStringToObject(item, "type", "text");
    ty_cJSON_AddStringToObject(item, "text", text);
    return item;
}

ty_cJSON *mcp_content_make_image(CONST CHAR_T *mime_type,
                                  CONST VOID *data, UINT_T data_len)
{
    UINT_T encoded_len;
    CHAR_T *base64_buf;
    ty_cJSON *item;

    if (!mime_type || !data || data_len == 0)
        return NULL;

    encoded_len = TY_BASE64_BUF_LEN_CALC(data_len);
    base64_buf = (CHAR_T *)tal_malloc(encoded_len);
    if (!base64_buf)
        return NULL;

    tuya_base64_encode((CONST unsigned char *)data, base64_buf, (int)data_len);

    item = mcp_content_make_image_base64(mime_type, base64_buf);
    tal_free(base64_buf);
    return item;
}

ty_cJSON *mcp_content_make_image_base64(CONST CHAR_T *mime_type,
                                         CONST CHAR_T *base64_data)
{
    ty_cJSON *item;

    if (!mime_type || !base64_data)
        return NULL;

    item = ty_cJSON_CreateObject();
    if (!item)
        return NULL;

    ty_cJSON_AddStringToObject(item, "type", "image");
    ty_cJSON_AddStringToObject(item, "mimeType", mime_type);
    ty_cJSON_AddStringToObject(item, "data", base64_data);
    return item;
}

ty_cJSON *mcp_content_make_resource(CONST CHAR_T *uri,
                                     CONST CHAR_T *mime_type,
                                     CONST CHAR_T *text)
{
    ty_cJSON *item, *resource;

    if (!uri || !text)
        return NULL;

    item = ty_cJSON_CreateObject();
    if (!item)
        return NULL;

    ty_cJSON_AddStringToObject(item, "type", "resource");

    resource = ty_cJSON_CreateObject();
    if (!resource) {
        ty_cJSON_Delete(item);
        return NULL;
    }
    ty_cJSON_AddStringToObject(resource, "uri", uri);
    if (mime_type)
        ty_cJSON_AddStringToObject(resource, "mimeType", mime_type);
    ty_cJSON_AddStringToObject(resource, "text", text);

    ty_cJSON_AddItemToObject(item, "resource", resource);
    return item;
}

/* ========================================================================== */
/*                         Base64 Utility                                     */
/* ========================================================================== */

OPERATE_RET mcp_base64_encode(CONST VOID *input, UINT_T input_len,
                               CHAR_T *output, UINT_T output_len)
{
    if (!input || !output || input_len == 0 || output_len == 0)
        return OPRT_INVALID_PARM;

    if (TY_BASE64_BUF_LEN_CALC(input_len) > output_len)
        return OPRT_COM_ERROR;

    tuya_base64_encode((CONST unsigned char *)input, output, (int)input_len);
    return OPRT_OK;
}
