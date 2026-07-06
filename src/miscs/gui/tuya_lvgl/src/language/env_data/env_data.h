#ifndef __SYSTEM_ENV_DATA__H_
#define __SYSTEM_ENV_DATA__H_

#include "tuya_app_gui.h"

/**
 * @brief 初始化语言
 * @return null
 */
void ed_init_language(void);

/**
 * @brief 获取语言
 * @return 语言
 */
GUI_LANGUAGE_E ed_get_language(void);

/**
 * @brief 设置语言
 * @param[in] language 
 * @return OPRT_OK 成功
 * @return < 0 失败
 */
OPERATE_RET ed_set_language(GUI_LANGUAGE_E language);

/**
 * @brief 更新语言
 * @param[in] null 
 * @return null
 */
void ed_update_language(void);

#endif

