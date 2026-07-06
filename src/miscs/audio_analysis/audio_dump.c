/**
* Copyright (C) by Tuya Inc                                                  
* All rights reserved                                                        
*
* @file audio_dump.c
* @brief audio dump mic/ref/aec
* @version 1.0
* @author linch
* @date 2025-06-02
*
*/

#include "tuya_device_cfg.h"
#include "tuya_cloud_types.h"
#include "audio_dump.h"
#include "tal_memory.h"
#include "tal_uart.h"
#include "tal_log.h"
#include "tal_thread.h"
#include "tal_mutex.h"
#include "audio_analysis.h"
#include "tkl_audio.h"
#if defined(ENABLE_APP_AI_MONITOR) && (ENABLE_APP_AI_MONITOR == 1)
#include "tuya_ai_monitor.h"
#include "tal_workq_service.h"
#endif

#define AUDIO_DUMP_BUF          1024*1024

typedef struct {
    uint8_t  *data;
    uint32_t  datalen;
} audio_dump_t;

STATIC audio_dump_t audio_dump[AUDIO_DUMP_MAX];
STATIC BOOL_T audio_dump_flag = FALSE;
STATIC MUTEX_HANDLE __s_dump_mutex = NULL;

/* State tracking for GET operations */
STATIC INT_T __s_current_volume = 50;     // Default volume
STATIC INT_T __s_current_micgain = 70;    // Default mic gain

#if defined(ENABLE_APP_AI_MONITOR) && (ENABLE_APP_AI_MONITOR == 1)
typedef enum {
    AUDIO_DUMP_MODE_BUFFER = 0,
    AUDIO_DUMP_MODE_REALTIME,
} AUDIO_DUMP_MODE_E;

typedef struct {
    AUDIO_DUMP_MODE_E mode;
    UINT32_T batch_count;
    UINT32_T frame_size;
} AUDIO_DUMP_REALTIME_CFG_T;

STATIC AUDIO_DUMP_MODE_E __s_dump_mode = AUDIO_DUMP_MODE_BUFFER;
STATIC UINT32_T __s_realtime_batch_count = 50;
STATIC UINT32_T __s_realtime_frame_size = 640;
STATIC CHAR_T *__s_realtime_buf[AUDIO_DUMP_MAX] = {NULL};
STATIC UINT32_T __s_realtime_idx = 0;
STATIC UINT32_T __s_realtime_written[AUDIO_DUMP_MAX] = {0};
STATIC BOOL_T __s_realtime_first_send_flags[AUDIO_DUMP_MAX] = {TRUE, TRUE, TRUE, TRUE, TRUE};
STATIC volatile INT_T __s_realtime_pending_count = 0;
#define REALTIME_PENDING_MAX 10

typedef struct {
    INT_T type;
    CHAR_T *data;
    UINT32_T size;
    AI_STREAM_TYPE stream_type;
} AUDIO_REALTIME_WORKQ_DATA_T;

STATIC OPERATE_RET __audio_dump_realtime_init(CONST AUDIO_DUMP_REALTIME_CFG_T *cfg);
STATIC VOID __audio_dump_realtime_deinit(VOID);
STATIC VOID __audio_dump_set_mode(AUDIO_DUMP_MODE_E mode);

STATIC VOID_T __audio_realtime_workq_cb(VOID_T *data)
{
    AUDIO_REALTIME_WORKQ_DATA_T *workq_data = (AUDIO_REALTIME_WORKQ_DATA_T *)data;
    if (workq_data == NULL || workq_data->data == NULL) {
        return;
    }
    
    switch (workq_data->type) {
    case AUDIO_DUMP_MIC:
        tuya_ai_monitor_broadcast_audio_mic(workq_data->stream_type, workq_data->data, workq_data->size);
        break;
    case AUDIO_DUMP_REF:
        tuya_ai_monitor_broadcast_audio_ref(workq_data->stream_type, workq_data->data, workq_data->size);
        break;
    case AUDIO_DUMP_AEC:
        tuya_ai_monitor_broadcast_audio_aec(workq_data->stream_type, workq_data->data, workq_data->size);
        break;
    case AUDIO_DUMP_KWS:
        tuya_ai_monitor_broadcast_audio_kws(workq_data->stream_type, workq_data->data, workq_data->size);
        break;
    case AUDIO_DUMP_VAD:
        tuya_ai_monitor_broadcast_audio_vad(workq_data->stream_type, workq_data->data, workq_data->size);
        break;
    default:
        break;
    }
    
    tal_free(workq_data->data);
    tal_free(workq_data);
    
    if (__s_dump_mutex != NULL) {
        tal_mutex_lock(__s_dump_mutex);
        __s_realtime_pending_count--;
        tal_mutex_unlock(__s_dump_mutex);
    }
}

STATIC VOID_T __audio_realtime_send(INT_T type, UINT32_T batch_size, AI_STREAM_TYPE stream_type)
{
    if (__s_realtime_buf[type] == NULL) {
        return;
    }
    
    if (__s_realtime_pending_count >= REALTIME_PENDING_MAX) {
        TAL_PR_WARN("realtime workq full, drop type %d", type);
        return;
    }
    
    AUDIO_REALTIME_WORKQ_DATA_T *workq_data = (AUDIO_REALTIME_WORKQ_DATA_T *)tal_malloc(sizeof(AUDIO_REALTIME_WORKQ_DATA_T));
    if (workq_data == NULL) {
        return;
    }
    
    workq_data->type = type;
    workq_data->size = batch_size;
    workq_data->stream_type = stream_type;
    workq_data->data = (CHAR_T *)tal_malloc(batch_size);
    if (workq_data->data == NULL) {
        tal_free(workq_data);
        return;
    }
    
    memcpy(workq_data->data, __s_realtime_buf[type], batch_size);
    
    if (tal_workq_schedule(WORKQ_SYSTEM, __audio_realtime_workq_cb, workq_data) != OPRT_OK) {
        tal_free(workq_data->data);
        tal_free(workq_data);
    } else {
        __s_realtime_pending_count++;
    }
}
#endif

VOID audio_dump_init(VOID);
VOID audio_dump_write(INT_T type, uint8_t *data, uint16_t datalen)
{
#if ENABLE_AUDIO_ANALYSIS
    STATIC INT_T init = 0;
    if (!init) {
        audio_dump_init();
        init = 1;
    }

    if (!audio_dump_flag) {
        return;
    }

    if (type >= AUDIO_DUMP_MAX) {
        return;
    }
    
    if (__s_dump_mutex == NULL) {
        return;
    }

#if defined(ENABLE_APP_AI_MONITOR) && (ENABLE_APP_AI_MONITOR == 1)
    if (__s_dump_mode == AUDIO_DUMP_MODE_REALTIME && __s_realtime_buf[type] != NULL) {
        tal_mutex_lock(__s_dump_mutex);
        if (__s_dump_mode != AUDIO_DUMP_MODE_REALTIME || __s_realtime_buf[type] == NULL) {
            tal_mutex_unlock(__s_dump_mutex);
            return;
        }
        
        UINT32_T buf_capacity = __s_realtime_batch_count * __s_realtime_frame_size;
        if (__s_realtime_written[type] + datalen <= buf_capacity) {
            memcpy(__s_realtime_buf[type] + __s_realtime_written[type], data, datalen);
            __s_realtime_written[type] += datalen;
        }
        
        if (type == AUDIO_DUMP_AEC) {
            __s_realtime_idx++;
            if (__s_realtime_idx >= __s_realtime_batch_count) {
                for (INT_T i = 0; i < AUDIO_DUMP_MAX; i++) {
                    if (__s_realtime_buf[i] != NULL && __s_realtime_written[i] > 0) {
                        AI_STREAM_TYPE st = __s_realtime_first_send_flags[i] ? AI_STREAM_START : AI_STREAM_ING;
                        __audio_realtime_send(i, __s_realtime_written[i], st);
                        __s_realtime_first_send_flags[i] = FALSE;
                        __s_realtime_written[i] = 0;
                    }
                }
                __s_realtime_idx = 0;
            }
        }
        tal_mutex_unlock(__s_dump_mutex);
        return;
    }
#endif

    if (audio_dump[type].datalen + datalen > AUDIO_DUMP_BUF) {
        return;
    }
#if ENABLE_EXT_RAM
    if (audio_dump[type].data != NULL) {
        memcpy(audio_dump[type].data + audio_dump[type].datalen, data, datalen);
        audio_dump[type].datalen += datalen;
    }
#else
    if (__s_dump_type != AUDIO_DUMP_MAX) {
        audio_dump_with_uart(type, data, datalen);
    }
#endif

#endif // ENABLE_AUDIO_ANALYSIS
}

VOID audio_dump_enable(VOID)
{
    if (__s_dump_mutex == NULL) {
        return;
    }
    
    tal_mutex_lock(__s_dump_mutex);
    
#if ENABLE_EXT_RAM
    for (INT_T i = 0; i < AUDIO_DUMP_MAX; i++) {
        if (audio_dump[i].data == NULL) {
            audio_dump[i].data = tal_psram_malloc(AUDIO_DUMP_BUF);
            if (audio_dump[i].data == NULL) {
                TAL_PR_ERR("audio_dump_enable: malloc failed for type %d", i);
            }
        }
        audio_dump[i].datalen = 0;
    }
#endif
    
    audio_dump_flag = TRUE;
    tal_mutex_unlock(__s_dump_mutex);
    TAL_PR_DEBUG("audio_dump enabled");
}

VOID audio_dump_stop(VOID)
{
    if (__s_dump_mutex == NULL) {
        return;
    }
    
    tal_mutex_lock(__s_dump_mutex);
    audio_dump_flag = FALSE;
    tal_mutex_unlock(__s_dump_mutex);
    TAL_PR_DEBUG("audio_dump stopped (data preserved for dump)");
}

VOID audio_dump_disable(VOID)
{
    if (__s_dump_mutex == NULL) {
        return;
    }
    
    tal_mutex_lock(__s_dump_mutex);
    audio_dump_flag = FALSE;
    
#if ENABLE_EXT_RAM
    for (INT_T i = 0; i < AUDIO_DUMP_MAX; i++) {
        if (audio_dump[i].data != NULL) {
            tal_psram_free(audio_dump[i].data);
            audio_dump[i].data = NULL;
        }
        audio_dump[i].datalen = 0;
    }
#endif
    
    tal_mutex_unlock(__s_dump_mutex);
    TAL_PR_DEBUG("audio_dump disabled");
}

VOID audio_dump_reset(VOID)
{
    audio_dump[0].datalen = 0;
    audio_dump[1].datalen = 0;
    audio_dump[2].datalen = 0;

#ifndef ENABLE_EXT_RAM
    // __s_dump_type = AUDIO_DUMP_MAX;
#endif    
}

VOID audio_dump_with_uart(INT_T type)
{
    TAL_PR_DEBUG("audio_dump type[%d] len %d\r\n", type, audio_dump[type].datalen);
#if ENABLE_EXT_RAM
    tal_uart_write(TUYA_UART_NUM_0, audio_dump[type].data , audio_dump[type].datalen);
    audio_dump[type].datalen = 0;
#else
    // __s_dump_type = type;
#endif
}

VOID audio_dump_with_net(INT_T type)
{
    TAL_PR_DEBUG("audio_dump type[%d] len %d\r\n", type, audio_dump[type].datalen);

#if defined(ENABLE_APP_AI_MONITOR) && (ENABLE_APP_AI_MONITOR == 1)
    audio_dump_t *dump = &audio_dump[type];
    if (audio_dump && dump->data && dump->datalen > 0) {
        TAL_PR_DEBUG("net dump audio type %d, len %d", type, dump->datalen);
        // dump pcm data interval is 100ms
        for (int i = 0; i < dump->datalen; i += 3200) {
            TAL_PR_DEBUG("net dump audio data offset %d, len %d", i, MIN(3200, dump->datalen - i));
            AI_STREAM_TYPE stype = AI_STREAM_ONE;
            if (i == 0 && i + 3200 >= dump->datalen) {
                stype = AI_STREAM_ONE;
            } else if (i == 0) {
                stype = AI_STREAM_START;
            } else if (i + 3200 >= dump->datalen) {
                stype = AI_STREAM_END;
            } else {
                stype = AI_STREAM_ING;
            }

            if (type == 0) {
                tuya_ai_monitor_broadcast_audio_mic(stype, dump->data + i, MIN(3200, dump->datalen - i));
            } else if (type == 1) {
                tuya_ai_monitor_broadcast_audio_ref(stype, dump->data + i, MIN(3200, dump->datalen - i));
            } else if (type == 2) {
                tuya_ai_monitor_broadcast_audio_aec(stype, dump->data + i, MIN(3200, dump->datalen - i));
            } else if (type == 3) {
                tuya_ai_monitor_broadcast_audio_kws(stype, dump->data + i, MIN(3200, dump->datalen - i));
            } else if (type == 4) {
                tuya_ai_monitor_broadcast_audio_vad(stype, dump->data + i, MIN(3200, dump->datalen - i));
            }
        }
        dump->datalen = 0; // reset dump data length
    }
#else
    TAL_PR_ERR("ENABLE_APP_AI_MONITOR is not enabled, can not net dump audio");
#endif
}

VOID audio_play_bgm(INT_T type, INT_T freq)
{
    if (0 == type) {
        AUDIO_ANALYSIS_PARAMS_T params = {0};
        AUDIO_ANALYSIS_DEFAULT_PARAMS_GET_RANG(&params);
        audio_analysis_play(AUDIO_ANALYSIS_TYPE_RANG, &params);
    } else if (1 == type) { /*单频*/
        AUDIO_ANALYSIS_PARAMS_T params = {0};
        AUDIO_ANALYSIS_DEFAULT_PARAMS_GET_SINGLE(&params);
        if (freq > 0) {
            params.freq = freq;
        }                
        audio_analysis_play(AUDIO_ANALYSIS_TYPE_SINGLE, &params);
    } else if (2 == type) { /*白噪声*/
        AUDIO_ANALYSIS_PARAMS_T params = {0};
        AUDIO_ANALYSIS_DEFAULT_PARAMS_GET_SWEEP(&params);
        audio_analysis_play(AUDIO_ANALYSIS_TYPE_SWEEP, &params);
    } else if (3 == type) { /*调频*/                
        AUDIO_ANALYSIS_PARAMS_T params = {0};
        INT_T amp = freq;
        if (amp < 0) {
            params.amp = amp;
        }
        AUDIO_ANALYSIS_DEFAULT_PARAMS_GET_SWEEPSPECIAL(&params);
        audio_analysis_play(AUDIO_ANALYSIS_TYPE_SWEEPSPECIAL, &params);
    } else if (4 == type) { /*最小信号*/
        AUDIO_ANALYSIS_PARAMS_T params = {0};
        AUDIO_ANALYSIS_DEFAULT_PARAMS_GET_MINSIN(&params);
        if (freq > 0) {
            params.freq = freq;
        }                
        audio_analysis_play(AUDIO_ANALYSIS_TYPE_MINSIN, &params);
    } else {
        TAL_PR_ERR("unknown audio test data %d", type);
    }
}

VOID audio_set_volume(INT_T volume)
{
    if (volume < 0) {
        volume = 0;
    } else if (volume > 100) {
        volume = 100;
    }

    __s_current_volume = volume;
    tkl_ao_set_vol(TKL_AUDIO_TYPE_BOARD, TKL_AO_0, NULL, volume);
}

VOID audio_set_micgain(INT_T micgain)
{
    if (micgain < 0) {
        micgain = 0;
    } else if (micgain > 100) {
        micgain = 100;
    }

    __s_current_micgain = micgain;
    tkl_ai_set_vol(TKL_AUDIO_TYPE_BOARD, 0, micgain);
}

// VOID audio_ctrl_alg(INT_T argc, CHAR_T *argv[])
// {
//     if (0 == strcmp(argv[2], "set")) {
//         // ! ao alg set <para> <value>
//         if (argc != 5) {
//             TAL_PR_DEBUG("audio alg set cmd error\r\n");
//             return;
//         }
//         INT_T i;
//         for (i = 0; i < sizeof(audio_alg_para_map) / sizeof(aa_alg_para_map_t); i++) {
//             if (0 == strcmp(argv[3], audio_alg_para_map[i].name)) {
//                 break;
//             }
//         }
//         if (i >= sizeof(audio_alg_para_map) / sizeof(aa_alg_para_map_t)) {
//             TAL_PR_DEBUG("audio alg set para %s not found\r\n", argv[3]);
//             return;
//         }
//         uint32_t type = audio_alg_para_map[i].para;
//         uint32_t value = atoi(argv[4]);
//         _audio_test_event(AUDIO_TEST_EVENT_SET_ALG_PARA, type, value);
//     } else if (0 == strcmp(argv[2], "get")) {
//         // ! ao alg get <para>
//         if (argc != 4 && argc != 5) {
//             return;
//         }
//         INT_T i;
//         for (i = 0; i < sizeof(audio_alg_para_map) / sizeof(aa_alg_para_map_t); i++) {
//             if (0 == strcmp(argv[3], audio_alg_para_map[i].name)) {
//                 break;
//             }
//         }
//         if (i >= sizeof(audio_alg_para_map) / sizeof(aa_alg_para_map_t)) {
//             TAL_PR_DEBUG("audio alg get para %s not found\r\n", argv[3]);
//             return;
//         }
//         uint32_t type = audio_alg_para_map[i].para;
//         uint32_t value = 0;
//         if (argc == 5) {
//             value = atoi(argv[4]);
//         }
//         _audio_test_event(AUDIO_TEST_EVENT_GET_ALG_PARA, type, value);
//     } else if (0 == strcmp(argv[2], "dump")) {
//         // ! ao alg dump
//         _audio_test_event(AUDIO_TEST_EVENT_DUMP_ALG_PARA, 0, 0);
//     } else {
//         TAL_PR_DEBUG("audio alg cmd error\r\n");
//     }
// }

//！ ao start
//！ ao stop
//！ ao reset
//！ ao dump 0
//！ ao dump 1
//！ ao dump 2
//！ ao netdump 0
//！ ao netdump 1
//！ ao netdump 2
//！ ao bg 0
//！ ao bg 1 (ao bg 1 1000)
//！ ao bg 2
//！ ao volume 50
// ! ao micgain 70(default)
// ! ao alg set <para> [<para2>] <value>
// ! ao alg get <para> [<para2>]
// ! ao alg dump
// ! ao echo <info>
// ! ao realtime start [batch] [framesize]
// ! ao realtime stop
VOID audio_dump_exec(INT_T argc, CHAR_T *argv[])
{
    if (0 == strcmp(argv[1], "start")) {
        audio_dump_enable();
        TAL_PR_DEBUG("audio_dump start\r\n");
    } else if (0 == strcmp(argv[1], "stop")) {
        audio_dump_stop();
        TAL_PR_DEBUG("audio_dump stop\r\n");
    } else if (0 == strcmp(argv[1], "dump")) {
        audio_dump_with_uart(atoi(argv[2]));
        TAL_PR_DEBUG("audio_dump, %d\r\n", atoi(argv[2]));
    } else if (0 == strcmp(argv[1], "netdump")) {
        audio_dump_with_net(atoi(argv[2]));
        TAL_PR_DEBUG("audio_dump, %d\r\n", atoi(argv[2]));
    } else if (0 == strcmp(argv[1], "reset")) {
        audio_dump_reset();
        TAL_PR_DEBUG("audio_dump reset\r\n");
    } else if (0 == strcmp(argv[1], "bg")) {
        INT_T freq = 0;
        if (argc > 3) {
            freq = atoi(argv[3]);
        }
        audio_play_bgm(atoi(argv[2]), freq);
        TAL_PR_DEBUG("audio_dump play bgm %d\r\n", atoi(argv[2]));
    } else if (0 == strcmp(argv[1], "volume")) {
        audio_set_volume(atoi(argv[2]));
        TAL_PR_DEBUG("audio_dump set volume %d\r\n", atoi(argv[2]));
    } else if (0 == strcmp(argv[1], "micgain")) {
        audio_set_micgain(atoi(argv[2]));
        TAL_PR_DEBUG("audio_dump set micgain %d\r\n", atoi(argv[2]));
    } else if (0 == strcmp(argv[1], "alg")) {
        // audio_ctrl_alg(argc, argv);
    } else if (0 == strcmp(argv[1], "echo")) {
        TAL_PR_DEBUG("echo %s", argv[2]);
#if defined(ENABLE_APP_AI_MONITOR) && (ENABLE_APP_AI_MONITOR == 1)
    } else if (0 == strcmp(argv[1], "realtime")) {
        if (argc < 3) {
            TAL_PR_DEBUG("usage: ao realtime start|stop [batch] [framesize]\r\n");
            return;
        }
        if (0 == strcmp(argv[2], "start")) {
            AUDIO_DUMP_REALTIME_CFG_T cfg = {0};
            cfg.mode = AUDIO_DUMP_MODE_REALTIME;
            cfg.batch_count = (argc > 3) ? atoi(argv[3]) : 50;
            cfg.frame_size = (argc > 4) ? atoi(argv[4]) : 640;
            audio_dump_enable();
            if (OPRT_OK == __audio_dump_realtime_init(&cfg)) {
                TAL_PR_DEBUG("audio realtime start: batch=%d, frame=%d\r\n", cfg.batch_count, cfg.frame_size);
            } else {
                TAL_PR_ERR("audio realtime init failed\r\n");
            }
        } else if (0 == strcmp(argv[2], "stop")) {
            __audio_dump_set_mode(AUDIO_DUMP_MODE_BUFFER);
            __audio_dump_realtime_deinit();
            TAL_PR_DEBUG("audio realtime stop\r\n");
        } else {
            TAL_PR_DEBUG("usage: ao realtime start|stop\r\n");
        }
#endif
    } else {
        TAL_PR_DEBUG("audio_dump cmd error\r\n");
    }
}


INT_T strsplit(CHAR_T* input, INT_T *argc, CHAR_T *argv[])
{
    CONST CHAR_T delimiter[] = " ";
    CHAR_T *token = NULL;

    *argc = 0;
    // Get the first token
    token = strtok(input, delimiter);
    argv[(*argc)++] = token;
    TAL_PR_DEBUG("token %s\r\n", token);
    // Iterate over tokens
    while (token != NULL) {
        // Get the next token
        token = strtok(NULL, delimiter);
        if (token) {
            argv[(*argc)++] = token;
        }
        if (*argc >= 10) {
            return OPRT_INDEX_OUT_OF_BOUND;
        }
    }

    return OPRT_OK;
}

VOID __audio_dump_task(VOID *params)
{
    INT_T     rt;

    TAL_UART_CFG_T cfg = {0};
    cfg.base_cfg.baudrate = 460800;
    cfg.base_cfg.databits = TUYA_UART_DATA_LEN_8BIT;
    cfg.base_cfg.stopbits = TUYA_UART_STOP_LEN_1BIT;
    cfg.base_cfg.parity = TUYA_UART_PARITY_TYPE_NONE;
    cfg.rx_buffer_size = 256;
    cfg.open_mode = O_BLOCK;
    rt = tal_uart_init(TUYA_UART_NUM_0, &cfg);

    uint8_t ch;
    uint8_t buffer[255];
    uint8_t index = 0;
    INT_T     argc;
    CHAR_T   *argv[10];

    for (;;) {
        tal_uart_read(TUYA_UART_NUM_0, (UINT8_T*)&ch, 1);
        if (ch != '\r' && ch != '\n') {
            buffer[index++] = ch;
            continue;
        }
        buffer[index] = '\0';
        //! if '\r\n' is end of CHAR_T, '\n' is need discard， so check index
        if (index && OPRT_OK == strsplit(buffer, &argc, argv)) {
            TAL_PR_DEBUG("dump command:%s %s", argv[0], argv[1]);
            //! parse cmd
            if (0 == strcmp(argv[0], "ao")) {
                audio_dump_exec(argc, argv);
            }
        }
        index = 0;
    }
}

#include "tkl_thread.h"

STATIC TKL_THREAD_HANDLE audio_dump_handle = NULL;

#if defined(ENABLE_APP_AI_MONITOR) && (ENABLE_APP_AI_MONITOR == 1)
STATIC OPERATE_RET __audio_dump_alg_ctrl_cb(UINT8_T op, UINT8_T cmd,
                                             UINT16_T param1, UINT16_T param2,
                                             UINT16_T *out_val1, UINT16_T *out_val2,
                                             UINT16_T *out_val3, UINT16_T *out_val4);
#endif

VOID audio_dump_init(VOID)
{
    INT_T i = 0;
    for (i = 0; i < AUDIO_DUMP_MAX; i++) {
        audio_dump[i].datalen = 0;
        audio_dump[i].data = NULL;
    }
    
    if (__s_dump_mutex == NULL) {
        tal_mutex_create_init(&__s_dump_mutex);
    }

#if defined(ENABLE_APP_AI_MONITOR) && (ENABLE_APP_AI_MONITOR == 1)
    tuya_ai_monitor_register_alg_ctrl_cb(AI_ALG_CTRL_MODULE_AUDIO_DUMP, __audio_dump_alg_ctrl_cb);
#endif

    THREAD_CFG_T thread_param = {0};
    thread_param.stackDepth = 1024*4;
    thread_param.priority = THREAD_PRIO_1;
    thread_param.thrdname = "audio_dump";
#if defined(ENABLE_EXT_RAM) && (ENABLE_EXT_RAM == 1)
    thread_param.psram_mode = 1;
#endif
    tal_thread_create_and_start(&audio_dump_handle, NULL, NULL, __audio_dump_task, NULL, &thread_param);
}

#if defined(ENABLE_APP_AI_MONITOR) && (ENABLE_APP_AI_MONITOR == 1)
STATIC OPERATE_RET __audio_dump_realtime_init(CONST AUDIO_DUMP_REALTIME_CFG_T *cfg)
{
    if (cfg == NULL) {
        return OPRT_INVALID_PARM;
    }
    
    if (__s_dump_mutex == NULL) {
        return OPRT_NOT_FOUND;
    }
    
    tal_mutex_lock(__s_dump_mutex);
    
    __s_realtime_batch_count = cfg->batch_count;
    __s_realtime_frame_size = cfg->frame_size;
    __s_realtime_idx = 0;
    
    UINT32_T buf_size = cfg->batch_count * cfg->frame_size;
    for (INT_T i = 0; i < AUDIO_DUMP_MAX; i++) {
        __s_realtime_written[i] = 0;
        __s_realtime_first_send_flags[i] = TRUE;
        if (__s_realtime_buf[i] == NULL) {
            __s_realtime_buf[i] = (CHAR_T *)tal_malloc(buf_size);
            if (__s_realtime_buf[i] == NULL) {
                tal_mutex_unlock(__s_dump_mutex);
                __audio_dump_realtime_deinit();
                return OPRT_MALLOC_FAILED;
            }
        }
    }
    
    __s_dump_mode = cfg->mode;
    tal_mutex_unlock(__s_dump_mutex);
    
    TAL_PR_DEBUG("audio_dump realtime init: mode=%d, batch=%d, frame=%d", 
                 cfg->mode, cfg->batch_count, cfg->frame_size);
    return OPRT_OK;
}

STATIC VOID __audio_dump_realtime_deinit(VOID)
{
    if (__s_dump_mutex == NULL) {
        return;
    }
    
    tal_mutex_lock(__s_dump_mutex);
    
    if (__s_dump_mode == AUDIO_DUMP_MODE_REALTIME) {
        for (INT_T i = 0; i < AUDIO_DUMP_MAX; i++) {
            if (__s_realtime_buf[i] != NULL && __s_realtime_written[i] > 0) {
                AI_STREAM_TYPE stype = __s_realtime_first_send_flags[i] ? AI_STREAM_ONE : AI_STREAM_END;
                __audio_realtime_send(i, __s_realtime_written[i], stype);
            }
        }
    }
    
    __s_dump_mode = AUDIO_DUMP_MODE_BUFFER;
    __s_realtime_idx = 0;
    
    for (INT_T i = 0; i < AUDIO_DUMP_MAX; i++) {
        __s_realtime_written[i] = 0;
        __s_realtime_first_send_flags[i] = TRUE;
        if (__s_realtime_buf[i] != NULL) {
            tal_free(__s_realtime_buf[i]);
            __s_realtime_buf[i] = NULL;
        }
    }
    
    tal_mutex_unlock(__s_dump_mutex);
}

STATIC VOID __audio_dump_set_mode(AUDIO_DUMP_MODE_E mode)
{
    if (__s_dump_mutex == NULL) {
        return;
    }
    tal_mutex_lock(__s_dump_mutex);
    __s_dump_mode = mode;
    __s_realtime_idx = 0;
    tal_mutex_unlock(__s_dump_mutex);
    TAL_PR_DEBUG("audio_dump set mode: %d", mode);
}

STATIC OPERATE_RET __audio_dump_alg_ctrl_cb(UINT8_T op, UINT8_T cmd,
                                             UINT16_T param1, UINT16_T param2,
                                             UINT16_T *out_val1, UINT16_T *out_val2,
                                             UINT16_T *out_val3, UINT16_T *out_val4)
{
    OPERATE_RET rt = OPRT_OK;

    TAL_PR_DEBUG("audio_dump alg_ctrl: op=%d, cmd=0x%02X, param1=%d, param2=%d",
                 op, cmd, param1, param2);

    if (op == AI_ALG_CTRL_OP_SET) {
        switch (cmd) {
        case AUDIO_DUMP_CMD_ENABLE:
            if (param1) {
                audio_dump_enable();
            } else {
                audio_dump_stop();
            }
            break;

        case AUDIO_DUMP_CMD_RESET:
            audio_dump_reset();
            break;

        case AUDIO_DUMP_CMD_DUMP_NET:
            if (param1 < AUDIO_DUMP_MAX) {
                audio_dump_with_net((INT_T)param1);
            } else {
                rt = OPRT_INVALID_PARM;
            }
            break;

        case AUDIO_DUMP_CMD_REALTIME:
            if (param1 == 0 && param2 == 0) {
                __audio_dump_set_mode(AUDIO_DUMP_MODE_BUFFER);
                __audio_dump_realtime_deinit();
            } else {
                AUDIO_DUMP_REALTIME_CFG_T cfg = {0};
                cfg.mode = AUDIO_DUMP_MODE_REALTIME;
                cfg.batch_count = (param1 > 0) ? param1 : 50;
                cfg.frame_size = (param2 > 0) ? param2 : 640;
                audio_dump_enable();
                rt = __audio_dump_realtime_init(&cfg);
            }
            break;

        case AUDIO_DUMP_CMD_PLAY_BGM:
            audio_play_bgm((INT_T)param1, (INT_T)param2);
            break;

        case AUDIO_DUMP_CMD_VOLUME:
            audio_set_volume((INT_T)param1);
            break;

        case AUDIO_DUMP_CMD_MICGAIN:
            audio_set_micgain((INT_T)param1);
            break;

        default:
            rt = OPRT_NOT_SUPPORTED;
            break;
        }
    } else if (op == AI_ALG_CTRL_OP_GET) {
        switch (cmd) {
        case AUDIO_DUMP_CMD_ENABLE:
            if (out_val1) *out_val1 = audio_dump_flag ? 1 : 0;
            break;

        case AUDIO_DUMP_CMD_REALTIME:
            if (out_val1) *out_val1 = (UINT16_T)__s_dump_mode;
            break;

        case AUDIO_DUMP_CMD_VOLUME:
            if (out_val1) *out_val1 = (UINT16_T)__s_current_volume;
            break;

        case AUDIO_DUMP_CMD_MICGAIN:
            if (out_val1) *out_val1 = (UINT16_T)__s_current_micgain;
            break;

        case AUDIO_DUMP_CMD_GET_STATUS:
            if (out_val1) *out_val1 = audio_dump_flag ? 1 : 0;
            if (out_val2) *out_val2 = (UINT16_T)__s_dump_mode;
            break;

        case AUDIO_DUMP_CMD_GET_ALL:
            if (out_val1) *out_val1 = audio_dump_flag ? 1 : 0;
            if (out_val2) *out_val2 = (UINT16_T)__s_dump_mode;
            if (out_val3) *out_val3 = (UINT16_T)__s_current_volume;
            if (out_val4) *out_val4 = (UINT16_T)__s_current_micgain;
            break;

        default:
            rt = OPRT_NOT_SUPPORTED;
            break;
        }
    } else {
        rt = OPRT_NOT_SUPPORTED;
    }

    return rt;
}
#endif
