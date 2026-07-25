#ifndef __LV_VENDOR_EVENT_H
#define __LV_VENDOR_EVENT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "tuya_cloud_types.h"
#include "tuya_app_gui_core_config.h"

#if defined(TUYA_LVGL_VERSION) && (TUYA_LVGL_VERSION == 9)
//#pragma message("########################LVGL-v9########################")
    #if defined(TUYA_CPU_ARCH_SMP) && (TUYA_CPU_ARCH_SMP == 1)
    #include "lvgl.h"
        #if defined(LVGL_VERSION_MINOR) && (LVGL_VERSION_MINOR == 3)
        //#pragma message("########################SMP LVGL-v9.3########################")
        #define LVGL_V9_3
        #else
        //#pragma message("########################SMP LVGL-v9.1########################")
        #define LVGL_V9_1
        #endif
    #else
    //#pragma message("########################LVGL-v9.1########################")
    #define LVGL_V9_1
    #endif
#elif defined(TUYA_LVGL_VERSION) && (TUYA_LVGL_VERSION == 8)
//#pragma message("########################LVGL-v8########################")
#else
#ifdef LVGL_AS_APP_COMPONENT
#error "########################LVGL-version undefined########################"
#endif
#endif

#define LVGL_MSG_QUEUE_SIZE          64

typedef enum{
    GUI_LG_UNKNOWN,
    GUI_LG_CHINESE,             //中文
    GUI_LG_ENGLISH,             //英文
    GUI_LG_KOREAN,              //韩文
    GUI_LG_JAPANESE,            //日文
    GUI_LG_FRENCH,              //法文
    GUI_LG_GERMAN,              //德文
    GUI_LG_RUSSIAN,             //俄文
    GUI_LG_INDIA,               //印度文
    GUI_LG_ALL,                 //所有集中语言
    GUI_LG_MAX
}GUI_LANGUAGE_E;

typedef enum {
    LV_TUYAOS_EVENT_DP_INFO_CREATE = 0,                     //cp1->cp0请求创建DP结构体,cp0->cp1发送创建DP结构体
    LV_TUYAOS_EVENT_DP_REPORT,                              //cp0->cp1有新DP上报
    /*文件相关*/
    LV_TUYAOS_EVENT_FS_PATH_TYPE_REPORT,                    //cp1->cp0请求获取当前文件路径类型信息
    LV_TUYAOS_EVENT_FS_FORMAT_REPORT,                       //cp1->cp0请求格式化文件系统,慎用!
    /*系统基本相关*/
    LV_TUYAOS_EVENT_WIFI_STATUS_REPORT,                     //cp1->cp0请求获取wifi连接状态
    LV_TUYAOS_EVENT_ACTIVE_STATE_REPORT,                    //cp1->cp0请求获取设备是否处于激活状态
    LV_TUYAOS_EVENT_UNBIND_DEV_EXECUTE,                     //cp1->cp0请求执行设备接绑
    LV_TUYAOS_EVENT_LOCAL_TIME_REPORT,                      //cp1->cp0请求获取当前时间
    LV_TUYAOS_EVENT_WEATHER_REPORT,                         //cp1->cp0请求获取天气预报
    /*屏幕启动相关*/
    LV_TUYAOS_EVENT_MAIN_SCREEN_STRATUP,                    //cp1->cp0通知主屏幕启动完成
    /*资源更新相关*/
    LV_TUYAOS_EVENT_RESOURCE_UPDATE,                        //cp1->cp0请求查询是否有新资源
    LV_TUYAOS_EVENT_REFUSE_RESOURCE_UPDATE,                 //cp1->cp0拒绝执行更新资源
    LV_TUYAOS_EVENT_AGREE_RESOURCE_UPDATE,                  //cp1->cp0接受执行更新资源
    LV_TUYAOS_EVENT_HAS_NEW_RESOURCE,                       //cp0->cp1通知有新的资源更新
    LV_TUYAOS_EVENT_NO_NEW_RESOURCE,                        //cp0->cp1通知没有新的资源更新
    LV_TUYAOS_EVENT_RESOURCE_UPDATE_PROGRESS,               //cp0->cp1通知更新资源进度
    LV_TUYAOS_EVENT_RESOURCE_UPDATE_END,                    //cp0->cp1通知更新资源结束(可以释放内存资源)
    /*kv读写操作相关*/
    LV_TUYAOS_EVENT_KV_WRITE,                               //cp1->cp0请求kv写操作
    LV_TUYAOS_EVENT_KV_READ,                                //cp1->cp0请求kv读操作
    /*文件系统kv读写操作相关*/
    LV_TUYAOS_EVENT_FS_KV_WRITE,                            //cp1->cp0请求文件kv写操作
    LV_TUYAOS_EVENT_FS_KV_READ,                             //cp1->cp0请求文件kv读操作
    LV_TUYAOS_EVENT_FS_KV_DELETE,                           //cp1->cp0请求文件kv删除操作

    /*跨cp核内存申请及释放*/
    LV_TUYAOS_EVENT_CP1_MALLOC,                             //cp0->cp1请求申请PSRAM内存操作
    LV_TUYAOS_EVENT_CP1_FREE,                               //cp0->cp1请求释放PSRAM内存操作
    LV_TUYAOS_EVENT_CP0_MALLOC,                             //cp1->cp0请求申请PSRAM内存操作
    LV_TUYAOS_EVENT_CP0_FREE,                               //cp1->cp0请求释放PSRAM内存操作

    /*音频播放*/
    LV_TUYAOS_EVENT_AUDIO_FILE_PLAY,                        //cp1->cp0请求播放音频文件操作
    LV_TUYAOS_EVENT_AUDIO_VOLUME_SET,                       //cp1->cp0请求设置音量操作
    LV_TUYAOS_EVENT_AUDIO_VOLUME_GET,                       //cp1->cp0请求获取音量操作
    LV_TUYAOS_EVENT_AUDIO_PLAY_STATUS,                      //cp0->cp1当前音频文件播放状态上报
    LV_TUYAOS_EVENT_AUDIO_PLAY_PAUSE,                       //cp1->cp0请求播放暂停
    LV_TUYAOS_EVENT_AUDIO_PLAY_RESUME,                      //cp1->cp0请求播放继续

    /*云食谱操作相关*/
    LV_TUYAOS_EVENT_CLOUD_RECIPE,                           //cp0<->cp1云食谱列表请求事件相关交互

    //last line, don't change it !!!
    LV_TUYAOS_EVENT_UNKNOWN = 0xff,
} LV_TUYAOS_EVENT_TYPE_E;

/*****************cloud recipe******************/
typedef struct
{
    uint32_t    req_type;
    void        *req_param;
} gui_cloud_recipe_interactive_s;

/*****************data resource******************/
#define TyResUpdateTag        "ty_ResDlPro"

typedef unsigned char GUI_RESOURCE_TYPE_E;
#define T_GUI_RES_UNKNOWN       0       // gui资源类型:未知
#define T_GUI_RES_LANG_CONFIG   1       // gui资源类型:语言配置(data_file)
#define T_GUI_RES_FONT          2       // gui资源类型:字库(font)
#define T_GUI_RES_MUSIC         3       // gui资源类型:音乐(music)
#define T_GUI_RES_PICTURE       4       // gui资源类型:图片(picture)
#define T_GUI_RES_RESERVE       5       // gui资源类型:保留

typedef struct
{
    GUI_LANGUAGE_E  language;
    char            *file_name;
    uint32_t        size;               //文件下载时文件大小
    uint32_t        crc32;              //hash crc32
    char            *md5;               //hash md5
    char            *url;               //文件下载时使用(一定是支持psk模式的地址)
    void            *file_hdl;          //文件打开后句柄
    bool            dl_finish;          //文件下载是否结束!
} gui_res_lang_s;

typedef struct
{
    char                *file_name;         //如id为1,图片名为1.jpg或1.png,音频文件为1.mp3等...
    GUI_RESOURCE_TYPE_E type;               //资源文件类型:
    uint32_t            id;                 //id必须与文件名保持一致(字体库文件不需要id),程序方便查找其映射关系!
    uint32_t            size;               //文件下载时文件大小
    uint32_t            crc32;              //hash crc32
    char                *md5;               //hash md5
    char                *url;               //文件下载时使用(一定是支持psk模式的地址)
    void                *file_hdl;          //文件打开后句柄
    bool                dl_finish;          //文件下载是否结束!
} gui_res_file_s;

typedef struct
{
    uint32_t node_num;
    gui_res_lang_s *node;
} gui_res_lang_ss;

typedef struct
{
    uint32_t node_num;
    gui_res_file_s *node;
} gui_res_file_ss;

typedef struct
{
    char *file_name;
    char *ver;              //面板id
} gui_version_s;

typedef struct
{
    gui_res_lang_ss data_file;              //语言配置文件
    gui_res_file_ss font;
    gui_res_file_ss music;
    gui_res_file_ss picture;
    gui_version_s   version;                //资源信息id文件
} gui_resource_info_s;
/*****************data resource******************/

typedef struct
{
    char *tag;                  //widget tag,uniqueness!!!
    uint32_t obj_type;          //widget type
    uint32_t event;
    uint32_t param;
} lvglMsg_t;

typedef lvglMsg_t LV_DISP_EVENT_S;

typedef struct {
    LV_TUYAOS_EVENT_TYPE_E  event_typ;
    uint32_t param1;                    //数据1
    uint32_t param2;                    //返回状态!(1:失败, 0:成功)
    uint32_t param3;                    //保留数据1
    uint32_t param4;                    //保留数据2
} LV_TUYAOS_EVENT_S;

typedef struct {
    unsigned char   files_dl_count;             //当前下载到第几个文件
    unsigned char   files_num;                  //文件总数目
    bool            curr_dl_is_config;          //当前是否为语言配置文件
    void            *curr_dl_node;              //当前文件信息节点
    uint32_t        dl_prog;                    //当前文件下载进度
} LV_RESOURCE_DL_EVENT_SS;

/**
 * Type of event being sent to the object copyed from lv_event.h of lvgl.
 */
#if defined(LVGL_V9_1)
typedef enum {
    LLV_EVENT_ALL = 0,

    /** Input device events*/
    LLV_EVENT_PRESSED,             /**< The object has been pressed*/
    LLV_EVENT_PRESSING,            /**< The object is being pressed (called continuously while pressing)*/
    LLV_EVENT_PRESS_LOST,          /**< The object is still being pressed but slid cursor/finger off of the object */
    LLV_EVENT_SHORT_CLICKED,       /**< The object was pressed for a short period of time, then released it. Not called if scrolled.*/
    LLV_EVENT_LONG_PRESSED,        /**< Object has been pressed for at least `long_press_time`.  Not called if scrolled.*/
    LLV_EVENT_LONG_PRESSED_REPEAT, /**< Called after `long_press_time` in every `long_press_repeat_time` ms.  Not called if scrolled.*/
    LLV_EVENT_CLICKED,             /**< Called on release if not scrolled (regardless to long press)*/
    LLV_EVENT_RELEASED,            /**< Called in every cases when the object has been released*/
    LLV_EVENT_SCROLL_BEGIN,        /**< Scrolling begins. The event parameter is a pointer to the animation of the scroll. Can be modified*/
    LLV_EVENT_SCROLL_THROW_BEGIN,
    LLV_EVENT_SCROLL_END,          /**< Scrolling ends*/
    LLV_EVENT_SCROLL,              /**< Scrolling*/
    LLV_EVENT_GESTURE,             /**< A gesture is detected. Get the gesture with `lv_indev_get_gesture_dir(lv_indev_active());` */
    LLV_EVENT_KEY,                 /**< A key is sent to the object. Get the key with `lv_indev_get_key(lv_indev_active());`*/
    LLV_EVENT_ROTARY,              /**< An encoder or wheel was rotated. Get the rotation count with `lv_event_get_rotary_diff(e);`*/
    LLV_EVENT_FOCUSED,             /**< The object is focused*/
    LLV_EVENT_DEFOCUSED,           /**< The object is defocused*/
    LLV_EVENT_LEAVE,               /**< The object is defocused but still selected*/
    LLV_EVENT_HIT_TEST,            /**< Perform advanced hit-testing*/
    LLV_EVENT_INDEV_RESET,         /**< Indev has been reset*/

    /** Drawing events*/
    LLV_EVENT_COVER_CHECK,        /**< Check if the object fully covers an area. The event parameter is `lv_cover_check_info_t *`.*/
    LLV_EVENT_REFR_EXT_DRAW_SIZE, /**< Get the required extra draw area around the object (e.g. for shadow). The event parameter is `int32_t *` to store the size.*/
    LLV_EVENT_DRAW_MAIN_BEGIN,    /**< Starting the main drawing phase*/
    LLV_EVENT_DRAW_MAIN,          /**< Perform the main drawing*/
    LLV_EVENT_DRAW_MAIN_END,      /**< Finishing the main drawing phase*/
    LLV_EVENT_DRAW_POST_BEGIN,    /**< Starting the post draw phase (when all children are drawn)*/
    LLV_EVENT_DRAW_POST,          /**< Perform the post draw phase (when all children are drawn)*/
    LLV_EVENT_DRAW_POST_END,      /**< Finishing the post draw phase (when all children are drawn)*/
    LLV_EVENT_DRAW_TASK_ADDED,      /**< Adding a draw task */

    /** Special events*/
    LLV_EVENT_VALUE_CHANGED,       /**< The object's value has changed (i.e. slider moved)*/
    LLV_EVENT_INSERT,              /**< A text is inserted to the object. The event data is `char *` being inserted.*/
    LLV_EVENT_REFRESH,             /**< Notify the object to refresh something on it (for the user)*/
    LLV_EVENT_READY,               /**< A process has finished*/
    LLV_EVENT_CANCEL,              /**< A process has been cancelled */

    /** Other events*/
    LLV_EVENT_CREATE,              /**< Object is being created*/
    LLV_EVENT_DELETE,              /**< Object is being deleted*/
    LLV_EVENT_CHILD_CHANGED,       /**< Child was removed, added, or its size, position were changed */
    LLV_EVENT_CHILD_CREATED,       /**< Child was created, always bubbles up to all parents*/
    LLV_EVENT_CHILD_DELETED,       /**< Child was deleted, always bubbles up to all parents*/
    LLV_EVENT_SCREEN_UNLOAD_START, /**< A screen unload started, fired immediately when scr_load is called*/
    LLV_EVENT_SCREEN_LOAD_START,   /**< A screen load started, fired when the screen change delay is expired*/
    LLV_EVENT_SCREEN_LOADED,       /**< A screen was loaded*/
    LLV_EVENT_SCREEN_UNLOADED,     /**< A screen was unloaded*/
    LLV_EVENT_SIZE_CHANGED,        /**< Object coordinates/size have changed*/
    LLV_EVENT_STYLE_CHANGED,       /**< Object's style has changed*/
    LLV_EVENT_LAYOUT_CHANGED,      /**< The children position has changed due to a layout recalculation*/
    LLV_EVENT_GET_SELF_SIZE,       /**< Get the internal size of a widget*/

    /** Events of optional LVGL components*/
    LLV_EVENT_INVALIDATE_AREA,
    LLV_EVENT_RESOLUTION_CHANGED,
    LLV_EVENT_COLOR_FORMAT_CHANGED,
    LLV_EVENT_REFR_REQUEST,
    LLV_EVENT_REFR_START,
    LLV_EVENT_REFR_READY,
    LLV_EVENT_RENDER_START,
    LLV_EVENT_RENDER_READY,
    LLV_EVENT_FLUSH_START,
    LLV_EVENT_FLUSH_FINISH,
    LLV_EVENT_FLUSH_WAIT_START,
    LLV_EVENT_FLUSH_WAIT_FINISH,

    LLV_EVENT_VSYNC,

    _LLV_EVENT_LAST,                 /** Number of default events*/

// Modified by TUYA Start
    /*Customer define events*/
    LLV_EVENT_GESTURE_RELEASED,
// Modified by TUYA End

    LLV_EVENT_PREPROCESS = 0x8000,   /** This is a flag that can be set with an event so it's processed
                                      before the class default event processing */
} llv_event_code_t;
#elif defined(LVGL_V9_3)
typedef enum {
    LLV_EVENT_ALL = 0,

    /** Input device events*/
    LLV_EVENT_PRESSED,             /**< Widget has been pressed */
    LLV_EVENT_PRESSING,            /**< Widget is being pressed (sent continuously while pressing)*/
    LLV_EVENT_PRESS_LOST,          /**< Widget is still being pressed but slid cursor/finger off Widget */
    LLV_EVENT_SHORT_CLICKED,       /**< Widget was pressed for a short period of time, then released. Not sent if scrolled. */
    LLV_EVENT_SINGLE_CLICKED,      /**< Sent for first short click within a small distance and short time */
    LLV_EVENT_DOUBLE_CLICKED,      /**< Sent for second short click within small distance and short time */
    LLV_EVENT_TRIPLE_CLICKED,      /**< Sent for third short click within small distance and short time */
    LLV_EVENT_LONG_PRESSED,        /**< Object has been pressed for at least `long_press_time`.  Not sent if scrolled. */
    LLV_EVENT_LONG_PRESSED_REPEAT, /**< Sent after `long_press_time` in every `long_press_repeat_time` ms.  Not sent if scrolled. */
    LLV_EVENT_CLICKED,             /**< Sent on release if not scrolled (regardless to long press)*/
    LLV_EVENT_RELEASED,            /**< Sent in every cases when Widget has been released */
    LLV_EVENT_SCROLL_BEGIN,        /**< Scrolling begins. The event parameter is a pointer to the animation of the scroll. Can be modified */
    LLV_EVENT_SCROLL_THROW_BEGIN,
    LLV_EVENT_SCROLL_END,          /**< Scrolling ends */
    LLV_EVENT_SCROLL,              /**< Scrolling */
    LLV_EVENT_GESTURE,             /**< A gesture is detected. Get gesture with `lv_indev_get_gesture_dir(lv_indev_active());` */
    LLV_EVENT_KEY,                 /**< A key is sent to Widget. Get key with `lv_indev_get_key(lv_indev_active());`*/
    LLV_EVENT_ROTARY,              /**< An encoder or wheel was rotated. Get rotation count with `lv_event_get_rotary_diff(e);`*/
    LLV_EVENT_FOCUSED,             /**< Widget received focus */
    LLV_EVENT_DEFOCUSED,           /**< Widget's focus has been lost */
    LLV_EVENT_LEAVE,               /**< Widget's focus has been lost but is still selected */
    LLV_EVENT_HIT_TEST,            /**< Perform advanced hit-testing */
    LLV_EVENT_INDEV_RESET,         /**< Indev has been reset */
    LLV_EVENT_HOVER_OVER,          /**< Indev hover over object */
    LLV_EVENT_HOVER_LEAVE,         /**< Indev hover leave object */

    /** Drawing events */
    LLV_EVENT_COVER_CHECK,         /**< Check if Widget fully covers an area. The event parameter is `lv_cover_check_info_t *`. */
    LLV_EVENT_REFR_EXT_DRAW_SIZE,  /**< Get required extra draw area around Widget (e.g. for shadow). The event parameter is `int32_t *` to store the size. */
    LLV_EVENT_DRAW_MAIN_BEGIN,     /**< Starting the main drawing phase */
    LLV_EVENT_DRAW_MAIN,           /**< Perform the main drawing */
    LLV_EVENT_DRAW_MAIN_END,       /**< Finishing the main drawing phase */
    LLV_EVENT_DRAW_POST_BEGIN,     /**< Starting the post draw phase (when all children are drawn)*/
    LLV_EVENT_DRAW_POST,           /**< Perform the post draw phase (when all children are drawn)*/
    LLV_EVENT_DRAW_POST_END,       /**< Finishing the post draw phase (when all children are drawn)*/
    LLV_EVENT_DRAW_TASK_ADDED,     /**< Adding a draw task. The `LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS` flag needs to be set */

    /** Special events */
    LLV_EVENT_VALUE_CHANGED,       /**< Widget's value has changed (i.e. slider moved)*/
    LLV_EVENT_INSERT,              /**< Text has been inserted into Widget. The event data is `char *` being inserted. */
    LLV_EVENT_REFRESH,             /**< Notify Widget to refresh something on it (for user)*/
    LLV_EVENT_READY,               /**< A process has finished */
    LLV_EVENT_CANCEL,              /**< A process has been cancelled */

    /** Other events */
    LLV_EVENT_CREATE,              /**< Object is being created */
    LLV_EVENT_DELETE,              /**< Object is being deleted */
    LLV_EVENT_CHILD_CHANGED,       /**< Child was removed, added, or its size, position were changed */
    LLV_EVENT_CHILD_CREATED,       /**< Child was created, always bubbles up to all parents */
    LLV_EVENT_CHILD_DELETED,       /**< Child was deleted, always bubbles up to all parents */
    LLV_EVENT_SCREEN_UNLOAD_START, /**< A screen unload started, fired immediately when scr_load is called */
    LLV_EVENT_SCREEN_LOAD_START,   /**< A screen load started, fired when the screen change delay is expired */
    LLV_EVENT_SCREEN_LOADED,       /**< A screen was loaded */
    LLV_EVENT_SCREEN_UNLOADED,     /**< A screen was unloaded */
    LLV_EVENT_SIZE_CHANGED,        /**< Object coordinates/size have changed */
    LLV_EVENT_STYLE_CHANGED,       /**< Object's style has changed */
    LLV_EVENT_LAYOUT_CHANGED,      /**< A child's position position has changed due to a layout recalculation */
    LLV_EVENT_GET_SELF_SIZE,       /**< Get internal size of a widget */

    /** Events of optional LVGL components */
    LLV_EVENT_INVALIDATE_AREA,     /**< An area is invalidated (marked for redraw). `lv_event_get_param(e)`
                                   * returns a pointer to an `lv_area_t` object with the coordinates of the
                                   * area to be invalidated.  The area can be freely modified if needed to
                                   * adapt it a special requirement of the display. Usually needed with
                                   * monochrome displays to invalidate `N x 8` rows or columns in one pass. */
    LLV_EVENT_RESOLUTION_CHANGED,  /**< Sent when the resolution changes due to `lv_display_set_resolution()` or `lv_display_set_rotation()`. */
    LLV_EVENT_COLOR_FORMAT_CHANGED,/**< Sent as a result of any call to `lv_display_set_color_format()`. */
    LLV_EVENT_REFR_REQUEST,        /**< Sent when something happened that requires redraw. */
    LLV_EVENT_REFR_START,          /**< Sent before a refreshing cycle starts. Sent even if there is nothing to redraw. */
    LLV_EVENT_REFR_READY,          /**< Sent when refreshing has been completed (after rendering and calling flush callback). Sent even if no redraw happened. */
    LLV_EVENT_RENDER_START,        /**< Sent just before rendering begins. */
    LLV_EVENT_RENDER_READY,        /**< Sent after rendering has been completed (before calling flush callback) */
    LLV_EVENT_FLUSH_START,         /**< Sent before flush callback is called. */
    LLV_EVENT_FLUSH_FINISH,        /**< Sent after flush callback call has returned. */
    LLV_EVENT_FLUSH_WAIT_START,    /**< Sent before flush wait callback is called. */
    LLV_EVENT_FLUSH_WAIT_FINISH,   /**< Sent after flush wait callback call has returned. */

    LLV_EVENT_VSYNC,
    LLV_EVENT_VSYNC_REQUEST,

    LLV_EVENT_LAST,                 /** Number of default events */

// Modified by TUYA Start
    /*Customer define events*/
    LLV_EVENT_GESTURE_RELEASED,
// Modified by TUYA End

    LLV_EVENT_PREPROCESS = 0x8000,   /** This is a flag that can be set with an event so it's processed
                                      before the class default event processing */
    LLV_EVENT_MARKED_DELETING = 0x10000,
} llv_event_code_t;
#else

typedef enum {
    LLV_EVENT_ALL = 0,

    /** Input device events*/
    LLV_EVENT_PRESSED,             /**< The object has been pressed*/
    LLV_EVENT_PRESSING,            /**< The object is being pressed (called continuously while pressing)*/
    LLV_EVENT_PRESS_LOST,          /**< The object is still being pressed but slid cursor/finger off of the object */
    LLV_EVENT_SHORT_CLICKED,       /**< The object was pressed for a short period of time, then released it. Not called if scrolled.*/
    LLV_EVENT_LONG_PRESSED,        /**< Object has been pressed for at least `long_press_time`.  Not called if scrolled.*/
    LLV_EVENT_LONG_PRESSED_REPEAT, /**< Called after `long_press_time` in every `long_press_repeat_time` ms.  Not called if scrolled.*/
    LLV_EVENT_CLICKED,             /**< Called on release if not scrolled (regardless to long press)*/
    LLV_EVENT_RELEASED,            /**< Called in every cases when the object has been released*/
    LLV_EVENT_SCROLL_BEGIN,        /**< Scrolling begins. The event parameter is a pointer to the animation of the scroll. Can be modified*/
    LLV_EVENT_SCROLL_END,          /**< Scrolling ends*/
    LLV_EVENT_SCROLL,              /**< Scrolling*/
    LLV_EVENT_GESTURE,             /**< A gesture is detected. Get the gesture with `lv_indev_get_gesture_dir(lv_indev_get_act());` */
    LLV_EVENT_KEY,                 /**< A key is sent to the object. Get the key with `lv_indev_get_key(lv_indev_get_act());`*/
    LLV_EVENT_FOCUSED,             /**< The object is focused*/
    LLV_EVENT_DEFOCUSED,           /**< The object is defocused*/
    LLV_EVENT_LEAVE,               /**< The object is defocused but still selected*/
    LLV_EVENT_HIT_TEST,            /**< Perform advanced hit-testing*/

    /** Drawing events*/
    LLV_EVENT_COVER_CHECK,        /**< Check if the object fully covers an area. The event parameter is `lv_cover_check_info_t *`.*/
    LLV_EVENT_REFR_EXT_DRAW_SIZE, /**< Get the required extra draw area around the object (e.g. for shadow). The event parameter is `lv_coord_t *` to store the size.*/
    LLV_EVENT_DRAW_MAIN_BEGIN,    /**< Starting the main drawing phase*/
    LLV_EVENT_DRAW_MAIN,          /**< Perform the main drawing*/
    LLV_EVENT_DRAW_MAIN_END,      /**< Finishing the main drawing phase*/
    LLV_EVENT_DRAW_POST_BEGIN,    /**< Starting the post draw phase (when all children are drawn)*/
    LLV_EVENT_DRAW_POST,          /**< Perform the post draw phase (when all children are drawn)*/
    LLV_EVENT_DRAW_POST_END,      /**< Finishing the post draw phase (when all children are drawn)*/
    LLV_EVENT_DRAW_PART_BEGIN,    /**< Starting to draw a part. The event parameter is `lv_obj_draw_dsc_t *`. */
    LLV_EVENT_DRAW_PART_END,      /**< Finishing to draw a part. The event parameter is `lv_obj_draw_dsc_t *`. */

    /** Special events*/
    LLV_EVENT_VALUE_CHANGED,       /**< The object's value has changed (i.e. slider moved)*/
    LLV_EVENT_INSERT,              /**< A text is inserted to the object. The event data is `char *` being inserted.*/
    LLV_EVENT_REFRESH,             /**< Notify the object to refresh something on it (for the user)*/
    LLV_EVENT_READY,               /**< A process has finished*/
    LLV_EVENT_CANCEL,              /**< A process has been cancelled */

    /** Other events*/
    LLV_EVENT_DELETE,              /**< Object is being deleted*/
    LLV_EVENT_CHILD_CHANGED,       /**< Child was removed, added, or its size, position were changed */
    LLV_EVENT_CHILD_CREATED,       /**< Child was created, always bubbles up to all parents*/
    LLV_EVENT_CHILD_DELETED,       /**< Child was deleted, always bubbles up to all parents*/
    LLV_EVENT_SCREEN_UNLOAD_START, /**< A screen unload started, fired immediately when scr_load is called*/
    LLV_EVENT_SCREEN_LOAD_START,   /**< A screen load started, fired when the screen change delay is expired*/
    LLV_EVENT_SCREEN_LOADED,       /**< A screen was loaded*/
    LLV_EVENT_SCREEN_UNLOADED,     /**< A screen was unloaded*/
    LLV_EVENT_SIZE_CHANGED,        /**< Object coordinates/size have changed*/
    LLV_EVENT_STYLE_CHANGED,       /**< Object's style has changed*/
    LLV_EVENT_LAYOUT_CHANGED,      /**< The children position has changed due to a layout recalculation*/
    LLV_EVENT_GET_SELF_SIZE,       /**< Get the internal size of a widget*/

    _LLV_EVENT_LAST,               /** Number of default events*/

    /*Customer define events*/
    LLV_EVENT_GESTURE_RELEASED,

    LLV_EVENT_PREPROCESS = 0x80,   /** This is a flag that can be set with an event so it's processed
                                      before the class default event processing */
} llv_event_code_t;
#endif

#define LLV_EVENT_BY_TUYA_APP        0x80000000     //事件来自app或widget改变标识
#define LLV_EVENT_BY_SYNC_PROCESS    0x40000000     //事件需要同步处理标识
#define LLV_EVENT_BY_DIRECT_REPORT   0x20000000     //从cp1->cp0的控件事件需要直接上报(无此标志则由cp0经业务逻辑处理后上报)

typedef enum{
    LLV_EVENT_VENDOR_MIN = LLV_EVENT_GESTURE_RELEASED+1,   //控件最后一个事件加 1

    /*产测事件*/
    LLV_EVENT_VENDOR_MF_TEST,                               //查询是否产测模式

    /*gui 心跳事件*/
    LLV_EVENT_VENDOR_HEART_BEAT,                            //GUI心跳事件.长时间未发送心跳,GUI有可能挂了!

    /*屏幕事件*/
    LLV_EVENT_VENDOR_LCD_IDEL,                               //屏幕长时间未被操作(超过10s以上):用于进入休眠
    LLV_EVENT_VENDOR_LCD_WAKE,                               //屏幕长时间未被操作到被用户首次操作:用于休眠唤醒
    LLV_EVENT_VENDOR_LCD_LIGHT_PERCENT,                      //屏幕亮度百分比

    /*系统语言相关的事件*/
    LLV_EVENT_VENDOR_LANGUAGE_CHANGE,                        //全局事件:系统语言信息改变请求及响应
    LLV_EVENT_VENDOR_LANGUAGE_INFO,                          //全局事件:当前系统语言类型信息请求及响应

    LLV_EVENT_VENDOR_REBOOT,                                 //全局事件:请求系统重启

    LLV_EVENT_USER_PRIVATE,                                  //全局事件:用户自定义系统事件

    LLV_EVENT_VENDOR_USER_MAX,                               //全局事件:用户处理最大系统事件
    /*********************以上为用户处理事件*********************/

    /*通用处理相关的事件*/
    LLV_EVENT_VENDOR_TUYAOS,                                 //TUYAOS系统事件，用于通用事件处理

//#if (CONFIG_SYS_CPU1)
    LLV_EVENT_VENDOR_MAX = LLV_EVENT_PREPROCESS - 1             //自定义事件不要超过:LLV_EVENT_PREPROCESS
//#endif
}LV_VENDOR_EVENT_E;

typedef enum {
    LV_DISP_GUI_OBJ_OBJ = 0,                        //屏幕,容器等对象
    LV_DISP_GUI_OBJ_TABVIEW,                        //选项卡对象(多页面)
    LV_DISP_GUI_OBJ_LABEL,                          //标签
    LV_DISP_GUI_OBJ_WIN,                            //窗口对象(一般用于弹出提示窗口)
    LV_DISP_GUI_OBJ_CALENDAR,                       //日历对象
    LV_DISP_GUI_OBJ_BTN,                            //按钮对象
    LV_DISP_GUI_OBJ_RADIOBTN,                       //单选按钮
    LV_DISP_GUI_OBJ_RADIOBTN_ITEM,
    LV_DISP_GUI_OBJ_MENU,                           //菜单对象
    LV_DISP_GUI_OBJ_LINE,                           //直线对象
    LV_DISP_GUI_OBJ_BTNMATRIX,
    LV_DISP_GUI_OBJ_MSGBOX,                         //消息框对象(一般用于弹出提示用于交互)
    LV_DISP_GUI_OBJ_BAR,                            //进度条对象
    LV_DISP_GUI_OBJ_SLIDER,                         //滑动条对象
    LV_DISP_GUI_OBJ_TABLE,
    LV_DISP_GUI_OBJ_CHECKBOX,
    LV_DISP_GUI_OBJ_SWITCH,                         //开关对象
    LV_DISP_GUI_OBJ_CHART,
    LV_DISP_GUI_OBJ_ROLLER,                         //滚动框对象
    LV_DISP_GUI_OBJ_DROPDOWN,                       //下拉框对象
    LV_DISP_GUI_OBJ_DROPDOWNLIST,
    LV_DISP_GUI_OBJ_ARC,                            //曲线对象
    LV_DISP_GUI_OBJ_SPINNER,                        //加载器对象
    LV_DISP_GUI_OBJ_METER,                          //仪表盘对象
    LV_DISP_GUI_OBJ_TEXTAREA,                       //输入框对象(文本输入)
    LV_DISP_GUI_OBJ_CALENDAR_HEADER_ARROW,
    LV_DISP_GUI_OBJ_CALENDAR_HEADER_DROPDOWN,
    LV_DISP_GUI_OBJ_KEYBOARD,
    LV_DISP_GUI_OBJ_LIST,                           //列表对象
    LV_DISP_GUI_OBJ_LIST_TEXT,
    LV_DISP_GUI_OBJ_LIST_BTN,
    LV_DISP_GUI_OBJ_MENU_SIDEBAR_CONT,
    LV_DISP_GUI_OBJ_MENU_MAIN_CONT,
    LV_DISP_GUI_OBJ_MENU_CONT,
    LV_DISP_GUI_OBJ_MENU_SIDEBAR_HEADER_CONT,
    LV_DISP_GUI_OBJ_MENU_PAGE,
    LV_DISP_GUI_OBJ_MENU_SECTION,
    LV_DISP_GUI_OBJ_MENU_SEPARATOR,
    LV_DISP_GUI_OBJ_MSGBOX_BACKDROP,
    LV_DISP_GUI_OBJ_SPINBOX,                        //微调框对象
    LV_DISP_GUI_OBJ_TILEVIEW,                       //滑动页对象
    LV_DISP_GUI_OBJ_TILEVIEW_TILE,
    LV_DISP_GUI_OBJ_COLORWHEEL,                     //拾色器对象
    LV_DISP_GUI_OBJ_IMG,                            //图片对象
    LV_DISP_GUI_OBJ_ANIMIMG,                        //动画(图片切换)
    LV_DISP_GUI_OBJ_DCLOCK,                         //数字时钟
    LV_DISP_GUI_OBJ_TEXTPROGRESS,                   //进度文本对象
    LV_DISP_GUI_OBJ_CAROUSEL,                       //滑动块对象(跑马灯)
    LV_DISP_GUI_OBJ_CAROUSEL_ELEMENT,
    LV_DISP_GUI_OBJ_LED,                            //led对象
    //last line, don't change it !!!
    LV_DISP_GUI_OBJ_UNKNOWN
} LV_DISP_GUI_OBJ_TYPE_E;

typedef void (*LV_DISP_GUI_EVENT_CB)(LV_DISP_EVENT_S *evt);

typedef enum {
    LV_LCD_LIGHT_FROM_BOOT = 0,         //背光点亮:来自启动
    LV_LCD_LIGHT_FROM_WAKE,             //背光点亮:来自换醒
    LV_LCD_LIGHT_FROM_UNKNOWN
} LV_LCD_LIGHT_TYPE_E;

//#if (CONFIG_SYS_CPU1)

/**
 * @brief msg queue队列消息处理
 * @param[in] 无
 * @return 无
 */
void lvMsgHandleTask(void);

void lvMsgSyncHandle(lvglMsg_t *msg);

void lvMsgEventUserDataDbg(void);

OPERATE_RET lvMsgSendToLvglWithEventid(uint32_t eventId);

/**
 * @brief 给lvgl消息队列发送数据
 * @param[in] msg->event: LV_VENDOR_EVENT_E结构里的事件码
 * @param[in] msg->param: 事件参数，可以是指针，但需约定哪边释放这个内存
 * @return 无
 */
OPERATE_RET lvMsgSendToLvgl(lvglMsg_t *msg);

/**
 * @brief lvgl msg queue初始化
 * @param[in] 无
 * @return 无
 */
void lvMsgEventInit(void);

//#elif (CONFIG_SYS_CPU0)

//OPERATE_RET lvMsgSendToLvglWithEventid(uint32_t eventId);

/**
 * @brief 给lvgl消息队列发送数据
 * @param[in] msg->event: LV_VENDOR_EVENT_E结构里的事件码
 * @param[in] msg->param: 事件参数，可以是指针，但需约定哪边释放这个内存
 * @return 无
 */
//OPERATE_RET lvMsgSendToLvgl(lvglMsg_t *msg);
//#endif

bool lv_event_from_app(void *e);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif

