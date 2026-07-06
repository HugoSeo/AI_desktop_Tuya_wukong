/**
 * @file mcp_tool_social.h
 * @brief MCP tools: social media feed (Weibo, YouTube, X/Twitter, etc.)
 *
 * @copyright Copyright (c) 2025 Tuya Inc. All Rights Reserved.
 */

#ifndef __MCP_TOOL_SOCIAL_H__
#define __MCP_TOOL_SOCIAL_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SOCIAL_PLATFORM_WEIBO = 0,
    SOCIAL_PLATFORM_YOUTUBE,
    SOCIAL_PLATFORM_X,
    SOCIAL_PLATFORM_TIKTOK,
    SOCIAL_PLATFORM_BILIBILI,
    SOCIAL_PLATFORM_MAX,
} SOCIAL_PLATFORM_E;

typedef enum {
    SOCIAL_FEED_TRENDING = 0,   /**< Trending / hot posts */
    SOCIAL_FEED_FOLLOWING,      /**< Posts from followed accounts */
    SOCIAL_FEED_USER,           /**< Posts from a specific user */
    SOCIAL_FEED_SEARCH,         /**< Search results */
} SOCIAL_FEED_TYPE_E;

/**
 * Callback invoked to fetch social media feed.
 *
 * @param[in]  platform   Target platform
 * @param[in]  feed_type  Type of feed to fetch
 * @param[in]  query      Search keyword or username (depends on feed_type, may be NULL)
 * @param[in]  count      Max items to return
 * @param[out] out_json   Output JSON string. Caller must free with tal_free().
 * @return OPRT_OK on success
 */
typedef OPERATE_RET (*SOCIAL_FEED_CB)(SOCIAL_PLATFORM_E platform,
                                       SOCIAL_FEED_TYPE_E feed_type,
                                       CONST CHAR_T *query,
                                       INT_T count,
                                       CHAR_T **out_json);

/**
 * Register a platform-specific feed callback.
 */
OPERATE_RET mcp_tool_social_register_platform(SOCIAL_PLATFORM_E platform,
                                               SOCIAL_FEED_CB feed_cb);

/**
 * Initialize the social media MCP tool (registers device_social_feed).
 */
OPERATE_RET mcp_tool_social_init(VOID);

#ifdef __cplusplus
}
#endif

#endif /* __MCP_TOOL_SOCIAL_H__ */
