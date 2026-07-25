/**
 * @file ui_nav.c
 * @brief Screen navigation stack manager for T5AI_BOARD
 * @version 1.0
 * @date 2025-04-02
 * @copyright Copyright (c) Tuya Inc.
 */
#include "ui_common.h"

/* ---------------------------------------------------------------------------
 * Macros
 * --------------------------------------------------------------------------- */
#define NAV_STACK_DEPTH  8

/* ---------------------------------------------------------------------------
 * Type definitions
 * --------------------------------------------------------------------------- */
typedef VOID_T (*ui_nav_show_fn)(VOID_T);
typedef VOID_T (*ui_nav_on_leave_fn)(VOID_T);
typedef lv_obj_t *(*ui_nav_get_scr_fn)(VOID_T);

typedef enum {
    UI_NAV_PAGE_NORMAL = 0,
    UI_NAV_PAGE_OVERLAY,
} UI_NAV_PAGE_TYPE_E;

typedef enum {
    UI_NAV_SWITCH_PUSH = 0,
    UI_NAV_SWITCH_REPLACE,
    UI_NAV_SWITCH_BACK,
    UI_NAV_SWITCH_BACK_TO,
    UI_NAV_SWITCH_RESET,
    UI_NAV_SWITCH_OVERLAY,
} UI_NAV_SWITCH_TYPE_E;

typedef struct {
    UI_SCR_ID_E id;
    CONST CHAR_T *name;
    ui_nav_show_fn show;
    ui_nav_on_leave_fn on_leave;
    ui_nav_get_scr_fn get_scr;
    UI_SCR_ID_E default_back;
    UI_NAV_PAGE_TYPE_E type;
} UI_NAV_PAGE_DESC_T;

/* ---------------------------------------------------------------------------
 * Forward declarations
 * --------------------------------------------------------------------------- */
STATIC CONST UI_NAV_PAGE_DESC_T *__nav_get_page(UI_SCR_ID_E id);
STATIC VOID_T __nav_call_show(UI_SCR_ID_E id);
STATIC VOID_T __nav_call_on_leave(UI_SCR_ID_E id);
STATIC VOID_T __nav_push(UI_SCR_ID_E id);
STATIC VOID_T __nav_clear_to(UI_SCR_ID_E id);

/* ---------------------------------------------------------------------------
 * File scope variables
 * --------------------------------------------------------------------------- */
STATIC UI_SCR_ID_E s_nav_stack[NAV_STACK_DEPTH];
STATIC INT_T s_nav_top = -1;

STATIC CONST UI_NAV_PAGE_DESC_T s_page_registry[] = {
    { UI_SCR_HOME,        "home",        ui_home_show,        NULL,               NULL,                 UI_SCR_NONE,   UI_NAV_PAGE_NORMAL },
    { UI_SCR_CHAT,        "chat",        ui_chat_show,        NULL,               ui_chat_get_scr,      UI_SCR_HOME,   UI_NAV_PAGE_NORMAL },
    { UI_SCR_CAMERA,      "camera",      ui_camera_show,      NULL,               ui_camera_get_scr,    UI_SCR_HOME,   UI_NAV_PAGE_NORMAL },
    { UI_SCR_ALBUM,       "album",       ui_album_show,       ui_album_hide,      ui_album_get_scr,     UI_SCR_CAMERA, UI_NAV_PAGE_NORMAL },
    { UI_SCR_ALBUM_GRID,  "album_grid",  ui_album_grid_show,  ui_album_grid_hide, ui_album_grid_get_scr,UI_SCR_ALBUM,  UI_NAV_PAGE_NORMAL },
    { UI_SCR_DEVICE_MODE, "device_mode", ui_device_mode_show, ui_device_mode_hide,ui_device_mode_get_scr,UI_SCR_HOME,   UI_NAV_PAGE_NORMAL },
    { UI_SCR_RECORD,      "record",      ui_record_show,      ui_record_hide,     ui_record_get_scr,     UI_SCR_HOME,   UI_NAV_PAGE_NORMAL },
    { UI_SCR_RECORD_LIST, "record_list", ui_record_list_show, ui_record_list_hide,ui_record_list_get_scr,UI_SCR_RECORD, UI_NAV_PAGE_NORMAL },
    { UI_SCR_MUSIC,       "music",       ui_music_show,       ui_music_hide,      ui_music_get_scr,      UI_SCR_HOME,   UI_NAV_PAGE_NORMAL },
    { UI_SCR_MUSIC_LIST,  "music_list",  ui_music_list_show,  ui_music_list_hide, ui_music_list_get_scr, UI_SCR_MUSIC,  UI_NAV_PAGE_NORMAL },
    { UI_SCR_CALL,        "call",        ui_call_show,        ui_call_hide,       ui_call_get_scr,       UI_SCR_HOME,   UI_NAV_PAGE_NORMAL },
    { UI_SCR_DETECTION,   "detection",   ui_detection_show,   ui_detection_hide,  ui_detection_get_scr,  UI_SCR_HOME,   UI_NAV_PAGE_NORMAL },
    { UI_SCR_SETTINGS,    "settings",    ui_settings_show,    ui_settings_hide,   ui_settings_get_scr,   UI_SCR_HOME,   UI_NAV_PAGE_NORMAL },
};

#define NAV_PAGE_COUNT (sizeof(s_page_registry) / sizeof(s_page_registry[0]))

/* ---------------------------------------------------------------------------
 * Function implementations
 * --------------------------------------------------------------------------- */

STATIC CONST UI_NAV_PAGE_DESC_T *__nav_get_page(UI_SCR_ID_E id)
{
    UINT32_T i;

    for (i = 0; i < NAV_PAGE_COUNT; i++) {
        if (s_page_registry[i].id == id) {
            return &s_page_registry[i];
        }
    }

    return NULL;
}

STATIC VOID_T __nav_call_show(UI_SCR_ID_E id)
{
    CONST UI_NAV_PAGE_DESC_T *page = __nav_get_page(id);

    if (page == NULL || page->show == NULL || page->type != UI_NAV_PAGE_NORMAL) {
        return;
    }

    page->show();
}

STATIC VOID_T __nav_call_on_leave(UI_SCR_ID_E id)
{
    CONST UI_NAV_PAGE_DESC_T *page = __nav_get_page(id);

    if (page == NULL || page->on_leave == NULL || page->type != UI_NAV_PAGE_NORMAL) {
        return;
    }

    page->on_leave();
}

STATIC VOID_T __nav_push(UI_SCR_ID_E id)
{
    if (s_nav_top < NAV_STACK_DEPTH - 1) {
        s_nav_top++;
    } else {
        UINT32_T i;
        for (i = 0; i < NAV_STACK_DEPTH - 1; i++) {
            s_nav_stack[i] = s_nav_stack[i + 1];
        }
    }

    s_nav_stack[s_nav_top] = id;
}

STATIC VOID_T __nav_clear_to(UI_SCR_ID_E id)
{
    s_nav_top = 0;
    s_nav_stack[s_nav_top] = id;
}

/**
 * @brief Initialize the navigation stack
 * @return none
 */
VOID_T ui_nav_init(VOID_T)
{
    s_nav_top = -1;
}

/**
 * @brief Navigate to a screen, pushing current onto the stack
 * @param[in] id target screen ID
 * @return none
 */
VOID_T ui_nav_to(UI_SCR_ID_E id)
{
    INT_T i;

    if (id == UI_SCR_NONE || id >= UI_SCR_MAX || __nav_get_page(id) == NULL) {
        return;
    }

    if (s_nav_top >= 0 && s_nav_stack[s_nav_top] == id) {
        return;
    }

    for (i = 0; i <= s_nav_top; i++) {
        if (s_nav_stack[i] == id) {
            ui_nav_back_to(id);
            return;
        }
    }

    __nav_push(id);
    __nav_call_show(id);
}

/**
 * @brief Go back to the previous screen in the stack
 * @return none
 */
VOID_T ui_nav_back(VOID_T)
{
    UI_SCR_ID_E cur;
    CONST UI_NAV_PAGE_DESC_T *page;

    if (s_nav_top < 0) {
        return;
    }

    cur = s_nav_stack[s_nav_top];
    __nav_call_on_leave(cur);

    if (s_nav_top > 0) {
        s_nav_top--;
        __nav_call_show(s_nav_stack[s_nav_top]);
        return;
    }

    page = __nav_get_page(cur);
    if (page != NULL && page->default_back != UI_SCR_NONE) {
        s_nav_stack[s_nav_top] = page->default_back;
        __nav_call_show(page->default_back);
    }
}

/**
 * @brief Go back until the target screen becomes the stack top
 * @param[in] id target screen ID
 * @return none
 */
VOID_T ui_nav_back_to(UI_SCR_ID_E id)
{
    if (id == UI_SCR_NONE || id >= UI_SCR_MAX || __nav_get_page(id) == NULL) {
        return;
    }

    while (s_nav_top >= 0 && s_nav_stack[s_nav_top] != id) {
        __nav_call_on_leave(s_nav_stack[s_nav_top]);
        s_nav_top--;
    }

    if (s_nav_top >= 0 && s_nav_stack[s_nav_top] == id) {
        __nav_call_show(id);
    }
}

/**
 * @brief Replace current screen without pushing (for screen refresh)
 * @param[in] id target screen ID
 * @return none
 */
VOID_T ui_nav_replace(UI_SCR_ID_E id)
{
    if (id == UI_SCR_NONE || id >= UI_SCR_MAX || __nav_get_page(id) == NULL) {
        return;
    }

    if (s_nav_top >= 0) {
        __nav_call_on_leave(s_nav_stack[s_nav_top]);
    }

    if (s_nav_top < 0) {
        s_nav_top = 0;
    }
    s_nav_stack[s_nav_top] = id;

    __nav_call_show(id);
}

/**
 * @brief Clear navigation stack and show the target screen
 * @param[in] id target screen ID
 * @return none
 */
VOID_T ui_nav_reset_to(UI_SCR_ID_E id)
{
    if (id == UI_SCR_NONE || id >= UI_SCR_MAX || __nav_get_page(id) == NULL) {
        return;
    }

    if (s_nav_top >= 0) {
        __nav_call_on_leave(s_nav_stack[s_nav_top]);
    }

    __nav_clear_to(id);
    __nav_call_show(id);
}

/**
 * @brief Get the current screen ID
 * @return current screen ID, UI_SCR_NONE if stack is empty
 */
UI_SCR_ID_E ui_nav_current(VOID_T)
{
    if (s_nav_top < 0) {
        return UI_SCR_NONE;
    }
    return s_nav_stack[s_nav_top];
}

/**
 * @brief Get the previous screen ID (one below top of stack)
 * @return previous screen ID, UI_SCR_NONE if no previous
 */
UI_SCR_ID_E ui_nav_previous(VOID_T)
{
    if (s_nav_top < 1) {
        return UI_SCR_NONE;
    }
    return s_nav_stack[s_nav_top - 1];
}
