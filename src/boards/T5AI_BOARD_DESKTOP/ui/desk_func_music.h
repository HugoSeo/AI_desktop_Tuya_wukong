#ifndef __DESK_FUNC_MUSIC_H__
#define __DESK_FUNC_MUSIC_H__ 
#include "desk_ui_res.h"
#include "desk_event_handle.h"

#define MUSIC_SCR_PLAY_MODE_SINGLE_LOOP 0
#define MUSIC_SCR_PLAY_MODE_LIST_LOOP 1
#define MUSIC_SCR_PLAY_MODE_RANDOM 2
#define MUSIC_SCR_PLAY_MODE_MAX 3

#define MUSIC_SCR_MUSIC_PAUSE 0
#define MUSIC_SCR_MUSIC_PLAY 1

typedef struct 
{
    lv_img_dsc_t back_ground_icon;

    lv_img_dsc_t back_icon;

    lv_img_dsc_t play_mode_icon;
    lv_img_dsc_t play_previous_icon;
    lv_img_dsc_t play_next_icon;
    lv_img_dsc_t play_list_icon;
    lv_img_dsc_t play_music_icon;
    lv_img_dsc_t playlist_play_icon;
    lv_img_dsc_t playlist_pause_icon;
}music_scr_res_t;

typedef struct
{
    lv_obj_t *music_scr;
    lv_obj_t *title;
    lv_obj_t *content;

    lv_obj_t *song_name;
    lv_obj_t *singer_name;

    lv_obj_t *play_mode_btn;
    lv_obj_t *play_mode_img;

    lv_obj_t *play_music_btn;
    lv_obj_t *play_music_icon;

    lv_obj_t *music_playlist_scr;
    lv_obj_t *music_playlist_title;
    lv_obj_t *music_playlist_list_cont;
}lv_music_ui_t;

void setup_music_scr(void);

void setup_music_playlist_scr(void);

void music_scr_res_clear(void);

void music_playlist_scr_res_clear(void);

int music_player_event(void *data);

int music_player_interrupt(void *data);

#endif //__DESK_FUNC_MUSIC_H__