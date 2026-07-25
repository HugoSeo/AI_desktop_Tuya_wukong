#include "lvgl.h"
#include "app_ipc_command.h"
#include "lv_vendor_event.h"
#include "img_utility.h"
#include "ty_gui_fs.h"
#include "tuya_app_gui_display_text.h"
#include "tuya_app_gui_fs_path.h"
#include "tuya_app_gui_booton_page.h"
#include "img_direct_flush.h"
#include "tal_sw_timer.h"
#include "tkl_system.h"
#include "tkl_thread.h"
#include <stdio.h>

static bool screen_backlight_auto = false;              //屏幕背光自动开关!
static bool main_screen_loaded = false;
static user_app_gui_screen_entry_cb_t user_screen_start = NULL;
static void user_main_screen_start(bool has_bootonpage);
static void screen_backlight_delay_off2on(uint32_t delay_ms);
static void bootOnPage_screen_backlight(bool on);

#ifdef TUYA_GUI_IMG_PRE_DECODE
static user_app_gui_img_pre_decode_cb_t user_img_pre_decode_pp = NULL;      //第一优先级预解码回调:Primary Priority
static user_app_gui_img_pre_decode_cb_t user_img_pre_decode_sp = NULL;      //次优先级预解码回调:Secondary Priority
static bool user_img_pre_decoded_sp = false;                                //次优先级预解码是否完成
static lv_obj_t * ui_powerOnPage = NULL;        //启动背景图屏幕
static lv_obj_t* default_screen = NULL;         //系统默认屏幕
static lv_img_dsc_t ui_bootOnPage_img = {0};
static GUI_IMAGE_TYPE_E powerOnPage_type = image_type_unknown;
static unsigned long long int bootOnPage_start_ms = 0;              //启动页面展示开始时间:ms
static lv_obj_t *screen_mask_obj = NULL;
static lv_timer_t *screen_mask_timer = NULL;

extern unsigned int tuya_app_gui_bootOnPage2mainPage_transition_time_ms(void);
extern unsigned int tuya_app_gui_bootOnPage_minmum_elpase_time_ms(void);

static void __time_get_system_time(unsigned long *pSecTime, unsigned long *pMsTime)
{
    static unsigned long long int last_ms = 0;
    static unsigned long long int roll_ms = 0;
    unsigned long long int curr_ms = 0;

    curr_ms = tkl_system_get_millisecond();
    if (last_ms > curr_ms) { // recycle
        roll_ms += 0x100000000;
    }
    if (pMsTime) {
        *pMsTime = (curr_ms + roll_ms) % 1000;
    }
    if (pSecTime) {
        *pSecTime = (curr_ms + roll_ms) / 1000;
    }
    last_ms = curr_ms;
}

unsigned long long int __current_timestamp(void)
{
    unsigned long sectime = 0;
    unsigned long mstime = 0;
    unsigned long long int cur_time_ms = 0;

    __time_get_system_time(&sectime, &mstime);
    cur_time_ms = (unsigned long long int)sectime * 1000 + mstime;

    return cur_time_ms;
}

static void img_set_zoom_adaptive(lv_obj_t * img_obj, lv_coord_t des_w, lv_coord_t des_h)
{
    const lv_img_dsc_t * img_dsc = lv_img_get_src(img_obj);

    if (img_dsc == NULL || img_dsc->data == NULL || img_dsc->data_size == 0) {
        TY_GUI_LOG_PRINT("------------------------img not exist ???\r\n");
        return;
    }
    lv_coord_t img_width = img_dsc->header.w;
    lv_coord_t img_height = img_dsc->header.h;

    // 计算缩放比例（保持宽高比）
    uint32_t zoom_width = (uint32_t)(des_w * 256)/img_width;
    uint32_t zoom_height = (uint32_t)(des_h * 256)/img_height;
    uint32_t zoom = LV_MIN(zoom_width, zoom_height);

    TY_GUI_LOG_PRINT("------------------------des_w '%d', des_h '%d' zoom '%d'\r\n", des_w, des_h, zoom);
    lv_img_set_zoom(img_obj, zoom);
}

static void user_main_screen_mask_delete(void)
{
    if (screen_mask_timer != NULL)
        lv_timer_del(screen_mask_timer);
    if (screen_mask_obj != NULL)
        lv_obj_del(screen_mask_obj);
}

static void user_img_pre_decoded_sp_check_cb(struct _lv_timer_t *timer)
{
    if (user_img_pre_decoded_sp) {      //次预解码完成!
        LV_LOG_WARN("...secondary priority img pre-decode completed, unlock user main screen !!!\n");
        user_main_screen_mask_delete();
    }
}

static void user_main_screen_mask_create(void)
{
    screen_mask_obj = lv_obj_create(lv_scr_act());
    lv_obj_set_pos(screen_mask_obj, 0, 0);
    lv_obj_set_size(screen_mask_obj, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_opa(screen_mask_obj, LV_OPA_TRANSP, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(screen_mask_obj, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_move_foreground(screen_mask_obj);
}

static void user_main_screen_mask_delete_polling(void)
{
    screen_mask_timer = lv_timer_create(user_img_pre_decoded_sp_check_cb, 50, NULL);     //定时查询次预解码是否完成!
}

static void user_main_screen_delay_light_cb(struct _lv_timer_t *timer)
{
    bootOnPage_screen_backlight(true);
    main_screen_loaded = true;
}

static void user_main_screen_delay_start(uint32_t delay_ms)
{
    lv_timer_t *timer = NULL;

    bootOnPage_screen_backlight(false);
    user_main_screen_start(true);
    if (!user_img_pre_decoded_sp) {     //次优先级预解码未完成,锁定当前屏幕?
        LV_LOG_WARN("...main screen is ready, lock it due to secondary priority img pre-decode not ready !!!\n");
        user_main_screen_mask_create();
        user_main_screen_mask_delete_polling();
    }
    else {
        LV_LOG_WARN("...main screen is ready, and secondary priority img pre-decode is also ready!!!\n");
    }
    timer = lv_timer_create(user_main_screen_delay_light_cb, delay_ms, NULL);
    if (timer != NULL)
        lv_timer_set_repeat_count(timer, 1);
}

static void bootOnPage_event_handler(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);
    lv_obj_t * target = lv_event_get_target(e);

    if(event_code == LV_EVENT_CANCEL && ui_powerOnPage != NULL) {
        LV_LOG_WARN("bootOnPage cancel event triggered by user screen is ready !!!\n");
        if (powerOnPage_type == image_type_png)
            png_img_unload(&ui_bootOnPage_img);
        else if (powerOnPage_type == image_type_gif)
            gif_img_unload(&ui_bootOnPage_img);
        else if (powerOnPage_type == image_type_jpg || powerOnPage_type == image_type_jpeg)
            jpg_img_unload(&ui_bootOnPage_img);
    /*
        即将删除当前屏幕,lv_scr_act()指向的系统默认屏幕会失效,
        用户应用避免马上直接操作该系统默认屏幕,如lv_obj_set_style_bg_color(lv_scr_act(), lv_color_black(), 0),否则系统crash(如gesture中的tuya_tileview_demo).
        解决方法:用户需先显式创建并激活一个屏幕后方可对其操作,如:
        lv_obj_t* screen = lv_obj_create(NULL);  // 创建新屏幕
        lv_scr_load(screen);                    // 激活该屏幕
        lv_obj_set_style_bg_color(screen, lv_color_black(), 0); // 安全操作
    */
        if (default_screen)
            lv_scr_load(default_screen); // 先切换回默认,保证默认的活动屏幕一直存在!
        lv_obj_del(target);              // 删除当前活动屏幕
        ui_powerOnPage = NULL;
        //user_main_screen_start(true);
        user_main_screen_delay_start(tuya_app_gui_bootOnPage2mainPage_transition_time_ms());
    }
}

static TKL_THREAD_HANDLE img_pre_decode_thread_handle = NULL;
static void img_pre_decode_finish_notify_cb(struct _lv_timer_t *timer)
{
#if LVGL_VERSION_MAJOR < 9
    lv_event_send(ui_powerOnPage, LV_EVENT_CANCEL, NULL);
#else
    lv_obj_send_event(ui_powerOnPage, LV_EVENT_CANCEL, NULL);
#endif
}

static void img_pre_decode_task(void *arg)
{
    lv_timer_t *timer = NULL;
    unsigned long long int curr_run_ms = 0;
    unsigned int timeout_ms = 100;

    TY_GUI_LOG_PRINT("%s img high priority pre decode start !\r\n", __func__);
    tuya_app_display_text_init(); //语言环境初始化
    if (user_img_pre_decode_pp) {
        user_img_pre_decode_pp();
    }
    TY_GUI_LOG_PRINT("%s img high priority pre decode completed !\r\n", __func__);
    curr_run_ms = __current_timestamp();
    if ((curr_run_ms-bootOnPage_start_ms)>0 && (curr_run_ms-bootOnPage_start_ms)<tuya_app_gui_bootOnPage_minmum_elpase_time_ms()) {
        timeout_ms = tuya_app_gui_bootOnPage_minmum_elpase_time_ms()-(curr_run_ms-bootOnPage_start_ms);
        if (timeout_ms<100)
            timeout_ms = 100;
    }
    TY_GUI_LOG_PRINT("%s bootonpage run start time '%llu'ms, curr run time '%llu'ms, timeout_ms '%lu'ms\r\n", __func__, bootOnPage_start_ms, curr_run_ms, timeout_ms);
    timer = lv_timer_create(img_pre_decode_finish_notify_cb, timeout_ms, NULL);
    if (timer != NULL)
        lv_timer_set_repeat_count(timer, 1);

    TY_GUI_LOG_PRINT("%s img low priority pre decode start !\r\n", __func__);
    if (user_img_pre_decode_sp) {
        user_img_pre_decode_sp();
    }
    user_img_pre_decoded_sp = true;             //次优先级预解码完成!
    TY_GUI_LOG_PRINT("%s img low priority pre decode completed !\r\n", __func__);

    tkl_thread_release(img_pre_decode_thread_handle);
    img_pre_decode_thread_handle = NULL;
}

static void img_pre_decode2_task(void *arg)
{
    TY_GUI_LOG_PRINT("%s img low priority pre decode start !\r\n", __func__);
    if (user_img_pre_decode_sp) {
        user_main_screen_mask_delete_polling();
        user_img_pre_decode_sp();
    }
    user_img_pre_decoded_sp = true;             //次优先级预解码完成!
    TY_GUI_LOG_PRINT("%s img low priority pre decode completed !\r\n", __func__);

    tkl_thread_release(img_pre_decode_thread_handle);
    img_pre_decode_thread_handle = NULL;
}

bool tuya_user_app_gui_main_screen_loaded(void)
{
    return main_screen_loaded;
}

int tuya_user_app_gui_startup_register(const void *powerOnPagePic, BOOT_PAGE_IMG_SRC_E powerOnPagePicType,
                                        user_app_gui_screen_entry_cb_t screen_entry_cb, 
                                        user_app_gui_img_pre_decode_cb_t h_pri_pre_decode_cb, 
                                        user_app_gui_img_pre_decode_cb_t l_pri_pre_decode_cb)
{
//创建主函数异步处理任务
    OPERATE_RET ret = OPRT_COM_ERROR;
    lv_obj_t * obj_img = NULL;
    BOOL_T is_exist = false;
    char *powerOnPageImg = NULL;

    if (screen_entry_cb == NULL) {
        ret = tuya_app_display_text_init(); //仅做语言环境初始化
        return ret;
    }

    if (powerOnPagePic != NULL) {
        TY_GUI_LOG_PRINT("%s input poweron page source type - '%d'\r\n", __func__, powerOnPagePicType);
        if(powerOnPagePicType == PAGE_IMG_SRC_FILE) {
            //a file path
            powerOnPageImg = (char *)powerOnPagePic;
            TY_GUI_LOG_PRINT("%s input poweron page file path - '%s'\r\n", __func__, powerOnPageImg);
        }
        else if(powerOnPagePicType == PAGE_IMG_SRC_DIGIT){
            //a digit number
            uint32_t file_id = *(uint32_t *)powerOnPagePic;
            const char *fmt_array[8] = {"png", "jpg", "jpeg", "gif", "PNG", "JPG", "JPEG", "GIF"};
            char file_name[32];

            TY_GUI_LOG_PRINT("%s input poweron page digit number - '%d'\r\n", __func__, file_id);
            for (uint32_t i=0; i<8; i++) {
                memset(file_name, 0, sizeof(file_name));
                snprintf(file_name, sizeof(file_name), "%lu.%s", file_id, fmt_array[i]);
                ty_gui_fs_is_exist(tuya_app_gui_get_picture_full_path(file_name), &is_exist);
                if (is_exist)
                    break;
            }
            if (is_exist) {
                powerOnPageImg = tuya_app_gui_get_picture_full_path(file_name);
                TY_GUI_LOG_PRINT("%s input poweron page digit number - '%d'-'%s'\r\n", __func__, file_id, powerOnPageImg);
            }
            else {
                TY_GUI_LOG_PRINT("%s input poweron page digit number - '%d'- related file not exist ?\r\n", __func__, file_id);
            }
        }
        else if (powerOnPagePicType == PAGE_IMG_SRC_STRUCT) {
            //a struct img data
            powerOnPageImg = BOOT_PAGE_IMG_STRUCT_INDICATE;
        }
        else if (powerOnPagePicType == PAGE_IMG_SRC_STRUCT_GIF) {
            //a struct gif data
            powerOnPageImg = BOOT_PAGE_IMG_STRUCT_GIF_INDICATE;
        }
        else {
            TY_GUI_LOG_PRINT("%s unknown poweron page source type ?\r\n", __func__);
        }
    }

#if defined(TUYA_IMG_DIRECT_FLUSH) && (TUYA_IMG_DIRECT_FLUSH == 1)
    powerOnPageImg = NULL;
    powerOnPage_type = image_type_unknown;              //直接屏幕刷新不支持启动页面(因为会调用lvgl未初始化)
    user_img_pre_decode_sp = NULL;                      //直接屏幕刷新不支持二级预加载(因为会调用lvgl未初始化)
#else
    user_img_pre_decode_sp = l_pri_pre_decode_cb;              //次优先级预解码回调
#endif
    if (tuya_img_direct_flush_poweronpage_started()) {          //上电快速页面已经启动,无需再做启动页面!
        powerOnPageImg = NULL;
    }
    powerOnPage_type = get_image_type(powerOnPageImg);
    user_screen_start = screen_entry_cb;                       //主页面入口函数
    user_img_pre_decode_pp = h_pri_pre_decode_cb;              //高优先级预解码回调
    is_exist = false;
    if (powerOnPageImg)
        ty_gui_fs_is_exist(powerOnPageImg, &is_exist);

    if (powerOnPageImg != NULL) {
        if (strcmp(powerOnPageImg, BOOT_PAGE_IMG_STRUCT_INDICATE) == 0) {           //处理特殊图像结构体数据输入!
            powerOnPage_type = image_type_struct;
            is_exist = true;
        }
        else if (strcmp(powerOnPageImg, BOOT_PAGE_IMG_STRUCT_GIF_INDICATE) == 0) {  //处理特殊gif图像结构体数据输入!
            powerOnPage_type = image_type_struct_gif;
            is_exist = true;
        }
    }

    if (powerOnPage_type == image_type_gif) {               //暂不支持gif启动背景, 全屏gif占用资源太大,启动更慢!
        powerOnPage_type = image_type_unknown;
    }
    if (powerOnPage_type == image_type_unknown) {
        if (powerOnPageImg) {
            if (!is_exist) {
                TY_GUI_LOG_PRINT("%s unknown poweron page-------------------------------------[img %s not exist]\r\n", __func__, powerOnPageImg);
            }
            else {
                TY_GUI_LOG_PRINT("%s unknown poweron page-------------------------------------[img %s unknown]\r\n", __func__, powerOnPageImg);
            }
        }
        else {          //user does not set booton iamge!
            TY_GUI_LOG_PRINT("%s unknown poweron page-------------------------------------[empty img]\r\n", __func__);
        }
    }
    else {
        if (is_exist) {
            default_screen = lv_scr_act(); // 保存默认屏幕,便于后面恢复!
            ui_powerOnPage = lv_obj_create(NULL);
            lv_obj_set_tag(ui_powerOnPage, "ui_powerOnPage");
            lv_obj_set_style_bg_opa(ui_powerOnPage, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(ui_powerOnPage, lv_color_hex(0xffffff), LV_PART_MAIN|LV_STATE_DEFAULT);
            if (powerOnPage_type == image_type_jpg || powerOnPage_type == image_type_jpeg || powerOnPage_type == image_type_png ||  \
                powerOnPage_type == image_type_struct) {
                if (powerOnPage_type == image_type_png)
                    ret = png_img_load(powerOnPageImg, &ui_bootOnPage_img);
                else if (powerOnPage_type == image_type_jpg || powerOnPage_type == image_type_jpeg)
                    ret = jpg_img_load(powerOnPageImg, &ui_bootOnPage_img, false);
                else if (powerOnPage_type == image_type_struct) {
                    ret = OPRT_OK;
                }
                if (ret == OPRT_OK) {
                    obj_img = lv_img_create(ui_powerOnPage);
                    if (powerOnPage_type == image_type_struct)
                        lv_img_set_src(obj_img, powerOnPagePic);
                    else
                        lv_img_set_src(obj_img, &ui_bootOnPage_img);
                    img_set_zoom_adaptive(obj_img, LV_HOR_RES-5, LV_VER_RES-5);         //占满全屏
                    lv_obj_align_to(obj_img, ui_powerOnPage, LV_ALIGN_CENTER, 0, 0);    //居中
                }
                else {
                    TY_GUI_LOG_PRINT("%s decode powerOnPage img '%s' failed-------------------------------------\r\n", __func__, powerOnPageImg);
                }
            }
        #if LV_USE_GIF
            else if (powerOnPage_type == image_type_gif) {
                ret = gif_img_load(powerOnPageImg, &ui_bootOnPage_img);
                if (ret == OPRT_OK) {
                    obj_img = lv_gif_create(ui_powerOnPage);
                    lv_gif_set_src(obj_img, (const void *)&ui_bootOnPage_img);
                    //img_set_zoom_adaptive(obj_img, LV_HOR_RES-5, LV_VER_RES-5);         //占满全屏
                    lv_obj_align_to(obj_img, ui_powerOnPage, LV_ALIGN_CENTER, 0, 0);    //居中
                    lv_gif_restart(obj_img);
                }
                else {
                    TY_GUI_LOG_PRINT("%s decode powerOnPage gif img '%s' failed-------------------------------------\r\n", __func__, powerOnPageImg);
                }
            }
            else if (powerOnPage_type == image_type_struct_gif) {
                obj_img = lv_gif_create(ui_powerOnPage);
                lv_gif_set_src(obj_img, powerOnPagePic);
                lv_obj_align_to(obj_img, ui_powerOnPage, LV_ALIGN_CENTER, 0, 0);    //居中
                lv_gif_restart(obj_img);
            }
        #endif
        }
        else {
            TY_GUI_LOG_PRINT("%s poweron page-------------------------------------[img %s not exist]\r\n", __func__, powerOnPageImg);
        }
    }

    if (ret == OPRT_OK) {
        if (ui_powerOnPage != NULL) {
            lv_scr_load(ui_powerOnPage);             //屏幕加载
            bootOnPage_start_ms = __current_timestamp();
            lv_obj_add_event_cb(ui_powerOnPage, bootOnPage_event_handler, LV_EVENT_CANCEL, NULL);
            //后台启动图片预加载...
            ret = tkl_thread_create(&img_pre_decode_thread_handle, "img_pre_decode", 4096, 6, img_pre_decode_task, NULL);
            if(OPRT_OK != ret) {
                TY_GUI_LOG_PRINT("%s img_pre_decode task create failed------------------------------------\r\n", __func__);
            }
        }
        else {
            ret = OPRT_COM_ERROR;
        }
    }

    if (ret != OPRT_OK) {
        TY_GUI_LOG_PRINT("%s poweron boot page fail, direct start user screen !!!\r\n", __func__);
        ret = tuya_app_display_text_init(); //语言环境初始化
        if (user_img_pre_decode_pp)
            user_img_pre_decode_pp();
        if (tuya_img_direct_flush_poweronpage_started()) {
            screen_backlight_delay_off2on(tuya_app_gui_bootOnPage2mainPage_transition_time_ms());
            tuya_img_direct_flush_poweronpage_exit();   //退出上电快速启动页面!
            user_main_screen_start(true);
        }
        else
            user_main_screen_start(false);
        if (user_img_pre_decode_sp) {
            user_main_screen_mask_create();         //建立当前屏幕屏罩!
            //后台启动二级图片预加载...
            ret = tkl_thread_create(&img_pre_decode_thread_handle, "img_pre_decode2", 4096, 6, img_pre_decode2_task, NULL);
            if(OPRT_OK != ret) {
                TY_GUI_LOG_PRINT("%s img_pre_decode2 task create failed------------------------------------\r\n", __func__);
            }
        }
        main_screen_loaded = true;
    }
    else {
        TY_GUI_LOG_PRINT("%s poweron boot page successful  !!!\r\n", __func__);
    }
    return ret;
}

#else
bool tuya_user_app_gui_main_screen_loaded(void)
{
    return main_screen_loaded;
}

int tuya_user_app_gui_startup_register(const void *powerOnPagePic, BOOT_PAGE_IMG_SRC_E powerOnPagePicType,
                                        user_app_gui_screen_entry_cb_t screen_entry_cb, 
                                        user_app_gui_img_pre_decode_cb_t h_pri_pre_decode_cb, 
                                        user_app_gui_img_pre_decode_cb_t l_pri_pre_decode_cb)
{
//创建主函数异步处理任务
    OPERATE_RET ret = OPRT_COM_ERROR;

    user_screen_start = screen_entry_cb;
    ret = tuya_app_display_text_init(); //仅做语言环境初始化
    if (tuya_img_direct_flush_poweronpage_started()) {
        screen_backlight_delay_off2on(tuya_app_gui_bootOnPage2mainPage_transition_time_ms());
        tuya_img_direct_flush_poweronpage_exit();   //退出上电快速启动页面!
        user_main_screen_start(true);
    }
    else
        user_main_screen_start(false);
    main_screen_loaded = true;
    return ret;
}
#endif

static TIMER_ID backlight_timer_id = NULL;

static void bootOnPage_screen_backlight(bool on)
{
    LV_DISP_EVENT_S lv_msg = { 0 };

    if (!on)
        screen_backlight_auto = true;               //背光由启动页面自动开关,主页面无需干涉!
    lv_msg.tag = NULL;
    lv_msg.event = (on==true)?LLV_EVENT_VENDOR_LCD_WAKE:LLV_EVENT_VENDOR_LCD_IDEL;
    lv_msg.param = LV_LCD_LIGHT_FROM_BOOT;
    aic_lvgl_msg_event_change((void *)&lv_msg);
}

static VOID screen_backlight_delay_on_cb(TIMER_ID timerID,PVOID_T pTimerArg)
{
    bootOnPage_screen_backlight(true);
    tal_sw_timer_stop(backlight_timer_id);
    tal_sw_timer_delete(backlight_timer_id);
}

static void screen_backlight_delay_off2on(uint32_t delay_ms)
{
    bootOnPage_screen_backlight(false);
    tal_sw_timer_create(screen_backlight_delay_on_cb,NULL,&backlight_timer_id);
    tal_sw_timer_start(backlight_timer_id, delay_ms, TAL_TIMER_ONCE);
}

static void user_main_screen_start(bool has_bootonpage)
{
    if (user_screen_start) {
        user_screen_start();
        //用户主屏幕启动完成,通知app可以同步初始化业务数据!
        LV_DISP_EVENT_S lv_msg = {0};
        LV_TUYAOS_EVENT_S g_dp_evt_s = {.event_typ = LV_TUYAOS_EVENT_MAIN_SCREEN_STRATUP, .param1 = (uint32_t)has_bootonpage};

        lv_msg.event = LLV_EVENT_VENDOR_TUYAOS;
        lv_msg.param = (UINT_T)&g_dp_evt_s;
        aic_lvgl_msg_event_change((void *)&lv_msg);
    }
}

bool tuya_user_app_PwrOnPage_backlight_auto(void)
{
    return screen_backlight_auto;
}
