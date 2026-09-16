/**
 * @file test_audio_i2s.c
 * @brief I2S audio test cases (CLI-driven, no SD card I/O, multi-instance)
 * @version 1.0
 * @date 2025-02-13
 * @copyright Copyright (c) Tuya Inc. All Rights Reserved.
 *
 * Permission is hereby granted, to any person obtaining a copy of this software and
 * associated documentation files (the "Software"), Under the premise of complying
 * with the license of the third-party open source software contained in the software,
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software.
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 */
#include "tal_audio_input.h"
#include "tal_audio_output.h"
#include "tkl_memory.h"
#include "tkl_system.h"
#include "tkl_thread.h"
#include "uni_log.h"
#include "tuya_iot_config.h"
#include "tal_log.h"
#include <string.h>
#include "tkl_fs.h"
#include "tuya_ringbuf.h"

/* ---------------------------------------------------------------------------
 * Macros
 * --------------------------------------------------------------------------- */
#if defined(ENABLE_EXT_RAM) && (ENABLE_EXT_RAM == 1)
#define I2S_TEST_MALLOC(size)       tkl_system_psram_malloc(size)
#define I2S_TEST_FREE(ptr)          tkl_system_psram_free(ptr)
#else
#define I2S_TEST_MALLOC(size)       tkl_system_malloc(size)
#define I2S_TEST_FREE(ptr)          tkl_system_free(ptr)
#endif

#define AUDIO_TEST_MIN(a, b)        (((a) < (b)) ? (a) : (b))

#define TAL_AUDIO_I2S_TEST_RAW_DATA     0
#define TAL_AUDIO_I2S_TEST_PCM_DATA     1

#if (TAL_AUDIO_I2S_TEST_RAW_DATA == 1) && (TAL_AUDIO_I2S_TEST_PCM_DATA == 1)
#error test raw data or pcm only support one
#endif

/*
 * TX mode: generates 4 KB random PCM data per iteration and writes it out.
 * RX mode: receives frames via callback and logs byte counts periodically.
 * direction == 2: starts both RX and TX threads on the same port (the TKL
 *                 layer merges directions internally via tkl_i2s_init).
 *
 * Instance array: sg_i2s_inst[port][dir], dir 0 = RX, dir 1 = TX.
 *
 * CLI: xt i2s <port> <m|s> <0|1|2>  (0=rx, 1=tx, 2=both)
 */
#define I2S_TEST_CHUNK_SIZE         4096U
#define I2S_TEST_SAMPLE_RATE        16000U
#define I2S_TEST_SAMPLE_BITS        TUYA_AUDIO_SAMPLE_BITS_16
#define I2S_TEST_PROTOCOL           I2S_COMM_FORMAT_STAND_I2S
#define I2S_TEST_CHANNEL_FMT        TUYA_I2S_CHANNEL_FMT_RIGHT_LEFT
#define I2S_TEST_FRAME_TIME_MS      20U
#define I2S_TEST_DIR_RX             0
#define I2S_TEST_DIR_TX             1
#define I2S_TEST_DIR_NUM            2

#if TAL_AUDIO_I2S_TEST_PCM_DATA
    #define PCM_DATA_FROME_FILE     0       // 0: data form local generic, 1: read file

    #if PCM_DATA_FROME_FILE
        #define TAL_AUDIO_TEST_MOUNT_POINT      "/sdcard"
        #define TAL_AUDIO_TEST_PLAY_FILE        TAL_AUDIO_TEST_MOUNT_POINT "/dingdong_zh.pcm"
    #endif // PCM_DATA_FROME_FILE

#define TAL_AUDIO_TEST_RINGBUF_SIZE     (I2S_TEST_CHUNK_SIZE * 4)
#define TAL_AUDIO_TEST_VOLUME           50U
STATIC TUYA_RINGBUFF_T s_i2s_recv_rb = NULL;

#endif // TAL_AUDIO_I2S_TEST_PCM_DATA

/* ---------------------------------------------------------------------------
 * Type definitions
 * --------------------------------------------------------------------------- */
typedef struct {
    TKL_THREAD_HANDLE           thread;
    volatile BOOL_T             running;
    volatile UINT32_T           rx_total_bytes;
    TAL_AUDIO_INPUT_HANDLE      input_handle;
    TAL_AUDIO_OUTPUT_HANDLE     output_handle;
    UINT8_T                     port;
    UINT8_T                     is_master;
} I2S_TEST_INST_T;

/* ---------------------------------------------------------------------------
 * File scope variables
 * --------------------------------------------------------------------------- */
STATIC I2S_TEST_INST_T sg_i2s_inst[TUYA_I2S_NUM_MAX][I2S_TEST_DIR_NUM] = {{{0}}};

/* ---------------------------------------------------------------------------
 * Function implementations
 * --------------------------------------------------------------------------- */
#if TAL_AUDIO_I2S_TEST_PCM_DATA
#if !PCM_DATA_FROME_FILE

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int __generate_double_chnl_pcm(char *signal, int sr, int freq, int amp, int t)
{
    int16_t *s = (int16_t *)signal;
    double a = powf(10, amp/20.0f);
    bk_printf("amp = %d, %.3f\r\n", amp, a);

    int samples = sr*t;
    for (int i=0; i<samples; i+=2) {
        s[i] = (int16_t)(a * sin(2 * M_PI * freq * i * 1.0f / sr) * 32767);
        s[i+1] = s[i];
    }

    return 0;
}
#endif // !PCM_DATA_FROME_FILE
#endif // TAL_AUDIO_I2S_TEST_PCM_DATA

/**
 * @brief I2S RX test callback, outputs checksum for each 4 KB chunk
 * @param[in] frame audio frame from TAL input layer
 * @param[in] args pointer to the owning I2S_TEST_INST_T
 * @return none
 */
static uint32_t __i2s_rx_data_sum = 0;
static uint32_t __i2s_rx_data_len = 0;
STATIC VOID_T __audio_i2s_rx_test_cb(TAL_AUDIO_FRAME_T *frame, VOID_T *args)
{
    I2S_TEST_INST_T *inst = (I2S_TEST_INST_T *)args;
    UINT32_T copy_len = 0;
    UINT32_T remain_len = 0;
    UINT32_T i = 0;

    if (inst == NULL || !inst->running ||
        frame == NULL || frame->buf == NULL || frame->len == 0U) {
        return;
    }

#if TAL_AUDIO_I2S_TEST_RAW_DATA

    inst->rx_total_bytes += frame->len;
    copy_len = I2S_TEST_CHUNK_SIZE - __i2s_rx_data_len;
    if (copy_len > frame->len) {
        copy_len = frame->len;
    }

    for (i = 0; i < copy_len; i++) {
        __i2s_rx_data_sum += frame->buf[i];
    }
    __i2s_rx_data_len += copy_len;

    if (__i2s_rx_data_len == I2S_TEST_CHUNK_SIZE) {
        bk_printf("recv: %u %u\r\n", I2S_TEST_CHUNK_SIZE, __i2s_rx_data_sum);
        __i2s_rx_data_sum = 0;
        __i2s_rx_data_len = 0;

        remain_len = frame->len - copy_len;
        for (i = 0; i < remain_len; i++) {
            __i2s_rx_data_sum += frame->buf[copy_len + i];
        }
        __i2s_rx_data_len = remain_len;
    }
#elif TAL_AUDIO_I2S_TEST_PCM_DATA
    // write to spk rb
    tuya_ring_buff_write(s_i2s_recv_rb, frame->buf, frame->len);

    for (int i = 0; i < frame->len; i++) {
        __i2s_rx_data_sum += frame->buf[i];
    }
    __i2s_rx_data_len += frame->len;
#endif
}

/**
 * @brief I2S TX test worker thread
 * @param[in] args pointer to the owning I2S_TEST_INST_T
 * @return none
 * @note Generates 4 KB random data each iteration and sends via TAL output.
 */
STATIC VOID_T __audio_i2s_tx_test_thread(PVOID_T args)
{
    I2S_TEST_INST_T *inst = (I2S_TEST_INST_T *)args;
    TAL_AUDIO_OUTPUT_CFG_T config = {0};
    UINT8_T *tx_buf = NULL;
    UINT32_T total_sent = 0;
    OPERATE_RET rt = OPRT_OK;

    tx_buf = I2S_TEST_MALLOC(I2S_TEST_CHUNK_SIZE);
    if (tx_buf == NULL) {
        TAL_PR_ERR("i2s%d tx test malloc failed", inst->port);
        goto __i2s_tx_exit;
    }

    config.type = TAL_AUDIO_OUTPUT_I2S;
    config.dev.i2s_config.port = (TUYA_I2S_NUM_E)inst->port;
    config.dev.i2s_config.mode = (inst->is_master ? TUYA_I2S_MODE_MASTER : TUYA_I2S_MODE_SLAVE)
                                 | TUYA_I2S_MODE_TX;
    config.dev.i2s_config.i2s_protocol = I2S_TEST_PROTOCOL;
    config.dev.i2s_config.data_format = I2S_TEST_CHANNEL_FMT;
    config.sample_bits = I2S_TEST_SAMPLE_BITS;
    config.sample_rate = I2S_TEST_SAMPLE_RATE;
    config.frame_time_ms = I2S_TEST_FRAME_TIME_MS;

    inst->output_handle = tal_audio_output_init(&config);
    if (inst->output_handle == NULL) {
        TAL_PR_ERR("i2s%d tx test init failed", inst->port);
        goto __i2s_tx_exit;
    }

    uint32_t i2s_tx_data_sum = 0;

#if TAL_AUDIO_I2S_TEST_RAW_DATA

    i2s_tx_data_sum = 0;
    for (UINT32_T i = 0; i < I2S_TEST_CHUNK_SIZE; i += 2) {
        UINT16_T val = (UINT16_T)i;
        tx_buf[i]     = (UINT8_T)(val & 0xFF);
        tx_buf[i + 1] = (UINT8_T)((val >> 8) & 0xFF);

        i2s_tx_data_sum += tx_buf[i];
        i2s_tx_data_sum += tx_buf[i + 1];
    }

    rt = tal_audio_output_start(inst->output_handle);
    if (rt != OPRT_OK) {
        TAL_PR_ERR("i2s%d tx test start failed: %d", inst->port, rt);
        goto __i2s_tx_exit;
    }

    inst->running = TRUE;
    bk_printf("i2s%d tx test started, %s, prepare to sent %u bytes, sum %d\r\n",
            inst->port, inst->is_master ? "master" : "slave", I2S_TEST_CHUNK_SIZE, i2s_tx_data_sum);

    while (inst->running) {

        rt = tal_audio_output_write(inst->output_handle, tx_buf, I2S_TEST_CHUNK_SIZE);
        if (rt != OPRT_OK) {
            TAL_PR_ERR("i2s%d tx write failed: %d", inst->port, rt);
            break;
        }

        total_sent += I2S_TEST_CHUNK_SIZE;
        if ((total_sent % (I2S_TEST_CHUNK_SIZE * 16)) == 0) {
            TAL_PR_NOTICE("\ti2s%d tx sent %u bytes", inst->port, total_sent);
        }
    }

#elif TAL_AUDIO_I2S_TEST_PCM_DATA
    UINT32_T write_len = 0;

#if PCM_DATA_FROME_FILE
    TUYA_FILE pcm_file = NULL;

    bk_printf("audio output test mount\r\n");
    rt = tkl_fs_mount(TAL_AUDIO_TEST_MOUNT_POINT, DEV_SDCARD);
    if (rt != 0) {
        TAL_PR_ERR("audio output test mount %s failed %d", TAL_AUDIO_TEST_MOUNT_POINT, rt);
        goto __i2s_tx_exit;
    }

    bk_printf("audio output test open %s\r\n", TAL_AUDIO_TEST_PLAY_FILE);
    pcm_file = tkl_fopen(TAL_AUDIO_TEST_PLAY_FILE, "r");
    if (pcm_file == NULL) {
        TAL_PR_ERR("audio output test open file failed: %s", TAL_AUDIO_TEST_PLAY_FILE);
        goto __i2s_tx_exit;
    }

    bk_printf("audio output test start, play pcm from %s\r\n", TAL_AUDIO_TEST_PLAY_FILE);

#else   // !PCM_DATA_FROME_FILE

    INT_T amp = -6;   // 振幅（dB）
    INT_T t = 4;    // 时长（秒）
    INT_T sr = 16000; // 采样率（Hz）
    INT_T freq = 1000; // 频率（Hz）
    INT_T chnl = 2; // 通道数
    UINT32_T offset = 0;
    UINT32_T signal_len = 0;

    signal_len = sr * t * sizeof(int16_t) * chnl;
    CHAR_T *signal = (CHAR_T *)I2S_TEST_MALLOC(signal_len);
    if (signal == NULL) {
        TAL_PR_ERR("malloc audio analysis signal fail");
        goto __i2s_tx_exit;
    }
    memset(signal, 0, signal_len);
    // 生成扫频信号
    __generate_double_chnl_pcm(signal, sr, freq, amp, t);

#endif // PCM_DATA_FROME_FILE

    rt = tal_audio_output_start(inst->output_handle);
    if (rt != OPRT_OK) {
        TAL_PR_ERR("i2s%d tx test start failed: %d", inst->port, rt);
        goto __i2s_tx_exit;
    }

    inst->running = TRUE;
    bk_printf("i2s%d %s tx test started\r\n",
            inst->port, inst->is_master ? "master" : "slave");

    while (inst->running) {
#if PCM_DATA_FROME_FILE
        INT_T read_len = tkl_fread(tx_buf, I2S_TEST_CHUNK_SIZE, pcm_file);
        if (read_len < 0) {
            TAL_PR_ERR("audio output test read file failed");
            break;
        }
        if (read_len == 0) {
            bk_printf("i2s%d tx check sum %x\r\n", inst->port, i2s_tx_data_sum);
            break;
        }
        write_len = (UINT32_T)read_len;

#else // !PCM_DATA_FROME_FILE

        write_len = AUDIO_TEST_MIN(I2S_TEST_CHUNK_SIZE, signal_len - offset);
        if (write_len == 0U) {
            bk_printf("i2s%d tx check sum %x\r\n", inst->port, i2s_tx_data_sum);
            i2s_tx_data_sum = 0;
            // offset = 0;
            // write_len = AUDIO_TEST_MIN(I2S_TEST_CHUNK_SIZE, signal_len - offset);
            break;
        }
        memcpy(tx_buf, signal + offset, write_len);
        offset += write_len;

#endif // PCM_DATA_FROME_FILE

        for (int i = 0; i < write_len; i++) {
            i2s_tx_data_sum += tx_buf[i];
        }

        rt = tal_audio_output_write(inst->output_handle, tx_buf, write_len);
        if (rt != OPRT_OK) {
            TAL_PR_ERR("i2s%d tx write failed: %d", inst->port, rt);
            break;
        }

        total_sent += write_len;
        if ((total_sent % (I2S_TEST_CHUNK_SIZE * 16)) == 0) {
            TAL_PR_NOTICE("\ti2s%d tx sent %u bytes", inst->port, total_sent);
        }
    }

#if PCM_DATA_FROME_FILE
    if (pcm_file != NULL) {
        tkl_fclose(pcm_file);
    }
#else
    if (signal) {
        I2S_TEST_FREE(signal);
        signal = NULL;
    }
#endif // PCM_DATA_FROME_FILE

#endif // TAL_AUDIO_I2S_TEST_PCM_DATA || TAL_AUDIO_I2S_TEST_RAW_DATA

__i2s_tx_exit:
    inst->running = FALSE;

    if (inst->output_handle != NULL) {
        tal_audio_output_stop(inst->output_handle);
        tal_audio_output_deinit(inst->output_handle);
        inst->output_handle = NULL;
    }

    if (tx_buf != NULL) {
        I2S_TEST_FREE(tx_buf);
    }

    TAL_PR_NOTICE("i2s%d tx test finish, total_sent=%u", inst->port, total_sent);

    if (inst->thread != NULL) {
        TKL_THREAD_HANDLE self = inst->thread;
        inst->thread = NULL;
        tkl_thread_release(self);
    }
}

/**
 * @brief I2S RX test worker thread
 * @param[in] args pointer to the owning I2S_TEST_INST_T
 * @return none
 * @note Receives data via callback and periodically logs byte count.
 */
STATIC VOID_T __audio_i2s_rx_test_thread(PVOID_T args)
{
    I2S_TEST_INST_T *inst = (I2S_TEST_INST_T *)args;
    TAL_AUDIO_INPUT_CFG_T config = {0};
    OPERATE_RET rt = OPRT_OK;

    inst->rx_total_bytes = 0;

#if TAL_AUDIO_I2S_TEST_PCM_DATA
    UINT8_T *play_buf = NULL;
    TAL_AUDIO_OUTPUT_CFG_T spk_config = {0};
    TAL_AUDIO_OUTPUT_HANDLE onboard_spk_handle = NULL;
    /* 16kHz / 16bit / 20ms = (16000 * (16/8) / 1000) * 20 = 640 bytes */
    UINT32_T frame_size = (I2S_TEST_SAMPLE_RATE * (16 / 8) / 1000) * I2S_TEST_FRAME_TIME_MS;

    spk_config.type = TAL_AUDIO_OUTPUT_DAC;
    spk_config.dev.dac_config.port = 0;
    spk_config.dev.dac_config.spk_num = 1;
    /* spk_config.dev.dac_config.pa_gpio = TUYA_GPIO_NUM_28; */     /* T5-BOARD EVB */
    /* spk_config.dev.dac_config.pa_gpio = TUYA_GPIO_NUM_26; */     /* EVB PRO */
    spk_config.dev.dac_config.pa_gpio = TUYA_GPIO_NUM_39;           /* T5-AI-CORE */
    spk_config.dev.dac_config.pa_active_level = TUYA_GPIO_LEVEL_HIGH;
    spk_config.sample_bits = TUYA_AUDIO_SAMPLE_BITS_16;
    spk_config.sample_rate = I2S_TEST_SAMPLE_RATE;
    spk_config.frame_time_ms = I2S_TEST_FRAME_TIME_MS;

    onboard_spk_handle = tal_audio_output_init(&spk_config);
    if (onboard_spk_handle == NULL) {
        TAL_PR_ERR("audio output test init failed");
        goto __i2s_rx_exit;
    }

    rt = tal_audio_output_set_volume(onboard_spk_handle, TAL_AUDIO_TEST_VOLUME);
    if (rt != OPRT_OK) {
        TAL_PR_ERR("audio output test set volume failed: %d", rt);
        goto __i2s_rx_exit;
    }

    rt = tal_audio_output_start(onboard_spk_handle);
    if (rt != OPRT_OK) {
        TAL_PR_ERR("audio output test start failed: %d", rt);
        goto __i2s_rx_exit;
    }

    rt = tuya_ring_buff_create(TAL_AUDIO_TEST_RINGBUF_SIZE, OVERFLOW_PSRAM_STOP_TYPE, &s_i2s_recv_rb);
    if (rt != OPRT_OK) {
        TAL_PR_ERR("i2s%d rx ring buffer create failed: %d", inst->port, rt);
        goto __i2s_rx_exit;
    }

    play_buf = I2S_TEST_MALLOC(I2S_TEST_CHUNK_SIZE);
    if (play_buf == NULL) {
        TAL_PR_ERR("i2s%d rx play_buf malloc failed", inst->port);
        goto __i2s_rx_exit;
    }
#endif // TAL_AUDIO_I2S_TEST_PCM_DATA

    config.type = TAL_AUDIO_INPUT_I2S;
    config.dev.ai_i2s_conf.port = (TUYA_I2S_NUM_E)inst->port;
    config.dev.ai_i2s_conf.mode = (inst->is_master ? TUYA_I2S_MODE_MASTER : TUYA_I2S_MODE_SLAVE)
                                  | TUYA_I2S_MODE_RX;
    config.dev.ai_i2s_conf.i2s_protocol = I2S_TEST_PROTOCOL;
    config.dev.ai_i2s_conf.data_format = I2S_TEST_CHANNEL_FMT;
    config.dev.ai_i2s_conf.use_dma = TRUE;
    config.sample_bits = I2S_TEST_SAMPLE_BITS;
    config.sample_rate = I2S_TEST_SAMPLE_RATE;
    config.frame_time_ms = I2S_TEST_FRAME_TIME_MS;
    config.audio_input_cb = __audio_i2s_rx_test_cb;
    config.args = inst;

    inst->input_handle = tal_audio_input_init(&config);
    if (inst->input_handle == NULL) {
        TAL_PR_ERR("i2s%d rx test init failed", inst->port);
        goto __i2s_rx_exit;
    }

    rt = tal_audio_input_start(inst->input_handle);
    if (rt != OPRT_OK) {
        TAL_PR_ERR("i2s%d rx test start failed: %d", inst->port, rt);
        goto __i2s_rx_exit;
    }

    inst->running = TRUE;
    TAL_PR_NOTICE("i2s%d rx test started, %s",
                  inst->port, inst->is_master ? "master" : "slave");

    while (inst->running) {
#if TAL_AUDIO_I2S_TEST_RAW_DATA
        tkl_system_sleep(1000);
        TAL_PR_NOTICE("i2s%d rx received %u bytes", inst->port, inst->rx_total_bytes);
#elif TAL_AUDIO_I2S_TEST_PCM_DATA
        UINT32_T avail = tuya_ring_buff_used_size_get(s_i2s_recv_rb);
        if (avail < frame_size) {
            tkl_system_sleep(10);
            continue;
        }
        tuya_ring_buff_read(s_i2s_recv_rb, play_buf, frame_size);
        tal_audio_output_write(onboard_spk_handle, play_buf, frame_size);
        bk_printf("rx %d, check sum %x\r\n", __i2s_rx_data_len, __i2s_rx_data_sum);
#endif
    }

__i2s_rx_exit:
    inst->running = FALSE;

    if (inst->input_handle != NULL) {
        tal_audio_input_stop(inst->input_handle);
        tal_audio_input_deinit(inst->input_handle);
        inst->input_handle = NULL;
    }

#if TAL_AUDIO_I2S_TEST_PCM_DATA
    if (onboard_spk_handle != NULL) {
        tal_audio_output_stop(onboard_spk_handle);
        tal_audio_output_deinit(onboard_spk_handle);
        onboard_spk_handle = NULL;
    }
    if (s_i2s_recv_rb != NULL) {
        tuya_ring_buff_free(s_i2s_recv_rb);
        s_i2s_recv_rb = NULL;
    }
    if (play_buf != NULL) {
        I2S_TEST_FREE(play_buf);
        play_buf = NULL;
    }
#endif

    TAL_PR_NOTICE("i2s%d rx test finish, total_received=%u", inst->port, inst->rx_total_bytes);

    if (inst->thread != NULL) {
        TKL_THREAD_HANDLE self = inst->thread;
        inst->thread = NULL;
        tkl_thread_release(self);
    }
}

/**
 * @brief Start a single I2S test instance (TX or RX)
 * @param[in] port I2S port number (0, 1, 2)
 * @param[in] is_master 1 for master, 0 for slave
 * @param[in] dir I2S_TEST_DIR_RX or I2S_TEST_DIR_TX
 * @return none
 */
STATIC VOID_T __audio_i2s_test_start_one(UINT8_T port, UINT8_T is_master, UINT8_T dir)
{
    OPERATE_RET rt;
    I2S_TEST_INST_T *inst = &sg_i2s_inst[port][dir];
    CONST CHAR_T *dir_str = (dir == I2S_TEST_DIR_TX) ? "tx" : "rx";
    CHAR_T name[16];

    if (inst->thread != NULL) {
        TAL_PR_NOTICE("i2s%d %s test already running", port, dir_str);
        return;
    }

    memset(inst, 0, sizeof(I2S_TEST_INST_T));
    inst->port = port;
    inst->is_master = is_master;

    snprintf(name, sizeof(name), "ti2s%d_%s", port, dir_str);

    rt = tkl_thread_create(&inst->thread, name, 8192, 5,
                           (dir == I2S_TEST_DIR_TX) ? __audio_i2s_tx_test_thread
                                                    : __audio_i2s_rx_test_thread,
                           inst);
    if (rt != OPRT_OK) {
        inst->thread = NULL;
        TAL_PR_ERR("i2s%d %s test create thread failed: %d", port, dir_str, rt);
        return;
    }

    TAL_PR_NOTICE("i2s test thread created, port=%d %s %s",
                  port, is_master ? "master" : "slave", dir_str);
}

/**
 * @brief Start I2S test from CLI
 * @param[in] port I2S port number (0, 1, 2)
 * @param[in] is_master 1 for master, 0 for slave
 * @param[in] direction 0: RX, 1: TX, 2: both RX and TX
 * @return none
 * @note When direction == 2, two threads are created (RX + TX). The TKL
 *       layer merges both directions into one HW instance via tkl_i2s_init.
 */
void tal_audio_i2s_test(UINT8_T port, UINT8_T is_master, UINT8_T direction)
{
    if (port >= TUYA_I2S_NUM_MAX) {
        TAL_PR_ERR("i2s test invalid port %d", port);
        return;
    }

    if (direction > 2) {
        TAL_PR_ERR("i2s test invalid direction %d (0=rx,1=tx,2=both)", direction);
        return;
    }

    if (direction == 0 || direction == 2) {
        __audio_i2s_test_start_one(port, is_master, I2S_TEST_DIR_RX);
    }
    if (direction == 1 || direction == 2) {
        __audio_i2s_test_start_one(port, is_master, I2S_TEST_DIR_TX);
    }
}
