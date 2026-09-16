/**
 * @file test_mcp_control.c
 * @brief MCP control tool tests — schema + mode set/get handlers.
 * Run via pytest test_suite.py -k mcp_control.
 */
#include <stdio.h>
#include <string.h>

#include "wukong_test.h"
#include "ty_cJSON.h"
#include "wukong_tool.h"
#include "tool_control.h"
#include "wukong_ai_mode.h"

/* Accessors provided by stubs_mcp_control.c */
int test_has_tool(const char *name);
const char *test_tool_description(const char *name);
const char *test_prop_description(const char *tool_name, const char *prop_name);
WUKONG_TOOL_HANDLER_CB test_get_tool_handler(const char *name);
const char *test_prop_type(const char *tool_name, const char *prop_name);
const char *test_prop_enum_values(const char *tool_name, const char *prop_name);
BOOL_T test_prop_required(const char *tool_name, const char *prop_name);
void test_control_set_trigger_mode(AI_CHAT_SUB_MODE_E mode);
BOOL_T test_switch_called(void);
AI_CHAT_SUB_MODE_E test_last_switch_mode(void);
BOOL_T test_toy_volume_set_called(void);
UINT8_T test_toy_volume_set_value(void);
void test_control_reset(void);

/* --------------------------------------------------------------------------
 * Helpers
 * -------------------------------------------------------------------------- */

/** Call a tool handler with a mode string argument; returns out_content array. */
static ty_cJSON *call_set_handler(WUKONG_TOOL_HANDLER_CB handler, const char *mode_value)
{
    ty_cJSON *args = ty_cJSON_CreateObject();
    ty_cJSON *out_content = NULL;

    if (args && mode_value) {
        ty_cJSON_AddStringToObject(args, "mode", mode_value);
    }
    handler("test", args, &out_content, NULL);
    if (args) ty_cJSON_Delete(args);

    return out_content;
}

/** Call a tool handler with a numeric volume argument; returns out_content array. */
static ty_cJSON *call_volume_handler(WUKONG_TOOL_HANDLER_CB handler, double volume)
{
    ty_cJSON *args = ty_cJSON_CreateObject();
    ty_cJSON *out_content = NULL;

    if (args) {
        ty_cJSON_AddNumberToObject(args, "volume", volume);
    }
    handler("test", args, &out_content, NULL);
    if (args) ty_cJSON_Delete(args);

    return out_content;
}

/** Call a tool handler with no args; returns out_content array. */
static ty_cJSON *call_handler_no_args(WUKONG_TOOL_HANDLER_CB handler)
{
    ty_cJSON *out_content = NULL;

    handler("test", NULL, &out_content, NULL);
    return out_content;
}

/** Extract text from first content item of tool result. */
static const char *result_text(ty_cJSON *content)
{
    ty_cJSON *item;
    if (!content) return NULL;
    item = ty_cJSON_GetArrayItem(content, 0);
    if (!item) return NULL;
    return item->valuestring;
}

int main(void)
{
    WUKONG_TOOL_HANDLER_CB set_handler = NULL;
    WUKONG_TOOL_HANDLER_CB get_handler = NULL;
    ty_cJSON *result = NULL;

    /* ---- Init ---- */
    tool_control_init();

    /* ==== 1. Tool registration ========================================== */

    EXPECT(test_has_tool("device_audio_mode_set"),
           "device_audio_mode_set is registered");
    EXPECT(test_has_tool("device_audio_mode_get"),
           "device_audio_mode_get is registered");

    /* ==== 2. Schema: mode param is string enum ========================= */

    EXPECT_STR_EQ(test_prop_type("device_audio_mode_set", "mode"), "string",
                  "mode param type is string");
    EXPECT(test_prop_required("device_audio_mode_set", "mode"),
           "mode param is required");

    {
        const char *enums = test_prop_enum_values("device_audio_mode_set", "mode");
        EXPECT_NOT_NULL(enums, "mode enum_values is not NULL");
        EXPECT_STR_CONTAINS(enums, "hold_to_talk", "enum contains hold_to_talk");
        EXPECT_STR_CONTAINS(enums, "press_to_talk", "enum contains press_to_talk");
        EXPECT_STR_CONTAINS(enums, "wake_word", "enum contains wake_word");
        EXPECT_STR_CONTAINS(enums, "free_conversation", "enum contains free_conversation");
    }

    /* ==== 3. Description includes Chinese labels ======================= */

    {
        const char *desc = test_prop_description("device_audio_mode_set", "mode");
        EXPECT_STR_CONTAINS(desc, "长按说话", "description contains 长按说话");
        EXPECT_STR_CONTAINS(desc, "按键说话", "description contains 按键说话");
        EXPECT_STR_CONTAINS(desc, "唤醒词对话", "description contains 唤醒词对话");
        EXPECT_STR_CONTAINS(desc, "自由对话", "description contains 自由对话");
    }

    /* ==== 4. mode_set: valid strings =================================== */

    set_handler = test_get_tool_handler("device_audio_mode_set");
    EXPECT_NOT_NULL(set_handler, "mode_set handler is not NULL");

    if (set_handler) {
        test_control_reset();

        /* hold_to_talk */
        result = call_set_handler(set_handler, "hold_to_talk");
        EXPECT(test_switch_called(), "hold_to_talk triggers switch");
        EXPECT_EQ(test_last_switch_mode(), AI_CHAT_SUB_HOLD,
                  "hold_to_talk maps to AI_CHAT_SUB_HOLD");
        if (result) ty_cJSON_Delete(result);

        /* press_to_talk */
        test_control_reset();
        result = call_set_handler(set_handler, "press_to_talk");
        EXPECT(test_switch_called(), "press_to_talk triggers switch");
        EXPECT_EQ(test_last_switch_mode(), AI_CHAT_SUB_ONESHOT,
                  "press_to_talk maps to AI_CHAT_SUB_ONESHOT");
        if (result) ty_cJSON_Delete(result);

        /* wake_word */
        test_control_reset();
        result = call_set_handler(set_handler, "wake_word");
        EXPECT(test_switch_called(), "wake_word triggers switch");
        EXPECT_EQ(test_last_switch_mode(), AI_CHAT_SUB_WAKEUP,
                  "wake_word maps to AI_CHAT_SUB_WAKEUP");
        if (result) ty_cJSON_Delete(result);

        /* free_conversation */
        test_control_reset();
        result = call_set_handler(set_handler, "free_conversation");
        EXPECT(test_switch_called(), "free_conversation triggers switch");
        EXPECT_EQ(test_last_switch_mode(), AI_CHAT_SUB_FREE,
                  "free_conversation maps to AI_CHAT_SUB_FREE");
        if (result) ty_cJSON_Delete(result);
    }

    /* ==== 5. mode_set: invalid string ================================== */

    if (set_handler) {
        ty_cJSON *args = ty_cJSON_CreateObject();
        ty_cJSON *out_content = NULL;

        ty_cJSON_AddStringToObject(args, "mode", "invalid_mode");

        test_control_reset();
        OPERATE_RET rc = set_handler("test", args, &out_content, NULL);
        EXPECT(rc != OPRT_OK, "invalid mode string returns error");
        EXPECT(!test_switch_called(), "invalid mode does not trigger switch");
        if (out_content) ty_cJSON_Delete(out_content);
        if (args) ty_cJSON_Delete(args);
    }

    /* ==== 6. mode_set: missing mode param ============================== */

    if (set_handler) {
        ty_cJSON *out_content = NULL;

        test_control_reset();
        OPERATE_RET rc = set_handler("test", NULL, &out_content, NULL);
        EXPECT(rc != OPRT_OK, "missing args returns error");
        EXPECT(!test_switch_called(), "missing args does not trigger switch");
        if (out_content) ty_cJSON_Delete(out_content);
    }

    /* ==== 7. mode_get: returns current mode ============================ */

    get_handler = test_get_tool_handler("device_audio_mode_get");
    EXPECT_NOT_NULL(get_handler, "mode_get handler is not NULL");

    if (get_handler) {
        const char *text = NULL;

        /* When trigger_mode = AI_CHAT_SUB_WAKEUP */
        test_control_set_trigger_mode(AI_CHAT_SUB_WAKEUP);
        result = call_handler_no_args(get_handler);
        text = result_text(result);
        EXPECT_STR_EQ(text, "wake_word", "get_mode returns wake_word when mode is WAKEUP");
        if (result) ty_cJSON_Delete(result);

        /* When trigger_mode = AI_CHAT_SUB_FREE */
        test_control_set_trigger_mode(AI_CHAT_SUB_FREE);
        result = call_handler_no_args(get_handler);
        text = result_text(result);
        EXPECT_STR_EQ(text, "free_conversation", "get_mode returns free_conversation when mode is FREE");
        if (result) ty_cJSON_Delete(result);
    }

    /* ==== 8. volume_set: routed through the canonical toy setter ======== */

    {
        WUKONG_TOOL_HANDLER_CB vol_set = test_get_tool_handler("device_audio_volume_set");

        EXPECT_NOT_NULL(vol_set, "volume_set handler is not NULL");

        if (vol_set) {
            test_control_reset();
            result = call_volume_handler(vol_set, 100.0);
            EXPECT(test_toy_volume_set_called(), "volume_set calls the toy volume setter");
            EXPECT_EQ(test_toy_volume_set_value(), 100, "volume 100 reaches the toy setter");
            if (result) ty_cJSON_Delete(result);

            test_control_reset();
            result = call_volume_handler(vol_set, 150.0);
            EXPECT_EQ(test_toy_volume_set_value(), 100, "volume above 100 is clamped to 100");
            if (result) ty_cJSON_Delete(result);

            test_control_reset();
            result = call_volume_handler(vol_set, -20.0);
            EXPECT_EQ(test_toy_volume_set_value(), 0, "negative volume is clamped to 0");
            if (result) ty_cJSON_Delete(result);
        }
    }

    TEST_END();
}
