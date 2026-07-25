/**
 * @file wukong_tm_internal.h
 * @brief Internal helpers shared by time-manage feature files.
 */

#ifndef __WUKONG_TM_INTERNAL_H__
#define __WUKONG_TM_INTERNAL_H__

#include <string.h>

#include "wukong_tm.h"

/**
 * @brief TLV format defined: T(2 byte) + L(2 byte) + V(x byte).
 */
#define WUKONG_TM_TLV_T_LEN   2
/**
 * @brief TLV format defined: T(2 byte) + L(2 byte) + V(x byte).
 */
#define WUKONG_TM_TLV_L_LEN   2
/**
 * @brief Combined TLV header length.
 */
#define WUKONG_TM_TLV_TL_LEN  (WUKONG_TM_TLV_T_LEN + WUKONG_TM_TLV_L_LEN)

/**
 * @brief Time-manage timer operation type.
 */
typedef enum {
    WUKONG_TM_TIMER_OPR_START = 0,
    WUKONG_TM_TIMER_OPR_PAUSE = 1,
    WUKONG_TM_TIMER_OPR_RESUME = 2,
    WUKONG_TM_TIMER_OPR_STOP = 3,
    WUKONG_TM_TIMER_OPR_RESET = 4,
    WUKONG_TM_TIMER_OPR_TICK = 5,
    WUKONG_TM_TIMER_OPR_FINISH = 6,
    WUKONG_TM_TIMER_OPR_SYNC_LIST = 7,
} WUKONG_TM_TIMER_OPR_E;

/**
 * @brief Time-manage TLV tag type used by relative timer events.
 */
typedef enum {
    WUKONG_TM_TAG_COUNTDOWN_OPR = 0,
    WUKONG_TM_TAG_COUNTDOWN_HOUR,
    WUKONG_TM_TAG_COUNTDOWN_MINUTE,
    WUKONG_TM_TAG_COUNTDOWN_SECOND,
    WUKONG_TM_TAG_COUNTDOWN_HANDLE,
    WUKONG_TM_TAG_COUNTDOWN_REMAINING_SEC,
    WUKONG_TM_TAG_STOPWATCH_OPR,
    WUKONG_TM_TAG_POMODORO_OPR,
    /** 1-byte WUKONG_TM_POMODORO_PHASE_E; on FINISH, phase that just ended */
    WUKONG_TM_TAG_POMODORO_PHASE,
    WUKONG_TM_TAG_ALARM_OPR,
    WUKONG_TM_TAG_ALARM_ITEM,
    WUKONG_TM_TAG_ALARM_COUNT,
    /** Null-terminated alarm_id string; L = strlen(alarm_id) + 1 */
    WUKONG_TM_TAG_ALARM_ID,
    /** sizeof(UINT_T) little-endian; 1 = first ring, >1 = snooze replay round */
    WUKONG_TM_TAG_ALARM_RING_SEQ,
    /** sizeof(UINT_T) LE; cumulative elapsed seconds (pause/stop/reset snapshots) */
    WUKONG_TM_TAG_STOPWATCH_ELAPSED_SEC,
    /** sizeof(UINT_T) LE; elapsed seconds since countdown start (e.g. PAUSE snapshot) */
    WUKONG_TM_TAG_COUNTDOWN_ELAPSED_SEC,
    WUKONG_TM_TAG_REMINDER_OPR,
    WUKONG_TM_TAG_REMINDER_ITEM,
    WUKONG_TM_TAG_REMINDER_COUNT,
} WUKONG_TM_TAG_TYPE_E;

/**
 * @brief Return a human-readable name for a timer operation code.
 *
 * @param[in] opr Timer operation.
 * @return Static string such as "START", "PAUSE", etc.
 */
static inline CONST CHAR_T *__tm_opr_name(WUKONG_TM_TIMER_OPR_E opr)
{
    switch (opr) {
    case WUKONG_TM_TIMER_OPR_START:     return "START";
    case WUKONG_TM_TIMER_OPR_PAUSE:     return "PAUSE";
    case WUKONG_TM_TIMER_OPR_RESUME:    return "RESUME";
    case WUKONG_TM_TIMER_OPR_STOP:      return "STOP";
    case WUKONG_TM_TIMER_OPR_RESET:     return "RESET";
    case WUKONG_TM_TIMER_OPR_TICK:      return "TICK";
    case WUKONG_TM_TIMER_OPR_FINISH:    return "FINISH";
    case WUKONG_TM_TIMER_OPR_SYNC_LIST: return "SYNC_LIST";
    default:                            return "UNKNOWN";
    }
}

/**
 * @brief Return a human-readable name for an alarm repeat type.
 *
 * @param[in] type Repeat type enum value.
 * @return Static string such as "ONCE", "DAILY", etc.
 */
static inline CONST CHAR_T *__tm_repeat_name(INT_T type)
{
    switch (type) {
    case WUKONG_TM_ALARM_REPEAT_ONCE:    return "ONCE";
    case WUKONG_TM_ALARM_REPEAT_DAILY:   return "DAILY";
    case WUKONG_TM_ALARM_REPEAT_WEEKLY:  return "WEEKLY";
    case WUKONG_TM_ALARM_REPEAT_MONTHLY: return "MONTHLY";
    default:                             return "UNKNOWN";
    }
}

/**
 * @brief Return a human-readable name for a pomodoro phase.
 *
 * @param[in] phase Pomodoro phase enum value.
 * @return Static string such as "WORK", "SHORT_BREAK", etc.
 */
static inline CONST CHAR_T *__tm_pomodoro_phase_name(INT_T phase)
{
    switch (phase) {
    case WUKONG_TM_POMODORO_PHASE_WORK:        return "WORK";
    case WUKONG_TM_POMODORO_PHASE_SHORT_BREAK: return "SHORT_BREAK";
    case WUKONG_TM_POMODORO_PHASE_LONG_BREAK:  return "LONG_BREAK";
    default:                                   return "UNKNOWN";
    }
}

/**
 * @brief Pack one TLV field into the target buffer.
 *
 * @param[in,out] buff    Target buffer.
 * @param[in]     type    TLV type.
 * @param[in]     len     TLV value length.
 * @param[in]     value   TLV value pointer.
 * @param[in,out] offset  Current write offset.
 */
static inline VOID __tm_tlv_pack(UINT8_T *buff, UINT_T type, UINT_T len,
                                  CONST UINT8_T *value, UINT_T *offset)
{
    UINT16_T packed_type = (UINT16_T)type;
    UINT16_T packed_len = (UINT16_T)len;

    if (buff == NULL || offset == NULL) {
        return;
    }

    memcpy(buff + *offset, &packed_type, WUKONG_TM_TLV_T_LEN);
    *offset += WUKONG_TM_TLV_T_LEN;
    memcpy(buff + *offset, &packed_len, WUKONG_TM_TLV_L_LEN);
    *offset += WUKONG_TM_TLV_L_LEN;

    if (len > 0 && value != NULL) {
        memcpy(buff + *offset, value, len);
        *offset += len;
    }
}

#endif /* __WUKONG_TM_INTERNAL_H__ */
