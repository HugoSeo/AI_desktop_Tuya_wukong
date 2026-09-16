#ifndef __TY_AVDK_TYPES_H__
#define __TY_AVDK_TYPES_H__

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include "uni_log.h"

typedef int32_t ty_avdk_err_t;

#define TY_AVDK_ERR_OK      0
#define TY_AVDK_ERR_INVAL  -1
#define TY_AVDK_ERR_NOMEM  -2
#define TY_AVDK_ERR_IO     -3
#define TY_AVDK_ERR_BUSY   -4

#define TY_AVDK_RETURN_ON_FALSE(cond, err, tag, fmt, ...) \
    do { \
        if (!(cond)) { \
            PR_ERR("[%s] " fmt, tag, ##__VA_ARGS__); \
            return (err); \
        } \
    } while (0)

#endif
