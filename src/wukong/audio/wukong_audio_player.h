#ifndef __WUKONG_AUDIO_PLAYER_H__
#define __WUKONG_AUDIO_PLAYER_H__

#include "tuya_cloud_types.h"
#include "wukong_ai_agent.h"
#include "wukong_ai_skills.h"
#include "skill_cloudevent.h"
#include "skill_music_story.h"

typedef enum {
    AI_TOY_ALERT_TYPE_POWER_ON,             /* 开机上电播报 */
    AI_TOY_ALERT_TYPE_NOT_ACTIVE,           /* 还未配网 请先配网 */
    AI_TOY_ALERT_TYPE_NETWORK_CFG,          /* 进入配网状态，开始配网 */
    AI_TOY_ALERT_TYPE_NETWORK_CONNECTED,    /* 联网成功 */
    AI_TOY_ALERT_TYPE_NETWORK_FAIL,         /* 联网失败 重试 */
    AI_TOY_ALERT_TYPE_NETWORK_DISCONNECT,   /* 掉网 */
    AI_TOY_ALERT_TYPE_BATTERY_LOW,          /* 电量不足 */
    AI_TOY_ALERT_TYPE_PLEASE_AGAIN,         /* 请再说一遍 */
    AI_TOY_ALERT_TYPE_LONG_KEY_TALK,        /* 长按键对话 */
    AI_TOY_ALERT_TYPE_KEY_TALK,             /* 按键对话 */
    AI_TOY_ALERT_TYPE_WAKEUP_TALK,          /* 唤醒对话 */
    AI_TOY_ALERT_TYPE_RANDOM_TALK,          /* 任意对话 */
    AI_TOY_ALERT_TYPE_WAKEUP,               /* 你好我在*/
    AI_TOY_ALERT_TYPE_MAX,
} TY_AI_TOY_ALERT_TYPE_E;

typedef enum {
    AI_PLAYER_FG = 0,   // frontground player, used to play tts
    AI_PLAYER_BG = 1,   // background player, used to play music
    AI_PLAYER_ALL = 2,  // all player
} TY_AI_TOY_PLAYER_TYPE_E;

/** @brief Play raw audio data
 *  @param[in] format  Audio format identifier
 *  @param[in] data    Pointer to the encoded audio data
 *  @param[in] len     Length of the audio data in bytes
 *  @return OPRT_OK on success */
OPERATE_RET wukong_audio_play_data(INT_T format, CONST CHAR_T *data, INT_T len);

/** @brief Play a TTS audio stream
 *  @param[in] type   AI event type that triggered the TTS
 *  @param[in] codec  Audio codec of the incoming data (MP3, WAV, Opus, etc.)
 *  @param[in] data   Pointer to the encoded audio data chunk
 *  @param[in] len    Length of the audio data in bytes
 *  @return OPRT_OK on success */
OPERATE_RET wukong_audio_play_tts_stream(WUKONG_AI_EVENT_TYPE_E type, AI_AUDIO_CODEC_E codec, CHAR_T *data, INT_T len);

/** @brief Play TTS from a URL
 *  @param[in] tts      TTS descriptor (URL, codec, etc.)
 *  @param[in] is_loop  TRUE to loop playback, FALSE for single play
 *  @return OPRT_OK on success */
OPERATE_RET wukong_audio_play_tts_url(WUKONG_AI_PLAYTTS_T *tts, BOOL_T is_loop);

/** @brief Start playing a music track
 *  @param[in] music  Music descriptor (URL, codec, etc.)
 *  @return OPRT_OK on success */
OPERATE_RET wukong_audio_play_music(WUKONG_AI_MUSIC_T *music);

/** @brief Play a local audio file
 *  @param[in] url        File path or URL of the audio
 *  @param[in] song_name  Song name for display (may be NULL)
 *  @param[in] artist     Artist name for display (may be NULL)
 *  @param[in] format     Audio format identifier
 *  @param[in] size       File size in bytes
 *  @return OPRT_OK on success */
OPERATE_RET wukong_audio_play_local(CHAR_T *url, CHAR_T *song_name, CHAR_T *artist, INT_T format, INT_T size);

/** @brief Initialize the audio player subsystem
 *  @return OPRT_OK on success */
OPERATE_RET wukong_audio_player_init();

/** @brief Deinitialize the audio player and release resources
 *  @return OPRT_OK on success */
OPERATE_RET wukong_audio_player_deinit(VOID);

/** @brief Stop a player
 *  @param[in] type  Which player to stop (FG / BG / ALL)
 *  @return OPRT_OK on success */
OPERATE_RET wukong_audio_player_stop(TY_AI_TOY_PLAYER_TYPE_E type);

/** @brief Get current audio player volume
 *  @param[out] vol  Pointer to store volume level (0-100)
 *  @return OPRT_OK on success */
OPERATE_RET wukong_audio_player_get_vol(UINT8_T *vol);

/** @brief Set audio player volume
 *  @param[in] vol  Volume level (0-100)
 *  @return OPRT_OK on success */
OPERATE_RET wukong_audio_player_set_vol(UINT8_T vol);

/** @brief Check if any player is currently active
 *  @return TRUE if playing, FALSE otherwise */
BOOL_T wukong_audio_player_is_playing();

/** @brief Resume background music playback
 *  @return OPRT_OK on success */
OPERATE_RET wukong_audio_player_resume(VOID);

/** @brief Pause background music playback
 *  @return OPRT_OK on success */
OPERATE_RET wukong_audio_player_pause(VOID);

/** @brief Replay the current background music track from the beginning
 *  @return OPRT_OK on success */
OPERATE_RET wukong_audio_player_replay(VOID);

/** @brief Play an alert / notification sound
 *  @param[in] type      Alert type (e.g. power-on, low battery, network status)
 *  @param[in] send_eof  TRUE to send EOF event after alert finishes
 *  @return OPRT_OK on success */
OPERATE_RET wukong_audio_player_alert(TY_AI_TOY_ALERT_TYPE_E type, BOOL_T send_eof);

#endif  // __WUKONG_AUDIO_PLAYER_H__