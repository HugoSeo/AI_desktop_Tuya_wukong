#ifndef __TUYA_CLOUD_TYPES_H__
#define __TUYA_CLOUD_TYPES_H__

#include <stdint.h>
#include <stddef.h>

typedef int           OPERATE_RET;
typedef int           INT_T;
typedef unsigned int  UINT_T;
typedef uint64_t      UINT64_T;
typedef uint32_t      UINT32_T;
typedef uint8_t       UINT8_T;
typedef uint16_t      UINT16_T;
typedef unsigned char BYTE_T;
typedef unsigned char UCHAR_T;
typedef int           BOOL_T;
typedef char          CHAR_T;
typedef long long     TIME_T;
typedef long long     SYS_TIME_T;
typedef void          VOID;

typedef size_t        SIZE_T;

#define CONST   const
#define STATIC  static
#define VOID_T  void
#define OUT

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#define OPRT_OK            0
#define OPRT_INVALID_PARM (-1)
#define OPRT_MALLOC_FAILED (-3)
#define OPRT_COM_ERROR    (-4)
#define OPRT_BUFFER_NOT_ENOUGH (-5)
#define OPRT_NOT_FOUND    (-6)
#define OPRT_NOT_SUPPORTED (-8)
#define OPRT_EXCEED_UPPER_LIMIT (-24)

#define TUYA_CHECK_NULL_RETURN(x, ret) \
    do { if ((x) == NULL) { return (ret); } } while (0)

#endif
