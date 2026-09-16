/**
 * @file wukong_tool.c
 * @brief Neutral tool base: registry, per-caller visibility, single exec.
 *
 * Independent of the MCP transport (mcp/mcp_server_tools.c only frames JSON-RPC
 * on top of this base): a board that disables MCP_ENABLE_TOOLS still has a
 * working registry for the agent loop / fc dispatch.
 *
 * @copyright Copyright (c) 2025 Tuya Inc. All Rights Reserved.
 */

#include "wukong_tool.h"
#include "tal_memory.h"
#include "tal_log.h"
#include "mix_method.h"   /* mm_strdup */
#include "utilities/uni_base64.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ========================================================================== */
/*                            Internal Types                                  */
/* ========================================================================== */

/* A registered tool entry. Private to this base; the transport reaches tools
 * only through the exec/list surface, no longer through the raw entry. */
typedef struct WUKONG_TOOL_ENTRY {
    CHAR_T *name;
    CHAR_T *description;
    ty_cJSON *input_schema;
    WUKONG_TOOL_HANDLER_CB handler;
    VOID *user_data;
    UINT_T flags;
    struct WUKONG_TOOL_ENTRY *next;
} WUKONG_TOOL_ENTRY_T;

STATIC WUKONG_TOOL_ENTRY_T *s_tools = NULL;

/**
 * @brief Add a pipe-separated string enum list to one JSON schema property.
 */
STATIC VOID __schema_add_string_enum(ty_cJSON *prop_obj, CONST CHAR_T *enum_values)
{
    ty_cJSON *enum_arr = NULL;
    CONST CHAR_T *start = enum_values;
    CONST CHAR_T *end = NULL;
    CHAR_T value[64] = {0};
    UINT_T len = 0;

    if (prop_obj == NULL || enum_values == NULL || enum_values[0] == '\0') {
        return;
    }

    enum_arr = ty_cJSON_CreateArray();
    if (enum_arr == NULL) {
        return;
    }

    while (start != NULL && start[0] != '\0') {
        end = strchr(start, '|');
        if (end == NULL) {
            end = start + strlen(start);
        }

        len = (UINT_T)(end - start);
        if (len > 0 && len < sizeof(value)) {
            (VOID)snprintf(value, sizeof(value), "%.*s", (INT_T)len, start);
            ty_cJSON_AddItemToArray(enum_arr, ty_cJSON_CreateString(value));
        }

        if (end[0] == '\0') {
            break;
        }
        start = end + 1;
    }

    if (ty_cJSON_GetArraySize(enum_arr) > 0) {
        ty_cJSON_AddItemToObject(prop_obj, "enum", enum_arr);
    } else {
        ty_cJSON_Delete(enum_arr);
    }
}

/**
 * @brief Check whether one string value is present in a JSON schema enum array.
 */
STATIC BOOL_T __schema_enum_contains(CONST ty_cJSON *enum_arr, CONST CHAR_T *value)
{
    ty_cJSON *item = NULL;

    if (!ty_cJSON_IsArray(enum_arr) || value == NULL) {
        return FALSE;
    }

    for (item = enum_arr->child; item != NULL; item = item->next) {
        if (ty_cJSON_IsString(item) && item->valuestring != NULL &&
            strcmp(item->valuestring, value) == 0) {
            return TRUE;
        }
    }

    return FALSE;
}

/* ========================================================================== */
/*                       Schema Builder (internal)                            */
/* ========================================================================== */

STATIC ty_cJSON *__build_input_schema(va_list ap)
{
    ty_cJSON *schema, *props, *required;
    WUKONG_TOOL_SCHEMA_PROP_T *p;

    schema = ty_cJSON_CreateObject();
    if (!schema)
        return NULL;

    ty_cJSON_AddStringToObject(schema, "type", "object");

    props = ty_cJSON_CreateObject();
    required = ty_cJSON_CreateArray();
    if (!props || !required) {
        ty_cJSON_Delete(props);
        ty_cJSON_Delete(required);
        ty_cJSON_Delete(schema);
        return NULL;
    }

    while ((p = va_arg(ap, WUKONG_TOOL_SCHEMA_PROP_T *)) != NULL) {
        ty_cJSON *prop_obj = ty_cJSON_CreateObject();
        if (!prop_obj)
            continue;

        ty_cJSON_AddStringToObject(prop_obj, "type", p->type);
        if (p->description)
            ty_cJSON_AddStringToObject(prop_obj, "description", p->description);
        if (p->has_minimum)
            ty_cJSON_AddNumberToObject(prop_obj, "minimum", p->minimum);
        if (p->has_maximum)
            ty_cJSON_AddNumberToObject(prop_obj, "maximum", p->maximum);
        __schema_add_string_enum(prop_obj, p->enum_values);

        ty_cJSON_AddItemToObject(props, p->name, prop_obj);

        if (p->required)
            ty_cJSON_AddItemToArray(required, ty_cJSON_CreateString(p->name));
    }

    ty_cJSON_AddItemToObject(schema, "properties", props);

    if (ty_cJSON_GetArraySize(required) > 0)
        ty_cJSON_AddItemToObject(schema, "required", required);
    else
        ty_cJSON_Delete(required);

    return schema;
}

/* ========================================================================== */
/*                          Argument Validation                               */
/* ========================================================================== */

/**
 * @brief Whether property @p name is declared in the schema with a type other
 *        than "string". This is a whitelist: undeclared properties (no schema,
 *        or the property simply isn't listed) and properties declared as
 *        "string" both return FALSE — only an EXPLICIT non-string declaration
 *        makes a property eligible for promotion.
 */
STATIC BOOL_T __schema_prop_is_promotable(CONST WUKONG_TOOL_ENTRY_T *tool, CONST CHAR_T *name)
{
    ty_cJSON *props, *prop, *type;

    if (!tool || !tool->input_schema || !name)
        return FALSE;
    props = ty_cJSON_GetObjectItem(tool->input_schema, "properties");
    if (!props)
        return FALSE;
    prop = ty_cJSON_GetObjectItem(props, name);
    if (!prop)
        return FALSE;  /* not declared: never a promotion candidate */
    type = ty_cJSON_GetObjectItem(prop, "type");
    return (ty_cJSON_IsString(type) && type->valuestring != NULL &&
            strcmp(type->valuestring, "string") != 0) ? TRUE : FALSE;
}

/**
 * @brief Whether the tool's schema declares at least one non-string property,
 *        i.e. whether __promote_string_args has anything at all to promote.
 *
 * Lets exec skip the duplicate+promotion pass entirely for schema-less or
 * all-string tools (e.g. fc's music/story/PlayControl/cloud_event, which
 * register with zero schema properties): no reason to deep-copy a large cloud
 * payload just to walk it and promote nothing.
 */
STATIC BOOL_T __schema_has_promotable_props(CONST WUKONG_TOOL_ENTRY_T *tool)
{
    ty_cJSON *props, *prop, *type;

    if (!tool || !tool->input_schema)
        return FALSE;
    props = ty_cJSON_GetObjectItem(tool->input_schema, "properties");
    if (!props)
        return FALSE;
    for (prop = props->child; prop != NULL; prop = prop->next) {
        type = ty_cJSON_GetObjectItem(prop, "type");
        if (ty_cJSON_IsString(type) && type->valuestring != NULL &&
            strcmp(type->valuestring, "string") != 0)
            return TRUE;
    }
    return FALSE;
}

/**
 * @brief Promote JSON-encoded string args to their declared (non-string) types.
 *
 * LLMs often pass nested params as JSON-encoded strings, e.g. "tags":"[\"a\"]" —
 * parse them up so handlers only see real types. Whitelist semantics: only
 * properties EXPLICITLY declared in the schema with a non-string type are
 * candidates. Undeclared properties and properties declared as "string" are
 * left untouched — this matters for schema-less FC payloads (music/story/
 * PlayControl/cloud_event), where a cloud-sent field like "songId":"12345"
 * must stay a string instead of being silently parsed into a number.
 */
STATIC VOID __promote_string_args(CONST WUKONG_TOOL_ENTRY_T *tool, ty_cJSON *args)
{
    ty_cJSON *p;

    for (p = args ? args->child : NULL; p != NULL; p = p->next) {
        if (!ty_cJSON_IsString(p) || p->valuestring == NULL) continue;
        if (!__schema_prop_is_promotable(tool, p->string)) continue;  /* not declared non-string: leave it */
        ty_cJSON *parsed = ty_cJSON_Parse(p->valuestring);
        if (parsed != NULL) {
            ty_cJSON_ReplaceItemInObject(args, p->string, parsed);
            p = parsed;
        }
    }
}

/**
 * @brief Validate numeric/enum arguments against schema minimum/maximum/enum.
 */
STATIC OPERATE_RET __validate_schema_constraints(CONST WUKONG_TOOL_ENTRY_T *tool,
                                                 CONST ty_cJSON *arguments)
{
    ty_cJSON *props, *prop, *arg, *min_j, *max_j, *enum_j;

    if (!tool->input_schema || !arguments)
        return OPRT_OK;

    props = ty_cJSON_GetObjectItem(tool->input_schema, "properties");
    if (!props)
        return OPRT_OK;

    for (prop = props->child; prop; prop = prop->next) {
        arg = ty_cJSON_GetObjectItem(arguments, prop->string);
        if (!arg)
            continue;

        if (ty_cJSON_IsNumber(arg)) {
            min_j = ty_cJSON_GetObjectItem(prop, "minimum");
            if (min_j && ty_cJSON_IsNumber(min_j) && arg->valuedouble < min_j->valuedouble)
                return OPRT_INVALID_PARM;

            max_j = ty_cJSON_GetObjectItem(prop, "maximum");
            if (max_j && ty_cJSON_IsNumber(max_j) && arg->valuedouble > max_j->valuedouble)
                return OPRT_INVALID_PARM;
        }

        enum_j = ty_cJSON_GetObjectItem(prop, "enum");
        if (ty_cJSON_IsString(arg) && ty_cJSON_IsArray(enum_j) &&
            !__schema_enum_contains(enum_j, arg->valuestring))
            return OPRT_INVALID_PARM;
    }

    return OPRT_OK;
}

/**
 * @brief Validate declared argument types (after promotion). A property whose
 *        value still mismatches its declared JSON type is rejected with a
 *        message the model can read and self-correct on — e.g. glm packing
 *        array tags into one comma-separated string survives promotion as a
 *        string; silently letting it through made the handler skip the field
 *        and fake success. Undeclared properties are ignored (whitelist).
 *
 * On mismatch writes "Invalid argument '<name>': must be a JSON <type>" into
 * @p msg and returns OPRT_INVALID_PARM.
 */
STATIC OPERATE_RET __validate_arg_types(CONST WUKONG_TOOL_ENTRY_T *tool,
                                        CONST ty_cJSON *arguments,
                                        CHAR_T *msg, UINT_T msg_len)
{
    ty_cJSON *props, *prop, *arg, *type_j;

    if (!tool->input_schema || !arguments)
        return OPRT_OK;

    props = ty_cJSON_GetObjectItem(tool->input_schema, "properties");
    if (!props)
        return OPRT_OK;

    for (prop = props->child; prop; prop = prop->next) {
        arg = ty_cJSON_GetObjectItem((ty_cJSON *)arguments, prop->string);
        if (!arg)
            continue;
        type_j = ty_cJSON_GetObjectItem(prop, "type");
        if (!type_j || !ty_cJSON_IsString(type_j) || type_j->valuestring == NULL)
            continue;

        CONST CHAR_T *t = type_j->valuestring;
        BOOL_T ok = TRUE;
        if (strcmp(t, "array") == 0)
            ok = ty_cJSON_IsArray(arg);
        else if (strcmp(t, "object") == 0)
            ok = ty_cJSON_IsObject(arg);
        else if (strcmp(t, "string") == 0)
            ok = ty_cJSON_IsString(arg);
        else if (strcmp(t, "boolean") == 0)
            ok = ty_cJSON_IsBool(arg);
        else if (strcmp(t, "integer") == 0 || strcmp(t, "number") == 0)
            ok = ty_cJSON_IsNumber(arg);

        if (!ok) {
            (VOID)snprintf(msg, msg_len, "Invalid argument '%s': must be a JSON %s",
                           prop->string, t);
            return OPRT_INVALID_PARM;
        }
    }

    return OPRT_OK;
}

/**
 * @brief Validate that all required arguments are present.
 */
STATIC OPERATE_RET __validate_required_args(CONST WUKONG_TOOL_ENTRY_T *tool,
                                            CONST ty_cJSON *arguments)
{
    ty_cJSON *required;
    ty_cJSON *item;
    INT_T i, count;

    if (!tool || !tool->input_schema) {
        return OPRT_OK;
    }

    required = ty_cJSON_GetObjectItem(tool->input_schema, "required");
    if (!ty_cJSON_IsArray(required)) {
        return OPRT_OK;
    }

    count = ty_cJSON_GetArraySize(required);
    for (i = 0; i < count; i++) {
        item = ty_cJSON_GetArrayItem(required, i);
        if (!ty_cJSON_IsString(item)) {
            continue;
        }
        if (!arguments || !ty_cJSON_GetObjectItem(arguments, item->valuestring)) {
            return OPRT_INVALID_PARM;
        }
    }

    return OPRT_OK;
}

/* ========================================================================== */
/*                           Tool Registration                                */
/* ========================================================================== */

OPERATE_RET wukong_tool_register(CONST CHAR_T *name,
                                 CONST CHAR_T *description,
                                 WUKONG_TOOL_HANDLER_CB handler,
                                 VOID *user_data, UINT_T flags, ...)
{
    WUKONG_TOOL_ENTRY_T *entry, *cur;
    va_list ap;

    if (!name || !description || !handler)
        return OPRT_INVALID_PARM;

    for (cur = s_tools; cur; cur = cur->next) {
        if (strcmp(cur->name, name) == 0) {
            TAL_PR_WARN("Tool '%s' already registered", name);
            return OPRT_COM_ERROR;
        }
    }

    entry = (WUKONG_TOOL_ENTRY_T *)tal_calloc(1, sizeof(*entry));
    if (!entry)
        return OPRT_MALLOC_FAILED;

    entry->name = mm_strdup(name);
    entry->description = mm_strdup(description);
    if (!entry->name || !entry->description)
        goto err;

    entry->handler = handler;
    entry->user_data = user_data;
    entry->flags = flags;

    va_start(ap, flags);
    entry->input_schema = __build_input_schema(ap);
    va_end(ap);

    if (!entry->input_schema)
        goto err;

    entry->next = s_tools;
    s_tools = entry;

    TAL_PR_INFO("Tool registered: %s (flags=0x%x)", name, flags);
    return OPRT_OK;

err:
    if (entry) {
        tal_free(entry->name);
        tal_free(entry->description);
        if (entry->input_schema)
            ty_cJSON_Delete(entry->input_schema);
        tal_free(entry);
    }
    return OPRT_MALLOC_FAILED;
}

/* ========================================================================== */
/*                          Lookup + Traversal                                */
/* ========================================================================== */

STATIC WUKONG_TOOL_ENTRY_T *__find_tool(CONST CHAR_T *name)
{
    WUKONG_TOOL_ENTRY_T *tool;

    if (!name)
        return NULL;
    for (tool = s_tools; tool; tool = tool->next)
        if (strcmp(tool->name, name) == 0)
            return tool;
    return NULL;
}

/* 单个工具 → MCP 原生形状 {name,description,inputSchema}(A2A/云上传同款)。
 * inputSchema 用引用(零拷贝);数组被删时引用项不释放注册表 schema。 */
STATIC ty_cJSON *__tool_entry_to_json(CONST WUKONG_TOOL_ENTRY_T *tool)
{
    ty_cJSON *tj = ty_cJSON_CreateObject();
    if (!tj)
        return NULL;
    ty_cJSON_AddStringToObject(tj, "name", tool->name);
    ty_cJSON_AddStringToObject(tj, "description", tool->description);
    ty_cJSON_AddItemReferenceToObject(tj, "inputSchema", tool->input_schema);
    return tj;
}

/* 唯一遍历。只收 (flags & caller) 命中的工具;max_payload>0 → 分页(A2A 原逻辑);
 * ==0 → 全量不分页(Claw)。out_next_cursor(可选)指向注册表内常量 tool->name,
 * 调用方即用即取,不拥有。 */
ty_cJSON *wukong_tool_collect(UINT_T caller, CONST CHAR_T *cursor,
                              INT_T max_payload, CONST CHAR_T **out_next_cursor)
{
    ty_cJSON *arr = ty_cJSON_CreateArray();
    if (!arr)
        return NULL;
    if (out_next_cursor)
        *out_next_cursor = NULL;
    BOOL_T found = (cursor == NULL || cursor[0] == '\0');
    INT_T payload_len = 0;
    for (WUKONG_TOOL_ENTRY_T *t = s_tools; t; t = t->next) {
        if ((t->flags & caller) == 0)
            continue;
        if (!found) {
            if (strcmp(t->name, cursor) == 0)
                found = TRUE;
            else
                continue;
        }
        ty_cJSON *tj = __tool_entry_to_json(t);
        if (!tj)
            continue;
        if (max_payload > 0) {
            CHAR_T *ts = ty_cJSON_PrintUnformatted(tj);
            if (ts) {
                INT_T tl = (INT_T)strlen(ts);
                if (payload_len + tl + 128 > max_payload) {
                    if (out_next_cursor)
                        *out_next_cursor = t->name;
                    ty_cJSON_FreeBuffer(ts);
                    ty_cJSON_Delete(tj);
                    break;
                }
                payload_len += tl;
                ty_cJSON_FreeBuffer(ts);
            }
        }
        ty_cJSON_AddItemToArray(arr, tj);
    }
    return arr;
}

ty_cJSON *wukong_tool_list(UINT_T caller)
{
    return wukong_tool_collect(caller, NULL, 0, NULL);
}

/* ========================================================================== */
/*                    Content -> Text (agent flattening)                      */
/* ========================================================================== */

/**
 * @brief Concatenate the text of every {"type":"text","text":...} content item.
 * @return Heap string with all text items concatenated (empty string "" if
 *         content is NULL/has no text items), or NULL on allocation failure.
 */
STATIC CHAR_T *__content_to_text(CONST ty_cJSON *content)
{
    if (!content)
        return mm_strdup("");

    /* 先算总长,再拼 */
    SIZE_T total = 0;
    INT_T n = ty_cJSON_GetArraySize(content);
    for (INT_T i = 0; i < n; i++) {
        ty_cJSON *it = ty_cJSON_GetArrayItem(content, i);
        ty_cJSON *tp = it ? ty_cJSON_GetObjectItem(it, "type") : NULL;
        ty_cJSON *tx = it ? ty_cJSON_GetObjectItem(it, "text") : NULL;
        if (tp && ty_cJSON_IsString(tp) && strcmp(tp->valuestring, "text") == 0 &&
            tx && ty_cJSON_IsString(tx))
            total += strlen(tx->valuestring);
    }

    CHAR_T *out = (CHAR_T *)tal_malloc(total + 1);
    if (!out)
        return NULL;
    out[0] = '\0';

    SIZE_T off = 0;
    for (INT_T i = 0; i < n; i++) {
        ty_cJSON *it = ty_cJSON_GetArrayItem(content, i);
        ty_cJSON *tp = it ? ty_cJSON_GetObjectItem(it, "type") : NULL;
        ty_cJSON *tx = it ? ty_cJSON_GetObjectItem(it, "text") : NULL;
        if (tp && ty_cJSON_IsString(tp) && strcmp(tp->valuestring, "text") == 0 &&
            tx && ty_cJSON_IsString(tx)) {
            SIZE_T l = strlen(tx->valuestring);
            memcpy(out + off, tx->valuestring, l);
            off += l;
            out[off] = '\0';
        }
    }
    return out;
}

/* ========================================================================== */
/*                              Tool Execution                                */
/* ========================================================================== */

OPERATE_RET wukong_tool_exec(UINT_T caller, CONST CHAR_T *name,
                             CONST ty_cJSON *args, CHAR_T **out_text)
{
    WUKONG_TOOL_ENTRY_T *tool = __find_tool(name);
    ty_cJSON *content = NULL;
    ty_cJSON *dup = NULL;
    CONST ty_cJSON *exec_args = args;
    OPERATE_RET rt;

    if (out_text) *out_text = NULL;
    if (name == NULL) return OPRT_INVALID_PARM;
    /* ① flags 可见性: 调用时强制, 不含该位视同不存在 */
    if (tool == NULL || (tool->flags & caller) == 0) return OPRT_NOT_FOUND;

    /* Only duplicate+promote when the schema actually declares a non-string
     * property to promote. Schema-less/all-string tools (fc music/story/
     * PlayControl/cloud_event) skip this pass entirely: exec_args stays the
     * caller's original args, unmodified and NOT owned here — caller keeps
     * ownership, exec must not delete it. */
    if (args && __schema_has_promotable_props(tool)) {
        dup = ty_cJSON_Duplicate((ty_cJSON *)args, 1);
        __promote_string_args(tool, dup);
        exec_args = dup;
    }

    /* ②③ 校验(required + 数值/枚举约束, 实参已类型提升) */
    rt = __validate_required_args(tool, exec_args);
    if (rt == OPRT_OK) rt = __validate_schema_constraints(tool, exec_args);
    if (rt != OPRT_OK) {
        if (dup) ty_cJSON_Delete(dup);
        return rt;
    }

    /* ②b 类型一致性: 声明类型与实参不符(提升后仍不符)= 业务错误, 说明文本
     * 经 ⑤ 整形送达对端, 模型读到即可自纠重发。旧行为是静默放行, handler 把
     * 错型字段当"没传", 造成假成功(如 tags 逗号串被悄悄丢弃)。 */
    CHAR_T type_msg[128];
    if (__validate_arg_types(tool, exec_args, type_msg, sizeof(type_msg)) != OPRT_OK) {
        content = ty_cJSON_CreateArray();
        if (content) ty_cJSON_AddItemToArray(content, wukong_tool_make_text(type_msg));
        rt = OPRT_INVALID_PARM;
    } else {
        /* ④ invoke handler (no error-flag out-param; error = non-OK rc) */
        rt = tool->handler(tool->name, exec_args, &content, tool->user_data);
    }

    /* ⑤ 按 caller 整形为文本(content 可为 NULL) */
    if (out_text && content) {
        *out_text = (caller == WUKONG_TOOL_AGENT) ? __content_to_text(content)
                                                  : ty_cJSON_PrintUnformatted(content);
    }
    if (content) ty_cJSON_Delete(content);
    if (dup) ty_cJSON_Delete(dup);
    return rt;   /* rt≠OK 且 *out_text 非空 = 业务错误, 调用方必须消费并 tal_free */
}

/* ========================================================================== */
/*                         Capability Destroy                                 */
/* ========================================================================== */

VOID mcp_tools_cap_destroy(VOID)
{
    WUKONG_TOOL_ENTRY_T *tool, *next;

    for (tool = s_tools; tool; tool = next) {
        next = tool->next;
        tal_free(tool->name);
        tal_free(tool->description);
        ty_cJSON_Delete(tool->input_schema);
        tal_free(tool);
    }
    s_tools = NULL;
}

/* ========================================================================== */
/*                        Content Builder Helpers                             */
/* ========================================================================== */

ty_cJSON *wukong_tool_make_text(CONST CHAR_T *text)
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

ty_cJSON *wukong_tool_make_image(CONST CHAR_T *mime_type,
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

    item = wukong_tool_make_image_base64(mime_type, base64_buf);
    tal_free(base64_buf);
    return item;
}

ty_cJSON *wukong_tool_make_image_base64(CONST CHAR_T *mime_type,
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

ty_cJSON *wukong_tool_make_resource(CONST CHAR_T *uri,
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
/*                        Backend-Aware Schema Builder                        */
/* ========================================================================== */

CHAR_T *wukong_tool_build_schema(WUKONG_LLM_BACKEND_E backend)
{
    if (backend != WUKONG_LLM_BACKEND_OPENAI)
        return NULL;   /* M2b: OpenAI only */

    ty_cJSON *list = wukong_tool_list(WUKONG_TOOL_AGENT);
    if (!list)
        return NULL;
    INT_T n = ty_cJSON_GetArraySize(list);
    if (n == 0) {
        ty_cJSON_Delete(list);
        return NULL;
    }

    ty_cJSON *tools = ty_cJSON_CreateArray();
    if (!tools) {
        ty_cJSON_Delete(list);
        return NULL;
    }
    for (INT_T i = 0; i < n; i++) {
        ty_cJSON *e = ty_cJSON_GetArrayItem(list, i);
        ty_cJSON *nm = ty_cJSON_GetObjectItem(e, "name");
        ty_cJSON *ds = ty_cJSON_GetObjectItem(e, "description");
        ty_cJSON *sc = ty_cJSON_GetObjectItem(e, "inputSchema");

        ty_cJSON *fn = ty_cJSON_CreateObject();
        ty_cJSON_AddStringToObject(fn, "name", (nm && ty_cJSON_IsString(nm)) ? nm->valuestring : "");
        ty_cJSON_AddStringToObject(fn, "description", (ds && ty_cJSON_IsString(ds)) ? ds->valuestring : "");
        if (sc)
            ty_cJSON_AddItemToObject(fn, "parameters", ty_cJSON_Duplicate(sc, 1));

        ty_cJSON *t = ty_cJSON_CreateObject();
        ty_cJSON_AddStringToObject(t, "type", "function");
        ty_cJSON_AddItemToObject(t, "function", fn);
        ty_cJSON_AddItemToArray(tools, t);
    }

    CHAR_T *s = ty_cJSON_PrintUnformatted(tools);
    ty_cJSON_Delete(tools);
    ty_cJSON_Delete(list);
    return s;
}
