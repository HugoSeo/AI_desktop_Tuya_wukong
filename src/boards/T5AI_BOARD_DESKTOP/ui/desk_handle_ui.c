/**
 * @file desk_handle_ui.c
 * @brief 桌面 UI 页面切换框架实现
 */
#include "desk_handle_ui.h"
#include "desk_event_handle.h"
#include "tal_log.h"
#include <string.h>

typedef struct
{
    uint32_t           screen_id;
    dhui_screen_desc_t desc;
    uint32_t           last_source_id;
    bool               used;
} dhui_reg_entry_t;

typedef struct
{
    uint32_t           target_id;
    lv_scr_load_anim_t anim_type;
    dhui_switch_type_e switch_type;
} dhui_async_switch_arg_t;

typedef struct
{
    uint32_t           screen_id;
    dhui_screen_desc_t desc;
} dhui_screen_decl_t;

#define DHUI_ARRAY_SIZE(arr) ((uint32_t)(sizeof(arr) / sizeof((arr)[0])))
#define DHUI_SCREEN_DECL(id, setup_cb, clear_cb, back_id, screen_name) \
    {                                                                  \
        .screen_id = (id),                                             \
        .desc = {                                                      \
            .setup = (setup_cb),                                       \
            .res_clear = (clear_cb),                                   \
            .default_back_id = (back_id),                              \
            .name = (screen_name),                                     \
        },                                                             \
    }

static dhui_reg_entry_t s_reg[DHUI_SCREEN_ID_MAX];
static uint32_t s_current_screen_id = DHUI_SCREEN_ID_INVALID;
static lv_obj_t *s_current_screen_obj = NULL;

static dhui_reg_entry_t *dhui_get_entry(uint32_t screen_id)
{
    if (screen_id >= DHUI_SCREEN_ID_MAX || !s_reg[screen_id].used)
    {
        return NULL;
    }

    return &s_reg[screen_id];
}

static uint32_t dhui_resolve_back_target(void)
{
    dhui_reg_entry_t *cur_entry;

    if (s_current_screen_id == DHUI_SCREEN_ID_INVALID)
    {
        return DHUI_SCREEN_ID_INVALID;
    }

    cur_entry = dhui_get_entry(s_current_screen_id);
    if (cur_entry == NULL)
    {
        return DHUI_SCREEN_ID_INVALID;
    }

    /* 优先回到当前页最近一次的来源页，保证 A -> B -> back 回到 A。 */
    if (cur_entry->last_source_id != DHUI_SCREEN_ID_INVALID &&
        dhui_get_entry(cur_entry->last_source_id) != NULL) {
        return cur_entry->last_source_id;
    }

    /* 若当前页不是由框架切入，或来源页已失效，则回退到页面级缺省目标。 */
    if (cur_entry->desc.default_back_id != DHUI_SCREEN_ID_INVALID &&
        dhui_get_entry(cur_entry->desc.default_back_id) != NULL) {
        return cur_entry->desc.default_back_id;
    }

    return DHUI_SCREEN_ID_INVALID;
}

static void dhui_do_switch(void *arg)
{
    dhui_async_switch_arg_t *p = (dhui_async_switch_arg_t *)arg;
    dhui_reg_entry_t *target_entry;
    dhui_reg_entry_t *cur_entry;
    lv_obj_t *act_scr;
    lv_obj_t *new_scr;

    if (p == NULL)
    {
        return;
    }

    target_entry = dhui_get_entry(p->target_id);
    if (target_entry == NULL || target_entry->desc.setup == NULL)
    {
        TAL_PR_ERR("[desk_handle_ui] switch_to invalid target_id=%u", (unsigned)p->target_id);
        return;
    }

    /*
     * 记录目标页来源。
     * 这样目标页后续调用 back() 时，可以优先返回到本次切入前所在的页面。
     */
    if (s_current_screen_id != DHUI_SCREEN_ID_INVALID && s_current_screen_id != p->target_id)
    {
        target_entry->last_source_id = s_current_screen_id;
    }

    /* 当前页资源释放统一收敛到框架层，业务事件回调不再直接调用 res_clear。 */
    cur_entry = dhui_get_entry(s_current_screen_id);
    if (cur_entry != NULL && cur_entry->desc.res_clear != NULL)
    {
        cur_entry->desc.res_clear();
    }

    act_scr = lv_scr_act();
    switch (p->switch_type)
    {
        case DHUI_SWITCH_PERMANENT:
        case DHUI_SWITCH_DYNAMIC:
            if (act_scr != NULL)
            {
                lv_obj_clean(act_scr);
            }
            break;
        case DHUI_SWITCH_TEMPORARY:
        default:
            break;
    }

    new_scr = target_entry->desc.setup();
    if (new_scr == NULL)
    {
        TAL_PR_ERR("[desk_handle_ui] setup screen id=%u returned NULL", (unsigned)p->target_id);
        return;
    }

    lv_scr_load_anim(new_scr, p->anim_type, DHUI_SWITCH_DURATION_MS, DHUI_SWITCH_DELAY_MS,
                     (p->switch_type == DHUI_SWITCH_PERMANENT));
    s_current_screen_id = p->target_id;
    s_current_screen_obj = new_scr;
}

static void dhui_do_switch_with_free(void *arg)
{
    dhui_do_switch(arg);
    tal_free(arg);
}

static void dhui_switch_async(uint32_t screen_id, lv_scr_load_anim_t anim_type, dhui_switch_type_e switch_type)
{
    dhui_async_switch_arg_t *arg;

    if (dhui_get_entry(screen_id) == NULL)
    {
        TAL_PR_ERR("[desk_handle_ui] switch_to unknown screen_id=%u", (unsigned)screen_id);
        return;
    }

    arg = (dhui_async_switch_arg_t *)tal_malloc(sizeof(dhui_async_switch_arg_t));
    if (arg == NULL)
    {
        TAL_PR_ERR("[desk_handle_ui] switch_to malloc fail");
        return;
    }

    arg->target_id = screen_id;
    arg->anim_type = anim_type;
    arg->switch_type = switch_type;
    lv_async_call(dhui_do_switch_with_free, arg);
}

/* ============================ Setup Wrappers ============================ */
/* 启动流页面 */

static lv_obj_t *dhui_setup_startup(void)
{
    setup_scr_startup();
    return getContent()->st_startup.startup_scr;
}

static lv_obj_t *dhui_setup_language(void)
{
    setup_scr_language_scr();
    return getContent()->st_startup.language_lv.language_scr;
}

static lv_obj_t *dhui_setup_qrcode(void)
{
    setup_scr_qrcode_scr();
    return getContent()->st_startup.qrcode_lv.qrcode_scr;
}

static lv_obj_t *dhui_setup_network_cfg(void)
{
    setup_scr_network_cfg();
    return getContent()->st_startup.network_start_lv.cfg_network_scr;
}

/* 主页面 */

static lv_obj_t *dhui_setup_home1(void)
{
    setup_scr_home_scr1();
    return getContent()->st_home.home1_lv.home_scr1;
}

static lv_obj_t *dhui_setup_home2(void)
{
    setup_scr_home_scr2();
    return getContent()->st_home.home2_lv.home_scr2;
}

static lv_obj_t *dhui_setup_home3(void)
{
    setup_scr_home_scr3();
    return getContent()->st_home.home3_lv.home_scr3;
}

static lv_obj_t *dhui_setup_chat(void)
{
    setup_scr_chat_scr();
    return getContent()->st_chat.main_cont;
}

/* 功能页面 */

static lv_obj_t *dhui_setup_personal(void)
{
    setup_personal_center_scr();
    return getContent()->st_personal.personal_scr;
}

static lv_obj_t *dhui_setup_settings(void)
{
    setup_settings_scr();
    return getContent()->st_func_settings.settings_scr;
}

static lv_obj_t *dhui_setup_music(void)
{
    setup_music_scr();
    return getContent()->st_func_music.music_scr;
}

static lv_obj_t *dhui_setup_music_playlist(void)
{
    setup_music_playlist_scr();
    return getContent()->st_func_music.music_playlist_scr;
}

static lv_obj_t *dhui_setup_photo(void)
{
    setup_photo_scr();
    return getContent()->st_func_photo.photo_scr;
}

static lv_obj_t *dhui_setup_camera(void)
{
    setup_camera_scr();
    return getContent()->st_func_camera.camera_scr;
}

static lv_obj_t *dhui_setup_record(void)
{
    setup_record_scr();
    return getContent()->st_func_record.record_scr;
}

static lv_obj_t *dhui_setup_record_list(void)
{
    setup_record_list_scr();
    return getContent()->st_func_record.record_list_scr;
}

static lv_obj_t *dhui_setup_device_mode(void)
{
    setup_device_mode_scr();
    return getContent()->st_device_mode.device_mode_scr;
}

static lv_obj_t *dhui_setup_detection(void)
{
    setup_detection_scr();
    return getContent()->st_func_detection.detection_scr;
}

static lv_obj_t *dhui_setup_call(void)
{
    setup_call_scr();
    return getContent()->st_func_call.call_scr;
}

/* ============================ Screen Declarations ============================ */
/* 新增页面时，先补 setup/res_clear，再在这里增加一条声明。 */
static const dhui_screen_decl_t s_screen_decls[] = {
    /* 启动流页面 */
    DHUI_SCREEN_DECL(DHUI_SCREEN_ID_STARTUP,     dhui_setup_startup,     NULL,                      DHUI_SCREEN_ID_INVALID,         "startup"),
    DHUI_SCREEN_DECL(DHUI_SCREEN_ID_LANGUAGE,    dhui_setup_language,    NULL,                      DHUI_SCREEN_ID_INVALID,         "language"),
    DHUI_SCREEN_DECL(DHUI_SCREEN_ID_QRCODE,      dhui_setup_qrcode,      NULL,                      DHUI_SCREEN_ID_LANGUAGE,        "qrcode"),
    DHUI_SCREEN_DECL(DHUI_SCREEN_ID_NETWORK_CFG, dhui_setup_network_cfg, NULL,                      DHUI_SCREEN_ID_QRCODE,          "network_cfg"),

    /* 主页面 */
    DHUI_SCREEN_DECL(DHUI_SCREEN_ID_HOME1,       dhui_setup_home1,       home_scr1_res_clear,       DHUI_SCREEN_ID_INVALID,         "home1"),
    DHUI_SCREEN_DECL(DHUI_SCREEN_ID_HOME2,       dhui_setup_home2,       home_scr2_res_clear,       DHUI_SCREEN_ID_HOME1,           "home2"),
    DHUI_SCREEN_DECL(DHUI_SCREEN_ID_HOME3,       dhui_setup_home3,       home_scr3_res_clear,       DHUI_SCREEN_ID_HOME2,           "home3"),
    DHUI_SCREEN_DECL(DHUI_SCREEN_ID_CHAT,        dhui_setup_chat,        chat_scr_res_clear,        DHUI_SCREEN_ID_HOME1,           "chat"),

    /* 功能页面 */
    DHUI_SCREEN_DECL(DHUI_SCREEN_ID_PERSONAL_CENTER, dhui_setup_personal, personal_center_scr_res_clear, DHUI_SCREEN_ID_HOME2,           "personal"),
    DHUI_SCREEN_DECL(DHUI_SCREEN_ID_SETTINGS,        dhui_setup_settings, settings_scr_res_clear,        DHUI_SCREEN_ID_PERSONAL_CENTER, "settings"),
    DHUI_SCREEN_DECL(DHUI_SCREEN_ID_MUSIC,           dhui_setup_music,    music_scr_res_clear,           DHUI_SCREEN_ID_PERSONAL_CENTER, "music"),
    DHUI_SCREEN_DECL(DHUI_SCREEN_ID_MUSIC_PLAYLIST, dhui_setup_music_playlist, music_playlist_scr_res_clear, DHUI_SCREEN_ID_MUSIC, "music_playlist"),
    DHUI_SCREEN_DECL(DHUI_SCREEN_ID_PHOTO,           dhui_setup_photo,    photo_scr_res_clear,           DHUI_SCREEN_ID_PERSONAL_CENTER, "photo"),
#if defined(ENABLE_TUYA_CAMERA) && (ENABLE_TUYA_CAMERA == 1)
    DHUI_SCREEN_DECL(DHUI_SCREEN_ID_CAMERA,          dhui_setup_camera,   camera_scr_leave,              DHUI_SCREEN_ID_PERSONAL_CENTER, "camera"),
#endif
    DHUI_SCREEN_DECL(DHUI_SCREEN_ID_RECORD,        dhui_setup_record,    record_scr_res_clear,        DHUI_SCREEN_ID_PERSONAL_CENTER, "record"),
    DHUI_SCREEN_DECL(DHUI_SCREEN_ID_RECORD_LIST,  dhui_setup_record_list, record_list_scr_res_clear, DHUI_SCREEN_ID_RECORD,          "record_list"),
    DHUI_SCREEN_DECL(DHUI_SCREEN_ID_DEVICE_MODE,  dhui_setup_device_mode, device_mode_scr_res_clear, DHUI_SCREEN_ID_PERSONAL_CENTER, "device_mode"),
    DHUI_SCREEN_DECL(DHUI_SCREEN_ID_DETECTION,   dhui_setup_detection,   detection_scr_res_clear,   DHUI_SCREEN_ID_PERSONAL_CENTER, "detection"),
    DHUI_SCREEN_DECL(DHUI_SCREEN_ID_CALL,       dhui_setup_call,        call_scr_res_clear,        DHUI_SCREEN_ID_PERSONAL_CENTER, "call"),
};

void desk_handle_ui_init(void)
{
    memset(s_reg, 0, sizeof(s_reg));
    s_current_screen_id = DHUI_SCREEN_ID_INVALID;
    s_current_screen_obj = NULL;
}

void desk_handle_ui_register(uint32_t screen_id, const dhui_screen_desc_t *desc)
{
    if (desc == NULL || screen_id >= DHUI_SCREEN_ID_MAX)
    {
        TAL_PR_ERR("[desk_handle_ui] register invalid id=%u or null desc", (unsigned)screen_id);
        return;
    }

    s_reg[screen_id].screen_id = screen_id;
    s_reg[screen_id].desc = *desc;
    s_reg[screen_id].last_source_id = DHUI_SCREEN_ID_INVALID;
    s_reg[screen_id].used = true;
}

void desk_handle_ui_register_all(void)
{
    uint32_t i;

    /* 注册区：统一遍历声明表，避免 register_all 中重复展开页面细节。 */
    for (i = 0; i < DHUI_ARRAY_SIZE(s_screen_decls); ++i)
    {
        desk_handle_ui_register(s_screen_decls[i].screen_id, &s_screen_decls[i].desc);
    }
}

void desk_handle_ui_switch_to(uint32_t screen_id, lv_scr_load_anim_t anim_type, dhui_switch_type_e switch_type)
{
    dhui_switch_async(screen_id, anim_type, switch_type);
}

void desk_handle_ui_back(lv_scr_load_anim_t anim_type, dhui_switch_type_e switch_type)
{
    uint32_t target_id = dhui_resolve_back_target();

    if (target_id == DHUI_SCREEN_ID_INVALID)
    {
        TAL_PR_ERR("[desk_handle_ui] back target missing, current=%u", (unsigned)s_current_screen_id);
        return;
    }

    dhui_switch_async(target_id, anim_type, switch_type);
}

void desk_handle_ui_back_to(uint32_t screen_id, lv_scr_load_anim_t anim_type, dhui_switch_type_e switch_type)
{
    dhui_switch_async(screen_id, anim_type, switch_type);
}

uint32_t desk_handle_ui_get_current_screen_id(void)
{
    return s_current_screen_id;
}

lv_obj_t *desk_handle_ui_get_current_screen_obj(void)
{
    return s_current_screen_obj;
}
