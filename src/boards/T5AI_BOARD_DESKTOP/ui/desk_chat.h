#ifndef __DESK_CHAT_H__
#define __DESK_CHAT_H__ 
#include "desk_ui_res.h"

#define AI_CHAT_JPEG_MSG_PATH "/t5_fs/tmp/ai_jpeg_msg.jpeg"

typedef struct 
{
    lv_img_dsc_t ai_icon;
}chat_scr_res_t;

typedef struct
{
	lv_obj_t *main_cont;
	lv_obj_t *ai_icon;
    lv_obj_t *mode_label;
    lv_obj_t *stat_label;
    lv_obj_t *msg_container;
    lv_obj_t *picture_cont;
    lv_obj_t *picture_canvas;
    lv_obj_t *picture_spinner;
}lv_chat_ui_t;

void setup_scr_chat_scr(void);

void chat_scr_res_clear(void);

void set_chat_message(uint8_t *data, bool is_ai);

lv_obj_t* create_chat_message(lv_obj_t **lable, bool is_ai);

void setup_scr_chat_mode(int mode);

/**
 * @brief Refresh the mode label text in chat title bar
 * @return none
 */
void desk_chat_refresh_mode_label(void);

/**
 * @brief Set and refresh the status label (gui_stat_desc) in chat title bar
 * @param[in] stat GUI_STAT_* status value
 * @return none
 * @note The stat value is cached internally and reused when the chat screen is
 *       re-created or language is switched.
 */
void desk_chat_refresh_stat_label(uint8_t stat);

/**
 * @brief Re-render the status label using the cached stat (used on language switch)
 * @return none
 */
void desk_chat_refresh_stat_label_current(void);

/**
 * @brief Set a pending notification message to display when chat screen loads
 * @param[in] msg message text, NULL to clear
 * @return none
 */
void desk_chat_set_pending_notify(CONST CHAR_T *msg);

/**
 * @brief Flush pending notification message to chat screen if any
 * @return none
 */
void desk_chat_flush_pending_notify(void);

/**
 * @brief Suppress the next chat mode overlay display
 * @return none
 * @note Call before restoring mode to avoid showing transient mode switch UI
 */
void setup_scr_chat_mode_suppress(void);

int set_picture_message(BYTE_T *data, UINT_T len);

/**
 * @brief Set picture message display path (file already saved by caller)
 * @param[in] path JPEG file path to display
 * @return none
 */
void set_picture_message_file(CONST CHAR_T *path);

void disp_picture_message();

/**
 * @brief Show the picture loading spinner on chat screen
 * @return none
 * @note Null-safe: no-op if chat screen is not active
 */
void desk_chat_picture_spinner_show(void);

/**
 * @brief Hide the picture loading spinner on chat screen
 * @return none
 * @note Null-safe: no-op if chat screen is not active
 */
void desk_chat_picture_spinner_hide(void);

#endif  // __DESK_CHAT_H__