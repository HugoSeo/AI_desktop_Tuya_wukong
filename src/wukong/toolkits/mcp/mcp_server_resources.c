/**
 * @file mcp_server_resources.c
 * @brief MCP Resources capability — list, read, templates, subscribe, notify
 * @version 3.0.0
 *
 * @copyright Copyright (c) 2025 Tuya Inc. All Rights Reserved.
 */

#include "mcp_server.h"

#if MCP_ENABLE_RESOURCES

#include "mcp_server_resources.h"
#include "mcp_server_internal.h"

#include <string.h>

#include "tal_log.h"
#include "tal_memory.h"
#include "utilities/mix_method.h"

/* ========================================================================== */
/*                            Internal Types                                  */
/* ========================================================================== */

typedef struct MCP_RESOURCE_ENTRY {
    CHAR_T *uri;
    CHAR_T *name;
    CHAR_T *description;
    CHAR_T *mime_type;
    MCP_RESOURCE_READ_CB handler;
    VOID *user_data;
    struct MCP_RESOURCE_ENTRY *next;
} MCP_RESOURCE_ENTRY_T;

typedef struct MCP_SUBSCRIPTION {
    CHAR_T *uri;
    struct MCP_SUBSCRIPTION *next;
} MCP_SUBSCRIPTION_T;

STATIC MCP_RESOURCE_ENTRY_T *s_resources = NULL;
STATIC MCP_SUBSCRIPTION_T *s_subscriptions = NULL;

/* ========================================================================== */
/*                         Resource Registration                              */
/* ========================================================================== */

OPERATE_RET mcp_server_resource_add(CONST CHAR_T *uri,
                                     CONST CHAR_T *name,
                                     CONST CHAR_T *description,
                                     CONST CHAR_T *mime_type,
                                     MCP_RESOURCE_READ_CB handler,
                                     VOID *user_data)
{
    MCP_RESOURCE_ENTRY_T *entry, *cur;

    if (!uri || !name || !handler)
        return OPRT_INVALID_PARM;

    for (cur = s_resources; cur; cur = cur->next) {
        if (strcmp(cur->uri, uri) == 0) {
            TAL_PR_WARN("Resource '%s' already registered", uri);
            return OPRT_COM_ERROR;
        }
    }

    entry = (MCP_RESOURCE_ENTRY_T *)tal_calloc(1, sizeof(*entry));
    if (!entry)
        return OPRT_MALLOC_FAILED;

    entry->uri = mm_strdup(uri);
    entry->name = mm_strdup(name);
    entry->description = description ? mm_strdup(description) : NULL;
    entry->mime_type = mime_type ? mm_strdup(mime_type) : NULL;
    if (!entry->uri || !entry->name)
        goto err;

    entry->handler = handler;
    entry->user_data = user_data;

    entry->next = s_resources;
    s_resources = entry;

    TAL_PR_INFO("Resource registered: %s", uri);
    return OPRT_OK;

err:
    if (entry) {
        tal_free(entry->uri);
        tal_free(entry->name);
        tal_free(entry->description);
        tal_free(entry->mime_type);
        tal_free(entry);
    }
    return OPRT_MALLOC_FAILED;
}

OPERATE_RET mcp_server_resource_remove(CONST CHAR_T *uri)
{
    MCP_RESOURCE_ENTRY_T **pp, *entry;

    if (!uri)
        return OPRT_INVALID_PARM;

    for (pp = &s_resources; *pp; pp = &(*pp)->next) {
        if (strcmp((*pp)->uri, uri) == 0) {
            entry = *pp;
            *pp = entry->next;
            tal_free(entry->uri);
            tal_free(entry->name);
            tal_free(entry->description);
            tal_free(entry->mime_type);
            tal_free(entry);
            return OPRT_OK;
        }
    }
    return OPRT_NOT_FOUND;
}

/* ========================================================================== */
/*                       Method: resources/list                               */
/* ========================================================================== */

OPERATE_RET mcp_resources_handle_list(CHAR_T *sid, CHAR_T *eid,
                                       ty_cJSON *params, CONST CHAR_T *id)
{
    ty_cJSON *result, *arr;
    MCP_RESOURCE_ENTRY_T *res;

    result = ty_cJSON_CreateObject();
    arr = ty_cJSON_CreateArray();
    if (!result || !arr) {
        ty_cJSON_Delete(result);
        ty_cJSON_Delete(arr);
        return OPRT_MALLOC_FAILED;
    }

    for (res = s_resources; res; res = res->next) {
        ty_cJSON *obj = ty_cJSON_CreateObject();
        if (!obj)
            continue;
        ty_cJSON_AddStringToObject(obj, "uri", res->uri);
        ty_cJSON_AddStringToObject(obj, "name", res->name);
        if (res->description)
            ty_cJSON_AddStringToObject(obj, "description", res->description);
        if (res->mime_type)
            ty_cJSON_AddStringToObject(obj, "mimeType", res->mime_type);
        ty_cJSON_AddItemToArray(arr, obj);
    }

    ty_cJSON_AddItemToObject(result, "resources", arr);
    return mcp_server_reply_result(sid, eid, id, result);
}

/* ========================================================================== */
/*                       Method: resources/read                               */
/* ========================================================================== */

OPERATE_RET mcp_resources_handle_read(CHAR_T *sid, CHAR_T *eid,
                                       ty_cJSON *params, CONST CHAR_T *id)
{
    ty_cJSON *uri_j, *result;
    CONST CHAR_T *uri;
    MCP_RESOURCE_ENTRY_T *res;
    ty_cJSON *contents = NULL;
    OPERATE_RET rt;

    if (!ty_cJSON_IsObject(params))
        return mcp_server_reply_error(sid, eid, id, MCP_ERR_INVALID_PARAMS,
                                       "Missing params");

    uri_j = ty_cJSON_GetObjectItem(params, "uri");
    if (!ty_cJSON_IsString(uri_j))
        return mcp_server_reply_error(sid, eid, id, MCP_ERR_INVALID_PARAMS,
                                       "Missing URI");
    uri = uri_j->valuestring;

    for (res = s_resources; res; res = res->next) {
        if (strcmp(res->uri, uri) == 0)
            break;
    }
    if (!res)
        return mcp_server_reply_error(sid, eid, id, MCP_ERR_RESOURCE_NOT_FOUND,
                                       "Resource not found");

    rt = res->handler(uri, &contents, res->user_data);
    if (rt != OPRT_OK || !contents) {
        if (contents)
            ty_cJSON_Delete(contents);
        return mcp_server_reply_error(sid, eid, id, MCP_ERR_INTERNAL,
                                       "Failed to read resource");
    }

    result = ty_cJSON_CreateObject();
    if (!result) {
        ty_cJSON_Delete(contents);
        return OPRT_MALLOC_FAILED;
    }
    ty_cJSON_AddItemToObject(result, "contents", contents);

    return mcp_server_reply_result(sid, eid, id, result);
}

/* ========================================================================== */
/*                 Method: resources/templates/list                           */
/* ========================================================================== */

OPERATE_RET mcp_resources_handle_templates_list(CHAR_T *sid, CHAR_T *eid,
                                                 ty_cJSON *params, CONST CHAR_T *id)
{
    ty_cJSON *result = ty_cJSON_CreateObject();
    if (!result)
        return OPRT_MALLOC_FAILED;

    ty_cJSON_AddItemToObject(result, "resourceTemplates", ty_cJSON_CreateArray());
    return mcp_server_reply_result(sid, eid, id, result);
}

/* ========================================================================== */
/*              Method: resources/subscribe & unsubscribe                     */
/* ========================================================================== */

OPERATE_RET mcp_resources_handle_subscribe(CHAR_T *sid, CHAR_T *eid,
                                            ty_cJSON *params, CONST CHAR_T *id)
{
    ty_cJSON *uri_j;
    MCP_SUBSCRIPTION_T *sub;

    if (!ty_cJSON_IsObject(params))
        return mcp_server_reply_error(sid, eid, id, MCP_ERR_INVALID_PARAMS,
                                       "Missing params");

    uri_j = ty_cJSON_GetObjectItem(params, "uri");
    if (!ty_cJSON_IsString(uri_j))
        return mcp_server_reply_error(sid, eid, id, MCP_ERR_INVALID_PARAMS,
                                       "Missing URI");

    sub = (MCP_SUBSCRIPTION_T *)tal_calloc(1, sizeof(*sub));
    if (!sub)
        return mcp_server_reply_error(sid, eid, id, MCP_ERR_INTERNAL,
                                       "Allocation failed");

    sub->uri = mm_strdup(uri_j->valuestring);
    if (!sub->uri) {
        tal_free(sub);
        return mcp_server_reply_error(sid, eid, id, MCP_ERR_INTERNAL,
                                       "Allocation failed");
    }
    sub->next = s_subscriptions;
    s_subscriptions = sub;

    return mcp_server_reply_result(sid, eid, id, ty_cJSON_CreateObject());
}

OPERATE_RET mcp_resources_handle_unsubscribe(CHAR_T *sid, CHAR_T *eid,
                                              ty_cJSON *params, CONST CHAR_T *id)
{
    ty_cJSON *uri_j;
    MCP_SUBSCRIPTION_T **pp, *sub;

    if (!ty_cJSON_IsObject(params))
        return mcp_server_reply_error(sid, eid, id, MCP_ERR_INVALID_PARAMS,
                                       "Missing params");

    uri_j = ty_cJSON_GetObjectItem(params, "uri");
    if (!ty_cJSON_IsString(uri_j))
        return mcp_server_reply_error(sid, eid, id, MCP_ERR_INVALID_PARAMS,
                                       "Missing URI");

    for (pp = &s_subscriptions; *pp; pp = &(*pp)->next) {
        if (strcmp((*pp)->uri, uri_j->valuestring) == 0) {
            sub = *pp;
            *pp = sub->next;
            tal_free(sub->uri);
            tal_free(sub);
            return mcp_server_reply_result(sid, eid, id, ty_cJSON_CreateObject());
        }
    }

    return mcp_server_reply_result(sid, eid, id, ty_cJSON_CreateObject());
}

/* ========================================================================== */
/*                       Notifications                                        */
/* ========================================================================== */

OPERATE_RET mcp_server_resource_notify_updated(CONST CHAR_T *uri)
{
    MCP_SUBSCRIPTION_T *sub;
    BOOL_T subscribed = FALSE;
    ty_cJSON *params;

    if (!uri)
        return OPRT_INVALID_PARM;

    for (sub = s_subscriptions; sub; sub = sub->next) {
        if (strcmp(sub->uri, uri) == 0) {
            subscribed = TRUE;
            break;
        }
    }
    if (!subscribed)
        return OPRT_OK;

    params = ty_cJSON_CreateObject();
    if (!params)
        return OPRT_MALLOC_FAILED;

    ty_cJSON_AddStringToObject(params, "uri", uri);
    return mcp_server_send_notification("notifications/resources/updated", params);
}

OPERATE_RET mcp_server_notify_resources_changed(VOID)
{
    return mcp_server_send_notification("notifications/resources/list_changed", NULL);
}

/* ========================================================================== */
/*                         Capability Destroy                                 */
/* ========================================================================== */

VOID mcp_resources_cap_destroy(VOID)
{
    MCP_RESOURCE_ENTRY_T *res, *rnext;
    MCP_SUBSCRIPTION_T *sub, *snext;

    for (res = s_resources; res; res = rnext) {
        rnext = res->next;
        tal_free(res->uri);
        tal_free(res->name);
        tal_free(res->description);
        tal_free(res->mime_type);
        tal_free(res);
    }
    s_resources = NULL;

    for (sub = s_subscriptions; sub; sub = snext) {
        snext = sub->next;
        tal_free(sub->uri);
        tal_free(sub);
    }
    s_subscriptions = NULL;
}

#endif /* MCP_ENABLE_RESOURCES */
