/**
 * @file test_mcp_memory.c
 * @brief memory_get/save/update/delete tools: registration, module
 *        passthrough, array/object-as-string fallback, error paths.
 */
#include <stdio.h>
#include <string.h>
#include "wukong_test.h"
#include "ty_cJSON.h"
#include "wukong_tool.h"
#include "wukong_memory.h"
#include "memory_tool.h"

int test_has_tool(const char *name);
WUKONG_TOOL_HANDLER_CB test_get_tool_handler(const char *name);

/* stub control (declared in stubs_mcp_memory.c) */
void test_mem_set_get_json(const char *json);
void test_mem_set_get_ret(OPERATE_RET rt);
int  test_mem_get_ids_count(void);
const char *test_mem_get_id(int i);

void test_mem_set_save_id(const char *id);
void test_mem_set_save_ret(OPERATE_RET rt);
const char *test_mem_save_content(void);
int  test_mem_save_ntag(void);
const char *test_mem_save_tag(int i);
int  test_mem_save_importance(void);

void test_mem_set_update_ret(OPERATE_RET rt);
const char *test_mem_update_id(void);
const char *test_mem_update_json(void);

void test_mem_set_delete_ret(OPERATE_RET rt);
const char *test_mem_delete_id(void);
int  test_mem_delete_called(void);

static const char *result_text(ty_cJSON *c)
{
    ty_cJSON *it = c ? ty_cJSON_GetArrayItem(c, 0) : NULL;
    return it ? it->valuestring : NULL;
}

int main(void)
{
    memory_tool_init();

    /* ---- registration ---- */
    EXPECT(test_has_tool("memory_get"), "memory_get registered");
    EXPECT(test_has_tool("memory_save"), "memory_save registered");
    EXPECT(test_has_tool("memory_update"), "memory_update registered");
    EXPECT(test_has_tool("memory_delete"), "memory_delete registered");
    EXPECT(test_has_tool("memory_list"), "memory_list registered");

    WUKONG_TOOL_HANDLER_CB h_get = test_get_tool_handler("memory_get");
    WUKONG_TOOL_HANDLER_CB h_save = test_get_tool_handler("memory_save");
    WUKONG_TOOL_HANDLER_CB h_update = test_get_tool_handler("memory_update");
    WUKONG_TOOL_HANDLER_CB h_delete = test_get_tool_handler("memory_delete");
    EXPECT_NOT_NULL(h_get, "memory_get handler not NULL");
    EXPECT_NOT_NULL(h_save, "memory_save handler not NULL");
    EXPECT_NOT_NULL(h_update, "memory_update handler not NULL");
    EXPECT_NOT_NULL(h_delete, "memory_delete handler not NULL");

    /* ---- memory_get: ids as a real JSON array -> stubbed content passthrough ---- */
    if (h_get) {
        test_mem_set_get_json("{\"success\":true,\"memories\":[{\"id\":\"mem_0001\",\"content\":\"likes coffee\"}]}");
        ty_cJSON *args = ty_cJSON_CreateObject();
        ty_cJSON *ids = ty_cJSON_CreateArray();
        ty_cJSON_AddItemToArray(ids, ty_cJSON_CreateString("mem_0001"));
        ty_cJSON_AddItemToObject(args, "ids", ids);
        ty_cJSON *out = NULL;
        OPERATE_RET rc = h_get("memory_get", args, &out, NULL);
        EXPECT(rc == OPRT_OK, "memory_get (array ids) not error");
        EXPECT_STR_CONTAINS(result_text(out), "likes coffee", "memory_get returns stubbed content");
        EXPECT_EQ(test_mem_get_ids_count(), 1, "wukong_memory_get received 1 id");
        EXPECT_STR_EQ(test_mem_get_id(0), "mem_0001", "wukong_memory_get received the right id");
        if (out) ty_cJSON_Delete(out);
        ty_cJSON_Delete(args);
    }

    /* ---- memory_get: string-encoded ids are NOT decoded by the handler ----
     * Schema declares "ids" as array; decoding string form is wukong_tool_exec's
     * promotion job. A raw handler call with a string sees zero ids. */
    if (h_get) {
        test_mem_set_get_json("{\"success\":true,\"memories\":[]}");
        ty_cJSON *args = ty_cJSON_CreateObject();
        ty_cJSON_AddStringToObject(args, "ids", "[\"mem_0002\"]");
        ty_cJSON *out = NULL;
        OPERATE_RET rc = h_get("memory_get", args, &out, NULL);
        EXPECT(rc == OPRT_OK, "memory_get (string ids) still calls the module");
        EXPECT_EQ(test_mem_get_ids_count(), 0, "handler no longer parses string ids");
        if (out) ty_cJSON_Delete(out);
        ty_cJSON_Delete(args);
    }

    /* ---- memory_get: module failure -> non-OK rc + success:false ---- */
    if (h_get) {
        test_mem_set_get_ret(OPRT_COM_ERROR);
        ty_cJSON *args = ty_cJSON_CreateObject();
        ty_cJSON *out = NULL;
        OPERATE_RET rc = h_get("memory_get", args, &out, NULL);
        EXPECT(rc != OPRT_OK, "memory_get module failure returns non-OK");
        EXPECT_STR_CONTAINS(result_text(out), "\"success\":false", "memory_get failure body");
        if (out) ty_cJSON_Delete(out);
        ty_cJSON_Delete(args);
        test_mem_set_get_ret(OPRT_OK);
    }

    /* ---- memory_save: content + tags array + importance -> id returned ---- */
    if (h_save) {
        test_mem_set_save_id("mem_0007");
        ty_cJSON *args = ty_cJSON_CreateObject();
        ty_cJSON_AddStringToObject(args, "content", "user likes morning coffee");
        ty_cJSON *tags = ty_cJSON_CreateArray();
        ty_cJSON_AddItemToArray(tags, ty_cJSON_CreateString("coffee"));
        ty_cJSON_AddItemToArray(tags, ty_cJSON_CreateString("morning"));
        ty_cJSON_AddItemToObject(args, "tags", tags);
        ty_cJSON_AddNumberToObject(args, "importance", 8);
        ty_cJSON *out = NULL;
        OPERATE_RET rc = h_save("memory_save", args, &out, NULL);
        EXPECT(rc == OPRT_OK, "memory_save not error");
        EXPECT_STR_CONTAINS(result_text(out), "mem_0007", "memory_save returns id");
        EXPECT_STR_CONTAINS(result_text(out), "\"success\":true", "memory_save success body");
        EXPECT_STR_EQ(test_mem_save_content(), "user likes morning coffee", "wukong_memory_save received content");
        EXPECT_EQ(test_mem_save_ntag(), 2, "wukong_memory_save received 2 tags");
        EXPECT_STR_EQ(test_mem_save_tag(0), "coffee", "wukong_memory_save received tag 0");
        EXPECT_STR_EQ(test_mem_save_tag(1), "morning", "wukong_memory_save received tag 1");
        EXPECT_EQ(test_mem_save_importance(), 8, "wukong_memory_save received importance");
        if (out) ty_cJSON_Delete(out);
        ty_cJSON_Delete(args);
    }

    /* ---- memory_save: string-encoded tags are NOT decoded by the handler ----
     * Schema declares "tags" as array; exec's promotion owns the string form. */
    if (h_save) {
        test_mem_set_save_id("mem_0008");
        ty_cJSON *args = ty_cJSON_CreateObject();
        ty_cJSON_AddStringToObject(args, "content", "user dislikes spicy food");
        ty_cJSON_AddStringToObject(args, "tags", "[\"food\",\"spicy\"]");
        ty_cJSON *out = NULL;
        OPERATE_RET rc = h_save("memory_save", args, &out, NULL);
        EXPECT(rc == OPRT_OK, "memory_save (string tags) still saves");
        EXPECT_EQ(test_mem_save_ntag(), 0, "handler no longer parses string tags");
        if (out) ty_cJSON_Delete(out);
        ty_cJSON_Delete(args);
    }

    /* ---- memory_save: over-length content gets capped before reaching the module ---- */
    if (h_save) {
        char longc[400];
        memset(longc, 'a', sizeof(longc) - 1);
        longc[sizeof(longc) - 1] = '\0';
        test_mem_set_save_id("mem_0009");
        ty_cJSON *args = ty_cJSON_CreateObject();
        ty_cJSON_AddStringToObject(args, "content", longc);
        ty_cJSON *out = NULL;
        OPERATE_RET rc = h_save("memory_save", args, &out, NULL);
        EXPECT(rc == OPRT_OK, "memory_save (long content) not error");
        EXPECT((int)strlen(test_mem_save_content()) < WK_MEM_CONTENT_MAX, "content passed to module capped to WK_MEM_CONTENT_MAX");
        if (out) ty_cJSON_Delete(out);
        ty_cJSON_Delete(args);
    }

    /* ---- memory_update: updates as a real JSON object -> passthrough to module ---- */
    if (h_update) {
        ty_cJSON *args = ty_cJSON_CreateObject();
        ty_cJSON_AddStringToObject(args, "id", "mem_0001");
        ty_cJSON *updates = ty_cJSON_CreateObject();
        ty_cJSON_AddStringToObject(updates, "content", "updated content");
        ty_cJSON_AddItemToObject(args, "updates", updates);
        ty_cJSON *out = NULL;
        OPERATE_RET rc = h_update("memory_update", args, &out, NULL);
        EXPECT(rc == OPRT_OK, "memory_update not error");
        EXPECT_STR_CONTAINS(result_text(out), "\"success\":true", "memory_update success body");
        EXPECT_STR_EQ(test_mem_update_id(), "mem_0001", "wukong_memory_update received id");
        EXPECT_STR_CONTAINS(test_mem_update_json(), "updated content", "wukong_memory_update received updates object");
        if (out) ty_cJSON_Delete(out);
        ty_cJSON_Delete(args);
    }

    /* ---- memory_update: string-encoded updates -> business error ----
     * Schema declares "updates" as object; a raw handler call with the string
     * form has no usable updates and must fail (exec's promotion owns decoding). */
    if (h_update) {
        ty_cJSON *args = ty_cJSON_CreateObject();
        ty_cJSON_AddStringToObject(args, "id", "mem_0002");
        ty_cJSON_AddStringToObject(args, "updates", "{\"importance\":9}");
        ty_cJSON *out = NULL;
        OPERATE_RET rc = h_update("memory_update", args, &out, NULL);
        EXPECT(rc != OPRT_OK, "memory_update (string updates) rejected");
        EXPECT_STR_CONTAINS(result_text(out), "false", "rejection carries success:false");
        if (out) ty_cJSON_Delete(out);
        ty_cJSON_Delete(args);
    }

    /* ---- memory_update: not found -> success:false + reason ---- */
    if (h_update) {
        test_mem_set_update_ret(OPRT_NOT_FOUND);
        ty_cJSON *args = ty_cJSON_CreateObject();
        ty_cJSON_AddStringToObject(args, "id", "mem_ghost");
        ty_cJSON *updates = ty_cJSON_CreateObject();
        ty_cJSON_AddStringToObject(updates, "content", "x");
        ty_cJSON_AddItemToObject(args, "updates", updates);
        ty_cJSON *out = NULL;
        OPERATE_RET rc = h_update("memory_update", args, &out, NULL);
        EXPECT(rc == OPRT_NOT_FOUND, "memory_update not-found returns NOT_FOUND");
        EXPECT_STR_CONTAINS(result_text(out), "not found", "memory_update not-found reason");
        if (out) ty_cJSON_Delete(out);
        ty_cJSON_Delete(args);
        test_mem_set_update_ret(OPRT_OK);
    }

    /* ---- memory_delete: calls the stub with the given id ---- */
    if (h_delete) {
        ty_cJSON *args = ty_cJSON_CreateObject();
        ty_cJSON_AddStringToObject(args, "id", "mem_0003");
        ty_cJSON *out = NULL;
        OPERATE_RET rc = h_delete("memory_delete", args, &out, NULL);
        EXPECT(rc == OPRT_OK, "memory_delete not error");
        EXPECT_STR_CONTAINS(result_text(out), "\"success\":true", "memory_delete success body");
        EXPECT(test_mem_delete_called(), "wukong_memory_delete was called");
        EXPECT_STR_EQ(test_mem_delete_id(), "mem_0003", "wukong_memory_delete received the right id");
        if (out) ty_cJSON_Delete(out);
        ty_cJSON_Delete(args);
    }

    TEST_END();
}
