/**
 * @file ui_common.h
 * @brief Shared types and function declarations for T5AI_BOARD UI modules
 * @version 1.0
 * @date 2025-04-02
 * @copyright Copyright (c) Tuya Inc.
 */
#ifndef __UI_COMMON_H__
#define __UI_COMMON_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "tuya_cloud_types.h"
#include "tuya_ai_display.h"
#include "wukong_picture.h"
#include "lvgl.h"
#include "uni_log.h"

/* ---------------------------------------------------------------------------
 * Type definitions
 * --------------------------------------------------------------------------- */
typedef enum {
    UI_SCR_NONE = 0,
    UI_SCR_HOME,
    UI_SCR_CHAT,
    UI_SCR_CAMERA,
    UI_SCR_ALBUM,
    UI_SCR_ALBUM_GRID,
    UI_SCR_DEVICE_MODE,
    UI_SCR_MAX,
} UI_SCR_ID_E;

typedef enum {
    CHAT_MSG_ROLE_AI = 0,
    CHAT_MSG_ROLE_USER,
} CHAT_MSG_ROLE_TP_E;

typedef VOID_T (*UI_CHAT_LINK_CB)(VOID_T *arg);

/* ---------------------------------------------------------------------------
 * app_ui_action.c
 * --------------------------------------------------------------------------- */

/**
 * @brief Initialize the UI action handler
 * @return none
 */
VOID_T app_ui_action_init(VOID_T);

/* ---------------------------------------------------------------------------
 * ui_home.c
 * --------------------------------------------------------------------------- */

/**
 * @brief Build and show the startup welcome screen
 * @return none
 */
VOID_T setup_scr_startup(VOID_T);

/**
 * @brief Build and show the date/time home screen
 * @param[in] month month number (1-12)
 * @param[in] day day number (1-31)
 * @param[in] weekday weekday string, e.g. "星期三"
 * @param[in] cal_count calendar event count shown in status bar
 * @param[in] alarm_count alarm count shown in status bar
 * @return none
 */
VOID_T setup_scr_home(UINT8_T month, UINT8_T day, CONST CHAR_T *weekday,
                       UINT8_T cal_count, UINT8_T alarm_count);

/**
 * @brief Update home screen context used when showing HOME
 * @param[in] month month number (1-12)
 * @param[in] day day number (1-31)
 * @param[in] weekday weekday string, e.g. "星期三"
 * @param[in] cal_count calendar event count shown in status bar
 * @param[in] alarm_count alarm count shown in status bar
 * @return none
 */
VOID_T ui_home_set_ctx(UINT8_T month, UINT8_T day, CONST CHAR_T *weekday,
                       UINT8_T cal_count, UINT8_T alarm_count);

/**
 * @brief Show the home screen using the current home context
 * @return none
 */
VOID_T ui_home_show(VOID_T);

/* ---------------------------------------------------------------------------
 * ui_control.c
 * --------------------------------------------------------------------------- */

/**
 * @brief Build and show the control center overlay screen
 * @param[in] volume current volume (0-100)
 * @param[in] brightness current brightness (0-100)
 * @param[in] alarm_vol current alarm volume (0-100)
 * @return none
 */
VOID_T setup_scr_control(UINT8_T volume, UINT8_T brightness, UINT8_T alarm_vol);

/**
 * @brief Check whether control center is currently visible
 * @return TRUE if visible, FALSE otherwise
 */
BOOL_T ui_control_is_active(VOID_T);

/**
 * @brief Register swipe-down gesture on any screen to open control center
 * @param[in] scr screen object to register gesture on
 * @return none
 */
VOID_T ui_control_register_gesture(lv_obj_t *scr);

/* ---------------------------------------------------------------------------
 * ui_chat.c
 * --------------------------------------------------------------------------- */

/**
 * @brief Create chat screen objects (does NOT load/show it)
 * @return none
 */
VOID_T setup_scr_chat(VOID_T);

/**
 * @brief Add a text message to the chat
 * @param[in] role
 * @param[in] text message text string
 * @return none
 */
VOID_T ui_chat_add_text(CHAT_MSG_ROLE_TP_E role, CONST CHAR_T *text);

/**
 * @brief display a JPEG image to the chat, decoded to RGB565 and displayed via canvas
 * @param[in] jpeg_data JPEG image data
 * @param[in] jpeg_len JPEG data length
 * @return none
 * @note Click on the image navigates back to chat screen
 */
VOID_T ui_chat_disp_image(CONST UINT8_T *jpeg_data, UINT32_T jpeg_len);

/**
 * @brief Start a new AI streaming text message
 * @return none
 */
VOID_T ui_chat_stream_begin(VOID_T);

/**
 * @brief Append text chunk to the current AI streaming message
 * @param[in] chunk text fragment to append
 * @return none
 */
VOID_T ui_chat_stream_append(CONST CHAR_T *chunk);

/**
 * @brief End the current AI streaming message
 * @return none
 */
VOID_T ui_chat_stream_end(VOID_T);

/**
 * @brief Manually show the chat screen
 * @return none
 */
VOID_T ui_chat_show(VOID_T);

/**
 * @brief Hide chat screen and switch to target screen
 * @param[in] target_scr screen to switch to (NULL to stay)
 * @return none
 */
VOID_T ui_chat_hide(lv_obj_t *target_scr);

/**
 * @brief Add a clickable hyperlink to the chat (AI side)
 * @param[in] text display text for the link
 * @param[in] type message role type (AI or user)
 * @param[in] cb callback invoked when the link is clicked
 * @param[in] cb_arg argument data to copy (can be NULL if arg_len is 0)
 * @param[in] arg_len size in bytes of cb_arg data to copy
 * @return none
 */
VOID_T ui_chat_add_link(CHAT_MSG_ROLE_TP_E type, CONST CHAR_T *text, UI_CHAT_LINK_CB cb, CONST VOID_T *cb_arg, UINT32_T arg_len);

/**
 * @brief Clear all messages in the chat
 * @return none
 */
VOID_T ui_chat_clear(VOID_T);

/**
 * @brief Set a JPEG image as pending attachment thumbnail at chat bottom
 * @param[in] jpeg_data JPEG image data
 * @param[in] jpeg_len JPEG data length
 * @return none
 */
VOID_T ui_chat_set_attachment_jpeg(CONST UINT8_T *jpeg_data, UINT32_T jpeg_len);

/**
 * @brief Clear the pending attachment and restore chat layout
 * @return none
 */
VOID_T ui_chat_clear_attachment(VOID_T);

/**
 * @brief Get the chat screen object
 * @return chat screen pointer, NULL if not created
 */
lv_obj_t *ui_chat_get_scr(VOID_T);

/* ---------------------------------------------------------------------------
 * ui_camera.c
 * --------------------------------------------------------------------------- */

/**
 * @brief Create camera screen objects (does NOT load/show it)
 * @return none
 */
VOID_T setup_scr_camera(VOID_T);

/**
 * @brief Show the camera screen (creates if needed)
 * @return none
 */
VOID_T ui_camera_show(VOID_T);

/**
 * @brief Hide camera screen and switch to target screen
 * @param[in] target_scr screen to switch to (NULL to stay)
 * @return none
 */
VOID_T ui_camera_hide(lv_obj_t *target_scr);

/**
 * @brief Set the camera preview image source
 * @param[in] img_src pointer to lv_img_dsc_t
 * @return none
 */
VOID_T ui_camera_set_preview_yuv_format(uint16_t width, uint16_t height, uint8_t *data, uint32_t len);

/**
 * @brief Set the thumbnail image after a photo is taken
 * @param[in] img_src pointer to lv_img_dsc_t
 * @return none
 */
VOID_T ui_camera_set_thumbnail_jpeg(uint8_t *data, uint32_t len);

/**
 * @brief Hide thumbnail and release its buffer (e.g. album became empty)
 * @return none
 */
VOID_T ui_camera_clear_thumbnail(VOID_T);

/**
 * @brief Get the camera screen object
 * @return camera screen pointer, NULL if not created
 */
lv_obj_t *ui_camera_get_scr(VOID_T);

/* ---------------------------------------------------------------------------
 * ui_album.c
 * --------------------------------------------------------------------------- */

/**
 * @brief Create album viewer screen (does NOT load/show it)
 * @return none
 */
VOID_T setup_scr_album(VOID_T);

/**
 * @brief Show the album viewer (creates if needed)
 * @return none
 */
VOID_T ui_album_show(VOID_T);

/**
 * @brief Set the photo to display
 * @param[in] img_src pointer to lv_img_dsc_t
 * @return none
 */
VOID_T ui_album_set_jpeg_photo(uint16_t width, uint16_t height, uint8_t *data, uint32_t len);

/**
 * @brief Show or hide empty-album hint and release photo canvas buffer when empty
 * @param[in] empty TRUE to show "暂无图片" and hide photo; FALSE to show photo area
 * @return none
 */
VOID_T ui_album_set_empty_state(BOOL_T empty);

/**
 * @brief Update the title and time labels
 * @param[in] title title string (e.g. "今天")
 * @param[in] time time string (e.g. "10:00")
 * @return none
 */
VOID_T ui_album_set_info(CONST CHAR_T *title, CONST CHAR_T *time);

/**
 * @brief Get the album screen object
 * @return album screen pointer, NULL if not created
 */
lv_obj_t *ui_album_get_scr(VOID_T);

/**
 * @brief Hide album screen and release canvas buffer to save memory
 * @return none
 */
VOID_T ui_album_hide(VOID_T);

/* ---------------------------------------------------------------------------
 * ui_album_grid.c
 * --------------------------------------------------------------------------- */

/**
 * @brief Create album grid screen (does NOT load/show it)
 * @return none
 */
VOID_T setup_scr_album_grid(VOID_T);

/**
 * @brief Show the album grid screen (creates if needed)
 * @return none
 */
VOID_T ui_album_grid_show(VOID_T);

/**
 * @brief Set thumbnail data for the album grid display
 * @param[in] list thumbnail list (caller retains ownership, must stay alive until hide)
 * @return none
 */
VOID_T ui_album_grid_set_thumbs(CONST WUKONG_PICTURE_THUMB_LIST_T *list);

/**
 * @brief Get filenames of currently selected items in the grid
 * @param[out] names array to fill with pointers to internal filename strings
 * @param[in] max_count capacity of names array
 * @return number of selected filenames written
 */
UINT32_T ui_album_grid_get_selected_names(CONST CHAR_T *names[], UINT32_T max_count);

/**
 * @brief Get filenames pending deletion (consumed once: buffer is cleared after call)
 * @param[out] names array to fill with pointers to internal filename strings
 * @param[in] max_count capacity of names array
 * @return number of filenames written
 */
UINT32_T ui_album_grid_get_pending_delete_names(CONST CHAR_T *names[], UINT32_T max_count);

/**
 * @brief Hide album grid screen and release resources
 * @return none
 */
VOID_T ui_album_grid_hide(VOID_T);

/**
 * @brief Get the album grid screen object
 * @return album grid screen pointer, NULL if not created
 */
lv_obj_t *ui_album_grid_get_scr(VOID_T);

/* ---------------------------------------------------------------------------
 * ui_device_mode.c
 * --------------------------------------------------------------------------- */

/**
 * @brief Create device mode selection screen (does NOT load/show it)
 * @return none
 */
VOID_T setup_scr_device_mode(VOID_T);

/**
 * @brief Show the device mode selection screen (creates if needed)
 * @return none
 */
VOID_T ui_device_mode_show(VOID_T);

/**
 * @brief Hide device mode screen and release per-show resources
 * @return none
 */
VOID_T ui_device_mode_hide(VOID_T);

/**
 * @brief Get the device mode screen object
 * @return device mode screen pointer, NULL if not created
 */
lv_obj_t *ui_device_mode_get_scr(VOID_T);

/* ---------------------------------------------------------------------------
 * ui_nav.c
 * --------------------------------------------------------------------------- */

/**
 * @brief Initialize the navigation stack
 * @return none
 */
VOID_T ui_nav_init(VOID_T);

/**
 * @brief Navigate to a screen, pushing current onto the stack
 * @param[in] id target screen ID
 * @return none
 */
VOID_T ui_nav_to(UI_SCR_ID_E id);

/**
 * @brief Go back to the previous screen in the stack
 * @return none
 */
VOID_T ui_nav_back(VOID_T);

/**
 * @brief Navigate back until the target screen becomes current
 * @param[in] id target screen ID
 * @return none
 */
VOID_T ui_nav_back_to(UI_SCR_ID_E id);

/**
 * @brief Replace current screen without pushing (for screen refresh)
 * @param[in] id target screen ID
 * @return none
 */
VOID_T ui_nav_replace(UI_SCR_ID_E id);

/**
 * @brief Clear navigation stack and show the target screen
 * @param[in] id target screen ID
 * @return none
 */
VOID_T ui_nav_reset_to(UI_SCR_ID_E id);

/**
 * @brief Get the current screen ID
 * @return current screen ID, UI_SCR_NONE if stack is empty
 */
UI_SCR_ID_E ui_nav_current(VOID_T);

/**
 * @brief Get the previous screen ID
 * @return previous screen ID, UI_SCR_NONE if no previous
 */
UI_SCR_ID_E ui_nav_previous(VOID_T);

#ifdef __cplusplus
}
#endif

#endif /* __UI_COMMON_H__ */
