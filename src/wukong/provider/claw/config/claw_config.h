/**
 * @file claw_config.h
 * @brief claw 运行时阈值配置(wukong.json 只读)+ 编译默认聚合。
 */
#pragma once
#include "tuya_cloud_types.h"
#include "tal_memory.h"

/* ── 平台分配缝:claw 长生命周期分配唯一入口。不做 NULL 判空(内部已处理)。 ── */
static inline VOID_T *wukong_claw_malloc(SIZE_T n) {
#if defined(ENABLE_EXT_RAM) && (ENABLE_EXT_RAM == 1)
    return tal_psram_malloc(n);
#else
    return tal_malloc(n);
#endif
}
static inline VOID_T wukong_claw_free(VOID_T *p) {
#if defined(ENABLE_EXT_RAM) && (ENABLE_EXT_RAM == 1)
    tal_psram_free(p);
#else
    tal_free(p);
#endif
}

/* ── ① 编译期决定:数组维度/缓冲尺寸,不可运行时改 ── */
/* [session] wukong_session.c */
#define CLAW_FIXED_SESSION_SLOTS     4
#define CLAW_FIXED_SESSION_MAX_MSGS  100
#define CLAW_FIXED_SESSION_ID_MAX    64
#define CLAW_FIXED_SESSION_PATH_MAX  96
/* [memory] wukong_memory.c/.h + memory_tool.c */
#define CLAW_FIXED_MEM_CEIL          100   /* mem_max 运行时上限的天花板 */
#define CLAW_FIXED_MEM_TAG_MAX       16
#define CLAW_FIXED_MEM_TAGS_MAX      3
#define CLAW_FIXED_MEM_ID_LEN        12
#define CLAW_FIXED_MEM_CONTENT_MAX   256
/* [provider] wukong_provider_claw.c */
#define CLAW_FIXED_LLM_BUF           256

/* ── ② 运行时可调:wukong.json,缺失/坏值逐项回落默认 ── */
/* [session] wukong_session.c */
#define CLAW_CFG_COMPACT_TRIGGER   90
#define CLAW_CFG_COMPACT_KEEP_RATIO 3
#define CLAW_CFG_SEND_TOKENS       3000
#define CLAW_CFG_SESSION_MSG_CHARS 4096
#define CLAW_CFG_SESSION_FILE_MAX  131072
/* [memory] wukong_memory.c */
#define CLAW_CFG_MEM_MAX           100     /* clamp <= CLAW_FIXED_MEM_CEIL */
/* [agent] wukong_agent_loop.c */
#define CLAW_CFG_AGENT_MAX_ITER    10

typedef struct {
    INT_T compact_trigger;
    INT_T compact_keep;      /* = trigger / keep_ratio, load 时算好 */
    INT_T send_tokens;
    INT_T agent_max_iter;
    INT_T session_msg_chars;
    INT_T session_file_max;
    INT_T mem_max;
} CLAW_CONFIG_T;

/* storage.ready 后调一次。缺失/坏 → 全默认。幂等可重载。 */
OPERATE_RET claw_config_load(VOID_T);
/* 只读单例(始终有效:加载后=校验值,未加载=编译默认)。 */
CONST CLAW_CONFIG_T *claw_config_get(VOID_T);
/* 校验纯函数:v 在 [lo,hi] 用之,否则回落 dflt。 */
INT_T claw_config_clamp(INT_T v, INT_T lo, INT_T hi, INT_T dflt);
