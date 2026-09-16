#ifndef __TY_CJSON_H__
#define __TY_CJSON_H__

#include "tuya_cloud_types.h"

typedef struct ty_cJSON {
    int type;
    CHAR_T *string;
    CHAR_T *valuestring;
    INT_T valueint;
    double valuedouble;
    struct ty_cJSON *child;
    struct ty_cJSON *next;
} ty_cJSON;

#define TY_CJSON_OBJECT 1
#define TY_CJSON_STRING 2
#define TY_CJSON_NUMBER 3
#define TY_CJSON_ARRAY  4
#define TY_CJSON_FALSE  5
#define TY_CJSON_TRUE   6
#define TY_CJSON_NULL   7
#define TY_CJSON_RAW    8

ty_cJSON *ty_cJSON_CreateObject(void);
ty_cJSON *ty_cJSON_CreateArray(void);
ty_cJSON *ty_cJSON_CreateString(const CHAR_T *value);
ty_cJSON *ty_cJSON_CreateNumber(INT_T value);
ty_cJSON *ty_cJSON_CreateBool(BOOL_T value);
ty_cJSON *ty_cJSON_CreateRaw(const CHAR_T *raw);
void ty_cJSON_AddStringToObject(ty_cJSON *object, const CHAR_T *key, const CHAR_T *value);
void ty_cJSON_AddNumberToObject(ty_cJSON *object, const CHAR_T *key, INT_T value);
void ty_cJSON_AddBoolToObject(ty_cJSON *object, const CHAR_T *key, BOOL_T value);
void ty_cJSON_AddItemToObject(ty_cJSON *object, const CHAR_T *key, ty_cJSON *item);
void ty_cJSON_AddItemToArray(ty_cJSON *array, ty_cJSON *item);
/* Reference variant: object/array holds a lightweight wrapper that shares
 * item's value/children without taking ownership -- deleting the container
 * never frees item's own storage (mirrors real cJSON_AddItemReferenceTo*). */
void ty_cJSON_AddItemReferenceToObject(ty_cJSON *object, const CHAR_T *key, ty_cJSON *item);
ty_cJSON *ty_cJSON_GetObjectItem(CONST ty_cJSON *object, const CHAR_T *key);
BOOL_T ty_cJSON_IsString(CONST ty_cJSON *item);
BOOL_T ty_cJSON_IsNumber(CONST ty_cJSON *item);
BOOL_T ty_cJSON_IsObject(CONST ty_cJSON *item);
BOOL_T ty_cJSON_IsArray(CONST ty_cJSON *item);
BOOL_T ty_cJSON_IsBool(CONST ty_cJSON *item);
BOOL_T ty_cJSON_IsTrue(CONST ty_cJSON *item);
INT_T ty_cJSON_GetArraySize(CONST ty_cJSON *array);
ty_cJSON *ty_cJSON_GetArrayItem(CONST ty_cJSON *array, INT_T index);
ty_cJSON *ty_cJSON_Parse(CONST CHAR_T *value);
CHAR_T *ty_cJSON_PrintUnformatted(CONST ty_cJSON *item);
void ty_cJSON_Delete(ty_cJSON *item);
void ty_cJSON_FreeBuffer(CHAR_T *buffer);
ty_cJSON *ty_cJSON_Duplicate(const ty_cJSON *item, BOOL_T recurse);
void ty_cJSON_DeleteItemFromObject(ty_cJSON *object, const CHAR_T *key);
void ty_cJSON_ReplaceItemInObject(ty_cJSON *object, const CHAR_T *key, ty_cJSON *newitem);

#endif
