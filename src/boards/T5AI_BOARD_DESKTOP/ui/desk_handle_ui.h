/**
 * @file desk_handle_ui.h
 * @brief 桌面 UI 页面切换框架，统一页面注册、异步切换与返回逻辑
 */
#ifndef __DESK_HANDLE_UI_H__
#define __DESK_HANDLE_UI_H__

#include "tuya_cloud_types.h"
#include "lvgl/lvgl.h"

/* 动画时长/延时，与 desk_event_handle 保持一致时可共用宏 */
#define DHUI_SWITCH_DURATION_MS  50
#define DHUI_SWITCH_DELAY_MS    0

#define DHUI_SCREEN_ID_INVALID           ((uint32_t)-1)

/**
 * dhui_screen_id_t 定义运行时可切换页面。
 * 新业务页面若要接入统一导航，需先在此处分配页面 ID。
 */
typedef enum
{
    DHUI_SCREEN_ID_STARTUP = 0,
    DHUI_SCREEN_ID_LANGUAGE,
    DHUI_SCREEN_ID_QRCODE,
    DHUI_SCREEN_ID_NETWORK_CFG,
    DHUI_SCREEN_ID_HOME1,
    DHUI_SCREEN_ID_HOME2,
    DHUI_SCREEN_ID_HOME3,
    DHUI_SCREEN_ID_CHAT,
    DHUI_SCREEN_ID_PERSONAL_CENTER,
    DHUI_SCREEN_ID_SETTINGS,
    DHUI_SCREEN_ID_MUSIC,
    DHUI_SCREEN_ID_MUSIC_PLAYLIST,
    DHUI_SCREEN_ID_PHOTO,
    DHUI_SCREEN_ID_CAMERA,
    DHUI_SCREEN_ID_RECORD,
    DHUI_SCREEN_ID_RECORD_LIST,
    DHUI_SCREEN_ID_DEVICE_MODE,
    DHUI_SCREEN_ID_DETECTION,
    DHUI_SCREEN_ID_CALL,
    DHUI_SCREEN_ID_MAX
} dhui_screen_id_t;

/** 屏幕创建回调：创建并返回新屏幕对象，由框架在异步上下文中调用 */
typedef lv_obj_t *(*dhui_setup_scr_cb)(void);

/** 屏幕退出时资源释放回调（可选），在异步切换前调用 */
typedef void (*dhui_res_clear_cb)(void);

/** 切换类型：与 SWITCH_SCREEN_TYPE_E 语义一致 */
typedef enum
{
    DHUI_SWITCH_PERMANENT = 0,  /**< 永久切换，旧屏不再复用 */
    DHUI_SWITCH_TEMPORARY = 1,  /**< 临时切换，旧屏保留不释放 */
    DHUI_SWITCH_DYNAMIC   = 2,  /**< 旧屏复用但先释放资源再建新屏 */
} dhui_switch_type_e;

/**
 * 单屏描述：注册到框架时使用。
 * default_back_id 仅在当前页没有记录到有效来源页时作为兜底返回目标。
 */
typedef struct
{
    dhui_setup_scr_cb setup;          /**< 创建该屏的 setup 回调 */
    dhui_res_clear_cb res_clear;      /**< 可选，离开该屏时释放资源 */
    uint32_t          default_back_id;/**< 缺省返回页 */
    const char       *name;           /**< 调试用页面名 */
} dhui_screen_desc_t;

/**
 * 初始化 UI 切换框架（可在此处做全局状态初始化）
 */
void desk_handle_ui_init(void);

/**
 * 注册桌面 UI 运行时页面。
 * 该函数会把所有运行时页面的 setup/res_clear/default_back_id 收口到统一表中。
 */
void desk_handle_ui_register_all(void);

/**
 * 注册一个屏幕
 * @param screen_id 屏幕 ID，用于后续 switch_to 与 get_screen
 * @param desc      setup / res_clear 回调
 */
void desk_handle_ui_register(uint32_t screen_id, const dhui_screen_desc_t *desc);

/**
 * 异步切换到指定屏幕。
 * 该接口可安全地在 LVGL 事件回调中调用，实际切换逻辑会在异步上下文执行。
 * 切换时框架会先记录目标页来源，再统一执行当前页 res_clear，最后 setup 目标页并 load。
 * @param screen_id  目标屏幕 ID
 * @param anim_type  LVGL 动画类型
 * @param switch_type 永久/临时/动态
 */
void desk_handle_ui_switch_to(uint32_t screen_id, lv_scr_load_anim_t anim_type, dhui_switch_type_e switch_type);

/**
 * 按框架记录的来源页返回。
 * 优先使用当前页最近一次来源页，其次使用缺省返回页。
 * 适用于常规“返回上一页”场景。
 */
void desk_handle_ui_back(lv_scr_load_anim_t anim_type, dhui_switch_type_e switch_type);

/**
 * 显式返回到指定页面。
 * 适用于需要覆盖默认返回规则的特殊场景。
 */
void desk_handle_ui_back_to(uint32_t screen_id, lv_scr_load_anim_t anim_type, dhui_switch_type_e switch_type);

/**
 * 获取当前前台屏幕 ID（框架内部维护）
 */
uint32_t desk_handle_ui_get_current_screen_id(void);

/**
 * 获取已创建的屏幕对象（由 setup 创建后框架会记录；仅在使用“当前屏”语义时可靠）
 * @return 当前前台屏幕的 lv_obj_t*，可能为 NULL
 */
lv_obj_t *desk_handle_ui_get_current_screen_obj(void);

#endif /* __DESK_HANDLE_UI_H__ */
