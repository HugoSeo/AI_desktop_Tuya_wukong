/**
 * @file tuya_gui_error_report.h
 * @brief gui错误日志上报
 * 
 * @date 2024-07-24
 * @copyright Copyright (c) tuya.inc 2024
 */
#ifndef __TUYA_GUI_ERROR_REPORT_H__
#define __TUYA_GUI_ERROR_REPORT_H__
#include "tuya_gui_compatible.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef TUYA_GUI_PAGE_LOAD_SLOW_TIME // 页面加载过慢时间阈值
    #define TUYA_GUI_PAGE_LOAD_SLOW_TIME 250
#endif

#ifdef TUYA_GUI_ERR_LOG
// 页面加载过慢上报
#define TUYA_GUI_ERR_RPT_PAGE_LOAD_SLOW(page_id)   tuya_gui_err_log_page_err_rpt_str(GUI_LOGSEQ_PAGE_LOAD_SLOW, page_id)
// 页面加载失败上报
#define TUYA_GUI_ERR_RPT_PAGE_LOAD_FAILED(page_id) tuya_gui_err_log_page_err_rpt_str(GUI_LOGSEQ_PAGE_LOAD_FAIL, page_id)
#else 
#define TUYA_GUI_ERR_RPT_PAGE_LOAD_SLOW(page_id)  PR_ERR("page load slow, page_id: %s", page_id)
#define TUYA_GUI_ERR_RPT_PAGE_LOAD_FAILED(page_id) PR_ERR("page load failed, page_id: %s", page_id)
#endif

#ifdef __cplusplus
}
#endif
#endif //__TUYA_GUI_ERROR_REPORT_H__