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

void ty_cJSON_AddItemToArray(ty_cJSON *array, ty_cJSON *item)
{
    ty_cJSON *cursor = NULL;
    if (array == NULL || item == NULL) return;
    if (array->child == NULL) { array->child = item; return; }
    cursor = array->child;
    while (cursor->next != NULL) cursor = cursor->next;
    cursor->next = item;
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
{ return (item && item->type == TY_CJSON_STRING) ? TRUE : FALSE; }
BOOL_T ty_cJSON_IsNumber(CONST ty_cJSON *item)
{ return (item && item->type == TY_CJSON_NUMBER) ? TRUE : FALSE; }
BOOL_T ty_cJSON_IsObject(CONST ty_cJSON *item)
{ return (item && item->type == TY_CJSON_OBJECT) ? TRUE : FALSE; }
BOOL_T ty_cJSON_IsArray(CONST ty_cJSON *item)
{ return (item && item->type == TY_CJSON_ARRAY) ? TRUE : FALSE; }

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

ty_cJSON *ty_cJSON_Parse(CONST CHAR_T *value)
{ (void)value; return NULL; }

static void append_json(char *buf, size_t len, const ty_cJSON *item)
{
    const ty_cJSON *child = NULL;
    int first = 1;
    char num[32];
    if (item == NULL) { strncat(buf, "null", len - strlen(buf) - 1); return; }
    switch (item->type) {
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
    case TY_CJSON_NUMBER:
        snprintf(num, sizeof(num), "%d", item->valueint);
        strncat(buf, num, len - strlen(buf) - 1);
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
    child = item->child;
    while (child) { next = child->next; ty_cJSON_Delete(child); child = next; }
    free(item->string);
    free(item->valuestring);
    free(item);
}

void ty_cJSON_FreeBuffer(CHAR_T *buffer) { free(buffer); }
