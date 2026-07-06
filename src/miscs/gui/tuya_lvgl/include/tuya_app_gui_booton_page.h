/**
 * @file tuya_app_gui_booton_page.h
 */

#ifndef TUYA_APP_GUI_BOOTON_PAGE_H
#define TUYA_APP_GUI_BOOTON_PAGE_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

/*********************
 *      DEFINES
 *********************/
#define BOOT_PAGE_IMG_STRUCT_INDICATE           "struct_data"
#define BOOT_PAGE_IMG_STRUCT_GIF_INDICATE       "struct_gif_data"

/**********************
 *      TYPEDEFS
 **********************/
typedef enum {
    PAGE_IMG_SRC_UNKNOWN = 0x00,    /**Unknown**/
    PAGE_IMG_SRC_FILE = 0x01,       /**file**/
    PAGE_IMG_SRC_DIGIT = 0x02,      /**digit number**/
    PAGE_IMG_SRC_STRUCT = 0x03,     /**struct normal img**/
    PAGE_IMG_SRC_STRUCT_GIF = 0x04  /**struct gif**/
} BOOT_PAGE_IMG_SRC_E;

typedef void (*user_app_gui_screen_entry_cb_t)(void);
typedef void (*user_app_gui_img_pre_decode_cb_t)(void);

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**********************
 *      MACROS
 **********************/
bool tuya_user_app_gui_main_screen_loaded(void);

bool tuya_user_app_PwrOnPage_backlight_auto(void);

/**
 * @brief tuya app gui startup register
 *
 * @param[in] powerOnPagePic: boot on page
 * @param[in] powerOnPagePicType: boot on page source type
 * @param[in] screen_entry_cb: user main screen entry callback
 * @param[in] h_pri_pre_decode_cb: img pre-decoded needed when user main page loading as Primary Priority
 * @param[in] l_pri_pre_decode_cb: img pre-decoded when user other page loading as Secondary Priority
 *
 * @return 0 on success. Others on error.
 */
int tuya_user_app_gui_startup_register(const void *powerOnPagePic, BOOT_PAGE_IMG_SRC_E powerOnPagePicType,
                                        user_app_gui_screen_entry_cb_t screen_entry_cb, 
                                        user_app_gui_img_pre_decode_cb_t h_pri_pre_decode_cb, 
                                        user_app_gui_img_pre_decode_cb_t l_pri_pre_decode_cb);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*TUYA_APP_GUI_BOOTON_PAGE_H*/
