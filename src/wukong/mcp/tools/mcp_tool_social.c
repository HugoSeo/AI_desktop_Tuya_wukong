/**
 * @file mcp_tool_social.c
 * @brief MCP tool: social media feed — fetch updates from social platforms
 *
 * Framework implementation. Actual platform integrations are registered via
 * mcp_tool_social_register_platform() by external modules.
 *
 * @copyright Copyright (c) 2025 Tuya Inc. All Rights Reserved.
 */

#include "mcp_tool_social.h"
#include "wukong_ai_mcp.h"

#include <string.h>

#include "tal_log.h"

/* ========================================================================== */
/*                          Platform Registry                                 */
/* ========================================================================== */

typedef struct {
    CONST CHAR_T *name;
    SOCIAL_FEED_CB feed_cb;
} SOCIAL_PLATFORM_ENTRY_T;

STATIC SOCIAL_PLATFORM_ENTRY_T s_platforms[SOCIAL_PLATFORM_MAX] = {
    [SOCIAL_PLATFORM_WEIBO]    = { .name = "weibo" },
    [SOCIAL_PLATFORM_YOUTUBE]  = { .name = "youtube" },
    [SOCIAL_PLATFORM_X]        = { .name = "x" },
    [SOCIAL_PLATFORM_TIKTOK]   = { .name = "tiktok" },
    [SOCIAL_PLATFORM_BILIBILI] = { .name = "bilibili" },
};

STATIC SOCIAL_PLATFORM_E __platform_from_string(CONST CHAR_T *str)
{
    if (!str)
        return SOCIAL_PLATFORM_MAX;

    for (INT_T i = 0; i < SOCIAL_PLATFORM_MAX; i++) {
        if (strcmp(s_platforms[i].name, str) == 0)
            return (SOCIAL_PLATFORM_E)i;
    }
    return SOCIAL_PLATFORM_MAX;
}

STATIC SOCIAL_FEED_TYPE_E __feed_type_from_string(CONST CHAR_T *str)
{
    if (!str)
        return SOCIAL_FEED_TRENDING;

    if (strcmp(str, "trending") == 0)   return SOCIAL_FEED_TRENDING;
    if (strcmp(str, "following") == 0)  return SOCIAL_FEED_FOLLOWING;
    if (strcmp(str, "user") == 0)       return SOCIAL_FEED_USER;
    if (strcmp(str, "search") == 0)     return SOCIAL_FEED_SEARCH;

    return SOCIAL_FEED_TRENDING;
}

OPERATE_RET mcp_tool_social_register_platform(SOCIAL_PLATFORM_E platform,
                                               SOCIAL_FEED_CB feed_cb)
{
    if (platform >= SOCIAL_PLATFORM_MAX)
        return OPRT_INVALID_PARM;

    s_platforms[platform].feed_cb = feed_cb;

    TAL_PR_INFO("Social platform registered: %s", s_platforms[platform].name);
    return OPRT_OK;
}

/* ========================================================================== */
/*                       Tool: device_social_feed                             */
/* ========================================================================== */

STATIC OPERATE_RET __social_feed(CONST CHAR_T *name, CONST ty_cJSON *args,
                                  ty_cJSON **out_content, BOOL_T *is_error,
                                  VOID *user_data)
{
    CONST CHAR_T *platform_str = NULL;
    CONST CHAR_T *feed_type_str = NULL;
    CONST CHAR_T *query = NULL;
    INT_T count = 10;
    SOCIAL_PLATFORM_E platform;
    SOCIAL_FEED_TYPE_E feed_type;
    CHAR_T *result_json = NULL;
    ty_cJSON *j;
    OPERATE_RET rt;

    (VOID)name;
    (VOID)user_data;

    if (args) {
        j = ty_cJSON_GetObjectItem(args, "platform");
        if (j && ty_cJSON_IsString(j))
            platform_str = j->valuestring;
        j = ty_cJSON_GetObjectItem(args, "feed_type");
        if (j && ty_cJSON_IsString(j))
            feed_type_str = j->valuestring;
        j = ty_cJSON_GetObjectItem(args, "query");
        if (j && ty_cJSON_IsString(j))
            query = j->valuestring;
        j = ty_cJSON_GetObjectItem(args, "count");
        if (j && ty_cJSON_IsNumber(j))
            count = j->valueint;
    }

    if (!platform_str) {
        *out_content = ty_cJSON_CreateArray();
        if (*out_content)
            ty_cJSON_AddItemToArray(*out_content,
                mcp_content_make_text("Missing required parameter: platform"));
        *is_error = TRUE;
        return OPRT_OK;
    }

    platform = __platform_from_string(platform_str);
    if (platform >= SOCIAL_PLATFORM_MAX) {
        *out_content = ty_cJSON_CreateArray();
        if (*out_content)
            ty_cJSON_AddItemToArray(*out_content,
                mcp_content_make_text("Unsupported platform"));
        *is_error = TRUE;
        return OPRT_OK;
    }

    if (!s_platforms[platform].feed_cb) {
        *out_content = ty_cJSON_CreateArray();
        if (*out_content)
            ty_cJSON_AddItemToArray(*out_content,
                mcp_content_make_text("Platform not integrated yet"));
        *is_error = TRUE;
        return OPRT_OK;
    }

    feed_type = __feed_type_from_string(feed_type_str);

    TAL_PR_DEBUG("Social feed: platform=%s, type=%s, query=%s, count=%d",
                 platform_str,
                 feed_type_str ? feed_type_str : "trending",
                 query ? query : "(none)", count);

    rt = s_platforms[platform].feed_cb(platform, feed_type, query, count,
                                        &result_json);

    *out_content = ty_cJSON_CreateArray();
    if (*out_content) {
        if (rt == OPRT_OK && result_json) {
            ty_cJSON_AddItemToArray(*out_content,
                mcp_content_make_text(result_json));
        } else {
            ty_cJSON_AddItemToArray(*out_content,
                mcp_content_make_text("Failed to fetch feed"));
            *is_error = TRUE;
        }
    }

    if (result_json)
        tal_free(result_json);

    return OPRT_OK;
}

/* ========================================================================== */
/*                              Init                                          */
/* ========================================================================== */

OPERATE_RET mcp_tool_social_init(VOID)
{
    return MCP_TOOL_ADD(
        "device_social_feed",
        "Fetch updates from social media platforms "
        "(Weibo, YouTube, X/Twitter, TikTok, Bilibili).\n"
        "Supports trending topics, followed accounts feed, specific user posts, "
        "and keyword search.\n"
        "Returns a JSON array of feed items.",
        __social_feed, NULL,
        MCP_SCHEMA_STR("platform",
            "Social media platform: 'weibo', 'youtube', 'x', 'tiktok', 'bilibili'"),
        MCP_SCHEMA_STR_OPT("feed_type",
            "Feed type: 'trending' (default, hot/trending posts), "
            "'following' (posts from followed accounts), "
            "'user' (posts from a specific user, requires query), "
            "'search' (search results, requires query)"),
        MCP_SCHEMA_STR_OPT("query",
            "Username for 'user' feed type, or keyword for 'search' feed type"),
        MCP_SCHEMA_INT_OPT_RANGE("count",
            "Maximum number of feed items to return (default 10)", 1, 50)
    );
}
