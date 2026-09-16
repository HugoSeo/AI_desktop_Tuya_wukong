/**
 * @file tal_audio_onboard.c
 * @brief Onboard audio ADC / DAC / DMIC test cases
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
#include "tal_thread.h"
#include "tkl_fs.h"
#include "tkl_memory.h"
#include "tkl_mutex.h"
#include "tkl_system.h"
#include "tkl_thread.h"
#include "tuya_ringbuf.h"
#include "uni_log.h"
#include <math.h>
#include <string.h>

extern int test_fs_mount(CONST CHAR_T *path, FS_DEV_TYPE_T dev_type);
extern int test_fs_unmount(CONST CHAR_T *path);

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define SOC_AON_GPIO_REG_BASE_TEST    (0x44000400 + 0)
#ifndef GPIO_UP
    #define GPIO_UP(id) *(volatile uint32_t*) (SOC_AON_GPIO_REG_BASE_TEST + ((id) << 2)) = 2
#endif

#ifndef GPIO_DOWN
    #define GPIO_DOWN(id) *(volatile uint32_t*) (SOC_AON_GPIO_REG_BASE_TEST + ((id) << 2)) = 0
#endif

/* ---------------------------------------------------------------------------
 * Macros
 * --------------------------------------------------------------------------- */
#define TAL_AUDIO_TEST_MOUNT_POINT      "/sdcard"
#define TAL_AUDIO_TEST_RECORD_FILE      TAL_AUDIO_TEST_MOUNT_POINT "/tal_audio_input_adc.pcm"
// #define TAL_AUDIO_TEST_PLAY_FILE        TAL_AUDIO_TEST_MOUNT_POINT "/mic_record.pcm"
#define TAL_AUDIO_TEST_PLAY_FILE        TAL_AUDIO_TEST_MOUNT_POINT "/dingdong_zh.pcm"
#define TAL_AUDIO_TEST_SAMPLE_RATE      16000U
#define TAL_AUDIO_TEST_SAMPLE_BITS      16U
#define TAL_AUDIO_TEST_SAMPLE_CHAN_NUM  2U
#define TAL_AUDIO_TEST_FRAME_TIME_MS    20U
#define TAL_AUDIO_TEST_WRITE_CHUNK      4096U
#define TAL_AUDIO_TEST_RECORD_SECONDS   20U
#define TAL_AUDIO_TEST_MAX_WRITE_BYTES  (TAL_AUDIO_TEST_SAMPLE_RATE * TAL_AUDIO_TEST_SAMPLE_CHAN_NUM * (TAL_AUDIO_TEST_SAMPLE_BITS / 8U) * TAL_AUDIO_TEST_RECORD_SECONDS)
#define TAL_AUDIO_TEST_RB_SIZE          (256U * 1024U)
#define TAL_AUDIO_TEST_VOLUME           100U
#define TAL_AUDIO_TEST_MIN(a, b)        (((a) < (b)) ? (a) : (b))
#ifndef TAL_AUDIO_TEST_SPK_FILE
#define TAL_AUDIO_TEST_SPK_FILE         1
#endif

#if defined(ENABLE_EXT_RAM) && (ENABLE_EXT_RAM == 1)
#define TAL_AUDIO_TEST_MALLOC(size)     tkl_system_psram_malloc(size)
#define TAL_AUDIO_TEST_FREE(ptr)        tkl_system_psram_free(ptr)
#else
#define TAL_AUDIO_TEST_MALLOC(size)     tkl_system_malloc(size)
#define TAL_AUDIO_TEST_FREE(ptr)        tkl_system_free(ptr)
#endif

/* ---------------------------------------------------------------------------
 * File scope variables
 * --------------------------------------------------------------------------- */
static THREAD_HANDLE sg_audio_onboard_mic_test_thread = NULL;
static THREAD_HANDLE sg_audio_onboard_spk_test_thread = NULL;
static TAL_AUDIO_INPUT_HANDLE sg_audio_onboard_mic_handle = NULL;
static TAL_AUDIO_OUTPUT_HANDLE sg_audio_onboard_spk_handle = NULL;
static volatile BOOL_T sg_audio_onboard_mic_test_running = FALSE;
static volatile UINT32_T sg_audio_test_onboard_mic_drop_bytes = 0;
static TUYA_RINGBUFF_T sg_audio_onboard_mic_ringbuf = NULL;
static TKL_MUTEX_HANDLE sg_audio_test_file_mutex = NULL;

/* ---------------------------------------------------------------------------
 * File I/O helpers (mutex-protected)
 * --------------------------------------------------------------------------- */
/**
 * @brief Initialize file mutex (lazy, idempotent)
 * @return OPRT_OK on success
 */
STATIC OPERATE_RET __audio_test_file_mutex_init(VOID_T)
{
    if (sg_audio_test_file_mutex != NULL) {
        return OPRT_OK;
    }

    return tkl_mutex_create_init(&sg_audio_test_file_mutex);
}

/**
 * @brief Acquire file mutex
 * @return OPRT_OK on success
 */
STATIC OPERATE_RET __audio_test_file_lock(VOID_T)
{
    OPERATE_RET rt = __audio_test_file_mutex_init();
    if (rt != OPRT_OK) {
        return rt;
    }

    return tkl_mutex_lock(sg_audio_test_file_mutex);
}

/**
 * @brief Release file mutex
 * @return none
 */
STATIC VOID_T __audio_test_file_unlock(VOID_T)
{
    if (sg_audio_test_file_mutex != NULL) {
        tkl_mutex_unlock(sg_audio_test_file_mutex);
    }
}

/**
 * @brief Open file with mutex protection
 * @param[in] path File path
 * @param[in] mode Open mode string
 * @return File handle or NULL on failure
 */
STATIC TUYA_FILE __audio_test_fopen(CONST CHAR_T *path, CONST CHAR_T *mode)
{
    TUYA_FILE file = NULL;

    if (__audio_test_file_lock() != OPRT_OK) {
        return NULL;
    }

    file = tkl_fopen(path, mode);
    __audio_test_file_unlock();

    return file;
}

/**
 * @brief Close file with mutex protection
 * @param[in] file File handle
 * @return 0 on success, negative on error
 */
STATIC INT_T __audio_test_fclose(TUYA_FILE file)
{
    INT_T ret = OPRT_OS_ADAPTER_MUTEX_LOCK_FAILED;

    if (__audio_test_file_lock() != OPRT_OK) {
        return ret;
    }

    ret = tkl_fclose(file);
    __audio_test_file_unlock();

    return ret;
}

/**
 * @brief Read file with mutex protection
 * @param[out] buf Output buffer
 * @param[in] bytes Number of bytes to read
 * @param[in] file File handle
 * @return Bytes read, or negative on error
 */
STATIC INT_T __audio_test_fread(VOID_T *buf, INT_T bytes, TUYA_FILE file)
{
    INT_T ret = OPRT_OS_ADAPTER_MUTEX_LOCK_FAILED;

    if (__audio_test_file_lock() != OPRT_OK) {
        return ret;
    }

    ret = tkl_fread(buf, bytes, file);
    __audio_test_file_unlock();

    return ret;
}

/**
 * @brief Sync file with mutex protection
 * @param[in] fd File descriptor
 * @return OPRT_OK on success
 */
STATIC OPERATE_RET __audio_test_fsync(INT_T fd)
{
    OPERATE_RET ret = OPRT_OS_ADAPTER_MUTEX_LOCK_FAILED;

    if (__audio_test_file_lock() != OPRT_OK) {
        return ret;
    }

    ret = tkl_fsync(fd);
    __audio_test_file_unlock();

    return ret;
}

/**
 * @brief Write all bytes to file with mutex protection
 * @param[in] file File handle
 * @param[in] buf Data buffer
 * @param[in] len Number of bytes to write
 * @return OPRT_OK on success
 */
STATIC OPERATE_RET __audio_test_write_all(TUYA_FILE file, UINT8_T *buf, UINT32_T len)
{
    UINT32_T total_written = 0;
    OPERATE_RET lock_ret = OPRT_OK;

    if ((file == NULL) || (buf == NULL)) {
        return OPRT_INVALID_PARM;
    }

    lock_ret = __audio_test_file_lock();
    if (lock_ret != OPRT_OK) {
        return lock_ret;
    }

    while (total_written < len) {
        INT_T write_len = tkl_fwrite(buf + total_written, len - total_written, file);
        if (write_len <= 0) {
            __audio_test_file_unlock();
            return OPRT_COM_ERROR;
        }
        total_written += (UINT32_T)write_len;
    }

    __audio_test_file_unlock();
    return OPRT_OK;
}

/* ---------------------------------------------------------------------------
 * ADC mic input test
 * --------------------------------------------------------------------------- */
/**
 * @brief Audio input callback for onboard ADC mic test
 * @param[in] frame Audio frame from TAL input layer
 * @param[in] args Unused
 * @return none
 */
STATIC VOID_T __audio_onboard_mic_input_test_cb(TAL_AUDIO_FRAME_T *frame, VOID_T *args)
{
    UINT32_T push_len = 0;

    (void)args;

    if ((sg_audio_onboard_mic_test_running == FALSE) || (sg_audio_onboard_mic_ringbuf == NULL) ||
        (frame == NULL) || (frame->buf == NULL) || (frame->len == 0U)) {
        return;
    }

    push_len = tuya_ring_buff_write(sg_audio_onboard_mic_ringbuf, frame->buf, frame->len);
    if (push_len < frame->len) {
        sg_audio_test_onboard_mic_drop_bytes += (frame->len - push_len);
    }
}

/**
 * @brief ADC mic test worker thread
 * @param[in] args Unused
 * @return none
 * @note Records PCM data from onboard ADC to SD card file.
 */
STATIC VOID_T __audio_onboard_mic_test_thread(PVOID_T args)
{
    TUYA_FILE record_file = NULL;
    TAL_AUDIO_INPUT_CFG_T config = {0};
    UINT32_T total_write_bytes = 0;
    BOOL_T mounted_by_self = FALSE;
    INT_T mount_ret = 0;
    OPERATE_RET rt = OPRT_OK;

    (void)args;

    sg_audio_test_onboard_mic_drop_bytes = 0;

    UINT8_T *in_write_buf = TAL_AUDIO_TEST_MALLOC(TAL_AUDIO_TEST_WRITE_CHUNK);
    if (in_write_buf == NULL) {
        TAL_PR_ERR("audio input test malloc chunk buffer failed: %d");
        goto __audio_test_exit;
    }

    rt = tuya_ring_buff_create(TAL_AUDIO_TEST_RB_SIZE, OVERFLOW_STOP_TYPE | TY_RINGBUF_PSRAM_FLAG, &sg_audio_onboard_mic_ringbuf);
    if (rt != OPRT_OK) {
        TAL_PR_ERR("audio input test create ringbuf failed: %d", rt);
        sg_audio_onboard_mic_ringbuf = NULL;
        goto __audio_test_exit;
    }

    mount_ret = test_fs_mount(TAL_AUDIO_TEST_MOUNT_POINT, DEV_SDCARD);
    if (mount_ret != 0) {
        TAL_PR_ERR("audio input test mount %s failed %d", TAL_AUDIO_TEST_MOUNT_POINT, mount_ret);
        goto __audio_test_exit;
    }
    mounted_by_self = TRUE;

    record_file = __audio_test_fopen(TAL_AUDIO_TEST_RECORD_FILE, "w+");
    if (record_file == NULL) {
        TAL_PR_ERR("audio input test open file failed: %s", TAL_AUDIO_TEST_RECORD_FILE);
        goto __audio_test_exit;
    }

    config.type = TAL_AUDIO_INPUT_ADC;
    config.dev.ai_adc_conf.port = TUYA_AUDIO_ADC_PORT_0;
    config.dev.ai_adc_conf.chan = TUYA_AUDIO_ADC_CHANNEL_LR;
    config.sample_bits = TUYA_AUDIO_SAMPLE_BITS_16;
    config.sample_rate = TAL_AUDIO_TEST_SAMPLE_RATE;
    config.frame_time_ms = TAL_AUDIO_TEST_FRAME_TIME_MS;
    config.audio_input_cb = __audio_onboard_mic_input_test_cb;
    config.args = NULL;

    sg_audio_onboard_mic_handle = tal_audio_input_init(&config);
    if (sg_audio_onboard_mic_handle == NULL) {
        TAL_PR_ERR("audio input test init failed");
        goto __audio_test_exit;
    }

    rt = tal_audio_input_set_volume(sg_audio_onboard_mic_handle, TAL_AUDIO_TEST_VOLUME);
    if (rt != OPRT_OK) {
        TAL_PR_ERR("audio input test set volume failed: %d", rt);
        goto __audio_test_exit;
    }

    sg_audio_onboard_mic_test_running = TRUE;
    rt = tal_audio_input_start(sg_audio_onboard_mic_handle);
    if (rt != OPRT_OK) {
        TAL_PR_ERR("audio input test start failed: %d", rt);
        sg_audio_onboard_mic_test_running = FALSE;
        goto __audio_test_exit;
    }

    TAL_PR_NOTICE("audio input test start, write pcm to %s", TAL_AUDIO_TEST_RECORD_FILE);

    bk_printf("-------------------- %s %d\r\n", __func__, __LINE__);
    while (total_write_bytes < TAL_AUDIO_TEST_MAX_WRITE_BYTES) {
        UINT32_T avail = tuya_ring_buff_used_size_get(sg_audio_onboard_mic_ringbuf);
        UINT32_T remain = TAL_AUDIO_TEST_MAX_WRITE_BYTES - total_write_bytes;
        UINT32_T pop_len = 0;

        if (avail == 0U) {
            tkl_system_sleep(20);
            continue;
        }

        pop_len = TAL_AUDIO_TEST_MIN(avail, TAL_AUDIO_TEST_WRITE_CHUNK);
        pop_len = TAL_AUDIO_TEST_MIN(pop_len, remain);
        pop_len = tuya_ring_buff_read(sg_audio_onboard_mic_ringbuf, in_write_buf, pop_len);
        if (pop_len == 0U) {
            tkl_system_sleep(10);
            continue;
        }

        rt = __audio_test_write_all(record_file, in_write_buf, pop_len);
        if (rt != OPRT_OK) {
            TAL_PR_ERR("audio input test write file failed: %d", rt);
            break;
        }

        __audio_test_fsync((INT_T)record_file);
        total_write_bytes += pop_len;
    }
    bk_printf("-------------------- %s %d\r\n", __func__, __LINE__);

__audio_test_exit:
    sg_audio_onboard_mic_test_running = FALSE;

    if (sg_audio_onboard_mic_handle != NULL) {
        tal_audio_input_stop(sg_audio_onboard_mic_handle);
        tal_audio_input_deinit(sg_audio_onboard_mic_handle);
        sg_audio_onboard_mic_handle = NULL;
    }

    if (record_file != NULL) {
        __audio_test_fclose(record_file);
    }

    if (in_write_buf != NULL) {
        TAL_AUDIO_TEST_FREE(in_write_buf);
        in_write_buf = NULL;
    }

    if (sg_audio_onboard_mic_ringbuf != NULL) {
        tuya_ring_buff_free(sg_audio_onboard_mic_ringbuf);
        sg_audio_onboard_mic_ringbuf = NULL;
    }

    if (mounted_by_self == TRUE) {
        test_fs_unmount(TAL_AUDIO_TEST_MOUNT_POINT);
    }

    TAL_PR_NOTICE("audio input test finish, bytes=%u, dropped=%u, file=%s",
              total_write_bytes, sg_audio_test_onboard_mic_drop_bytes, TAL_AUDIO_TEST_RECORD_FILE);

    if (sg_audio_onboard_mic_test_thread != NULL) {
        THREAD_HANDLE self_thread = sg_audio_onboard_mic_test_thread;
        sg_audio_onboard_mic_test_thread = NULL;
        tal_thread_delete(self_thread);
    }
    bk_printf("-------------------- %s %d\r\n", __func__, __LINE__);
}

/* ---------------------------------------------------------------------------
 * DAC speaker output test
 * --------------------------------------------------------------------------- */
/**
 * @brief Generate single-frequency PCM signal
 * @param[out] signal Output buffer
 * @param[in] sr Sample rate (Hz)
 * @param[in] freq Frequency (Hz)
 * @param[in] amp Amplitude in dB (0, -3, -6, etc.)
 * @param[in] t Duration in seconds
 * @return 0 on success
 */
static int generate_single_pcm(char *signal, int sr, int freq, int amp, int t)
{
    int16_t *s = (int16_t *)signal;
    double a = powf(10, amp/20.0f);
    bk_printf("amp = %d, %.3f\r\n", amp, a);

    int samples = sr*t;
    for (int i=0; i<samples; i++) {
        s[i] = (int16_t)(a * sin(2 * M_PI * freq * i * 1.0f / sr) * 32767);
    }

    return 0;
}

/**
 * @brief DAC speaker test worker thread
 * @param[in] args Unused
 * @return none
 * @note Plays PCM data from SD card file or generated signal via onboard DAC.
 */
STATIC VOID_T __audio_onboard_spk_test_thread(PVOID_T args)
{
    TUYA_FILE record_file = NULL;
    TAL_AUDIO_OUTPUT_CFG_T config = {0};
    UINT8_T *play_buf = NULL;
    CHAR_T *signal = NULL;
    UINT32_T signal_len = 0;
    UINT32_T offset = 0;
    UINT32_T total_play_bytes = 0;
    BOOL_T mounted_by_self = FALSE;
    INT_T mount_ret = 0;
    OPERATE_RET rt = OPRT_OK;

    (void)args;
    TAL_PR_NOTICE("audio output test init");

    bk_printf("-------------------- %s %d\r\n", __func__, __LINE__);
    config.type = TAL_AUDIO_OUTPUT_DAC;
    config.dev.dac_config.port = 0;
    config.dev.dac_config.spk_num = 1;
    config.dev.dac_config.pa_gpio = TUYA_GPIO_NUM_28;        // T5-BOARD EVB BOARD
    // config.dev.dac_config.pa_gpio = TUYA_GPIO_NUM_26;           // EVB PRO BOARD
    config.dev.dac_config.pa_active_level = TUYA_GPIO_LEVEL_HIGH;
    config.sample_bits = TUYA_AUDIO_SAMPLE_BITS_16;
    config.sample_rate = TAL_AUDIO_TEST_SAMPLE_RATE;
    config.frame_time_ms = TAL_AUDIO_TEST_FRAME_TIME_MS;

    bk_printf("-------------------- %s %d\r\n", __func__, __LINE__);
    sg_audio_onboard_spk_handle = tal_audio_output_init(&config);
    if (sg_audio_onboard_spk_handle == NULL) {
        TAL_PR_ERR("audio output test init failed");
        goto __audio_output_test_exit;
    }

#if TAL_AUDIO_TEST_SPK_FILE
    bk_printf("-------------------- %s %d\r\n", __func__, __LINE__);
    TAL_PR_NOTICE("audio output test mount");
    mount_ret = test_fs_mount(TAL_AUDIO_TEST_MOUNT_POINT, DEV_SDCARD);
    if (mount_ret != 0) {
        TAL_PR_ERR("audio output test mount %s failed %d", TAL_AUDIO_TEST_MOUNT_POINT, mount_ret);
        goto __audio_output_test_exit;
    }
    mounted_by_self = TRUE;
    TAL_PR_NOTICE("audio output test open %s", TAL_AUDIO_TEST_PLAY_FILE);
    record_file = __audio_test_fopen(TAL_AUDIO_TEST_PLAY_FILE, "r");
    if (record_file == NULL) {
        TAL_PR_ERR("audio output test open file failed: %s", TAL_AUDIO_TEST_PLAY_FILE);
        goto __audio_output_test_exit;
    }
#else

#define TAL_AUDIO_TEST_SIGNAL_TIME 4

    INT_T amp = -6;   // 振幅（dB）
    INT_T t = TAL_AUDIO_TEST_SIGNAL_TIME;    // 时长（秒）
    INT_T sr = 16000; // 采样率（Hz）
    INT_T freq = 1000; // 频率（Hz）

    signal_len = sr * t * sizeof(int16_t);
    signal = (CHAR_T *)TAL_AUDIO_TEST_MALLOC(signal_len);
    if (signal == NULL) {
        TAL_PR_ERR("malloc audio analysis signal fail");
        goto __audio_output_test_exit;
    }
    memset(signal, 0, signal_len);
    // 生成扫频信号
    generate_single_pcm(signal, sr, freq, amp, t);
#endif

    play_buf = (UINT8_T *)TAL_AUDIO_TEST_MALLOC(TAL_AUDIO_TEST_WRITE_CHUNK);
    if (play_buf == NULL) {
        TAL_PR_ERR("audio output test malloc play buffer failed");
        goto __audio_output_test_exit;
    }

    rt = tal_audio_output_set_volume(sg_audio_onboard_spk_handle, TAL_AUDIO_TEST_VOLUME);
    if (rt != OPRT_OK) {
        TAL_PR_ERR("audio output test set volume failed: %d", rt);
        goto __audio_output_test_exit;
    }

    rt = tal_audio_output_start(sg_audio_onboard_spk_handle);
    if (rt != OPRT_OK) {
        TAL_PR_ERR("audio output test start failed: %d", rt);
        goto __audio_output_test_exit;
    }

#if TAL_AUDIO_TEST_SPK_FILE
    TAL_PR_NOTICE("audio output test start, play pcm from %s", TAL_AUDIO_TEST_PLAY_FILE);
#else
    TAL_PR_NOTICE("audio output test start, play generated pcm data");
#endif

    while (1) {
        UINT32_T write_len = 0;

#if TAL_AUDIO_TEST_SPK_FILE
        INT_T read_len = __audio_test_fread(play_buf, TAL_AUDIO_TEST_WRITE_CHUNK, record_file);

        if (read_len < 0) {
            TAL_PR_ERR("audio output test read file failed");
            break;
        }

        if (read_len == 0) {
            break;
        }
        write_len = (UINT32_T)read_len;
#else
        write_len = TAL_AUDIO_TEST_MIN(TAL_AUDIO_TEST_WRITE_CHUNK, signal_len - offset);
        if (write_len == 0U) {
            break;
        }
        memcpy(play_buf, signal + offset, write_len);
        offset += write_len;
#endif

        rt = tal_audio_output_write(sg_audio_onboard_spk_handle, play_buf, write_len);
        if (rt != OPRT_OK) {
            TAL_PR_ERR("audio output test write failed: %d", rt);
            break;
        }

        total_play_bytes += write_len;
    }

    bk_printf("-------------------- %s %d\r\n", __func__, __LINE__);
__audio_output_test_exit:
    if (sg_audio_onboard_spk_handle != NULL) {
        tal_audio_output_stop(sg_audio_onboard_spk_handle);
        tal_audio_output_deinit(sg_audio_onboard_spk_handle);
        sg_audio_onboard_spk_handle = NULL;
    }

    bk_printf("-------------------- %s %d\r\n", __func__, __LINE__);
    if (play_buf != NULL) {
        TAL_AUDIO_TEST_FREE(play_buf);
        play_buf = NULL;
    }

    bk_printf("-------------------- %s %d\r\n", __func__, __LINE__);
#if TAL_AUDIO_TEST_SPK_FILE
    if (record_file != NULL) {
        __audio_test_fclose(record_file);
    }
#else
    if (signal) {
        TAL_AUDIO_TEST_FREE(signal);
        signal = NULL;
    }
#endif

    bk_printf("-------------------- %s %d\r\n", __func__, __LINE__);
    if (mounted_by_self == TRUE) {
        test_fs_unmount(TAL_AUDIO_TEST_MOUNT_POINT);
    }

#if TAL_AUDIO_TEST_SPK_FILE
    TAL_PR_NOTICE("audio output test finish, bytes=%u, file=%s", total_play_bytes, TAL_AUDIO_TEST_PLAY_FILE);
#else
    TAL_PR_NOTICE("audio output test finish, bytes=%u, source=generated pcm", total_play_bytes);
#endif

    if (sg_audio_onboard_spk_test_thread != NULL) {
        THREAD_HANDLE self_thread = sg_audio_onboard_spk_test_thread;
        sg_audio_onboard_spk_test_thread = NULL;
        tal_thread_delete(self_thread);
    }
}

/* ---------------------------------------------------------------------------
 * Dual-input sync test (ADC ref + DMIC pickup)
 *
 * Each ISR callback serializes a complete frame (event/len/seq_no/time_stamp
 * + PCM data) into its own ring buffer. The sync thread reads one complete
 * frame at a time from each ring buffer, compares seq_no, discards the older
 * frame on mismatch, and writes matched pairs to two separate files:
 *   /sdcard/sync_adc_ref.pcm   -- ADC reference signal
 *   /sdcard/sync_dmic_pickup.pcm -- DMIC pickup signal
 * --------------------------------------------------------------------------- */
#define SYNC_TEST_FRAME_TIME_MS     8U
#define SYNC_TEST_SAMPLE_RATE       16000U
#define SYNC_TEST_SAMPLE_BITS       16U
#define SYNC_TEST_CHAN_NUM          2U
#define SYNC_TEST_RB_SIZE           (128U * 1024U)
#define SYNC_TEST_RECORD_SECONDS    6U
#define SYNC_TEST_ADC_FILE          TAL_AUDIO_TEST_MOUNT_POINT "/sync_adc_ref.pcm"
#define SYNC_TEST_DMIC_FILE         TAL_AUDIO_TEST_MOUNT_POINT "/sync_dmic_pickup.pcm"
#define SYNC_TEST_PCM_FRAME_SIZE    ((SYNC_TEST_SAMPLE_RATE * (SYNC_TEST_SAMPLE_BITS / 8U) / 1000U) * SYNC_TEST_CHAN_NUM * SYNC_TEST_FRAME_TIME_MS)
#define SYNC_TEST_MAX_PAIRS         ((SYNC_TEST_RECORD_SECONDS * 1000U) / SYNC_TEST_FRAME_TIME_MS)
#define SYNC_TEST_GAIN_COEFFICIENT  6
#define SYNC_TEST_HP_CUTOFF_HZ      60U

/* ---------------------------------------------------------------------------
 * Type definitions
 * --------------------------------------------------------------------------- */
typedef struct {
    TUYA_AUDIO_FRAME_EVT_E event;
    UINT32_T               len;
    UINT32_T               seq_no;
    SYS_TIME_T             time_stamp;
    UINT8_T                data[SYNC_TEST_PCM_FRAME_SIZE];
} SYNC_TEST_FRAME_T;

typedef struct {
    TUYA_RINGBUFF_T     adc_rb;
    TUYA_RINGBUFF_T     dmic_rb;
    volatile BOOL_T     running;
    volatile UINT32_T   adc_drop;
    volatile UINT32_T   dmic_drop;
} SYNC_TEST_CTX_T;

typedef struct {
    float alpha;
    float prev_x;
    float prev_y;
    int   inited;
} audio_hpf1_t;

#ifndef __maybe_unused
#if __GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ >= 4)
#define __maybe_unused __attribute__((unused))
#else
#define __maybe_unused
#endif /* __GNUC__ */
#endif /* __maybe_unused */

STATIC __maybe_unused audio_hpf1_t g_hpf1_l, g_hpf1_r, g_hpf1_ref;
STATIC UINT8_T sg_dmic_gain_coe = SYNC_TEST_GAIN_COEFFICIENT;
STATIC SYNC_TEST_CTX_T sg_sync_ctx;
STATIC TAL_AUDIO_INPUT_HANDLE sg_sync_adc_handle = NULL;
STATIC TAL_AUDIO_INPUT_HANDLE sg_sync_dmic_handle = NULL;
STATIC THREAD_HANDLE sg_sync_test_thread = NULL;
STATIC TAL_AUDIO_INPUT_CFG_T sg_dmic_cfg = {0};

/**
 * @brief Initialize first-order high-pass filter
 * @param[in] st Filter state
 * @param[in] fs Sample rate (Hz)
 * @param[in] fc Cutoff frequency (Hz)
 * @return none
 */
static inline void audio_hpf1_init(audio_hpf1_t *st, float fs, float fc)
{
    if (!st) return;
    if (fc < 1.0f) fc = 1.0f;
    float RC = 1.0f / (2.0f * (float)M_PI * fc);
    float dt = 1.0f / fs;
    st->alpha = RC / (RC + dt);
    st->prev_x = 0.0f;
    st->prev_y = 0.0f;
    st->inited = 1;
}

/**
 * @brief Process one sample through first-order high-pass filter
 * @param[in] st Filter state
 * @param[in] x Input sample
 * @return Filtered sample
 */
static inline int16_t audio_hpf1_process(audio_hpf1_t *st, int16_t x)
{
    if (!st || !st->inited) return x;
    float xf = (float)x;
    float y = st->alpha * (st->prev_y + xf - st->prev_x);
    st->prev_x = xf;
    st->prev_y = y;
    if (y > 32767.0f) y = 32767.0f;
    else if (y < -32768.0f) y = -32768.0f;
    return (int16_t)y;
}

/**
 * @brief Serialize a complete audio frame into ring buffer
 * @param[in] rb Target ring buffer
 * @param[in] frame Audio frame from TAL callback
 * @return Bytes written, 0 on insufficient space or invalid frame size
 */
STATIC UINT32_T __sync_rb_push_frame(TUYA_RINGBUFF_T rb, TAL_AUDIO_FRAME_T *frame)
{
    SYNC_TEST_FRAME_T rb_frame;

    if (frame->len > SYNC_TEST_PCM_FRAME_SIZE) {
        return 0;
    }

    if (tuya_ring_buff_free_size_get(rb) < sizeof(SYNC_TEST_FRAME_T)) {
        return 0;
    }

    memset(&rb_frame, 0, sizeof(SYNC_TEST_FRAME_T));
    rb_frame.event = frame->event;
    rb_frame.len = frame->len;
    rb_frame.seq_no = frame->seq_no;
    rb_frame.time_stamp = frame->time_stamp;
    memcpy(rb_frame.data, frame->buf, frame->len);

    return tuya_ring_buff_write(rb, (UINT8_T *)&rb_frame, sizeof(SYNC_TEST_FRAME_T));
}

/**
 * @brief ADC reference callback -- pushes complete frame into adc ring buffer
 * @param[in] frame Audio frame
 * @param[in] args Unused
 * @return none
 */
STATIC VOID_T __audio_adc_ref_cb(TAL_AUDIO_FRAME_T *frame, VOID_T *args)
{
    // GPIO_UP(15);
    // GPIO_DOWN(15);
    (void)args;
    if (!sg_sync_ctx.running || frame == NULL || frame->buf == NULL || frame->len == 0U) {
        return;
    }
    if (__sync_rb_push_frame(sg_sync_ctx.adc_rb, frame) < sizeof(SYNC_TEST_FRAME_T)) {
        sg_sync_ctx.adc_drop += frame->len;
    }
}

/**
 * @brief DMIC pickup callback -- pushes complete frame into dmic ring buffer
 * @param[in] frame Audio frame
 * @param[in] args Pointer to DMIC configuration (TAL_AUDIO_INPUT_CFG_T *)
 * @return none
 */
STATIC VOID_T __audio_dmic_pickup_cb(TAL_AUDIO_FRAME_T *frame, VOID_T *args)
{
    TAL_AUDIO_INPUT_CFG_T *dmic_config = (TAL_AUDIO_INPUT_CFG_T *)args;

    if (!sg_sync_ctx.running || frame == NULL || frame->buf == NULL || frame->len == 0U) {
        return;
    }

    if (sg_dmic_gain_coe != 1) {

        if (dmic_config == NULL) {
            return;
        }

        UINT32_T sample_bytes = dmic_config->sample_bits / 8;
        UINT32_T sample_cnt = frame->len / sample_bytes;
        UINT32_T i;

        switch (dmic_config->sample_bits) {
        case TUYA_AUDIO_SAMPLE_BITS_8: {
            UINT8_T *samples = frame->buf;
            for (i = 0; i < sample_cnt; i++) {
                UINT32_T val = (UINT32_T)samples[i] * sg_dmic_gain_coe;
                if (val > 0xFF) {
                    val = 0xFF;
                }
                samples[i] = (UINT8_T)val;
            }
            break;
        }
        case TUYA_AUDIO_SAMPLE_BITS_16: {
            INT16_T *samples = (INT16_T *)frame->buf;
            for (i = 0; i < sample_cnt; i++) {
                INT32_T val = (INT32_T)samples[i] * (INT32_T)sg_dmic_gain_coe;
                if (val > 0x7FFF) {
                    val = 0x7FFF;
                } else if (val < -0x8000) {
                    val = -0x8000;
                }
                samples[i] = (INT16_T)val;
            }
            break;
        }
        case TUYA_AUDIO_SAMPLE_BITS_24: {
            for (i = 0; i < sample_cnt; i++) {
                UINT32_T offset = i * 3;
                INT32_T val = (INT32_T)(frame->buf[offset] |
                              ((UINT32_T)frame->buf[offset + 1] << 8) |
                              ((UINT32_T)frame->buf[offset + 2] << 16));
                if (val & 0x00800000) {
                    val |= (INT32_T)0xFF000000;
                }
                val *= (INT32_T)sg_dmic_gain_coe;
                if (val > 0x7FFFFF) {
                    val = 0x7FFFFF;
                } else if (val < -0x800000) {
                    val = -0x800000;
                }
                frame->buf[offset]     = (UINT8_T)(val & 0xFF);
                frame->buf[offset + 1] = (UINT8_T)((val >> 8) & 0xFF);
                frame->buf[offset + 2] = (UINT8_T)((val >> 16) & 0xFF);
            }
            break;
        }
        case TUYA_AUDIO_SAMPLE_BITS_32: {
            INT32_T *samples = (INT32_T *)frame->buf;
            for (i = 0; i < sample_cnt; i++) {
                int64_t val = (int64_t)samples[i] * (int64_t)sg_dmic_gain_coe;
                if (val > (int64_t)0x7FFFFFFF) {
                    val = (int64_t)0x7FFFFFFF;
                } else if (val < -(int64_t)0x80000000) {
                    val = -(int64_t)0x80000000;
                }
                samples[i] = (INT32_T)val;
            }
            break;
        }
        default:
            break;
        }
    }

    if (__sync_rb_push_frame(sg_sync_ctx.dmic_rb, frame) < sizeof(SYNC_TEST_FRAME_T)) {
        sg_sync_ctx.dmic_drop += frame->len;
    }
}

/**
 * @brief Read one complete frame from ring buffer
 * @param[in] rb Source ring buffer
 * @param[out] frame Stored frame output
 * @return TRUE if a complete frame was read, FALSE if not enough data
 */
STATIC BOOL_T __sync_rb_pop_frame(TUYA_RINGBUFF_T rb, SYNC_TEST_FRAME_T *frame)
{
    if (tuya_ring_buff_used_size_get(rb) < sizeof(SYNC_TEST_FRAME_T)) {
        return FALSE;
    }

    tuya_ring_buff_read(rb, (UINT8_T *)frame, sizeof(SYNC_TEST_FRAME_T));
    if (frame->len > SYNC_TEST_PCM_FRAME_SIZE) {
        return FALSE;
    }

    return TRUE;
}

/**
 * @brief Sync test worker thread
 * @param[in] args Unused
 * @return none
 * @note Reads frames from both ring buffers, matches by seq_no,
 *       discards older frames on mismatch, and writes matched pairs
 *       to separate ADC and DMIC files.
 */
STATIC VOID_T __audio_onboard_dmic_with_ref_test_thread(PVOID_T args)
{
    TUYA_FILE adc_fp = NULL;
    TUYA_FILE dmic_fp = NULL;
    BOOL_T mounted = FALSE;
    UINT32_T matched_pairs = 0;
    UINT32_T discarded_adc = 0;
    UINT32_T discarded_dmic = 0;
    OPERATE_RET rt = OPRT_OK;
    INT_T mount_ret = 0;
    BOOL_T adc_valid = FALSE;
    BOOL_T dmic_valid = FALSE;
    SYNC_TEST_FRAME_T adc_frame = {0};
    SYNC_TEST_FRAME_T dmic_frame = {0};
    BOOL_T first_match_logged = FALSE;
    UINT32_T idle_loops = 0;
    CONST CHAR_T *exit_reason = "matched target";

    (void)args;

    memset(&sg_sync_ctx, 0, sizeof(sg_sync_ctx));

    rt = tuya_ring_buff_create(SYNC_TEST_RB_SIZE, OVERFLOW_STOP_TYPE | TY_RINGBUF_PSRAM_FLAG, &sg_sync_ctx.adc_rb);
    if (rt != OPRT_OK) {
        TAL_PR_ERR("sync: create adc rb failed %d", rt);
        goto __dmic_test_error_exit;
    }

    rt = tuya_ring_buff_create(SYNC_TEST_RB_SIZE, OVERFLOW_STOP_TYPE | TY_RINGBUF_PSRAM_FLAG, &sg_sync_ctx.dmic_rb);
    if (rt != OPRT_OK) {
        TAL_PR_ERR("sync: create dmic rb failed %d", rt);
        goto __dmic_test_error_exit;
    }

    mount_ret = test_fs_mount(TAL_AUDIO_TEST_MOUNT_POINT, DEV_SDCARD);
    if (mount_ret != 0) {
        TAL_PR_ERR("sync: mount sd failed %d", mount_ret);
        goto __dmic_test_error_exit;
    }
    mounted = TRUE;

    adc_fp = __audio_test_fopen(SYNC_TEST_ADC_FILE, "w+");
    if (adc_fp == NULL) {
        TAL_PR_ERR("sync: open adc file failed");
        goto __dmic_test_error_exit;
    }

    dmic_fp = __audio_test_fopen(SYNC_TEST_DMIC_FILE, "w+");
    if (dmic_fp == NULL) {
        TAL_PR_ERR("sync: open dmic file failed");
        goto __dmic_test_error_exit;
    }

    TAL_AUDIO_INPUT_CFG_T adc_ref_cfg = {0};
    adc_ref_cfg.type = TAL_AUDIO_INPUT_ADC;
    adc_ref_cfg.dev.ai_adc_conf.port = TUYA_AUDIO_ADC_PORT_0;
    // adc_ref_cfg.dev.ai_adc_conf.chan = TUYA_AUDIO_ADC_CHANNEL_R;        // mic2, reference data on t5 board
    adc_ref_cfg.dev.ai_adc_conf.chan = TUYA_AUDIO_ADC_CHANNEL_LR;
    adc_ref_cfg.sample_bits = TUYA_AUDIO_SAMPLE_BITS_16;
    adc_ref_cfg.sample_rate = SYNC_TEST_SAMPLE_RATE;
    adc_ref_cfg.frame_time_ms = SYNC_TEST_FRAME_TIME_MS;
    adc_ref_cfg.audio_input_cb = __audio_adc_ref_cb;
    adc_ref_cfg.args = NULL;

    sg_sync_adc_handle = tal_audio_input_init(&adc_ref_cfg);
    if (sg_sync_adc_handle == NULL) {
        TAL_PR_ERR("sync: adc init failed");
        goto __dmic_test_error_exit;
    }

    sg_dmic_cfg.type = TAL_AUDIO_INPUT_DMIC;
    sg_dmic_cfg.dev.ai_dmic_conf.port = TUYA_AUDIO_DMIC_PORT_0;
    sg_dmic_cfg.dev.ai_dmic_conf.chan = TUYA_AUDIO_DMIC_CHANNEL_LR;
    sg_dmic_cfg.sample_bits = TUYA_AUDIO_SAMPLE_BITS_16;
    sg_dmic_cfg.sample_rate = SYNC_TEST_SAMPLE_RATE;
    sg_dmic_cfg.frame_time_ms = SYNC_TEST_FRAME_TIME_MS;
    sg_dmic_cfg.audio_input_cb = __audio_dmic_pickup_cb;
    sg_dmic_cfg.args = &sg_dmic_cfg;

    sg_sync_dmic_handle = tal_audio_input_init(&sg_dmic_cfg);
    if (sg_sync_dmic_handle == NULL) {
        TAL_PR_ERR("sync: dmic init failed");
        goto __dmic_test_error_exit;
    }

    sg_sync_ctx.running = TRUE;

    rt = tal_audio_input_set_volume(sg_sync_adc_handle, TAL_AUDIO_TEST_VOLUME);
    if (rt != OPRT_OK) {
        TAL_PR_ERR("dmic test set volume failed: %d", rt);
        goto __dmic_test_error_exit;
    }

    rt = tal_audio_input_start(sg_sync_adc_handle);
    if (rt != OPRT_OK) {
        TAL_PR_ERR("sync: adc start failed %d", rt);
        sg_sync_ctx.running = FALSE;
        goto __dmic_test_error_exit;
    }

    rt = tal_audio_input_start(sg_sync_dmic_handle);
    if (rt != OPRT_OK) {
        TAL_PR_ERR("sync: dmic start failed %d", rt);
        sg_sync_ctx.running = FALSE;
        goto __dmic_test_error_exit;
    }

    float fs = (float)SYNC_TEST_SAMPLE_RATE;
    float fc = (float)SYNC_TEST_HP_CUTOFF_HZ;
    audio_hpf1_init(&g_hpf1_l, fs, fc);
    audio_hpf1_init(&g_hpf1_r, fs, fc);

    TAL_PR_NOTICE("sync: started, adc->%s, dmic->%s", SYNC_TEST_ADC_FILE, SYNC_TEST_DMIC_FILE);

    while (matched_pairs < SYNC_TEST_MAX_PAIRS) {
        if (!adc_valid) {
            adc_valid = __sync_rb_pop_frame(sg_sync_ctx.adc_rb, &adc_frame);
        }
        if (!dmic_valid) {
            dmic_valid = __sync_rb_pop_frame(sg_sync_ctx.dmic_rb, &dmic_frame);
        }

        if (!adc_valid || !dmic_valid) {
            idle_loops++;
            if ((idle_loops % 2000U) == 0U) {
                TAL_PR_NOTICE("sync: waiting frames, matched=%u adc_valid=%d dmic_valid=%d adc_rb=%u dmic_rb=%u",
                              matched_pairs, adc_valid, dmic_valid,
                              tuya_ring_buff_used_size_get(sg_sync_ctx.adc_rb),
                              tuya_ring_buff_used_size_get(sg_sync_ctx.dmic_rb));
            }
            tkl_system_sleep(5);
            continue;
        }
        idle_loops = 0;

        if (adc_frame.seq_no == dmic_frame.seq_no) {
            if (!first_match_logged) {
                INT64_T ts_delta = (INT64_T)adc_frame.time_stamp - (INT64_T)dmic_frame.time_stamp;
                TAL_PR_NOTICE("sync: first match seq=%u, adc_ts=%llu, dmic_ts=%llu, delta=%lld ms",
                              adc_frame.seq_no, adc_frame.time_stamp, dmic_frame.time_stamp, ts_delta);
                first_match_logged = TRUE;
            }

            if (__audio_test_write_all(adc_fp, adc_frame.data, adc_frame.len) != OPRT_OK) {
                exit_reason = "write adc file failed";
                TAL_PR_ERR("sync: write adc file failed");
                break;
            }

            #define CH_NUM 2
            int16_t *hp_samples = (int16_t *)dmic_frame.data;
            uint32_t samples_cnt = dmic_frame.len / sizeof(int16_t);  // 16bit
            for(uint32_t i = 0; i + 1 < samples_cnt; i += CH_NUM) {
                hp_samples[i] = audio_hpf1_process(&g_hpf1_l, hp_samples[i]);
                hp_samples[i + 1] = audio_hpf1_process(&g_hpf1_r, hp_samples[i + 1]);
            }
            if (__audio_test_write_all(dmic_fp, dmic_frame.data, dmic_frame.len) != OPRT_OK) {
                exit_reason = "write dmic file failed";
                TAL_PR_ERR("sync: write dmic file failed");
                break;
            }

            matched_pairs++;
            adc_valid = FALSE;
            dmic_valid = FALSE;

            if ((matched_pairs % 50) == 0) {
                __audio_test_fsync((INT_T)adc_fp);
                __audio_test_fsync((INT_T)dmic_fp);
            }
        } else if (adc_frame.seq_no < dmic_frame.seq_no) {
            discarded_adc++;
            adc_valid = FALSE;
            TAL_PR_ERR("sync: discard adc sample, %u %u", adc_frame.seq_no, dmic_frame.seq_no);
        } else {
            discarded_dmic++;
            dmic_valid = FALSE;
            TAL_PR_ERR("sync: discard dmic sample, %u %u", adc_frame.seq_no, dmic_frame.seq_no);
        }
    }

    TAL_PR_NOTICE("sync: loop exit, reason=%s matched=%u discarded_adc=%u discarded_dmic=%u",
                  exit_reason, matched_pairs, discarded_adc, discarded_dmic);
    TAL_PR_NOTICE("dmic test end, adc->%s, dmic->%s", SYNC_TEST_ADC_FILE, SYNC_TEST_DMIC_FILE);
    bk_printf("dmic test end, adc->%s, dmic->%s\r\n", SYNC_TEST_ADC_FILE, SYNC_TEST_DMIC_FILE);
    bk_printf("dmic test end, adc->%s, dmic->%s\r\n", SYNC_TEST_ADC_FILE, SYNC_TEST_DMIC_FILE);
    bk_printf("dmic test end, adc->%s, dmic->%s\r\n", SYNC_TEST_ADC_FILE, SYNC_TEST_DMIC_FILE);

__dmic_test_error_exit:
    TAL_PR_NOTICE("sync: enter cleanup");
    sg_sync_ctx.running = FALSE;

    if (sg_sync_adc_handle) {
        tal_audio_input_stop(sg_sync_adc_handle);
        tal_audio_input_deinit(sg_sync_adc_handle);
        sg_sync_adc_handle = NULL;
        TAL_PR_NOTICE("sync: adc handle cleanup done");
    }
    if (sg_sync_dmic_handle) {
        tal_audio_input_stop(sg_sync_dmic_handle);
        tal_audio_input_deinit(sg_sync_dmic_handle);
        sg_sync_dmic_handle = NULL;
        TAL_PR_NOTICE("sync: dmic handle cleanup done");
    }

    if (adc_fp) {
        TAL_PR_NOTICE("sync: close adc file");
        __audio_test_fclose(adc_fp);
    }
    if (dmic_fp) {
        TAL_PR_NOTICE("sync: close dmic file");
        __audio_test_fclose(dmic_fp);
    }
    if (sg_sync_ctx.adc_rb) {
        TAL_PR_NOTICE("sync: free adc rb");
        tuya_ring_buff_free(sg_sync_ctx.adc_rb);
        sg_sync_ctx.adc_rb = NULL;
    }
    if (sg_sync_ctx.dmic_rb) {
        TAL_PR_NOTICE("sync: free dmic rb");
        tuya_ring_buff_free(sg_sync_ctx.dmic_rb);
        sg_sync_ctx.dmic_rb = NULL;
    }
    if (mounted) {
        TAL_PR_NOTICE("sync: unmount fs");
        test_fs_unmount(TAL_AUDIO_TEST_MOUNT_POINT);
    }

    TAL_PR_NOTICE("sync: done, matched=%u, discarded_adc=%u, discarded_dmic=%u, "
              "adc_drop=%u, dmic_drop=%u",
              matched_pairs, discarded_adc, discarded_dmic,
              sg_sync_ctx.adc_drop, sg_sync_ctx.dmic_drop);

    if (sg_sync_test_thread) {
        TAL_PR_NOTICE("sync: deleting sync thread");
        THREAD_HANDLE self = sg_sync_test_thread;
        sg_sync_test_thread = NULL;
        tal_thread_delete(self);
    }
}

/* ---------------------------------------------------------------------------
 * Function implementations (public API)
 * --------------------------------------------------------------------------- */
/**
 * @brief Start the dual-input sync test (ADC ref + DMIC pickup)
 * @return none
 */
void tal_audio_digital_dual_mic_input_sync_test(void)
{
    THREAD_CFG_T cfg = {0};
    OPERATE_RET rt;

    if (sg_sync_test_thread != NULL) {
        TAL_PR_NOTICE("sync test already running");
        return;
    }

    cfg.stackDepth = 8192;
    cfg.priority = THREAD_PRIO_2;
    cfg.thrdname = "taudio_sync";

    rt = tal_thread_create_and_start(&sg_sync_test_thread, NULL, NULL, __audio_onboard_dmic_with_ref_test_thread, NULL, &cfg);
    if (rt != OPRT_OK) {
        sg_sync_test_thread = NULL;
        TAL_PR_ERR("sync: create thread failed %d", rt);
        return;
    }

    TAL_PR_NOTICE("sync: test thread created");
}

/**
 * @brief Start onboard ADC mic recording test
 * @return none
 */
void tal_audio_onboard_mic_test(void)
{
    THREAD_CFG_T thread_cfg = {0};
    OPERATE_RET rt = OPRT_OK;

    if (sg_audio_onboard_mic_test_thread != NULL) {
        TAL_PR_NOTICE("audio input test is running");
        return;
    }

    thread_cfg.stackDepth = 8192;
    thread_cfg.priority = THREAD_PRIO_2;
    thread_cfg.thrdname = "taudio_adc";

    rt = tal_thread_create_and_start(&sg_audio_onboard_mic_test_thread, NULL, NULL, __audio_onboard_mic_test_thread, NULL, &thread_cfg);
    if (rt != OPRT_OK) {
        sg_audio_onboard_mic_test_thread = NULL;
        TAL_PR_ERR("audio input test create thread failed: %d", rt);
        return;
    }

    TAL_PR_NOTICE("audio input test thread created");
}

/**
 * @brief Start onboard DAC speaker playback test
 * @return none
 */
void tal_audio_onboard_spk_test(void)
{
    THREAD_CFG_T thread_cfg = {0};
    OPERATE_RET rt = OPRT_OK;

    bk_printf("-------------------- %s %d\r\n", __func__, __LINE__);
    if (sg_audio_onboard_spk_test_thread != NULL) {
        TAL_PR_NOTICE("audio output test is running");
        return;
    }

    thread_cfg.stackDepth = 8192;
    thread_cfg.priority = 7;
    thread_cfg.thrdname = "taudio_dac";

    bk_printf("-------------------- %s %d\r\n", __func__, __LINE__);
    rt = tal_thread_create_and_start(&sg_audio_onboard_spk_test_thread, NULL, NULL, __audio_onboard_spk_test_thread, NULL, &thread_cfg);
    if (rt != OPRT_OK) {
        sg_audio_onboard_spk_test_thread = NULL;
        TAL_PR_ERR("audio output test create thread failed: %d", rt);
        return;
    }

    TAL_PR_NOTICE("audio output test thread created");
}
