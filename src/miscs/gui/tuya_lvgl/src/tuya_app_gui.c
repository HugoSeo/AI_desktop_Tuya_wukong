#include "tuya_app_gui.h"
#include "app_ipc_command.h"
#include "env_data.h"
#include "tuya_app_gui_display_text.h"
#include "tuya_app_gui_gw_core1.h"
#include "tuya_app_gui_core_config.h"
#include "tuya_app_gui_booton_page.h"
#include <limits.h>
#include "tkl_display.h"
#include "tkl_thread.h"
#include "tkl_mutex.h"
#include "tkl_semaphore.h"
#include "tkl_system.h"
#include "tkl_lvgl.h"
#include "tuya_port_disp.h"
#include "img_direct_flush.h"

static UINT32_T tp_no_touched = 0;
static bool GuiMfTestMode = false;

#define GUI_MONITOR_PERIOD_MS           1000                    //ms:定时器监控周期
#if LVGL_VERSION_MAJOR < 9
#define GUI_MSG_PROCESS_PERIOD_MS       LV_DISP_DEF_REFR_PERIOD //50 ms:事件处理定时器监控周期
#else
#define GUI_MSG_PROCESS_PERIOD_MS       LV_DEF_REFR_PERIOD
#endif

#ifndef TASK_PRIORITY_LVGL
#define TASK_PRIORITY_LVGL 5
#endif

#ifndef STACK_SIZE_LVGL
#define STACK_SIZE_LVGL (8*1024)
#endif

extern const char *tuya_app_gui_get_lcd_name(void);
extern int tuya_app_gui_get_lcd_width_size(void);
extern int tuya_app_gui_get_lcd_height_size(void);
extern int tuya_app_gui_get_lcd_rotation(void);
extern unsigned int tuya_app_gui_get_heartbeat_interval_time_ms(void);
extern unsigned int tuya_app_gui_get_screen_saver_maximum_time_ms(void);
extern unsigned int tuya_app_gui_lvgl_draw_buffer_rows(void);
extern bool tuya_app_gui_lvgl_draw_buffer_use_sram(void);
extern unsigned char tuya_app_gui_lvgl_draw_buffer_count(void);
extern unsigned char tuya_app_gui_lvgl_frame_buffer_count(void);
extern bool tuya_app_gui_get_screen_saver_enabled(void);
extern bool tuya_app_gui_memory_stack_dbg_enabled(void);
extern void tuya_app_gui_main_init(bool is_mf_test);

static void app_gui_watchdog_cb(struct _lv_timer_t *timer)
{
    static unsigned int elapse_cnt = 0;
    LV_DISP_EVENT_S lv_msg = { 0 };

    //period send heartbeat
    if (elapse_cnt++ >= (tuya_app_gui_get_heartbeat_interval_time_ms()/GUI_MONITOR_PERIOD_MS)) {
        lv_msg.tag = NULL;
        lv_msg.event = LLV_EVENT_VENDOR_HEART_BEAT;
        lv_msg.param = 0;
        aic_lvgl_msg_event_change((void *)&lv_msg);
        elapse_cnt = 0;
    }

    if (!tuya_user_app_gui_main_screen_loaded()) {
        LV_LOG_WARN("------>[%d]: screen is not loaded !\r\n", __LINE__);
    }
    else if (tuya_app_gui_get_screen_saver_enabled()) {
        if (++tp_no_touched >= (tuya_app_gui_get_screen_saver_maximum_time_ms()/GUI_MONITOR_PERIOD_MS)) {
            //LV_LOG_WARN("[%d]:tp no touched long time as '%d'sec !!!\r\n", __LINE__, lv_msg.param);
            lv_msg.tag = NULL;
            lv_msg.event = LLV_EVENT_VENDOR_LCD_IDEL;
            lv_msg.param = (tp_no_touched*GUI_MONITOR_PERIOD_MS)/1000;
            aic_lvgl_msg_event_change((void *)&lv_msg);
        }
        if (tp_no_touched >= UINT_MAX){
            tp_no_touched = tuya_app_gui_get_screen_saver_maximum_time_ms()/GUI_MONITOR_PERIOD_MS;
        }
    }

#ifdef TUYA_APP_MULTI_CORE_IPC
    if (tuya_app_gui_memory_stack_dbg_enabled()) {
        static UINT32_T cnt = 0;
        extern void rtos_dump_task_list(void);
        if (cnt != 0 && cnt%5 == 0) {
            rtos_dump_task_list();
        #if CONFIG_FREERTOS && CONFIG_MEM_DEBUG
            //os_dump_memory_stats(0, 0, NULL);
        #endif
        }
        cnt++;
    }
#endif
}

static void app_gui_msg_handle_cb(struct _lv_timer_t *timer)
{
    lvMsgHandleTask();
}

static void tuya_app_gui_set_mf_test(bool is_mf_test)
{
    GuiMfTestMode = is_mf_test;
}

static void tuya_app_gui_inquiry_mf_test_mode(void)
{
    LV_DISP_EVENT_S lv_msg = {.tag = NULL, .event = LLV_EVENT_VENDOR_MF_TEST, .param = 0};
    aic_lvgl_msg_event_change((void *)&lv_msg);
    tuya_app_gui_set_mf_test((bool)(lv_msg.param));
}

static lv_obj_t *screen_saver_obj = NULL;
static bool screen_saver_entry = false;

static void tuya_app_gui_wakeup(void)
{
    if (tp_no_touched >= (tuya_app_gui_get_screen_saver_maximum_time_ms()/GUI_MONITOR_PERIOD_MS)) {
        LV_DISP_EVENT_S lv_msg = {.tag = NULL, .event = LLV_EVENT_VENDOR_LCD_WAKE, .param = LV_LCD_LIGHT_FROM_WAKE};
        //LV_LOG_WARN("[%d]:tp detect wakeup !!!\r\n", __LINE__);
        aic_lvgl_msg_event_change((void *)&lv_msg);
    }
    tp_no_touched = 0;
}

static void screen_saver_event_handler (lv_event_t *e)
{
	lv_event_code_t code = lv_event_get_code(e);
    //bool evt_from_app = lv_event_from_app(e);       //event is from app or touch screen ?

	switch (code) {
	case LV_EVENT_PRESSING:
		break;
    case LV_EVENT_RELEASED:
    {
        if (screen_saver_obj != NULL) {
            LV_LOG_WARN("[%d]: recv saver screen evt by touching screen ... ... ...\r\n", __LINE__);
            tuya_app_gui_wakeup();
        }
        break;
    }
	default:
		break;
	}
}

static void tuya_app_gui_screen_saver_exit_async_cb(void * obj)
{
    LV_LOG_WARN("[%d]: screen_saver_exit_async_cb ... ... ...\r\n", __LINE__);
    tuya_app_gui_wakeup();
}

static bool tuya_app_gui_screen_is_saver_mode(void)
{
#if defined(TUYA_IMG_DIRECT_FLUSH) && (TUYA_IMG_DIRECT_FLUSH == 1)
    return screen_saver_entry;
#else
    return (screen_saver_entry && screen_saver_obj != NULL)?true:false;
#endif
}

void tuya_app_gui_screen_saver_process(void)
{
    if (tuya_app_gui_get_screen_saver_enabled()) {
        if (screen_saver_entry) {
            if (screen_saver_obj == NULL) {
                LV_LOG_WARN("[%d]:screen saver entry ... ... ...\r\n", __LINE__);
                screen_saver_obj = lv_obj_create(lv_layer_top());
                lv_obj_set_pos(screen_saver_obj, 0, 0);
                lv_obj_set_size(screen_saver_obj, LV_HOR_RES, LV_VER_RES);
                lv_obj_set_style_bg_opa(screen_saver_obj, LV_OPA_TRANSP, LV_PART_MAIN|LV_STATE_DEFAULT);
                lv_obj_set_style_border_width(screen_saver_obj, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
                lv_obj_move_foreground(screen_saver_obj);
                lv_obj_add_event_cb(screen_saver_obj, screen_saver_event_handler, LV_EVENT_ALL, NULL);
            }
        }
        else {
            if (screen_saver_obj != NULL) {
                LV_LOG_WARN("[%d]:screen saver exit ... ... ...\r\n", __LINE__);
                lv_obj_del(screen_saver_obj);
                screen_saver_obj = NULL;
            }
        }
    }
}

void tuya_app_gui_screen_saver_entry(void)
{
    screen_saver_entry = true;
}

void tuya_app_gui_screen_saver_exit(void)
{
    screen_saver_entry = false;
}

void tuya_app_gui_touch_monitor(void)
{
    if (!screen_saver_entry) {        //亮屏状态下清除计数操作!
        tp_no_touched = 0;
    }
}

void tuya_app_gui_screen_saver_exit_request(void)
{
    if (tuya_app_gui_get_screen_saver_enabled()) {
        if (tuya_app_gui_screen_is_saver_mode()) {
            #if defined(TUYA_IMG_DIRECT_FLUSH) && (TUYA_IMG_DIRECT_FLUSH == 1)
                tuya_app_gui_screen_saver_exit_async_cb(NULL);
            #else
                lv_async_call(tuya_app_gui_screen_saver_exit_async_cb, NULL);
            #endif
        }
        else {
            tuya_app_gui_touch_monitor();
        }
    }
}

void tuya_app_gui_screen_saver_entry_request(void)
{
    if (tuya_app_gui_get_screen_saver_enabled()) {
        if (tuya_app_gui_screen_is_saver_mode()) {
            //已经在睡眠状态!
        }
        else {
            //准备立刻进入睡眠!
            if (tp_no_touched < tuya_app_gui_get_screen_saver_maximum_time_ms()/GUI_MONITOR_PERIOD_MS)
                tp_no_touched = tuya_app_gui_get_screen_saver_maximum_time_ms()/GUI_MONITOR_PERIOD_MS;
        }
    }
}

static TKL_THREAD_HANDLE  gui_main_thread_handle = NULL;
static TKL_SEM_HANDLE gui_main_thread_sem = NULL;

static void tuya_app_gui_fs_path_type_init(void)
{
    TUYA_GUI_FS_PINPONG_E curr_path_type = TUYA_GUI_FS_PP_UNKNOWN;
    LV_DISP_EVENT_S lv_msg = {0};
    LV_TUYAOS_EVENT_S g_dp_evt_s = {.event_typ = LV_TUYAOS_EVENT_FS_PATH_TYPE_REPORT};

    lv_msg.event = LLV_EVENT_VENDOR_TUYAOS;
    lv_msg.param = (UINT_T)&g_dp_evt_s;
    aic_lvgl_msg_event_change((void *)&lv_msg);
    curr_path_type = (TUYA_GUI_FS_PINPONG_E)(g_dp_evt_s.param1);
    TY_GUI_LOG_PRINT("%s:%d-------------------------------------curr_path_type '%d'\r\n", __func__, __LINE__, curr_path_type);
    tuya_app_gui_set_path_type(curr_path_type);
    TY_GUI_LOG_PRINT("%s:%d----test top path '%s'\r\n", __func__, __LINE__, tuya_app_gui_get_top_path());
}

static void tuya_app_gui_dp_struct_init(void)
{
    LV_DISP_EVENT_S lv_msg = {0};
    LV_TUYAOS_EVENT_S g_dp_evt_s = {.event_typ = LV_TUYAOS_EVENT_DP_INFO_CREATE};

    lv_msg.event = LLV_EVENT_VENDOR_TUYAOS;
    lv_msg.param = (UINT_T)&g_dp_evt_s;
    aic_lvgl_msg_event_change((void *)&lv_msg);
    if (g_dp_evt_s.param2 == 0) {
        TY_GUI_LOG_PRINT("%s:%d-------------------------------------dp struct create request ok\r\n", __func__, __LINE__);
        tuya_app_gui_gw_DpInfoCreate((void *)(g_dp_evt_s.param1));
    }
    else {
        TY_GUI_LOG_PRINT("%s:%d-------------------------------------dp struct create request fail\r\n", __func__, __LINE__);
    }
}

static OPERATE_RET tuya_app_gui_init(void)
{
    TKL_DISP_INFO_S info;
#ifdef TUYA_APP_MULTI_CORE_IPC
    TKL_LVGL_CFG_T lv_cfg = {
        .recv_cb = aic_cpu1_callback,
        .recv_arg = NULL,
    };
#endif
    OPERATE_RET ret = OPRT_COM_ERROR;
    const char *lcd_name = tuya_app_gui_get_lcd_name();

    memset(&info, 0, sizeof(TKL_DISP_INFO_S));
    info.width = tuya_app_gui_get_lcd_width_size();
    info.height = tuya_app_gui_get_lcd_height_size();
    int len = (IC_NAME_LENGTH < strlen(lcd_name))? IC_NAME_LENGTH: strlen(lcd_name);
    memcpy(info.ll_ctrl.ic_name, lcd_name, len);

#ifndef TUYA_APP_MULTI_CORE_IPC
    app_no_ipc_cp1_recv_callback_register(aic_cpu1_callback);
#endif

#ifdef TUYA_APP_USE_TAL_IPC
    if (tuya_app_init_ipc(aic_cpu1_callback) != OPRT_OK) {
        TY_GUI_LOG_PRINT("[%s:%d]:------core1 ipc init fail ???\r\n", __func__, __LINE__);
        return ret;
    }
    //if (ty_event_init_ipc() != OPRT_OK) {  //创建base_event p2p端
    //    TY_GUI_LOG_PRINT("[%s:%d]:------core1 base_event init fail ???\r\n", __func__, __LINE__);
    //    return ret;
    //}
    if (uf_db_init() != OPRT_OK) {          //创建uf文件系统客户端
        TY_GUI_LOG_PRINT("[%s:%d]:------core1 uf init fail ???\r\n", __func__, __LINE__);
        return ret;
    }
    if (ws_db_init_ipc() != OPRT_OK) {      //创建kv客户端
        TY_GUI_LOG_PRINT("[%s:%d]:------core1 ws_db init fail ???\r\n", __func__, __LINE__);
        return ret;
    }
#endif
#ifdef TUYA_APP_MULTI_CORE_IPC
    tkl_lvgl_init(&info, &lv_cfg);      //初始化ipc事件通道
#endif
    lvMsgEventInit();                   //事件初始化
    tuya_app_gui_fs_path_type_init();   //文件系统路径类型初始化
    ed_init_language();                 //获取系统默认语言类型
    //ret = tuya_app_display_text_init(); //语言环境初始化,@2025/2/8移至启动页面执行
    tuya_app_gui_inquiry_mf_test_mode();//产测模式查询
    tuya_app_gui_dp_struct_init();      //cp1的dp数据结构初始化
    ret = OPRT_OK;
    return ret;
}

#if LVGL_VERSION_MAJOR < 9
static void __lv_log_printf(const char *buf)
{
    TY_GUI_LOG_PRINT(buf);
}
#else
static void __lv_log_printf(lv_log_level_t level, const char *buf)
{
    TY_GUI_LOG_PRINT(buf);
}
#endif

#if defined(TUYA_IMG_DIRECT_FLUSH) && (TUYA_IMG_DIRECT_FLUSH == 1)
#include "tal_sw_timer.h"
static void __app_gui_watchdog_cb(TIMER_ID timerID,void *pTimerArg)
{
    app_gui_watchdog_cb(NULL);
}
#endif

static void gui_main_task(void *arg)
{
#if defined(TUYA_IMG_DIRECT_FLUSH) && (TUYA_IMG_DIRECT_FLUSH == 1)
    TIMER_ID  wd_timer_id = NULL;

    tuya_app_gui_init();
    if (tuya_img_direct_flush_init(TRUE) != OPRT_OK) {
        TY_GUI_LOG_PRINT("[%s:%d]:------img direct flush fail ???\r\n", __func__, __LINE__);
    }
    tuya_app_gui_main_init(GuiMfTestMode);
    tal_sw_timer_create(__app_gui_watchdog_cb, NULL, &wd_timer_id);
    tal_sw_timer_start(wd_timer_id, GUI_MONITOR_PERIOD_MS, TAL_TIMER_CYCLE);
#else
    #if LV_USE_LOG
    lv_log_register_print_cb(__lv_log_printf);//日志接口注册
    #endif
    LV_LOG_WARN("------>[%d]\r\n", __LINE__);
    tuya_app_gui_init();
    tuya_app_gui_main_init(GuiMfTestMode);

    lv_timer_create(app_gui_msg_handle_cb,  GUI_MSG_PROCESS_PERIOD_MS,  NULL);              //gui msg handle!
    lv_timer_create(app_gui_watchdog_cb,    GUI_MONITOR_PERIOD_MS,      NULL);              //gui watchdog!
#endif

    tkl_semaphore_post(gui_main_thread_sem);
    tkl_thread_release(gui_main_thread_handle);
    gui_main_thread_handle = NULL;
}

#ifdef LVGL_AS_APP_COMPONENT
#include "lv_vendor.h"

#ifndef LV_DRAW_BUF_ALIGN
#define LV_DRAW_BUF_ALIGN                       4
#endif

static void * tuya_app_gui_draw_buf_align_alloc(size_t size_bytes, void **unaligned_hd, bool use_sram)
{
    uint8_t *buf_u8= NULL;
    /*Allocate larger memory to be sure it can be aligned as needed*/
    size_bytes += LV_DRAW_BUF_ALIGN - 1;
    if (use_sram)
        buf_u8 = tkl_system_sram_malloc(size_bytes);
    else
        buf_u8 = tkl_system_psram_malloc(size_bytes);
    if (buf_u8) {
        *unaligned_hd = (void *)buf_u8;
        memset(buf_u8, 0x00, size_bytes);
        TY_GUI_LOG_PRINT("%s-%d: original buf ptr '%p'\r\n", __func__, __LINE__, (void *)buf_u8);
        buf_u8 += LV_DRAW_BUF_ALIGN - 1;
        buf_u8 = (uint8_t *)((uint32_t) buf_u8 & ~(LV_DRAW_BUF_ALIGN - 1));
        TY_GUI_LOG_PRINT("%s-%d: aligned buf ptr '%p'\r\n", __func__, __LINE__, (void *)buf_u8);
    }
    else {
        TY_GUI_LOG_PRINT("%s-%d: malloc draw buf fail ???\r\n", __func__, __LINE__);
    }
    return buf_u8;
}

static void * tuya_app_gui_frame_buf_align_alloc(size_t size_bytes, void **unaligned_hd)
{
    uint8_t *buf_u8= NULL;
    /*Allocate larger memory to be sure it can be aligned as needed*/
    size_bytes += LV_DRAW_BUF_ALIGN - 1;
    buf_u8 = tkl_system_psram_malloc(size_bytes);
    if (buf_u8) {
        *unaligned_hd = (void *)buf_u8;
        memset(buf_u8, 0x00, size_bytes);
        TY_GUI_LOG_PRINT("%s-%d: original buf ptr '%p'\r\n", __func__, __LINE__, (void *)buf_u8);
        buf_u8 += LV_DRAW_BUF_ALIGN - 1;
        buf_u8 = (uint8_t *)((uint32_t) buf_u8 & ~(LV_DRAW_BUF_ALIGN - 1));
        TY_GUI_LOG_PRINT("%s-%d: aligned buf ptr '%p'\r\n", __func__, __LINE__, (void *)buf_u8);
    }
    else {
        TY_GUI_LOG_PRINT("%s-%d: malloc frame buf fail ???\r\n", __func__, __LINE__);
    }
    return buf_u8;
}

static uint32_t tuya_app_gui_draw_buff_size_calc(uint32_t hor_res, uint32_t ver_res, uint8_t rotation, unsigned char bytes_per_pixel)
{
    uint32_t row = tuya_app_gui_lvgl_draw_buffer_rows();
    uint32_t _hor_res = hor_res;
    uint32_t draw_buff_size = 0;

    //if (rotation == TKL_DISP_ROTATION_90 || rotation == TKL_DISP_ROTATION_270) {
    //    _hor_res = ver_res;
    //    if (row == 0) {
    //        row = (hor_res / 10);     //1/10 屏幕高度区域
    //    }
    //    else if (row >= hor_res)
    //        row = hor_res;
    //}
    //else {
        if (row == 0) {
            row = (ver_res / 10);     //1/10 屏幕高度区域
        }
        else if (row >= ver_res)
            row = ver_res;
    //}
    draw_buff_size = (_hor_res * row * bytes_per_pixel);
    TY_GUI_LOG_PRINT("%s-%d: final row '%d' draw buff size '%lu' bytes\r\n", __func__, __LINE__, row, draw_buff_size);
    return draw_buff_size;
}

static OPERATE_RET tuya_app_gui_lvgl_init(lv_vnd_config_t *lv_vnd_config)
{
    extern size_t rtos_get_psram_total_heap_size(void);
    extern size_t rtos_get_psram_free_heap_size(void);
    extern size_t rtos_get_psram_minimum_free_heap_size(void);

    OPERATE_RET ret = OPRT_OK;
    unsigned char bytes_per_pixel = 2;
#if LV_COLOR_DEPTH == 16
    bytes_per_pixel = 2;
#elif LV_COLOR_DEPTH == 24
    bytes_per_pixel = 3;
#elif LV_COLOR_DEPTH == 32
    bytes_per_pixel = 4;
#else
    #error "unknown color depth"
#endif

    TY_GUI_LOG_PRINT("%s-%d: darw_buffer count %d, frame_buffer count %d, psram total %d, free %d, minimum %d \r\n", __func__, __LINE__, 
        tuya_app_gui_lvgl_draw_buffer_count(), tuya_app_gui_lvgl_frame_buffer_count(),
        rtos_get_psram_total_heap_size(), rtos_get_psram_free_heap_size(), rtos_get_psram_minimum_free_heap_size());

    uint32_t tmp_draw_buff_size = tuya_app_gui_draw_buff_size_calc(lv_vnd_config->lcd_hor_res, lv_vnd_config->lcd_ver_res, lv_vnd_config->rotation, bytes_per_pixel);
    lv_vnd_config->draw_pixel_size = tmp_draw_buff_size / bytes_per_pixel;
    //draw buffer alloc...
    if (tuya_app_gui_lvgl_draw_buffer_count() > 0) {
        lv_vnd_config->draw_buf_2_1 = tuya_app_gui_draw_buf_align_alloc(lv_vnd_config->draw_pixel_size * bytes_per_pixel, &lv_vnd_config->draw_buf_2_1_unalign, lv_vnd_config->draw_buf_use_sram);
        if (lv_vnd_config->draw_buf_2_1 == NULL) {
            ret = OPRT_MALLOC_FAILED;
            return ret;
        }
        if (tuya_app_gui_lvgl_draw_buffer_count() > 1) {
            lv_vnd_config->draw_buf_2_2 = tuya_app_gui_draw_buf_align_alloc(lv_vnd_config->draw_pixel_size * bytes_per_pixel, &lv_vnd_config->draw_buf_2_2_unalign, lv_vnd_config->draw_buf_use_sram);
            if (lv_vnd_config->draw_buf_2_2 == NULL) {
                ret = OPRT_MALLOC_FAILED;
                return ret;
            }
        }
        else
            lv_vnd_config->draw_buf_2_2 = NULL;
    }
    else {
        TY_GUI_LOG_PRINT("%s-%d: darw_buffer count 0, one draw buffer at least should be set ??? \r\n", __func__, __LINE__);
        lv_vnd_config->draw_buf_2_1 = NULL;
        lv_vnd_config->draw_buf_2_2 = NULL;
        return OPRT_INVALID_PARM;
    }

    //frame buffer alloc...
    if (tuya_app_gui_lvgl_frame_buffer_count() > 0) {
        lv_vnd_config->frame_buf_1 = (void *)tuya_app_gui_frame_buf_align_alloc(lv_vnd_config->lcd_hor_res * lv_vnd_config->lcd_ver_res * bytes_per_pixel, &lv_vnd_config->frame_buf_1_unalign);
        if (lv_vnd_config->frame_buf_1 == NULL) {
            ret = OPRT_MALLOC_FAILED;
            return ret;
        }
        if (tuya_app_gui_lvgl_frame_buffer_count() > 1) {
            lv_vnd_config->frame_buf_2 = (void *)tuya_app_gui_frame_buf_align_alloc(lv_vnd_config->lcd_hor_res * lv_vnd_config->lcd_ver_res * bytes_per_pixel, &lv_vnd_config->frame_buf_2_unalign);
            if (lv_vnd_config->frame_buf_2 == NULL) {
                ret = OPRT_MALLOC_FAILED;
                return ret;
            }
        }
        else
            lv_vnd_config->frame_buf_2 = NULL;
    }
    else {
        lv_vnd_config->frame_buf_1 = NULL;
        lv_vnd_config->frame_buf_2 = NULL;
    }

    TY_GUI_LOG_PRINT("lvgl init: \r\n");
    TY_GUI_LOG_PRINT("\tx: %d, y: %d, color: %d, rotate: %d\r\n",
            lv_vnd_config->lcd_hor_res,
            lv_vnd_config->lcd_ver_res,
            bytes_per_pixel, lv_vnd_config->rotation);

    TY_GUI_LOG_PRINT("\tdraw buf1 %x, buf2 %x, size %d, in %s\r\n",
            lv_vnd_config->draw_buf_2_1,
            lv_vnd_config->draw_buf_2_2,
            lv_vnd_config->draw_pixel_size,
            (lv_vnd_config->draw_buf_use_sram==true)?"sram":"psram");

    TY_GUI_LOG_PRINT("\tframe buf1 %x, buf2 %x\r\n",
            lv_vnd_config->frame_buf_1,
            lv_vnd_config->frame_buf_2);
    return ret;
}

#endif

//sram中创建任务
#ifdef TUYA_TKL_THREAD
static OPERATE_RET app_gui_thread_create(TKL_THREAD_HANDLE* thread,
                           CONST CHAR_T* name,
                           UINT_T stack_size,
                           UINT_T priority,
                           THREAD_FUNC_T func,
                           VOID_T* CONST arg)
{
    if (!thread) {
        return OPRT_INVALID_PARM;
    }
    return tkl_thread_create(thread, name, stack_size, priority, func, arg);
}

#else
#include "FreeRTOS.h"
#include "task.h"
static OPERATE_RET app_gui_thread_create(TKL_THREAD_HANDLE* thread,
                           CONST CHAR_T* name,
                           UINT_T stack_size,
                           UINT_T priority,
                           THREAD_FUNC_T func,
                           VOID_T* CONST arg)
{
    if (!thread) {
        return OPRT_INVALID_PARM;
    }

    BaseType_t ret = 0;
    ret = xTaskCreate(func, name, stack_size / sizeof(portSTACK_TYPE), (void *const)arg, priority, (TaskHandle_t * const )thread);
    if (ret != pdPASS) {
        return OPRT_OS_ADAPTER_THRD_CREAT_FAILED;
    }

    return OPRT_OK;
}
#endif

void tuya_gui_main(void)
{
//创建主函数异步处理任务
    OPERATE_RET ret;

    ret = tkl_semaphore_create_init(&gui_main_thread_sem, 0, 1);
    if (OPRT_OK != ret) {
        TY_GUI_LOG_PRINT("%s semaphore init failed-------------------------------------\r\n", __func__);
        return;
    }

    //这里直接调用tkl_thread_create()(使用xTaskCreateInPsram出现异常!)
    ret = app_gui_thread_create(&gui_main_thread_handle,
                            "tuya_gui",
                            (unsigned short)8192,
                            6,
                            (THREAD_FUNC_T)gui_main_task,
                            0);
    if(OPRT_OK != ret) {
        TY_GUI_LOG_PRINT("%s gui main task create failed------------------------------------\r\n", __func__);
        return;
    }

    tkl_semaphore_wait(gui_main_thread_sem, TKL_SEM_WAIT_FOREVER);
    tkl_semaphore_release(gui_main_thread_sem);
    TY_GUI_LOG_PRINT("%s gui main task run completed, LVGL v%d.%d.%d, LV_COLOR_DEPTH '%d'\r\n", __func__, 
        lv_version_major(), lv_version_minor(), lv_version_patch(), LV_COLOR_DEPTH);
}

void __attribute__((weak)) user_gui_pause(void)
{
    TY_GUI_LOG_PRINT("%s:%d------------------------------------weak invoke\r\n", __func__, __LINE__);
}

void __attribute__((weak)) user_gui_resume(void)
{
    TY_GUI_LOG_PRINT("%s:%d------------------------------------weak invoke\r\n", __func__, __LINE__);
}

void tuya_gui_pause(void)
{
    user_gui_pause();
}

void tuya_gui_resume(void)
{
    user_gui_resume();
}

#ifdef LVGL_AS_APP_COMPONENT
#ifdef TUYA_MULTI_TYPES_LCD
#include "tal_display_service.h"
#endif
void tuya_gui_lvgl_start(void)
{
    lv_vnd_config_t lv_vnd_config = {0};

    lv_vnd_config.rotation          = tuya_app_gui_get_lcd_rotation();
    lv_vnd_config.draw_buf_use_sram = tuya_app_gui_lvgl_draw_buffer_use_sram();
#ifdef TUYA_MULTI_TYPES_LCD
    UINT16_T hor_res = 0, ver_res = 0;
    if (tkl_lvgl_display_resolution(&hor_res, &ver_res) != OPRT_OK)
        return;
    lv_vnd_config.lcd_hor_res = hor_res;
    lv_vnd_config.lcd_ver_res = ver_res;
#else
    lv_vnd_config.lcd_hor_res = tuya_app_gui_get_lcd_width_size();
    lv_vnd_config.lcd_ver_res = tuya_app_gui_get_lcd_height_size();
#endif
    TY_GUI_LOG_PRINT("%s:%d: screen resolution width '%d', height '%d', rotate '%s'\r\n", __func__, __LINE__, lv_vnd_config.lcd_hor_res, lv_vnd_config.lcd_ver_res, 
        (lv_vnd_config.rotation==TKL_DISP_ROTATION_0)?"0":(lv_vnd_config.rotation==TKL_DISP_ROTATION_90)?"90":(lv_vnd_config.rotation==TKL_DISP_ROTATION_180)?"180":"270");

//system SIMD support info:
#if defined(__ARM_FEATURE_MVE) && __ARM_FEATURE_MVE
    TY_GUI_LOG_PRINT("MVE (Helium) SIMD extension supported\n");
    #if (__ARM_FEATURE_MVE & 2)
        TY_GUI_LOG_PRINT("  - Floating-point MVE supported\n");
    #endif
#else
    TY_GUI_LOG_PRINT("MVE (Helium) not supported\n");
#endif

//system DSP support info:
#if defined(__ARM_FEATURE_DSP) && __ARM_FEATURE_DSP
    TY_GUI_LOG_PRINT("DSP instructions supported\n");
#else
    TY_GUI_LOG_PRINT("DSP instructions not supported\n");
#endif

#if defined(TUYA_LINGDONG_GUI)
    tuya_img_direct_flush_poweronpage_exit();   //退出上电快速启动页面!
    tkl_lvgl_tp_open(lv_vnd_config.lcd_hor_res, lv_vnd_config.lcd_ver_res);
    extern int lingdong_gui_start(int16_t hor_res, int16_t ver_res);
    lingdong_gui_start(lv_vnd_config.lcd_hor_res, lv_vnd_config.lcd_ver_res);
#elif defined(TUYA_IMG_DIRECT_FLUSH) && (TUYA_IMG_DIRECT_FLUSH == 1)
    if (tkl_lvgl_display_frame_init(lv_vnd_config.lcd_hor_res, lv_vnd_config.lcd_ver_res) != OPRT_OK) {
        TY_GUI_LOG_PRINT("%s:%d- tkl_lvgl_display_frame init fail ????\r\n", __func__, __LINE__);
        return;
    }
    tuya_img_direct_flush_screen_info_set(lv_vnd_config.lcd_hor_res, lv_vnd_config.lcd_ver_res, tuya_app_gui_get_lcd_rotation());
    tuya_gui_main();
#else
    if (tuya_app_gui_lvgl_init(&lv_vnd_config) != OPRT_OK) {
        TY_GUI_LOG_PRINT("%s:%d- gui lvgl init fail ????\r\n", __func__, __LINE__);
        return;
    }
    TY_GUI_LOG_PRINT("LVGL task priority '%d', stack size '%d'\n", TASK_PRIORITY_LVGL, STACK_SIZE_LVGL);
    tkl_lvgl_tp_open(lv_vnd_config.lcd_hor_res, lv_vnd_config.lcd_ver_res);

    lv_vendor_init(&lv_vnd_config);
    
    lv_vendor_disp_lock();
    tuya_gui_main();
    lv_vendor_disp_unlock();
    
    lv_vendor_start(TASK_PRIORITY_LVGL/*lvgl任务优先级*/, STACK_SIZE_LVGL/*lvgl任务栈空间大小*/);
#endif
}

void tuya_gui_lvgl_stop(void)
{
    tuya_gui_pause();
    lv_vendor_stop();
    lv_vendor_deinit();
    tkl_lvgl_tp_close();
}
#endif
