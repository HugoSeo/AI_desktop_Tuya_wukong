/**
 * @file ui_theme.h
 * @brief Shared UI design tokens: colors, dimensions, fonts and common icons
 *
 * Centralizes design-system constants reused across multiple screens so a
 * change to e.g. the dark background color or the title-bar height is
 * one-line. Per-screen idiosyncratic values stay in their respective .c.
 *
 * @version 1.0
 * @date 2026-05-13
 * @copyright Copyright (c) Tuya Inc.
 */
#ifndef __UI_THEME_H__
#define __UI_THEME_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

/* ---------------------------------------------------------------------------
 * Compile-time toggles
 * --------------------------------------------------------------------------- */
/* Master switch for the chat-screen stress-test feature: shrinks
 * msg_container and parks a cycling 320x240 GIF below it (see ui_chat.c).
 * Default ON. Comment out the line below — or set to 0 — to strip all
 * stress-test code at preprocessor stage (declarations, fields, GIF
 * table, timer, settings switch row). The feature is debug-only and
 * NOT persisted to KV. */
#define TUYA_DEBUG_STRESS_TESTING       1

/* ---------------------------------------------------------------------------
 * Background colors
 * --------------------------------------------------------------------------- */
#define UI_BG_COLOR_DARK        0x25262A   /* home / chat / control / record / record_list / device_mode */
#define UI_BG_COLOR_BLACK       0x000000   /* album / camera */

/* ---------------------------------------------------------------------------
 * Title-bar geometry (50px tall, side button 50px wide, 24px back icon)
 * --------------------------------------------------------------------------- */
#define UI_TITLE_BAR_H          50
#define UI_TITLE_BTN_W          50
#define UI_TITLE_ICON_SIZE      24

/* ---------------------------------------------------------------------------
 * Shared font handles (declarations only; bodies live in res/font/*.c)
 * --------------------------------------------------------------------------- */
LV_FONT_DECLARE(AlibabaPuHuiTi3_Regular16);
LV_FONT_DECLARE(AlibabaPuHuiTi3_Regular18_Static);
LV_FONT_DECLARE(AlibabaPuHuiTi3_Regular40);

/* ---------------------------------------------------------------------------
 * Shared icon handles (declarations only; bodies live in res/icon/*.c)
 * --------------------------------------------------------------------------- */
LV_IMG_DECLARE(icon_back_24_24);
LV_IMG_DECLARE(icon_delete);
LV_IMG_DECLARE(icon_ai_icon);
LV_IMG_DECLARE(icon_ai_on);

#define GIF_DEFAULT_EMOJ            "00_Default.gif"
#define GIF_HAPPY_EMOJ              "01_Happy.gif"
#define GIF_CONFUSED_EMOJ           "02_Confused.gif"
#define GIF_SHY_EMOJ                "03_Shy.gif"
#define GIF_CRY_EMOJ                "04_Cry.gif"
#define GIF_ANGRY_EMOJ              "05_Angry.gif"
#define GIF_SCARED_EMOJ             "06_Scared.gif"
#define GIF_SURPRISED_EMOJ          "07_Surprised.gif"
#define GIF_DISAPPOINTED_EMOJ       "08_Disappointed.gif"
#define GIF_ANNOYED_EMOJ            "09_Annoyed.gif"
#define GIF_THINKING_EMOJ           "10_Thinking.gif"
#define GIF_LAUGH_EMOJ              "11_Laugh.gif"
#define GIF_FUNNY_EMOJ              "12_Funny.gif"
#define GIF_LOVE_EMOJ               "13_Love.gif"
#define GIF_EMBARRASSED_EMOJ        "14_Embarrassed.gif"
#define GIF_BLINK_EMOJ              "15_Blink.gif"
#define GIF_COOL_EMOJ               "16_Cool.gif"
#define GIF_RELAXED_EMOJ            "17_Relaxed.gif"
#define GIF_DELICIOUS_EMOJ          "18_Delicious.gif"
#define GIF_UNHAPPY_EMOJ            "19_Unhappy.gif"

#ifdef __cplusplus
}
#endif

#endif /* __UI_THEME_H__ */
