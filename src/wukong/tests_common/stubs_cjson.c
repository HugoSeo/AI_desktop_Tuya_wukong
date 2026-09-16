/*
 * Shared cJSON stub implementation.
 * Used by: alarm, countdown, pomodoro, reminder, mcp tests.
 */
#include "ty_cJSON.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static ty_cJSON *new_item(int type)
{
    ty_cJSON *item = calloc(1, sizeof(*item));
    if (item != NULL) {
        item->type = type;
    }
    return item;
}

/* OR'd into ->type by ty_cJSON_AddItemReferenceToObject; masked off whenever
 * ->type is compared/switched on so a reference behaves like its base type
 * everywhere except ty_cJSON_Delete (which must not free shared storage). */
#define TY_CJSON_REF_FLAG   0x100
#define TY_CJSON_BASE_TYPE(t) ((t) & ~TY_CJSON_REF_FLAG)

ty_cJSON *ty_cJSON_CreateObject(void) { return new_item(TY_CJSON_OBJECT); }
ty_cJSON *ty_cJSON_CreateArray(void)  { return new_item(TY_CJSON_ARRAY); }

ty_cJSON *ty_cJSON_CreateString(const CHAR_T *value)
{
    ty_cJSON *item = new_item(TY_CJSON_STRING);
    if (item != NULL && value != NULL) {
        item->valuestring = strdup(value);
    }
    return item;
}

ty_cJSON *ty_cJSON_CreateNumber(INT_T value)
{
    ty_cJSON *item = new_item(TY_CJSON_NUMBER);
    if (item != NULL) {
        item->valueint = value;
        item->valuedouble = (double)value;   /* match real cjson: both fields set */
    }
    return item;
}

ty_cJSON *ty_cJSON_CreateBool(BOOL_T value)
{
    ty_cJSON *item = new_item(value ? TY_CJSON_TRUE : TY_CJSON_FALSE);
    if (item != NULL) {
        item->valueint = value ? 1 : 0;
    }
    return item;
}

/* Raw node: its valuestring is emitted verbatim by the printer (no quoting),
 * mirroring real cJSON_CreateRaw -- used by the MCP transport to splice an
 * already-serialized content string into a reply object. */
ty_cJSON *ty_cJSON_CreateRaw(const CHAR_T *raw)
{
    ty_cJSON *item = new_item(TY_CJSON_RAW);
    if (item != NULL && raw != NULL) {
        item->valuestring = strdup(raw);
    }
    return item;
}

void ty_cJSON_AddItemToObject(ty_cJSON *object, const CHAR_T *key,
                              ty_cJSON *item)
{
    ty_cJSON *cursor = NULL;
    if (object == NULL || item == NULL) return;
    item->string = key ? strdup(key) : NULL;
    if (object->child == NULL) {
        object->child = item;
        return;
    }
    cursor = object->child;
    while (cursor->next != NULL) cursor = cursor->next;
    cursor->next = item;
}

void ty_cJSON_AddStringToObject(ty_cJSON *object, const CHAR_T *key,
                                const CHAR_T *value)
{
    ty_cJSON_AddItemToObject(object, key, ty_cJSON_CreateString(value));
}

void ty_cJSON_AddNumberToObject(ty_cJSON *object, const CHAR_T *key,
                                INT_T value)
{
    ty_cJSON_AddItemToObject(object, key, ty_cJSON_CreateNumber(value));
}

void ty_cJSON_AddBoolToObject(ty_cJSON *object, const CHAR_T *key, BOOL_T value)
{
    ty_cJSON_AddItemToObject(object, key, ty_cJSON_CreateBool(value));
}

void ty_cJSON_AddItemToArray(ty_cJSON *array, ty_cJSON *item)
{
    ty_cJSON *cursor = NULL;
    if (array == NULL || item == NULL) return;
    if (array->child == NULL) { array->child = item; return; }
    cursor = array->child;
    while (cursor->next != NULL) cursor = cursor->next;
    cursor->next = item;
}

void ty_cJSON_AddItemReferenceToObject(ty_cJSON *object, const CHAR_T *key, ty_cJSON *item)
{
    ty_cJSON *ref = NULL;
    if (object == NULL || item == NULL) return;
    ref = calloc(1, sizeof(*ref));
    if (ref == NULL) return;
    ref->type = item->type | TY_CJSON_REF_FLAG;
    ref->valuestring = item->valuestring;   /* shared, not owned by ref */
    ref->valueint = item->valueint;
    ref->valuedouble = item->valuedouble;
    ref->child = item->child;               /* shared, not owned by ref */
    ty_cJSON_AddItemToObject(object, key, ref);
}

ty_cJSON *ty_cJSON_GetObjectItem(CONST ty_cJSON *object, const CHAR_T *key)
{
    ty_cJSON *cursor = NULL;
    if (object == NULL || key == NULL) return NULL;
    cursor = object->child;
    while (cursor != NULL) {
        if (cursor->string != NULL && strcmp(cursor->string, key) == 0)
            return cursor;
        cursor = cursor->next;
    }
    return NULL;
}

BOOL_T ty_cJSON_IsString(CONST ty_cJSON *item)
{ return (item && TY_CJSON_BASE_TYPE(item->type) == TY_CJSON_STRING) ? TRUE : FALSE; }
BOOL_T ty_cJSON_IsNumber(CONST ty_cJSON *item)
{ return (item && TY_CJSON_BASE_TYPE(item->type) == TY_CJSON_NUMBER) ? TRUE : FALSE; }
BOOL_T ty_cJSON_IsObject(CONST ty_cJSON *item)
{ return (item && TY_CJSON_BASE_TYPE(item->type) == TY_CJSON_OBJECT) ? TRUE : FALSE; }
BOOL_T ty_cJSON_IsArray(CONST ty_cJSON *item)
{ return (item && TY_CJSON_BASE_TYPE(item->type) == TY_CJSON_ARRAY) ? TRUE : FALSE; }
BOOL_T ty_cJSON_IsBool(CONST ty_cJSON *item)
{
    return (item && (TY_CJSON_BASE_TYPE(item->type) == TY_CJSON_TRUE ||
                      TY_CJSON_BASE_TYPE(item->type) == TY_CJSON_FALSE)) ?
           TRUE : FALSE;
}
BOOL_T ty_cJSON_IsTrue(CONST ty_cJSON *item)
{ return (item && TY_CJSON_BASE_TYPE(item->type) == TY_CJSON_TRUE) ? TRUE : FALSE; }

INT_T ty_cJSON_GetArraySize(CONST ty_cJSON *array)
{
    INT_T count = 0;
    ty_cJSON *child;
    if (array == NULL) return 0;
    child = array->child;
    while (child) { count++; child = child->next; }
    return count;
}

ty_cJSON *ty_cJSON_GetArrayItem(CONST ty_cJSON *array, INT_T index)
{
    ty_cJSON *child;
    if (array == NULL) return NULL;
    child = array->child;
    while (child && index > 0) { child = child->next; index--; }
    return child;
}

/*
 * Minimal recursive-descent JSON parser backing ty_cJSON_Parse.
 *
 * Prior consumers of this stub (tm/mcp tests) only ever exercised the
 * write-side Create-/Add-family API and never depended on Parse() actually
 * succeeding (e.g. wukong_tm_alarm.c's store-load path treats a NULL
 * root as "no persisted data" and continues fine -- no existing test
 * exercises that path with real data). provider/claw's wukong_llm host
 * test is the first consumer that must parse a real HTTP JSON response
 * body, so Parse needs to be a real (if small) parser rather than the
 * previous always-NULL placeholder.
 */
static void __json_skip_ws(const char **p)
{
    while (**p == ' ' || **p == '\t' || **p == '\n' || **p == '\r') (*p)++;
}

static ty_cJSON *__json_parse_value(const char **p);

static char *__json_parse_string(const char **p)
{
    /* assumes **p == '"' */
    size_t cap = 32, len = 0;
    char *out = malloc(cap);
    if (out == NULL) return NULL;
    (*p)++;
    while (**p != '\0' && **p != '"') {
        char c = **p;
        if (c == '\\' && (*p)[1] != '\0') {
            (*p)++;
            switch (**p) {
            case 'n': c = '\n'; break;
            case 't': c = '\t'; break;
            case 'r': c = '\r'; break;
            case 'b': c = '\b'; break;
            case 'f': c = '\f'; break;
            default:  c = **p;  break; /* '"', '\\', '/' and unknown escapes */
            }
        }
        if (len + 1 >= cap) {
            cap *= 2;
            char *grown = realloc(out, cap);
            if (grown == NULL) { free(out); return NULL; }
            out = grown;
        }
        out[len++] = c;
        (*p)++;
    }
    if (**p == '"') (*p)++;
    out[len] = '\0';
    return out;
}

static ty_cJSON *__json_parse_object(const char **p)
{
    ty_cJSON *obj = new_item(TY_CJSON_OBJECT);
    if (obj == NULL) return NULL;
    (*p)++; /* skip '{' */
    __json_skip_ws(p);
    if (**p == '}') { (*p)++; return obj; }
    for (;;) {
        __json_skip_ws(p);
        if (**p != '"') break;
        char *key = __json_parse_string(p);
        __json_skip_ws(p);
        if (**p == ':') (*p)++;
        ty_cJSON *val = __json_parse_value(p);
        if (key != NULL && val != NULL) {
            ty_cJSON_AddItemToObject(obj, key, val);
        } else if (val != NULL) {
            ty_cJSON_Delete(val);
        }
        free(key);
        __json_skip_ws(p);
        if (**p == ',') { (*p)++; continue; }
        break;
    }
    __json_skip_ws(p);
    if (**p == '}') (*p)++;
    return obj;
}

static ty_cJSON *__json_parse_array(const char **p)
{
    ty_cJSON *arr = new_item(TY_CJSON_ARRAY);
    if (arr == NULL) return NULL;
    (*p)++; /* skip '[' */
    __json_skip_ws(p);
    if (**p == ']') { (*p)++; return arr; }
    for (;;) {
        ty_cJSON *val = __json_parse_value(p);
        if (val != NULL) ty_cJSON_AddItemToArray(arr, val);
        __json_skip_ws(p);
        if (**p == ',') { (*p)++; continue; }
        break;
    }
    __json_skip_ws(p);
    if (**p == ']') (*p)++;
    return arr;
}

static ty_cJSON *__json_parse_value(const char **p)
{
    __json_skip_ws(p);
    switch (**p) {
    case '{': return __json_parse_object(p);
    case '[': return __json_parse_array(p);
    case '"': {
        char *s = __json_parse_string(p);
        ty_cJSON *item = ty_cJSON_CreateString(s ? s : "");
        free(s);
        return item;
    }
    case 't':
        if (strncmp(*p, "true", 4) == 0) { *p += 4; return ty_cJSON_CreateBool(TRUE); }
        return NULL;
    case 'f':
        if (strncmp(*p, "false", 5) == 0) { *p += 5; return ty_cJSON_CreateBool(FALSE); }
        return NULL;
    case 'n':
        if (strncmp(*p, "null", 4) == 0) { *p += 4; return new_item(TY_CJSON_NULL); }
        return NULL;
    default:
        if (**p == '-' || (**p >= '0' && **p <= '9')) {
            char *end = NULL;
            double d = strtod(*p, &end);
            if (end == *p) return NULL;
            *p = end;
            return ty_cJSON_CreateNumber((INT_T)d);
        }
        return NULL;
    }
}

ty_cJSON *ty_cJSON_Parse(CONST CHAR_T *value)
{
    if (value == NULL) return NULL;
    const char *p = value;
    return __json_parse_value(&p);
}

static void append_json(char *buf, size_t len, const ty_cJSON *item)
{
    const ty_cJSON *child = NULL;
    int first = 1;
    char num[32];
    if (item == NULL) { strncat(buf, "null", len - strlen(buf) - 1); return; }
    switch (TY_CJSON_BASE_TYPE(item->type)) {
    case TY_CJSON_OBJECT:
        strncat(buf, "{", len - strlen(buf) - 1);
        child = item->child;
        while (child) {
            if (!first) strncat(buf, ",", len - strlen(buf) - 1);
            first = 0;
            strncat(buf, "\"", len - strlen(buf) - 1);
            strncat(buf, child->string ? child->string : "", len - strlen(buf) - 1);
            strncat(buf, "\":", len - strlen(buf) - 1);
            append_json(buf, len, child);
            child = child->next;
        }
        strncat(buf, "}", len - strlen(buf) - 1);
        break;
    case TY_CJSON_ARRAY:
        strncat(buf, "[", len - strlen(buf) - 1);
        child = item->child;
        while (child) {
            if (!first) strncat(buf, ",", len - strlen(buf) - 1);
            first = 0;
            append_json(buf, len, child);
            child = child->next;
        }
        strncat(buf, "]", len - strlen(buf) - 1);
        break;
    case TY_CJSON_STRING:
        strncat(buf, "\"", len - strlen(buf) - 1);
        strncat(buf, item->valuestring ? item->valuestring : "", len - strlen(buf) - 1);
        strncat(buf, "\"", len - strlen(buf) - 1);
        break;
    case TY_CJSON_RAW:
        /* Emit the pre-serialized JSON payload verbatim (no surrounding quotes). */
        strncat(buf, item->valuestring ? item->valuestring : "null", len - strlen(buf) - 1);
        break;
    case TY_CJSON_NUMBER:
        snprintf(num, sizeof(num), "%d", item->valueint);
        strncat(buf, num, len - strlen(buf) - 1);
        break;
    case TY_CJSON_TRUE:
        strncat(buf, "true", len - strlen(buf) - 1);
        break;
    case TY_CJSON_FALSE:
        strncat(buf, "false", len - strlen(buf) - 1);
        break;
    default:
        strncat(buf, "null", len - strlen(buf) - 1);
        break;
    }
}

CHAR_T *ty_cJSON_PrintUnformatted(const ty_cJSON *item)
{
    CHAR_T *buf = calloc(1, 2048);
    if (buf == NULL) return NULL;
    append_json(buf, 2048, item);
    return buf;
}

void ty_cJSON_Delete(ty_cJSON *item)
{
    ty_cJSON *child, *next;
    if (item == NULL) return;
    /* A reference wrapper shares child/valuestring with the original owner
     * (see ty_cJSON_AddItemReferenceToObject) -- never free those here, only
     * the wrapper's own key string and itself. */
    if (!(item->type & TY_CJSON_REF_FLAG)) {
        child = item->child;
        while (child) { next = child->next; ty_cJSON_Delete(child); child = next; }
        free(item->valuestring);
    }
    free(item->string);
    free(item);
}

void ty_cJSON_FreeBuffer(CHAR_T *buffer) { free(buffer); }

ty_cJSON *ty_cJSON_Duplicate(const ty_cJSON *item, BOOL_T recurse)
{
    ty_cJSON *copy = NULL;
    ty_cJSON *child_copy = NULL;
    ty_cJSON *tail = NULL;
    const ty_cJSON *child = NULL;

    if (item == NULL) return NULL;
    copy = new_item(TY_CJSON_BASE_TYPE(item->type));   /* duplicate is a real owner, never a reference */
    if (copy == NULL) return NULL;
    if (item->valuestring != NULL) {
        copy->valuestring = strdup(item->valuestring);
    }
    copy->valueint = item->valueint;
    copy->valuedouble = item->valuedouble;
    if (item->string != NULL) {
        copy->string = strdup(item->string);
    }
    if (recurse && item->child != NULL) {
        child = item->child;
        while (child != NULL) {
            child_copy = ty_cJSON_Duplicate(child, TRUE);
            if (child_copy == NULL) { ty_cJSON_Delete(copy); return NULL; }
            if (tail != NULL) { tail->next = child_copy; } else { copy->child = child_copy; }
            tail = child_copy;
            child = child->next;
        }
    }
    return copy;
}

void ty_cJSON_DeleteItemFromObject(ty_cJSON *object, const CHAR_T *key)
{
    ty_cJSON *prev = NULL;
    ty_cJSON *cursor = NULL;

    if (object == NULL || key == NULL) return;
    cursor = object->child;
    while (cursor != NULL) {
        if (cursor->string != NULL && strcmp(cursor->string, key) == 0) {
            if (prev == NULL) { object->child = cursor->next; } else { prev->next = cursor->next; }
            cursor->next = NULL;
            ty_cJSON_Delete(cursor);
            return;
        }
        prev = cursor;
        cursor = cursor->next;
    }
}

/* Replace the value stored under key, preserving list position + key string.
 * If key is absent, newitem is deleted (no dangling insert). */
void ty_cJSON_ReplaceItemInObject(ty_cJSON *object, const CHAR_T *key, ty_cJSON *newitem)
{
    ty_cJSON *prev = NULL;
    ty_cJSON *cursor = NULL;

    if (object == NULL || key == NULL || newitem == NULL) return;
    cursor = object->child;
    while (cursor != NULL) {
        if (cursor->string != NULL && strcmp(cursor->string, key) == 0) {
            free(newitem->string);
            newitem->string = strdup(key);
            newitem->next = cursor->next;
            if (prev == NULL) { object->child = newitem; } else { prev->next = newitem; }
            cursor->next = NULL;
            ty_cJSON_Delete(cursor);
            return;
        }
        prev = cursor;
        cursor = cursor->next;
    }
    ty_cJSON_Delete(newitem);
}
