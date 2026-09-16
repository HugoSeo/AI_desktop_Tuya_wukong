/**
 * @file acoustic_test.c
 * @brief Acoustic test CLI: play WAV and record audio dump files.
 * @version 1.1
 * @date 2026-04-13
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio_dump.h"
#include "svc_ai_player.h"
#include "tal_system.h"
#include "tal_workq_service.h"
#include "tkl_fs.h"
#include "wukong_audio_output.h"
#include "wukong_audio_player.h"

extern void bk_printf(const char *fmt, ...);

#define ATEST_DEFAULT_DIR       "/atest"
#define ATEST_DEFAULT_DUMP_DIR  "/atest/dump/"

STATIC DELAYED_WORK_HANDLE s_stop_delayed_work = NULL;

STATIC VOID __build_path(CHAR_T *out, INT_T out_size, CONST CHAR_T *arg, CONST CHAR_T *default_dir)
{
    if (arg[0] == '/') {
        snprintf(out, out_size, "%s", arg);
    } else {
        snprintf(out, out_size, "%s/%s", default_dir, arg);
    }
}

STATIC VOID __build_dir_path(CHAR_T *out, INT_T out_size, CONST CHAR_T *arg, CONST CHAR_T *default_dir)
{
    INT_T len;

    __build_path(out, out_size, arg, default_dir);
    len = strlen(out);
    if (len > 0 && len < out_size - 1 && out[len - 1] != '/') {
        out[len] = '/';
        out[len + 1] = '\0';
    }
}

STATIC VOID_T __record_timeout_cb(VOID_T *data)
{
    (void)data;

    if (audio_dump_get_channel() == AUDIO_DUMP_CH_SDCARD) {
        bk_printf("\r\n======== atest: record duration reached, auto stop ========\r\n");
        audio_dump_set_channel(AUDIO_DUMP_CH_OFF, NULL);
    }
}

STATIC VOID __list_dir(CONST CHAR_T *path)
{
    TUYA_DIR dir;
    TUYA_FILEINFO info;
    CONST CHAR_T *name = NULL;
    BOOL_T is_dir = FALSE;

    if (tkl_dir_open(path, &dir) != 0) {
        bk_printf("open dir %s failed\r\n", path);
        return;
    }

    bk_printf("[%s]\r\n", path);
    while (tkl_dir_read(dir, &info) == 0) {
        if (tkl_dir_name(info, &name) != 0 || name == NULL) {
            break;
        }

        tkl_dir_is_directory(info, &is_dir);
        bk_printf("  %s%s\r\n", name, is_dir ? "/" : "");
    }
    tkl_dir_close(dir);
}

void cli_acoustic_test_cmd(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv)
{
    (void)pcWriteBuffer;
    (void)xWriteBufferLen;

    if (argc < 2) {
        bk_printf("usage:\r\n");
        bk_printf("  atest record [duration_s] [dir]  record audio dump files\r\n");
        bk_printf("  atest play <wav>                 play audio\r\n");
        bk_printf("  atest pstop                      stop playback only\r\n");
        bk_printf("  atest stop                       stop record + play\r\n");
        bk_printf("  atest vol [0-100]                get/set system volume\r\n");
        bk_printf("  atest gain [0-100]               set hw output gain\r\n");
        bk_printf("  atest status                     show recording state\r\n");
        bk_printf("  atest ls [dir]                   list directory\r\n");
        bk_printf("  atest reboot                     reboot device\r\n");
        bk_printf("defaults:\r\n");
        bk_printf("  audio dir:  %s\r\n", ATEST_DEFAULT_DIR);
        bk_printf("  record dir: %s\r\n", ATEST_DEFAULT_DUMP_DIR);
        bk_printf("example:\r\n");
        bk_printf("  atest record 80                  record 80s, auto-stop\r\n");
        bk_printf("  atest record                     record until 'atest stop'\r\n");
        bk_printf("  atest play 111.wav               play /atest/111.wav\r\n");
        return;
    }

    if (0 == strcmp(argv[1], "record")) {
        CONST CHAR_T *rec_dir = ATEST_DEFAULT_DUMP_DIR;
        CHAR_T rec_path[128];
        INT_T duration_s = 0;
        OPERATE_RET rt;

        if (argc >= 3 && atoi(argv[2]) > 0) {
            duration_s = atoi(argv[2]);
            if (argc >= 4) {
                __build_dir_path(rec_path, sizeof(rec_path), argv[3], ATEST_DEFAULT_DIR);
                rec_dir = rec_path;
            }
        } else if (argc >= 3) {
            __build_dir_path(rec_path, sizeof(rec_path), argv[2], ATEST_DEFAULT_DIR);
            rec_dir = rec_path;
        }

        rt = audio_dump_set_channel(AUDIO_DUMP_CH_SDCARD, rec_dir);
        if (rt != OPRT_OK) {
            bk_printf("file record start failed: %d\r\n", rt);
            return;
        }

        if (duration_s > 0) {
            if (s_stop_delayed_work == NULL) {
                tal_workq_init_delayed(WORKQ_SYSTEM, __record_timeout_cb, NULL, &s_stop_delayed_work);
            }
            tal_workq_start_delayed(s_stop_delayed_work, duration_s * 1000, LOOP_ONCE);
            bk_printf("atest recording to %s, auto-stop in %ds\r\n", rec_dir, duration_s);
        } else {
            bk_printf("atest recording to %s (use 'atest stop' to finish)\r\n", rec_dir);
        }
    } else if (0 == strcmp(argv[1], "play")) {
        CHAR_T play_path[128];

        if (argc < 3) {
            bk_printf("usage: atest play <wav>\r\n");
            return;
        }

        __build_path(play_path, sizeof(play_path), argv[2], ATEST_DEFAULT_DIR);
        wukong_audio_play_local(play_path, NULL, NULL, AI_AUDIO_CODEC_WAV, 0);
        bk_printf("atest play: %s\r\n", play_path);
    } else if (0 == strcmp(argv[1], "pstop")) {
        wukong_audio_player_stop(AI_PLAYER_BG);
        bk_printf("atest playback stopped\r\n");
    } else if (0 == strcmp(argv[1], "stop")) {
        if (s_stop_delayed_work) {
            tal_workq_stop_delayed(s_stop_delayed_work);
        }
        wukong_audio_player_stop(AI_PLAYER_BG);
        audio_dump_set_channel(AUDIO_DUMP_CH_OFF, NULL);
        bk_printf("\r\n======== atest stopped ========\r\n");
    } else if (0 == strcmp(argv[1], "vol")) {
        UINT8_T vol;

        if (argc < 3) {
            if (wukong_audio_player_get_vol(&vol) == OPRT_OK) {
                bk_printf("system vol: %d\r\n", vol);
            } else {
                bk_printf("system vol get failed\r\n");
            }
            return;
        }

        vol = (UINT8_T)atoi(argv[2]);
        wukong_audio_player_set_vol(vol);
        bk_printf("system vol set to %d\r\n", vol);
    } else if (0 == strcmp(argv[1], "gain")) {
        INT32_T gain;
        OPERATE_RET rt;

        if (argc < 3) {
            bk_printf("hw gain get not supported\r\n");
            return;
        }

        gain = atoi(argv[2]);
        rt = wukong_audio_output_set_vol(gain);
        bk_printf("hw gain set to %d, ret=%d\r\n", gain, rt);
    } else if (0 == strcmp(argv[1], "status")) {
        AUDIO_DUMP_CHANNEL_E ch = audio_dump_get_channel();

        bk_printf("recording: %s\r\n", (ch == AUDIO_DUMP_CH_SDCARD) ? "yes" : "no");
        bk_printf("dump channel: %d\r\n", ch);
    } else if (0 == strcmp(argv[1], "ls")) {
        CONST CHAR_T *dir = (argc >= 3) ? argv[2] : ATEST_DEFAULT_DIR;

        __list_dir(dir);
    } else if (0 == strcmp(argv[1], "reboot")) {
        bk_printf("atest: rebooting...\r\n");
        tal_system_reset();
    } else {
        bk_printf("unknown cmd: %s\r\n", argv[1]);
    }
}
