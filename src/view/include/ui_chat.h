/**
 * @file ui_chat.h
 * @brief Chat screen API and related types
 * @version 1.0
 * @date 2026-05-13
 * @copyright Copyright (c) Tuya Inc.
 */
#ifndef __UI_CHAT_H__
#define __UI_CHAT_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "tuya_cloud_types.h"
#include "lvgl.h"
#include "ui_theme.h"   /* TUYA_DEBUG_STRESS_TESTING */

/* ---------------------------------------------------------------------------
 * Type definitions
 * --------------------------------------------------------------------------- */
typedef enum {
    CHAT_MSG_ROLE_AI = 0,
    CHAT_MSG_ROLE_USER,
} CHAT_MSG_ROLE_TP_E;

typedef VOID_T (*UI_CHAT_LINK_CB)(VOID_T *arg);

/* ---------------------------------------------------------------------------
 * Function declarations
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
 * @param[in] type message role type (AI or user)
 * @param[in] text display text for the link
 * @param[in] cb callback invoked when the link is clicked
 * @param[in] cb_arg argument data to copy (can be NULL if arg_len is 0)
 * @param[in] arg_len size in bytes of cb_arg data to copy
 * @return none
 */
VOID_T ui_chat_add_link(CHAT_MSG_ROLE_TP_E type, CONST CHAR_T *text,
                        UI_CHAT_LINK_CB cb, CONST VOID_T *cb_arg,
                        UINT32_T arg_len);

/**
 * @brief Clear all messages in the chat
 * @return none
 */
VOID_T ui_chat_clear(VOID_T);

/**
 * @brief Set a pending notification text to be flushed onto chat as AI bubble
 * @param[in] msg notification text (NULL clears the pending buffer)
 * @return none
 * @note Only the latest notification is kept. Pair with ui_nav_replace(UI_SCR_CHAT)
 *       or wait until ui_chat_show() flushes it.
 */
VOID_T ui_chat_set_pending_notify(CONST CHAR_T *msg);

/**
 * @brief Flush pending notification text to chat as AI bubble
 * @return none
 * @note No-op when pending buffer is empty or chat screen is not yet created.
 */
VOID_T ui_chat_flush_pending_notify(VOID_T);

/**
 * @brief Set a JPEG image as pending attachment thumbnail at chat bottom
 * @param[in] jpeg_data JPEG image data
 * @param[in] jpeg_len JPEG data length
 * @return none
 */
VOID_T ui_chat_set_attachment_jpeg(CONST UINT8_T *jpeg_data, UINT32_T jpeg_len);

/**
 * @brief Refresh the device-mode label in the chat title bar
 * @return none
 * @note Safe to call before the chat screen is created (no-op until then).
 *       Typical caller: TY_DISPLAY_TP_MODE_NOTIFY / device-mode switch event.
 */
VOID_T ui_chat_refresh_mode(VOID_T);

/**
 * @brief Set and refresh the interaction-state label in the chat title bar
 * @param[in] state AI_CHAT_STATE_E value (0=INIT, 1=IDLE, 2=LISTEN,
 *                  3=UPLOAD, 4=THINK, 5=SPEAK)
 * @return none
 * @note Caches the value so the label is re-rendered after lazy chat
 *       creation. Safe to call before the chat screen is created.
 */
VOID_T ui_chat_set_chat_state(UINT8_T state);

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

#if defined(TUYA_DEBUG_STRESS_TESTING) && (TUYA_DEBUG_STRESS_TESTING == 1)
/**
 * @brief Toggle the chat-screen stress-test layout
 * @param[in] on TRUE to enable: shrink msg_container to leave room for a
 *               320x240 GIF container at the bottom and start cycling
 *               through the GIF_*_ORI_EMOJ animations from the SD card.
 *               FALSE to disable: stop the timer, free the loaded GIF,
 *               destroy the container and restore the original layout.
 * @return none
 * @note State is NOT persisted to KV — every cold boot starts OFF.
 *       Side effects when turning ON: any pending camera attachment is
 *       cleared via ui_chat_clear_attachment(), and subsequent
 *       ui_chat_set_attachment_jpeg() calls are suppressed until OFF.
 *       Safe to call before the chat screen is created; the flag is
 *       applied lazily by setup_scr_chat().
 */
VOID_T ui_chat_set_stress_test(BOOL_T on);

/**
 * @brief Read the current stress-test flag
 * @return TRUE when stress test is ON, FALSE otherwise
 */
BOOL_T ui_chat_get_stress_test(VOID_T);
#endif /* TUYA_DEBUG_STRESS_TESTING */

#ifdef __cplusplus
}
#endif

#endif /* __UI_CHAT_H__ */
