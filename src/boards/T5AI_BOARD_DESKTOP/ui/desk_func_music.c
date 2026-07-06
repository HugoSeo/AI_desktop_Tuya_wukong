#include <stdint.h>
#include <string.h>
#include "desk_event_handle.h"
#include "desk_handle_ui.h"
#include "ty_cJSON.h"
#include "tkl_fs.h"
#include "tal_log.h"
#include "tal_memory.h"
#include "tuya_error_code.h"
#include "tuya_iot_com_api.h"
#include "wukong_playback_ctrl.h"
#include "tuya_cloud_types.h"
#include "uni_random.h"

#if defined(ENABLE_TOOLKITS_PLAYBACK) && (ENABLE_TOOLKITS_PLAYBACK == 1)

#define DESKTOP_MUSIC_LIST_JSON_PATH "/t5_fs/music/music_list.json"

static music_scr_res_t s_music_res = {0};
static int s_music_play_mode = MUSIC_SCR_PLAY_MODE_LIST_LOOP;
static WUKONG_MUSIC_PLAYER_T s_music_player = {0};
static bool is_interrupt_music = false; //是否中断音乐
static uint32_t s_music_main_ui_generation = 0;
static uint32_t s_music_playlist_ui_generation = 0;

static void music_playlist_list_create(void);
void music_state_update(void);
void music_info_update(void);

static void music_info_safe_copy(char *dst, size_t dst_size, const char *src)
{
    if ((dst == NULL) || (dst_size == 0)) {
        return;
    }

    if (src == NULL) {
        dst[0] = '\0';
        return;
    }

    snprintf(dst, dst_size, "%s", src);
}

static BOOL_T music_obj_is_valid(lv_obj_t *obj)
{
    return (obj != NULL) && lv_obj_is_valid(obj);
}

static BOOL_T music_main_screen_ready(uint32_t generation)
{
    return (desk_handle_ui_get_current_screen_id() == DHUI_SCREEN_ID_MUSIC) &&
           (generation == s_music_main_ui_generation);
}

static BOOL_T music_playlist_screen_ready(uint32_t generation)
{
    return (desk_handle_ui_get_current_screen_id() == DHUI_SCREEN_ID_MUSIC_PLAYLIST) &&
           (generation == s_music_playlist_ui_generation);
}

static void music_playlist_refresh_async(void *arg)
{
    lv_music_ui_t *ui = &getContent()->st_func_music;
    uint32_t generation = (uint32_t)(uintptr_t)arg;

    if ((music_playlist_screen_ready(generation) == FALSE) ||
        (music_obj_is_valid(ui->music_playlist_list_cont) == FALSE)) {
        return;
    }

    music_playlist_list_create();
}

static void music_playlist_refresh(void)
{
    lv_vendor_disp_lock();
    lv_async_call(music_playlist_refresh_async, (void *)(uintptr_t)s_music_playlist_ui_generation);
    lv_vendor_disp_unlock();
}

static void music_refresh_visible_ui(void)
{
    DESKUI_INDEX deskui_index = getDeskUIIndex();

    if (deskui_index == DESKUI_INDEX_MUSIC) {
        music_info_update();
        music_state_update();
    } else if (deskui_index == DESKUI_INDEX_MUSIC_PLAYLIST) {
        music_playlist_refresh();
    }
}

static void music_uplink_generate_id(CHAR_T *id, INT_T id_len)
{
    BYTE_T i = 0, random[4] = {0};

    // 6c6f189feabb27bf1dcrii_1fd27277  20 + 1 + 4 bytes
    for (i = 0; i < SIZEOF(random); i++) {
        random[i] = (BYTE_T)uni_random_range(0xFF);
    }

    snprintf(id, id_len, "%s_%02x%02x%02x%02x", get_gw_cntl()->gw_if.id, random[0], random[1], random[2], random[3]);
}

static void music_play_ctrl_previous(void)
{
    OPERATE_RET pr = wukong_playback_playlist_prev();

    if (pr != OPRT_OK) {
        (void)wukong_playback_ctrl_send_mqtt("prev");
    }
}

static void music_play_ctrl_next(void)
{
    wukong_playback_ctrl_next();
}

static char *get_play_mdoe_icon_path(int mode)
{
    char *icon_path = ICON_PLAYMODE_LOOP;

    switch(mode)
    {
        case MUSIC_SCR_PLAY_MODE_SINGLE_LOOP:
            icon_path = ICON_PLAYMODE_SLOOP;
        break;

        case MUSIC_SCR_PLAY_MODE_LIST_LOOP:
            icon_path = ICON_PLAYMODE_LOOP;
        break;

        case MUSIC_SCR_PLAY_MODE_RANDOM:
            icon_path = ICON_PLAYMODE_RANDOM;
        break;

        default:
            icon_path = ICON_PLAYMODE_LOOP;
        break;
    }

    return icon_path;
}

static char *get_player_state_img_path(AI_PLAYER_STATE_T state)
{
    if(AI_PLAYER_PLAYING == state) 
    {
        return ICON_PLAY_MUSIC;
    }

    return ICON_PAUSE_PLAY;
}

static void music_play_ctrl_switch_mode(int mode)
{
    char data[256] = {0};
    char biz_id[64] = {0};
    int id = 0;

    music_uplink_generate_id(biz_id, 64);

    switch(mode)
    {
        case MUSIC_SCR_PLAY_MODE_SINGLE_LOOP:
            snprintf(data, 256, "{\"bizId\":\"%s\",\"bizType\":\"SKILL\", \"data\":{\"code\":\"PlayControl\", \"action\":\"single_loop\", \"id\":%d}}", biz_id, id);
        break;

        case MUSIC_SCR_PLAY_MODE_LIST_LOOP:
            snprintf(data, 256, "{\"bizId\":\"%s\",\"bizType\":\"SKILL\", \"data\":{\"code\":\"PlayControl\", \"action\":\"sequential_loop\", \"id\":%d}}", biz_id, id);
        break;

        case MUSIC_SCR_PLAY_MODE_RANDOM:
            snprintf(data, 256, "{\"bizId\":\"%s\",\"bizType\":\"SKILL\", \"data\":{\"code\":\"PlayControl\", \"action\":\"random_loop\", \"id\":%d}}", biz_id, id);
        break;

        default:
            snprintf(data, 256, "{\"bizId\":\"%s\",\"bizType\":\"SKILL\", \"data\":{\"code\":\"PlayControl\", \"action\":\"sequential_loop\", \"id\":%d}}", biz_id, id);
        break;
    }

    iot_mqc_send_custom_msg(9000, data, 0, 0, NULL, NULL);
}

void music_state_update_async(void *arg)
{
    lv_music_ui_t *ui = &getContent()->st_func_music;
    uint32_t generation = (uint32_t)(uintptr_t)arg;
    char *player_state = get_player_state_img_path(s_music_player.state);

    if ((music_main_screen_ready(generation) == FALSE) ||
        (music_obj_is_valid(ui->play_music_icon) == FALSE)) {
        return;
    }

    png_img_unload(&s_music_res.play_music_icon);
    if(png_img_load(tuya_app_gui_get_picture_full_path(player_state), &s_music_res.play_music_icon) == 0) 
    {
        if ((music_main_screen_ready(generation) == FALSE) ||
            (music_obj_is_valid(ui->play_music_icon) == FALSE)) {
            png_img_unload(&s_music_res.play_music_icon);
            return;
        }
        lv_img_set_src(ui->play_music_icon, &s_music_res.play_music_icon);  
    }
}

void music_state_update()
{
    //异步更新
    lv_vendor_disp_lock();
    lv_async_call(music_state_update_async, (void *)(uintptr_t)s_music_main_ui_generation);
    lv_vendor_disp_unlock();
}

void music_info_update_async(void *arg)
{  
    lv_music_ui_t *ui = &getContent()->st_func_music;
    uint32_t generation = (uint32_t)(uintptr_t)arg;

    if ((music_main_screen_ready(generation) == FALSE) ||
        (music_obj_is_valid(ui->song_name) == FALSE) ||
        (music_obj_is_valid(ui->singer_name) == FALSE)) {
        return;
    }

    lv_label_set_text(ui->song_name, s_music_player.song_name);
    lv_label_set_text(ui->singer_name, s_music_player.artist);
}

void music_info_update()
{
    //异步更新
    lv_vendor_disp_lock();
    lv_async_call(music_info_update_async, (void *)(uintptr_t)s_music_main_ui_generation);
    lv_vendor_disp_unlock();
}

int music_player_interrupt(void *data)
{
    (void)data;
    is_interrupt_music = true;
    return OPRT_OK;
}

int music_player_event(void *data)
{
    TUYA_CHECK_NULL_RETURN(data, OPRT_OK);
    WUKONG_MUSIC_PLAYER_T *msg = (WUKONG_MUSIC_PLAYER_T *)data;
    DESKUI_INDEX deskui_index = getDeskUIIndex();

    if (deskui_index != DESKUI_INDEX_MUSIC) {
        switch (msg->cmd) {
        case MUSIC_PLAYER_STATE:
            s_music_player.state = msg->state;
            music_refresh_visible_ui();
            if (msg->state == AI_PLAYER_STOPPED && is_interrupt_music) {
                is_interrupt_music = false;
                memset(&s_music_player, 0, sizeof(s_music_player));
                music_refresh_visible_ui();
            } else if (msg->state == AI_PLAYER_PLAYING) {
                is_interrupt_music = false;
            }
            break;

        case MUSIC_PLAYER_DATA:
            TAL_PR_DEBUG("[music_ui] song: %s singer: %s", msg->song_name, msg->artist);
            music_info_safe_copy(s_music_player.song_name, sizeof(s_music_player.song_name), msg->song_name);
            music_info_safe_copy(s_music_player.artist, sizeof(s_music_player.artist), msg->artist);
            music_info_safe_copy(s_music_player.song_url, sizeof(s_music_player.song_url), msg->song_url);
            s_music_player.song_name[sizeof(s_music_player.song_name) - 1] = '\0';
            s_music_player.artist[sizeof(s_music_player.artist) - 1] = '\0';
            s_music_player.song_url[sizeof(s_music_player.song_url) - 1] = '\0';
            s_music_player.state = AI_PLAYER_STOPPED;
            music_refresh_visible_ui();
            break;

        default:
            break;
        }
    } else {
        switch (msg->cmd) {
        case MUSIC_PLAYER_STATE:
            s_music_player.state = msg->state;
            music_state_update();
            if (msg->state == AI_PLAYER_STOPPED && is_interrupt_music) {
                is_interrupt_music = false;
                memset(&s_music_player, 0, sizeof(s_music_player));
                music_info_update();
                music_state_update();
            } else if (msg->state == AI_PLAYER_PLAYING) {
                is_interrupt_music = false;
            }
            break;

        case MUSIC_PLAYER_DATA:
            TAL_PR_DEBUG("[music_ui] song: %s singer: %s", msg->song_name, msg->artist);
            music_info_safe_copy(s_music_player.song_name, sizeof(s_music_player.song_name), msg->song_name);
            music_info_safe_copy(s_music_player.artist, sizeof(s_music_player.artist), msg->artist);
            music_info_safe_copy(s_music_player.song_url, sizeof(s_music_player.song_url), msg->song_url);
            s_music_player.song_name[sizeof(s_music_player.song_name) - 1] = '\0';
            s_music_player.artist[sizeof(s_music_player.artist) - 1] = '\0';
            s_music_player.song_url[sizeof(s_music_player.song_url) - 1] = '\0';
            music_info_update();
            s_music_player.state = AI_PLAYER_STOPPED;
            music_state_update();
            break;

        default:
            break;
        }
    }

    return OPRT_OK;
}

static void music_back_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED) 
    {
        TAL_PR_DEBUG("[%s] clicked !!!!!!", __func__);

        lv_indev_wait_release(lv_indev_get_act());
        desk_handle_ui_back_to(DHUI_SCREEN_ID_PERSONAL_CENTER, LV_SCR_LOAD_ANIM_MOVE_RIGHT, DHUI_SWITCH_PERMANENT);
    }       
}

static void music_play_mode_clicked_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_music_ui_t *ui = &getContent()->st_func_music;

    if (code == LV_EVENT_CLICKED) 
    {
        TAL_PR_DEBUG("[%s] clicked !!!!!!", __func__);

        s_music_play_mode = (s_music_play_mode + 1) % MUSIC_SCR_PLAY_MODE_MAX;
        music_play_ctrl_switch_mode(s_music_play_mode);
        char *play_mode_icon_path = get_play_mdoe_icon_path(s_music_play_mode);

        png_img_unload(&s_music_res.play_mode_icon);

        if(png_img_load(tuya_app_gui_get_picture_full_path(play_mode_icon_path), &s_music_res.play_mode_icon) == 0) 
        {
            lv_img_set_src(ui->play_mode_img, &s_music_res.play_mode_icon);
        }

        lv_obj_update_layout(ui->music_scr);
    }       
}

static void music_play_previous_clicked_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED) 
    {
        TAL_PR_DEBUG("[%s] clicked !!!!!!", __func__);
        music_play_ctrl_previous();
    }       
}

static void music_play_next_clicked_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED) 
    {
        TAL_PR_DEBUG("[%s] clicked !!!!!!", __func__);
        music_play_ctrl_next();
    }       
}

static void music_play_clicked_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED) 
    {
        TAL_PR_DEBUG("[%s] clicked !!!!!!, state: %d", __func__, s_music_player.state);

        if(s_music_player.state == AI_PLAYER_PLAYING)
        {
            //暂停播放
            s_music_player.state = AI_PLAYER_PAUSED;
            wukong_audio_player_pause();
        }
        else if(s_music_player.state == AI_PLAYER_PAUSED)
        {
            //继续播放
            s_music_player.state = AI_PLAYER_PLAYING;
            wukong_audio_player_resume();
        }
    }       
}

static void music_play_list_clicked_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED) 
    {
        TAL_PR_DEBUG("[%s] clicked !!!!!!", __func__);
        desk_handle_ui_switch_to(DHUI_SCREEN_ID_MUSIC_PLAYLIST, LV_SCR_LOAD_ANIM_FADE_ON, DHUI_SWITCH_PERMANENT);
    }       
}

static void music_content_create(void)
{
    lv_music_ui_t *ui = &getContent()->st_func_music;
    ui->content = lv_obj_create(ui->music_scr);
    lv_obj_remove_style_all(ui->content);
    lv_obj_set_size(ui->content, LV_HOR_RES, LV_VER_RES-50);
    lv_obj_set_pos(ui->content, 0, 50);
    lv_obj_set_style_bg_opa(ui->content, LV_OPA_TRANSP, 0);   

    //歌名&&歌手
    ui->song_name = lv_label_create(ui->content);
    lv_obj_remove_style_all(ui->song_name);
    lv_label_set_long_mode(ui->song_name, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(ui->song_name, ""); 
    lv_obj_set_size(ui->song_name, 100, 22);
    lv_obj_set_pos(ui->song_name, 10, 0);
    lv_obj_set_style_text_font(ui->song_name, &AlibabaPuHuiTi3_Regular18_Static, 0);
    lv_obj_set_style_text_color(ui->song_name, lv_color_white(), 0);
    lv_obj_set_style_text_align(ui->song_name, LV_TEXT_ALIGN_LEFT, 0);   

    ui->singer_name = lv_label_create(ui->content);
    lv_obj_remove_style_all(ui->singer_name);
    lv_label_set_long_mode(ui->singer_name, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(ui->singer_name, ""); 
    lv_obj_set_size(ui->singer_name, 100, 22);
    lv_obj_set_pos(ui->singer_name, 10, 22);
    lv_obj_set_style_text_font(ui->singer_name, &AlibabaPuHuiTi3_Regular18_Static, 0);
    lv_obj_set_style_text_color(ui->singer_name, lv_color_hex(0xB8BDDE), 0);
    lv_obj_set_style_text_align(ui->singer_name, LV_TEXT_ALIGN_LEFT, 0);
    
    //播放模式切换
    ui->play_mode_btn = lv_btn_create(ui->content);
    lv_obj_remove_style_all(ui->play_mode_btn);
    lv_obj_set_size(ui->play_mode_btn, 18, 18);
    lv_obj_set_pos(ui->play_mode_btn, 44, 133);
    lv_obj_set_style_bg_opa(ui->play_mode_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_tag(ui->play_mode_btn, NULL);
    lv_obj_add_event_cb(ui->play_mode_btn, music_play_mode_clicked_event, LV_EVENT_CLICKED, NULL);
    char *play_mode_icon_path = get_play_mdoe_icon_path(s_music_play_mode);
    music_play_ctrl_switch_mode(s_music_play_mode);
    if(png_img_load(tuya_app_gui_get_picture_full_path(play_mode_icon_path), &s_music_res.play_mode_icon) == 0) 
    {
        ui->play_mode_img = lv_img_create(ui->play_mode_btn);
        lv_img_set_src(ui->play_mode_img, &s_music_res.play_mode_icon);
        lv_obj_set_pos(ui->play_mode_img, 0, 0);
        lv_obj_set_size(ui->play_mode_img, 18, 18);
    }

    //上一首
    lv_obj_t *play_previous_btn = lv_btn_create(ui->content);
    lv_obj_remove_style_all(play_previous_btn);
    lv_obj_set_size(play_previous_btn, 32, 32);
    lv_obj_set_pos(play_previous_btn, 102, 126);
    lv_obj_set_style_bg_opa(play_previous_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_tag(play_previous_btn, NULL);
    lv_obj_add_event_cb(play_previous_btn, music_play_previous_clicked_event, LV_EVENT_CLICKED, NULL);
    if(png_img_load(tuya_app_gui_get_picture_full_path(ICON_PLAY_PREVIOUS), &s_music_res.play_previous_icon) == 0) 
    {
        lv_obj_t *play_previous_icon = lv_img_create(play_previous_btn);
        lv_img_set_src(play_previous_icon, &s_music_res.play_previous_icon);
        lv_obj_set_pos(play_previous_icon, 0, 0);
        lv_obj_set_size(play_previous_icon, 32, 32);    
    }   
    
    //播放/暂停
    ui->play_music_btn = lv_btn_create(ui->content);
    lv_obj_remove_style_all(ui->play_music_btn);
    lv_obj_set_size(ui->play_music_btn, 32, 32);
    lv_obj_set_pos(ui->play_music_btn, 144, 126);
    lv_obj_set_style_bg_opa(ui->play_music_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_tag(ui->play_music_btn, NULL);
    lv_obj_add_event_cb(ui->play_music_btn, music_play_clicked_event, LV_EVENT_CLICKED, NULL);
    char *play_stat_icon_path = get_player_state_img_path(s_music_player.state);
    if(png_img_load(tuya_app_gui_get_picture_full_path(play_stat_icon_path), &s_music_res.play_music_icon) == 0) 
    {
        ui->play_music_icon = lv_img_create(ui->play_music_btn);
        lv_img_set_src(ui->play_music_icon, &s_music_res.play_music_icon);
        lv_obj_set_pos(ui->play_music_icon, 0, 0);
        lv_obj_set_size(ui->play_music_icon, 32, 32);    
    }

    //下一首
    lv_obj_t *play_next_btn = lv_btn_create(ui->content);
    lv_obj_remove_style_all(play_next_btn);
    lv_obj_set_size(play_next_btn, 32, 32);
    lv_obj_set_pos(play_next_btn, 186, 126);
    lv_obj_set_style_bg_opa(play_next_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_tag(play_next_btn, NULL);
    lv_obj_add_event_cb(play_next_btn, music_play_next_clicked_event, LV_EVENT_CLICKED, NULL);
    if(png_img_load(tuya_app_gui_get_picture_full_path(ICON_PLAY_NEXT), &s_music_res.play_next_icon) == 0) 
    {
        lv_obj_t *play_next_icon = lv_img_create(play_next_btn);
        lv_img_set_src(play_next_icon, &s_music_res.play_next_icon);
        lv_obj_set_pos(play_next_icon, 0, 0);
        lv_obj_set_size(play_next_icon, 32, 32);
    }

    //播放列表
    lv_obj_t *play_list_btn = lv_btn_create(ui->content);
    lv_obj_remove_style_all(play_list_btn);
    lv_obj_set_size(play_list_btn, 18, 18);
    lv_obj_set_pos(play_list_btn, 260, 133);
    lv_obj_set_style_bg_opa(play_list_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_tag(play_list_btn, NULL);
    lv_obj_add_event_cb(play_list_btn, music_play_list_clicked_event, LV_EVENT_CLICKED, NULL);
    if(png_img_load(tuya_app_gui_get_picture_full_path(ICON_PLAY_LIST), &s_music_res.play_list_icon) == 0) 
    {
        lv_obj_t *play_list_icon = lv_img_create(play_list_btn);
        lv_img_set_src(play_list_icon, &s_music_res.play_list_icon);
        lv_obj_set_pos(play_list_icon, 0, 0);
        lv_obj_set_size(play_list_icon, 18, 18);
    }

}   

static void music_title_create(void)
{
    lv_music_ui_t *ui = &getContent()->st_func_music;
    ui->title = lv_obj_create(ui->music_scr);
    lv_obj_remove_style_all(ui->title);
    lv_obj_set_size(ui->title, LV_HOR_RES, 50);
    lv_obj_set_pos(ui->title, 0, 0);
    lv_obj_set_style_bg_opa(ui->title, LV_OPA_TRANSP, 0);

    lv_obj_t *title_name = lv_label_create(ui->title);
    lv_label_set_text(title_name, "音乐");
    lv_obj_set_style_text_font(title_name, &AlibabaPuHuiTi3_Regular18_Static, 0);
    lv_obj_set_style_text_color(title_name, lv_color_white(), 0);
    lv_obj_set_size(title_name, LV_SIZE_CONTENT, 20);
    lv_obj_align(title_name, LV_ALIGN_CENTER, 0, 0);    //相对于父对象居中
    lv_obj_set_style_text_align(title_name, LV_TEXT_ALIGN_CENTER, 0);   //label内部文本居中

    lv_obj_t *back_btn = lv_btn_create(ui->title);
    lv_obj_remove_style_all(back_btn);
    lv_obj_set_size(back_btn, 50, 50);
    lv_obj_set_pos(back_btn, 0, 0);
    lv_obj_set_style_bg_opa(back_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_tag(back_btn, NULL);
    lv_obj_add_event_cb(back_btn, music_back_event, LV_EVENT_CLICKED, NULL);

    if(png_img_load(tuya_app_gui_get_picture_full_path(ICON_BACK_LEFT_24_24), &s_music_res.back_icon) == 0) 
    {
        lv_obj_t *back_icon = lv_img_create(back_btn);
        lv_img_set_src(back_icon, &s_music_res.back_icon);
        lv_obj_set_pos(back_icon, 13, 13);
        lv_obj_set_size(back_icon, 24, 24);
    }

}

void setup_music_scr(void)
{
    TAL_PR_INFO("[%s] enter ", __func__);

    lv_music_ui_t *ui = &getContent()->st_func_music;
    s_music_main_ui_generation++;
    ui->music_scr = lv_obj_create(NULL);
    lv_obj_set_size(ui->music_scr, LV_HOR_RES, LV_VER_RES); 
    lv_obj_set_style_pad_all(ui->music_scr, 0, 0);
    lv_obj_set_scrollbar_mode(ui->music_scr, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(ui->music_scr, LV_DIR_NONE);

    if(png_img_load(tuya_app_gui_get_picture_full_path(ICON_MUSIC_BACK_GROUND), &s_music_res.back_ground_icon) == 0) 
    {
        lv_obj_t *back_top_icon = lv_img_create(ui->music_scr);
        lv_img_set_src(back_top_icon, &s_music_res.back_ground_icon);
        lv_obj_set_pos(back_top_icon, 0, 0);
        lv_obj_set_size(back_top_icon, 320, 240);
    }

    music_title_create();   //创建标题栏

    music_content_create(); //创建内容区域

    music_info_update();

    lv_obj_update_layout(ui->music_scr);

    setDeskUIIndex(DESKUI_INDEX_MUSIC);

}

void music_scr_res_clear(void)
{
    lv_music_ui_t *ui = &getContent()->st_func_music;

    TAL_PR_INFO("[%s] enter ", __func__);
    s_music_main_ui_generation++;
    png_img_unload(&s_music_res.back_ground_icon);

    png_img_unload(&s_music_res.back_icon);
    
    png_img_unload(&s_music_res.play_mode_icon);
    png_img_unload(&s_music_res.play_previous_icon);
    png_img_unload(&s_music_res.play_next_icon);
    png_img_unload(&s_music_res.play_list_icon);
    png_img_unload(&s_music_res.play_music_icon);
    png_img_unload(&s_music_res.playlist_play_icon);
    png_img_unload(&s_music_res.playlist_pause_icon);

    memset(&s_music_res, 0, sizeof(music_scr_res_t));
    ui->music_scr = NULL;
    ui->title = NULL;
    ui->content = NULL;
    ui->song_name = NULL;
    ui->singer_name = NULL;
    ui->play_mode_btn = NULL;
    ui->play_mode_img = NULL;
    ui->play_music_btn = NULL;
    ui->play_music_icon = NULL;
}


static void music_playlist_back_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED) 
    {
        desk_handle_ui_back(LV_SCR_LOAD_ANIM_FADE_ON, DHUI_SWITCH_PERMANENT);
    }       
}

static void music_playlist_item_clicked_event(lv_event_t *e)
{
    int music_id = (int)(uintptr_t)lv_event_get_user_data(e);
    OPERATE_RET rt = OPRT_OK;

    TAL_PR_DEBUG("[%s] clicked music id: %d", __func__, music_id);

    rt = wukong_playback_playlist_play_async(music_id);
    if (rt != OPRT_OK) {
        TAL_PR_WARN("[%s] play_async failed id:%d rt:%d", __func__, music_id, rt);
    }
}

static void music_playlist_item_delete_event(lv_event_t *e)
{
    lv_indev_t *indev = lv_indev_get_act();
    int music_id = (int)(uintptr_t)lv_event_get_user_data(e);
    OPERATE_RET rt = OPRT_OK;

    TAL_PR_INFO("[%s] delete music id: %d", __func__, music_id);

    rt = wukong_playback_playlist_remove(music_id);
    if (rt != OPRT_OK) {
        TAL_PR_WARN("[%s] delete music failed, id: %d", __func__, music_id);
        return;
    }

    if (indev != NULL) {
        lv_indev_wait_release(indev);
    }
}

static void music_playlist_item_delete_btn_create(lv_obj_t *parent, int music_id)
{
    lv_obj_t *delete_btn = NULL;
    lv_obj_t *delete_label = NULL;

    if (parent == NULL) {
        return;
    }

    delete_btn = lv_btn_create(parent);
    lv_obj_remove_style_all(delete_btn);
    lv_obj_set_size(delete_btn, 52, 28);
    lv_obj_align(delete_btn, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_radius(delete_btn, 14, 0);
    lv_obj_set_style_bg_opa(delete_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(delete_btn, lv_color_hex(0x4B4D59), 0);
    lv_obj_set_style_border_width(delete_btn, 0, 0);
    lv_obj_add_event_cb(delete_btn, music_playlist_item_delete_event, LV_EVENT_CLICKED, (void *)(uintptr_t)music_id);

    delete_label = lv_label_create(delete_btn);
    lv_obj_remove_style_all(delete_label);
    lv_label_set_text(delete_label, "删除");
    lv_obj_center(delete_label);
    lv_obj_set_style_text_font(delete_label, &AlibabaPuHuiTi3_Regular18_Static, 0);
    lv_obj_set_style_text_color(delete_label, lv_color_hex(0xF3E55D), 0);
}

static void music_playlist_list_create(void)
{
    lv_music_ui_t *ui = &getContent()->st_func_music;
    ty_cJSON *list = NULL;
    INT_T cur_id = -1;
    INT_T n = 0;
    INT_T i = 0;
    BOOL_T has_music = FALSE;
    BOOL_T play_icon_ready = FALSE;
    BOOL_T pause_icon_ready = FALSE;
    OPERATE_RET rt = OPRT_OK;

    lv_obj_clean(ui->music_playlist_list_cont);
    lv_obj_set_flex_flow(ui->music_playlist_list_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(ui->music_playlist_list_cont, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(ui->music_playlist_list_cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_ver(ui->music_playlist_list_cont, 12, 0);
    lv_obj_set_style_pad_hor(ui->music_playlist_list_cont, 20, 0);
    lv_obj_set_style_pad_row(ui->music_playlist_list_cont, 10, 0);
    lv_obj_set_style_pad_column(ui->music_playlist_list_cont, 0, 0);
    lv_obj_set_style_border_width(ui->music_playlist_list_cont, 0, 0);

    rt = wukong_playback_playlist_list(&list);
    if (rt != OPRT_OK || list == NULL) {
        if (list != NULL) {
            ty_cJSON_Delete(list);
        }
        goto empty_label;
    }
    if (!ty_cJSON_IsArray(list)) {
        ty_cJSON_Delete(list);
        list = NULL;
        goto empty_label;
    }

    cur_id = wukong_playback_ctrl_get_current_play_id();

    png_img_unload(&s_music_res.playlist_play_icon);
    if (png_img_load(tuya_app_gui_get_picture_full_path(ICON_PLAY_MUSIC), &s_music_res.playlist_play_icon) == 0) {
        play_icon_ready = TRUE;
    }

    png_img_unload(&s_music_res.playlist_pause_icon);
    if (png_img_load(tuya_app_gui_get_picture_full_path(ICON_PAUSE_PLAY), &s_music_res.playlist_pause_icon) == 0) {
        pause_icon_ready = TRUE;
    }

    n = ty_cJSON_GetArraySize(list);
    for (i = 0; i < n; i++) {
        ty_cJSON *row = ty_cJSON_GetArrayItem(list, i);
        ty_cJSON *j_id = NULL;
        ty_cJSON *j_name = NULL;
        ty_cJSON *j_artist = NULL;
        CONST CHAR_T *song = NULL;
        CONST CHAR_T *singer = NULL;
        INT_T music_id = -1;
        BOOL_T is_current = FALSE;
        lv_obj_t *item_cont = NULL;
        lv_obj_t *song_name = NULL;
        lv_obj_t *artist = NULL;
        lv_obj_t *state_icon = NULL;
        lv_img_dsc_t *state_icon_src = NULL;

        if (row == NULL || !ty_cJSON_IsObject(row)) {
            continue;
        }

        j_id = ty_cJSON_GetObjectItem(row, "id");
        j_name = ty_cJSON_GetObjectItem(row, "song_name");
        j_artist = ty_cJSON_GetObjectItem(row, "artist");
        music_id = (j_id != NULL && ty_cJSON_IsNumber(j_id)) ? j_id->valueint : -1;
        song = (j_name != NULL && ty_cJSON_IsString(j_name)) ? j_name->valuestring : "";
        singer = (j_artist != NULL && ty_cJSON_IsString(j_artist)) ? j_artist->valuestring : "";

        has_music = TRUE;
        is_current = (music_id >= 0 && music_id == cur_id);

        item_cont = lv_obj_create(ui->music_playlist_list_cont);
        lv_obj_remove_style_all(item_cont);
        lv_obj_set_size(item_cont, 280, 72);
        lv_obj_add_flag(item_cont, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_radius(item_cont, 16, 0);
        lv_obj_set_style_bg_opa(item_cont, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(item_cont, is_current ? lv_color_hex(0x3A3B46) : lv_color_hex(0x353740), 0);
        lv_obj_set_style_pad_left(item_cont, 16, 0);
        lv_obj_set_style_pad_right(item_cont, 16, 0);
        lv_obj_set_style_pad_top(item_cont, 12, 0);
        lv_obj_set_style_pad_bottom(item_cont, 12, 0);
        lv_obj_set_style_border_width(item_cont, 0, 0);
        lv_obj_set_scrollbar_mode(item_cont, LV_SCROLLBAR_MODE_OFF);
        lv_obj_set_scroll_dir(item_cont, LV_DIR_NONE);
        lv_obj_add_event_cb(item_cont, music_playlist_item_clicked_event, LV_EVENT_CLICKED, (void *)(uintptr_t)music_id);

        song_name = lv_label_create(item_cont);
        lv_obj_remove_style_all(song_name);
        lv_label_set_long_mode(song_name, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_label_set_text(song_name, (song != NULL && song[0] != '\0') ? song : "未知歌曲");
        lv_obj_set_size(song_name, 120, 24);
        lv_obj_set_pos(song_name, 0, 0);
        lv_obj_set_style_text_font(song_name, &AlibabaPuHuiTi3_Regular18_Static, 0);
        lv_obj_set_style_text_color(song_name, is_current ? lv_color_hex(0xF3E55D) : lv_color_white(), 0);
        lv_obj_set_style_text_align(song_name, LV_TEXT_ALIGN_LEFT, 0);

        artist = lv_label_create(item_cont);
        lv_obj_remove_style_all(artist);
        lv_label_set_long_mode(artist, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_label_set_text(artist, (singer != NULL && singer[0] != '\0') ? singer : "未知歌手");
        lv_obj_set_size(artist, 120, 22);
        lv_obj_set_pos(artist, 0, 28);
        lv_obj_set_style_text_font(artist, &AlibabaPuHuiTi3_Regular18_Static, 0);
        lv_obj_set_style_text_color(artist, lv_color_hex(0xB8BDDE), 0);
        lv_obj_set_style_text_align(artist, LV_TEXT_ALIGN_LEFT, 0);

        if (is_current && s_music_player.state == AI_PLAYER_PLAYING) {
            state_icon_src = pause_icon_ready ? &s_music_res.playlist_play_icon : NULL;
        } else {
            state_icon_src = play_icon_ready ? &s_music_res.playlist_pause_icon : NULL;
        }

        if (state_icon_src != NULL) {
            state_icon = lv_img_create(item_cont);
            lv_img_set_src(state_icon, state_icon_src);
            lv_obj_set_size(state_icon, 32, 32);
            lv_obj_align(state_icon, LV_ALIGN_RIGHT_MID, -76, 0);
        }

        music_playlist_item_delete_btn_create(item_cont, music_id);
    }

    ty_cJSON_Delete(list);
    list = NULL;

empty_label:
    if (has_music == FALSE) {
        lv_obj_t *empty_label = lv_label_create(ui->music_playlist_list_cont);
        lv_obj_remove_style_all(empty_label);
        lv_label_set_text(empty_label, "暂无歌曲");
        lv_obj_set_size(empty_label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_text_font(empty_label, &AlibabaPuHuiTi3_Regular18_Static, 0);
        lv_obj_set_style_text_color(empty_label, lv_color_hex(0xB8BDDE), 0);
        lv_obj_set_style_text_align(empty_label, LV_TEXT_ALIGN_CENTER, 0);
    }
}

void setup_music_playlist_scr(void)
{
    lv_music_ui_t *ui = &getContent()->st_func_music;
    s_music_playlist_ui_generation++;
    ui->music_playlist_scr = lv_obj_create(NULL);
    lv_obj_set_size(ui->music_playlist_scr, LV_HOR_RES, LV_VER_RES); 
    lv_obj_set_style_bg_color(ui->music_playlist_scr, lv_color_hex(0x25262A), 0);
    lv_obj_set_style_pad_all(ui->music_playlist_scr, 0, 0);
    lv_obj_set_scrollbar_mode(ui->music_playlist_scr, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(ui->music_playlist_scr, LV_DIR_NONE);

    ui->music_playlist_title = lv_obj_create(ui->music_playlist_scr);
    lv_obj_remove_style_all(ui->music_playlist_title);
    lv_obj_set_size(ui->music_playlist_title, LV_HOR_RES, 50);
    lv_obj_set_pos(ui->music_playlist_title, 0, 0);
    lv_obj_set_style_bg_opa(ui->music_playlist_title, LV_OPA_TRANSP, 0);

    lv_obj_t *title_name = lv_label_create(ui->music_playlist_title);
    lv_label_set_text(title_name, "所有歌曲");
    lv_obj_set_style_text_font(title_name, &AlibabaPuHuiTi3_Regular18_Static, 0);
    lv_obj_set_style_text_color(title_name, lv_color_white(), 0);
    lv_obj_set_size(title_name, LV_SIZE_CONTENT, 20);
    lv_obj_align(title_name, LV_ALIGN_CENTER, 0, 0);    //相对于父对象居中
    lv_obj_set_style_text_align(title_name, LV_TEXT_ALIGN_CENTER, 0);   //label内部文本居中

    lv_obj_t *back_btn = lv_btn_create(ui->music_playlist_title);
    lv_obj_remove_style_all(back_btn);
    lv_obj_set_size(back_btn, 50, 50);
    lv_obj_set_pos(back_btn, 0, 0);
    lv_obj_set_style_bg_opa(back_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_tag(back_btn, NULL);
    lv_obj_add_event_cb(back_btn, music_playlist_back_event, LV_EVENT_CLICKED, NULL);

    if(png_img_load(tuya_app_gui_get_picture_full_path(ICON_BACK_LEFT_24_24), &s_music_res.back_icon) == 0) 
    {
        lv_obj_t *back_icon = lv_img_create(back_btn);
        lv_img_set_src(back_icon, &s_music_res.back_icon);
        lv_obj_set_pos(back_icon, 13, 13);
        lv_obj_set_size(back_icon, 24, 24);
    }

    ui->music_playlist_list_cont = lv_obj_create(ui->music_playlist_scr);
    lv_obj_remove_style_all(ui->music_playlist_list_cont);
    lv_obj_set_size(ui->music_playlist_list_cont, LV_HOR_RES, LV_VER_RES-50);
    lv_obj_set_pos(ui->music_playlist_list_cont, 0, 50);
    lv_obj_set_style_bg_opa(ui->music_playlist_list_cont, LV_OPA_TRANSP, 0);   

    music_playlist_list_create();

    lv_obj_update_layout(ui->music_playlist_scr);
    setDeskUIIndex(DESKUI_INDEX_MUSIC_PLAYLIST);
}

void music_playlist_scr_res_clear(void)
{
    lv_music_ui_t *ui = &getContent()->st_func_music;

    s_music_playlist_ui_generation++;
    png_img_unload(&s_music_res.back_icon);
    png_img_unload(&s_music_res.playlist_play_icon);
    png_img_unload(&s_music_res.playlist_pause_icon);
    memset(&s_music_res, 0, sizeof(music_scr_res_t));
    ui->music_playlist_scr = NULL;
    ui->music_playlist_title = NULL;
    ui->music_playlist_list_cont = NULL;
}

static void __desktop_playlist_ui_refresh_async(void *arg)
{
    (void)arg;
    music_refresh_visible_ui();
}

static OPERATE_RET __desktop_playlist_save(CONST ty_cJSON *playlist_json)
{
    CHAR_T *json_str = NULL;
    TUYA_FILE fp = NULL;
    INT_T write_size = 0;
    OPERATE_RET rt = OPRT_OK;

    TUYA_CHECK_NULL_RETURN(playlist_json, OPRT_INVALID_PARM);

    json_str = ty_cJSON_PrintUnformatted((ty_cJSON *)playlist_json);
    if (json_str == NULL) {
        return OPRT_MALLOC_FAILED;
    }

    (void)tkl_fs_mkdir("/t5_fs/music");
    fp = tkl_fopen(DESKTOP_MUSIC_LIST_JSON_PATH, "wb");
    if (fp == NULL) {
        TAL_PR_ERR("[%s] open %s failed", __func__, DESKTOP_MUSIC_LIST_JSON_PATH);
        ty_cJSON_FreeBuffer(json_str);
        return OPRT_FILE_OPEN_FAILED;
    }

    write_size = (INT_T)strlen(json_str);
    if (tkl_fwrite(json_str, (UINT_T)write_size, fp) != (UINT_T)write_size) {
        TAL_PR_ERR("[%s] write failed", __func__);
        rt = OPRT_COM_ERROR;
    }
    tkl_fclose(fp);
    ty_cJSON_FreeBuffer(json_str);
    return rt;
}

static OPERATE_RET __desktop_playlist_load(ty_cJSON **playlist_json)
{
    ty_cJSON *parsed = NULL;
    ty_cJSON *list = NULL;
    CHAR_T *json_buf = NULL;
    INT_T file_size = 0;
    TUYA_FILE fp = NULL;

    TUYA_CHECK_NULL_RETURN(playlist_json, OPRT_INVALID_PARM);
    *playlist_json = NULL;

    if (tkl_faccess(DESKTOP_MUSIC_LIST_JSON_PATH, 0) != 0) {
        return OPRT_OK;
    }

    file_size = (INT_T)tkl_fgetsize(DESKTOP_MUSIC_LIST_JSON_PATH);
    if (file_size <= 0) {
        TAL_PR_WARN("[%s] invalid file size: %d", __func__, file_size);
        return OPRT_OK;
    }

    json_buf = (CHAR_T *)tal_malloc((UINT_T)file_size + 1U);
    if (json_buf == NULL) {
        return OPRT_MALLOC_FAILED;
    }
    memset(json_buf, 0, (UINT_T)file_size + 1U);

    fp = tkl_fopen(DESKTOP_MUSIC_LIST_JSON_PATH, "rb");
    if (fp == NULL) {
        tal_free(json_buf);
        return OPRT_FILE_OPEN_FAILED;
    }

    if (tkl_fread(json_buf, (UINT_T)file_size, fp) != (UINT_T)file_size) {
        TAL_PR_ERR("[%s] read failed", __func__);
        tkl_fclose(fp);
        tal_free(json_buf);
        return OPRT_COM_ERROR;
    }
    tkl_fclose(fp);

    parsed = ty_cJSON_Parse(json_buf);
    tal_free(json_buf);
    if (parsed == NULL) {
        return OPRT_CJSON_PARSE_ERR;
    }

    if (ty_cJSON_IsArray(parsed)) {
        *playlist_json = parsed;
        return OPRT_OK;
    }

    if (ty_cJSON_IsObject(parsed)) {
        list = ty_cJSON_GetObjectItem(parsed, "list");
        if (list != NULL && ty_cJSON_IsArray(list)) {
            *playlist_json = ty_cJSON_Duplicate(list, TRUE);
            ty_cJSON_Delete(parsed);
            if (*playlist_json == NULL) {
                return OPRT_MALLOC_FAILED;
            }
            return OPRT_OK;
        }
    }

    ty_cJSON_Delete(parsed);
    return OPRT_CJSON_PARSE_ERR;
}

static VOID __desktop_playlist_on_changed(VOID)
{
    lv_vendor_disp_lock();
    lv_async_call(__desktop_playlist_ui_refresh_async, NULL);
    lv_vendor_disp_unlock();
}

CONST WUKONG_PLAYBACK_STORAGE_OPS_T g_desktop_playlist_storage_ops = {
    .save = __desktop_playlist_save,
    .load = __desktop_playlist_load,
    .on_changed = __desktop_playlist_on_changed,
    .max_items = 20,
};
#endif /* ENABLE_TOOLKITS_PLAYBACK */