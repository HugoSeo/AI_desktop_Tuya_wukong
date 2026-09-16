/**
 * @file tuya_pn532_hsu.c
 * @brief PN532 NFC driver implementation over HSU (High Speed UART)
 * @version 0.2
 * @date 2026-07-07
 *
 * @copyright Copyright 2025-2026 Tuya Inc. All Rights Reserved.
 *
 * T5_BOARD接线方式：
 * PN532    T5_BOARD
 * GND      GND
 * GCC      5V
 * TX       P10
 * RX       P11
 * 接线前先手动断掉烧录口的映射
 *
 * Target chip: PN532_C1 (NXP PN532/C1 — see PN532_C1 datasheet).
 *
 * HSU frame format (standard, applies to every command):
 *   Preamble(00) + StartCode(00 FF) + LEN + LCS + TFI + DATA + DCS + Postamble(00)
 *     LCS = lower byte of (0x100 - LEN)
 *     DCS = lower byte of (0x100 - sum(TFI + all DATA bytes))
 *
 * Wake-up burst (FIRST command after power-up only):
 *   [0x55 x16] + Preamble(00) + StartCode(00 FF) + ...  (single continuous write)
 *   Per §8.3.4.4, the HSU wake-up generator counts rising edges on HSU_RX and is
 *   used ONLY to leave Soft-Power-Down. Once the PN532 firmware is running, the
 *   preamble filter's start_frame bit (§8.3.4.3) is re-armed by the firmware
 *   itself between frames, so normal commands are sent as plain 00 00 FF ... with
 *   NO 0x55 prefix. (Prepending 0x55 to every frame is a cargo-cult workaround
 *   that only "works" because the armed preamble filter discards it as noise.)
 *
 * ACK:   00 00 FF 00 FF 00
 * NACK:  00 00 FF FF 00 00   (PN532 busy, cannot accept the command now)
 * ERROR: 00 00 FF 01 FF 7F 81 00   (received command frame invalid / rejected)
 *
 * Reference: PN532_C1 datasheet §8.3.4 HSU, §8.3.4.3 preamble filter,
 *            §8.3.4.4 HSU wake-up generator
 */

#include <stdio.h>
#include <string.h>

#include "tal_log.h"
#include "tal_memory.h"
#include "tal_mutex.h"
#include "tal_system.h"
#include "tal_sleep.h"
#include "tal_thread.h"
#include "tal_gpio.h"
#include "tkl_uart.h"   /* for tkl_uart_write (one-shot, no byte-by-byte) */
#include "tal_uart.h"   /* for tal_uart_init / tal_uart_read (non-blocking poll) */

#include "tuya_cloud_types.h"
#include "tuya_pn532_hsu.h"

/****************************************************************************
 * Internal macros
 ****************************************************************************/

/* Max time to wait for ACK/NACK after sending a command (ms) */
#define PN532_ACK_TIMEOUT_MS        50

/* Max time to wait for a full response frame after receiving ACK (ms) */
#define PN532_RESP_TIMEOUT_MS       300

/* Max frame size: header(6) + max data(264) + DCS(1) + postamble(1) */
#define PN532_MAX_FRAME_SIZE        272

/* Normal frame max data payload */
#define PN532_MAX_DATA_LEN          264

/* MIFARE Classic block size */
#define MIFARE_BLOCK_SIZE           16

/* Default MIFARE key */
static const UINT8_T MIFARE_DEFAULT_KEY[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

/****************************************************************************
 * Internal types
 ****************************************************************************/

typedef struct {
    PN532_CFG_S     cfg;
    PN532_STATE_E   state;
    MUTEX_HANDLE    mutex;
    BOOL_T          initialized;
} PN532_CTX_S;

/****************************************************************************
 * Static variables
 ****************************************************************************/

STATIC PN532_CTX_S s_pn532_ctx;
STATIC BOOL_T      s_polling_running = FALSE;
STATIC MUTEX_HANDLE  s_rf_session_mutex = NULL; /* serializes poller vs manual RF ops */
STATIC THREAD_HANDLE s_polling_thread = NULL;

/* Forward declarations for polling thread (defined after public API) */
typedef struct {
    PN532_CARD_DETECT_CB    cb;
    VOID                    *arg;
    UINT_T                  interval;
} PN532_POLLING_ARGS_S;

STATIC PN532_POLLING_ARGS_S s_polling_args;
STATIC VOID __pn532_polling_thread_entry(VOID *param);

/****************************************************************************
 * Low-level HSU frame helpers
 ****************************************************************************/

/**
 * @brief Compute checksum byte for PN532 frame
 * @param sum  Cumulative byte sum
 * @return  Lower byte of (0x100 - sum)
 */
STATIC INLINE UINT8_T __pn532_checksum(UINT8_T sum)
{
    return (UINT8_T)(0x100 - (UINT_T)sum);
}

/**
 * @brief Write raw frame to PN532 via UART (blocking)
 */
STATIC OPERATE_RET __pn532_write(IN UINT8_T *data, IN UINT_T len)
{
    INT_T written = tkl_uart_write(s_pn532_ctx.cfg.uart_port, data, len);
    if (written < 0 || (UINT_T)written != len) {
        TAL_PR_ERR("PN532 uart write failed: expected %d, got %d", len, written);
        return OPRT_COM_ERROR;
    }
    return OPRT_OK;
}

/**
 * @brief Read bytes from PN532 UART with timeout (byte-by-byte polling)
 *
 * Uses tal_uart_read in non-blocking mode (open_mode=0).  Each call
 * returns immediately with whatever is in the ring buffer (0 if empty).
 * We poll byte-by-byte so the outer timeout loop can enforce the
 * caller's deadline.
 *
 * @param buf        Output buffer
 * @param len        Number of bytes to read
 * @param timeout_ms Overall timeout in milliseconds
 * @return OPRT_OK on success (all bytes read), OPRT_TIMEOUT or OPRT_COM_ERROR
 */
STATIC OPERATE_RET __pn532_read_timeout(OUT UINT8_T *buf, IN UINT_T len, IN UINT_T timeout_ms)
{
    /*
     * tal_uart_init internally calls tkl_uart_rx_irq_cb_reg, which
     * disables the BK UART SW FIFO.  With SW FIFO off, the hardware
     * ISR delivers bytes into the tal ring buffer, and tal_uart_read
     * (non-blocking) returns whatever is available immediately.
     *
     * We poll one byte at a time with a short sleep between attempts
     * so the overall timeout is honoured even when no data arrives.
     */
    UINT_T total_read = 0;
    UINT_T start_ms = tal_system_get_millisecond();
    INT_T rd;

    while (total_read < len) {
        if (tal_system_get_millisecond() - start_ms > timeout_ms) {
            return OPRT_TIMEOUT;
        }

        rd = tal_uart_read(s_pn532_ctx.cfg.uart_port,
                           buf + total_read, 1);
        if (rd > 0) {
            total_read += rd;
        } else if (rd < 0) {
            TAL_PR_ERR("PN532 uart read error: %d", rd);
            return OPRT_COM_ERROR;
        } else {
            /* rd == 0: ring buffer empty, wait a bit then retry */
            tal_system_sleep(1);
        }
    }
    return OPRT_OK;
}

/**
 * @brief Send a command to PN532 and wait for ACK
 *
 * Builds the standard HSU frame (00 00 FF LEN LCS TFI DATA DCS 00) with proper
 * checksums, sends it as a single continuous write, and reads the ACK.
 *
 * Per PN532 datasheet §8.3.4.3, the preamble filter is re-armed by PN532 firmware
 * between frames (the start_frame bit), so normal commands need NO 0x55 prefix.
 *
 * For the first command after cold power-up, set coldboot = TRUE: this prepends a
 * 16×0x55 wake-up burst (§8.3.4.4 HSU wake-up generator) to leave Soft-Power-Down
 * and let the PN532 auto-detect the baud rate. The burst and the standard frame
 * are sent as one continuous write with no gap.
 *
 * @param cmd      PN532 command byte
 * @param cmd_data Command parameter bytes (can be NULL if cmd_len == 0)
 * @param cmd_len  Length of command parameters
 * @param coldboot TRUE = prepend 16×0x55 wake-up (first command after power-up only)
 * @return OPRT_OK on ACK, error otherwise
 */
STATIC OPERATE_RET __pn532_send_command(IN UINT8_T cmd,
                                         IN UINT8_T *cmd_data,
                                         IN UINT8_T cmd_len,
                                         IN BOOL_T coldboot)
{
    UINT8_T frame[PN532_MAX_FRAME_SIZE + 16];  /* +16 for coldboot wakeup prefix */
    UINT8_T idx = 0;
    UINT8_T sum = 0;

    /*
     * Flush stale bytes from RX buffer before sending.
     * Without this, residual bytes from a prior failed transaction
     * would be misread as the ACK/response.
     */
    {
        UINT8_T dummy;
        INT_T drain_max = (INT_T)s_pn532_ctx.cfg.rx_buf_size;
        while (tal_uart_read(s_pn532_ctx.cfg.uart_port, &dummy, 1) > 0 && --drain_max > 0) {
            /* drain stale bytes, bounded to rx_buf_size to avoid infinite loop */
        }
    }

    /*
     * PN532 HSU frame (§8.3.4):
     *
     * Normal command (firmware already running):
     *   [00] [00 FF] [LEN LCS TFI DATA... DCS 00]
     *   No 0x55 prefix. The preamble filter is re-armed by PN532 firmware
     *   between frames (§8.3.4.3), so the host just sends the standard frame.
     *
     * Coldboot (first command after power-up only):
     *   [0x55 x16] [00] [00 FF] [LEN LCS TFI DATA... DCS 00]
     *   The 0x55 burst drives the HSU wake-up generator (§8.3.4.4, 5 rising
     *   edges) to leave Soft-Power-Down / auto-detect baud rate. Sent as a
     *   single continuous write with no gap before the standard frame.
     *
     * All writes are a single continuous tkl_uart_write (no gap between the
     * optional 0x55 burst and the standard frame).
     */
    if (coldboot) {
        for (INT_T i = 0; i < 16; i++) {
            frame[idx++] = 0x55;
        }
    }
    /* Preamble + Start Code (standard HSU format) */
    frame[idx++] = PN532_PREAMBLE;
    frame[idx++] = PN532_STARTCODE1;
    frame[idx++] = PN532_STARTCODE2;

    /* Length = TFI (1) + cmd (1) + cmd_data (cmd_len) */
    if (cmd_len > 253) {
        TAL_PR_ERR("PN532 cmd_len too large: %d (max 253)", cmd_len);
        return OPRT_INVALID_PARM;
    }
    UINT8_T len = 1 + 1 + cmd_len;
    frame[idx++] = len;
    frame[idx++] = __pn532_checksum(len);

    /* TFI = Host to PN532 */
    frame[idx++] = PN532_HOST_TO_PN532;
    sum += PN532_HOST_TO_PN532;

    /* Command code */
    frame[idx++] = cmd;
    sum += cmd;

    /* Command data (if any) */
    if (cmd_len > 0 && cmd_data != NULL) {
        for (UINT8_T i = 0; i < cmd_len; i++) {
            frame[idx++] = cmd_data[i];
            sum += cmd_data[i];
        }
    }

    /* DCS */
    frame[idx++] = __pn532_checksum(sum);

    /* Postamble */
    frame[idx++] = PN532_POSTAMBLE;

    /* Send the frame */
    OPERATE_RET rt = __pn532_write(frame, idx);
    if (rt != OPRT_OK) {
        return rt;
    }

    /* Read ACK (6 bytes) */
    UINT8_T ack_buf[PN532_ACK_PACKET_SIZE];
    rt = __pn532_read_timeout(ack_buf, PN532_ACK_PACKET_SIZE, PN532_ACK_TIMEOUT_MS);
    if (rt != OPRT_OK) {
        TAL_PR_ERR("PN532 ACK timeout");
        return rt;
    }

    /* Verify ACK frame: 00 00 FF 00 FF 00 */
    if (ack_buf[0] == 0x00 && ack_buf[1] == 0x00 && ack_buf[2] == 0xFF &&
        ack_buf[3] == 0x00 && ack_buf[4] == 0xFF && ack_buf[5] == 0x00) {
        return OPRT_OK;
    }

    /* Check for NACK */
    if (ack_buf[0] == 0x00 && ack_buf[1] == 0x00 && ack_buf[2] == 0xFF &&
        ack_buf[3] == 0xFF && ack_buf[4] == 0x00 && ack_buf[5] == 0x00) {
        TAL_PR_ERR("PN532 NACK received for cmd 0x%02X", cmd);
        return OPRT_COM_ERROR;
    }

    /* Check for error frame */
    if (ack_buf[0] == 0x00 && ack_buf[1] == 0x00 && ack_buf[2] == 0xFF &&
        ack_buf[3] == 0x01 && ack_buf[4] == 0xFF && ack_buf[5] == 0x00) {
        TAL_PR_ERR("PN532 error response for cmd 0x%02X", cmd);
        return OPRT_COM_ERROR;
    }

    TAL_PR_DEBUG("PN532 unexpected ACK response: %02X %02X %02X %02X %02X %02X",
                 ack_buf[0], ack_buf[1], ack_buf[2],
                 ack_buf[3], ack_buf[4], ack_buf[5]);
    return OPRT_COM_ERROR;
}

/**
 * @brief Read a response frame from PN532 after a successful command+ACK
 *
 * Parses the HSU frame, validates checksums, and returns the data payload.
 *
 * @param[out] data      Output buffer for response data (caller allocated)
 * @param[in]  data_size Size of output buffer
 * @param[out] data_len  Actual length of received data payload
 * @return OPRT_OK on success
 */
STATIC OPERATE_RET __pn532_read_response(OUT UINT8_T *data,
                                          IN  UINT8_T  data_size,
                                          OUT UINT8_T *data_len)
{
    UINT8_T buf[PN532_MAX_FRAME_SIZE];
    UINT8_T idx;
    UINT8_T retry = 0;
    BOOL_T recovered = FALSE;   /* TRUE after final recovery drain+wait */
    /*
     * PN532_C1 may send periodic "busy" status frames (TFI=0x7F) during
     * long-running operations such as MIFARE authentication.  After each
     * spurious frame we wait with progressive backoff to give the PN532
     * time to complete the RF transaction and deliver the real response.
     *
     * Delay schedule (retry → sleep):
     *   0 → 10ms,  1 → 30ms,  2 → 60ms,
     *   3 → 120ms, 4 → 250ms
     * Total wait budget: up to ~470ms + read time.
     */
    #define PN532_SPURIOUS_MAX_RETRIES  5

    /*
     * Drain any stale bytes that arrived between the ACK read and now.
     * The PN532 may send unsolicited status bytes or the UART ISR may
     * have buffered partial frames from a previous operation.
     * Only drain once before the first read attempt — on retries the real
     * response is already waiting and we must NOT consume it.
     */
    {
        UINT8_T dummy;
        INT_T drain_max = 64;
        while (tal_uart_read(s_pn532_ctx.cfg.uart_port, &dummy, 1) > 0 && --drain_max > 0) {
            TAL_PR_DEBUG("PN532 drained stale byte before response: %02X", dummy);
        }
    }

retry_read:
    idx = 0;

    /* Read preamble */
    OPERATE_RET rt = __pn532_read_timeout(&buf[idx], 1, PN532_RESP_TIMEOUT_MS);
    if (rt != OPRT_OK) { goto TIMEOUT_ERR; }
    idx++;

    /* Skip leading 0x00 bytes (preamble padding) */
    while (buf[idx - 1] == 0x00 && idx < 10) {
        rt = __pn532_read_timeout(&buf[idx], 1, PN532_RESP_TIMEOUT_MS);
        if (rt != OPRT_OK) { goto TIMEOUT_ERR; }
        idx++;
    }

    /* The last two bytes read should be start codes: 00 FF.
     * Require at least 3 bytes total (1 preamble + 2 start code) */
    if (idx < 3 || buf[idx - 2] != PN532_STARTCODE1 || buf[idx - 1] != PN532_STARTCODE2) {
        TAL_PR_ERR("PN532 bad start code: %02X %02X", buf[idx - 2], buf[idx - 1]);
        return OPRT_COM_ERROR;
    }

    /* Read LEN + LCS */
    rt = __pn532_read_timeout(&buf[idx], 2, PN532_RESP_TIMEOUT_MS);
    if (rt != OPRT_OK) { goto TIMEOUT_ERR; }
    UINT8_T len = buf[idx];
    UINT8_T lcs = buf[idx + 1];
    idx += 2;

    if ((UINT8_T)(len + lcs) != 0x00) {
        TAL_PR_ERR("PN532 LEN checksum mismatch: len=%02X lcs=%02X", len, lcs);
        return OPRT_COM_ERROR;
    }

    /* Read TFI + DATA (len bytes) + DCS + Postamble */
    UINT_T remaining = (UINT_T)len + 2; /* +DCS +postamble */
    rt = __pn532_read_timeout(&buf[idx], remaining, PN532_RESP_TIMEOUT_MS);
    if (rt != OPRT_OK) { goto TIMEOUT_ERR; }

    /* TFI must be PN532 to Host */
    UINT8_T tfi = buf[idx];
    if (tfi != PN532_PN532_TO_HOST) {
        /*
         * PN532 may send a spurious empty frame (LEN=1, no payload)
         * with TFI=0x7F after InDataExchange, before the real response.
         * This is seen as: 00 00 FF 01 FF 7F [DCS] 00.
         * Drain this bogus frame and retry reading the real response.
         */
        if (tfi == 0x7F && len <= 2) {
            /* Progressive backoff: 10, 30, 60, 120, 250 ms */
            UINT8_T delay;
            switch (retry) {
            case 0: delay = 10;  break;
            case 1: delay = 30;  break;
            case 2: delay = 60;  break;
            case 3: delay = 120; break;
            default: delay = 250; break;
            }
            TAL_PR_DEBUG("PN532 spurious empty frame (TFI=%02X len=%02X lcs=%02X), "
                         "retrying in %dms (attempt %d/%d)",
                         tfi, len, lcs, delay, retry + 1, PN532_SPURIOUS_MAX_RETRIES);
            /*
             * The spurious frame has already been fully read from UART.
             * DO NOT drain — the real response follows after the PN532
             * completes its RF transaction.  Wait with progressive backoff
             * to give MIFARE auth time to finish.
             */
            tal_system_sleep(delay);
            if (++retry < PN532_SPURIOUS_MAX_RETRIES) {
                goto retry_read;
            }
            /*
             * All progressive-backoff retries consumed; the PN532_C1 may be
             * stuck in a bad RF state (e.g. card removed mid-transaction, or
             * MIFARE auth state clobbered by a concurrent InListPassiveTarget).
             *
             * Perform one final recovery attempt:
             *   1. Drain all stale bytes from the UART RX buffer
             *   2. Wait 300ms for the PN532_C1 RF state machine to reset
             *   3. Reset the retry counter and try once more
             *
             * If this also fails the function returns an error; the caller
             * (write_block / read_block) should retry the whole auth+exchange
             * sequence from scratch.
             */
            if (!recovered) {
                recovered = TRUE;
                TAL_PR_WARN("PN532 read_response: all retries exhausted, "
                            "draining UART and attempting final recovery...");
                {
                    UINT8_T dummy;
                    INT_T drain_max = 128;
                    while (tal_uart_read(s_pn532_ctx.cfg.uart_port, &dummy, 1) > 0
                           && --drain_max > 0) {
                        /* drain all remaining stale data */
                    }
                }
                tal_system_sleep(300);
                retry = 0;
                goto retry_read;
            }
            TAL_PR_ERR("PN532 read_response: all retries exhausted "
                       "even after recovery (spurious frames)");
            return OPRT_COM_ERROR;
        }
        TAL_PR_ERR("PN532 unexpected TFI: %02X (expected %02X), len=%02X lcs=%02X "
                   "preamble_bytes=%d ctx=[%02X %02X %02X %02X %02X]",
                   tfi, PN532_PN532_TO_HOST, len, lcs, idx,
                   buf[0], buf[1], buf[2], buf[3], buf[4]);
        return OPRT_COM_ERROR;
    }
    idx++;

    /* Verify DCS */
    UINT8_T sum = tfi;
    for (UINT_T i = idx; i < idx + len - 1; i++) {
        sum += buf[i];
    }
    UINT8_T dcs = buf[idx + len - 1];
    if ((UINT8_T)(sum + dcs) != 0x00) {
        TAL_PR_ERR("PN532 DCS checksum mismatch: sum=%02X dcs=%02X", sum, dcs);
        return OPRT_COM_ERROR;
    }

    /* Copy data payload (len-1 because TFI is included in len but not data) */
    UINT8_T payload_len = len - 1;
    if (payload_len > 0 && data != NULL) {
        UINT8_T copy_len = (payload_len < data_size) ? payload_len : data_size;
        memcpy(data, &buf[idx], copy_len);
    }
    if (data_len != NULL) {
        *data_len = payload_len;
    }

    return OPRT_OK;

TIMEOUT_ERR:
    //TAL_PR_ERR("PN532 read_response timeout (retry_count=%d)", retry);
    return rt;
}

/**
 * @brief Send a command and read the full response
 *
 * Combines __pn532_send_command + __pn532_read_response with proper mutex.
 *
 * @param  cmd       Command byte
 * @param  cmd_data  Command parameter bytes
 * @param  cmd_len   Length of command parameters
 * @param  resp_data Output buffer for response data
 * @param  resp_size Size of response buffer
 * @param  resp_len  Actual response data length
 * @param  coldboot  TRUE for the first command after power-up
 * @return OPRT_OK on success
 */
STATIC OPERATE_RET __pn532_command_exchange(IN  UINT8_T  cmd,
                                              IN  UINT8_T *cmd_data,
                                              IN  UINT8_T  cmd_len,
                                              OUT UINT8_T *resp_data,
                                              IN  UINT8_T  resp_size,
                                              OUT UINT8_T *resp_len,
                                              IN  BOOL_T   coldboot)
{
    OPERATE_RET rt;

    tal_mutex_lock(s_pn532_ctx.mutex);

    rt = __pn532_send_command(cmd, cmd_data, cmd_len, coldboot);
    if (rt != OPRT_OK) {
        TAL_PR_ERR("PN532 send command 0x%02X failed: %d", cmd, rt);
        tal_mutex_unlock(s_pn532_ctx.mutex);
        return rt;
    }

    rt = __pn532_read_response(resp_data, resp_size, resp_len);
    if (rt != OPRT_OK) {
        //TAL_PR_ERR("PN532 read response for cmd 0x%02X failed: 0x%02X", cmd, rt);
    }

    tal_mutex_unlock(s_pn532_ctx.mutex);
    return rt;
}

/****************************************************************************
 * High-level PN532 commands
 ****************************************************************************/

/**
 * @brief [DEPRECATED] Standalone HSU wakeup is no longer used.
 *
 * The wakeup sequence (0x55 bytes) is now prepended to the command frame
 * in __pn532_send_command(coldboot=TRUE) and sent as a single continuous
 * tkl_uart_write.  Kept as a no-op for API compatibility.
 *
 * @return OPRT_OK always
 */
STATIC OPERATE_RET __pn532_hsu_wakeup(VOID)
{
    TAL_PR_DEBUG("PN532 HSU wakeup: now embedded in coldboot command, skipping standalone");
    return OPRT_OK;
}

/**
 * @brief Perform SAM (Security Access Module) configuration
 *
 * This is mandatory after PN532 power-up before any RF operations.
 * Normal mode: SAMConfig(mode=0x01, timeout=0x14, irq=0x01)
 *  - mode 0x01 = normal mode
 *  - timeout 0x14 = 20 * 50ms = 1 second virtual card timeout
 *  - irq 0x01 = use IRQ pin
 */
STATIC OPERATE_RET __pn532_sam_config(IN BOOL_T coldboot)
{
    UINT8_T cmd_data[] = {
        0x01,   /* Normal mode */
        0x14,   /* Timeout: 20 * 50ms = 1s */
        0x01,   /* IRQ enabled */
    };

    UINT8_T resp[4];
    UINT8_T resp_len = 0;

    OPERATE_RET rt = __pn532_command_exchange(PN532_CMD_SAMCONFIGURATION,
                                                cmd_data, sizeof(cmd_data),
                                                resp, sizeof(resp), &resp_len,
                                                coldboot);
    if (rt != OPRT_OK) {
        return rt;
    }

    /* Check that SAM config response starts with 0x15 (SAMConfig reply) */
    if (resp_len < 1 || resp[0] != 0x15) {
        TAL_PR_ERR("PN532 SAMConfig unexpected response: %02X", resp[0]);
        return OPRT_COM_ERROR;
    }

    TAL_PR_INFO("PN532 SAM configured OK");
    return OPRT_OK;
}

/**
 * @brief Poll for a single ISO/IEC 14443A card (106 kbps)
 *
 * The InListPassiveTarget command:
 *   MaxTg (1 byte) = 1 (poll for 1 card)
 *   BrTy  (1 byte) = 0x00 (106 kbps type A)
 *
 * Response payload (after stripping TFI from HSU frame):
 *   [0] ResponseCmd (1 byte) = 0x4B (always cmd + 1)
 *   [1] NbTg (1 byte) = number of targets found
 *   If NbTg >= 1, for each target:
 *     [2] Tg (1 byte) = target index
 *     [3..4] SENS_RES (2 bytes) = ATQA
 *     [5] SEL_RES (1 byte) = SAK
 *     [6] NFCIDLen (1 byte)
 *     [7..] NFCID (NFCIDLen bytes) = UID
 */
STATIC OPERATE_RET __pn532_in_list_passive_target(OUT PN532_CARD_INFO_S *card_info)
{
    UINT8_T cmd_data[] = {
        0x01,   /* MaxTg: poll for 1 target */
        0x00,   /* BrTy: 106 kbps type A (ISO/IEC 14443A) */
    };

    UINT8_T resp[32];
    UINT8_T resp_len = 0;

    OPERATE_RET rt = __pn532_command_exchange(PN532_CMD_INLISTPASSIVETARGET,
                                                cmd_data, sizeof(cmd_data),
                                                resp, sizeof(resp), &resp_len, FALSE);
    if (rt != OPRT_OK) {
        return rt;
    }

    /*
     * Response format (after TFI strip):
     *   [0] ResponseCmd (0x4B = PN532_CMD_INLISTPASSIVETARGET + 1)
     *   [1] NbTg (number of targets)
     *   [2+] Target data (only if NbTg >= 1):
     *     Tg (1), SENS_RES/ATQA (2), SEL_RES/SAK (1), NFCIDLen (1), NFCID (...)
     */
    if (resp_len < 3) {
        /* Minimum: ResponseCmd + NbTg + at least Tg hint */
        TAL_PR_DEBUG("PN532 no card detected (resp_len=%d)", resp_len);
        return OPRT_TIMEOUT;
    }

    UINT8_T offset = 0;
    UINT8_T resp_cmd = resp[offset++];  /* 0x4B = InListPassiveTarget response */
    (void)resp_cmd;

    UINT8_T nbtg = resp[offset++];      /* Number of targets found */
    if (nbtg < 1) {
        TAL_PR_DEBUG("PN532 no card in field (NbTg=0)");
        return OPRT_TIMEOUT;
    }

    /* Tg (target number) -- skip */
    offset++;

    /* SENS_RES (ATQA) */
    card_info->atqa[0] = resp[offset++];
    card_info->atqa[1] = resp[offset++];

    /* SEL_RES (SAK) */
    card_info->sak = resp[offset++];

    /* NFCID Length */
    UINT8_T uid_len = resp[offset++];
    if (uid_len > 10) uid_len = 10;
    card_info->uid_len = uid_len;

    /* NFCID (UID) */
    memcpy(card_info->uid, &resp[offset], uid_len);
    offset += uid_len;

    /* Determine card type from ATQA/SAK */
    if (card_info->uid_len == 4) {
        /* MIFARE Classic 1K: SAK 0x08, MIFARE Classic 4K: SAK 0x18 */
        if (card_info->sak == 0x08) {
            card_info->type = PN532_CARD_TYPE_MIFARE_1K;
        } else if (card_info->sak == 0x18) {
            card_info->type = PN532_CARD_TYPE_MIFARE_4K;
        } else {
            card_info->type = PN532_CARD_TYPE_ISO14443_4;
        }
    } else if (card_info->uid_len == 7) {
        card_info->type = PN532_CARD_TYPE_MIFARE_UL;
    } else {
        card_info->type = PN532_CARD_TYPE_UNKNOWN;
    }

    return OPRT_OK;
}

/**
 * @brief Authenticate with a MIFARE Classic block
 *
 * The InDataExchange command with MIFARE authentication:
 *   Cmd 0x40, data = [Tg, AuthCmd(0x60/0x61), BlockNum, Key[6], UID[4]]
 *   Tg = 0x01 (always 1 for InListPassiveTarget with MaxTg=1)
 *
 * @note UID is the 4-byte MIFARE Classic UID (last 4 bytes of the polling UID).
 *       For 4-byte UIDs this is the whole UID. For 7-byte UIDs (Ultralight)
 *       this function is not applicable.
 */
STATIC OPERATE_RET __pn532_mifare_auth(IN PN532_CARD_INFO_S  *card_info,
                                        IN UINT8_T            block_num,
                                        IN PN532_MIFARE_KEY_TYPE_E key_type,
                                        IN UINT8_T            *key)
{
    if (card_info->uid_len < 4) {
        TAL_PR_ERR("PN532 MIFARE auth requires UID length >= 4");
        return OPRT_INVALID_PARM;
    }

    /* MIFARE Classic auth payload = Tg(1) + AuthCmd(1) + Block(1) + Key(6) + UID(4) = 13 bytes.
     * InDataExchange rejects an over-length payload with an ERROR frame
     * (00 00 FF 01 FF 7F 81 00), so the array MUST be sized to exactly 13. */
    UINT8_T auth_data[13];
    const UINT8_T *k = (key != NULL) ? key : MIFARE_DEFAULT_KEY;

    auth_data[0] = 0x01;                   /* Tg: logical target number */
    auth_data[1] = (UINT8_T)key_type;      /* 0x60 = Key A, 0x61 = Key B */
    auth_data[2] = block_num;              /* Block number */
    memcpy(&auth_data[3], k, 6);           /* 6-byte key */
    /* Use the last 4 bytes of UID for MIFARE Classic authentication */
    memcpy(&auth_data[9], &card_info->uid[card_info->uid_len - 4], 4);

    UINT8_T resp[4];
    UINT8_T resp_len = 0;

    OPERATE_RET rt = __pn532_command_exchange(PN532_CMD_INDATAEXCHANGE,
                                                auth_data, sizeof(auth_data),
                                                resp, sizeof(resp), &resp_len, FALSE);
    if (rt != OPRT_OK) {
        return rt;
    }

    /* Check response: resp[0] is InDataExchange response code (0x41),
     * resp[1] is the MIFARE auth status (0x00 = success) */
    if (resp_len < 2 || resp[1] != 0x00) {
        UINT8_T err_code = (resp_len >= 3) ? resp[2] : 0xFF;
        TAL_PR_ERR("PN532 MIFARE auth failed: status=%02X err=%02X", resp[1], err_code);
        return OPRT_COM_ERROR;
    }

    return OPRT_OK;
}

/****************************************************************************
 * Public API implementation
 ****************************************************************************/

OPERATE_RET tuya_pn532_init(IN PN532_CFG_S *cfg)
{
    if (cfg == NULL) {
        return OPRT_INVALID_PARM;
    }

    if (s_pn532_ctx.initialized) {
        TAL_PR_WARN("PN532 already initialized");
        return OPRT_OK;
    }

    memset(&s_pn532_ctx, 0, sizeof(PN532_CTX_S));
    memcpy(&s_pn532_ctx.cfg, cfg, sizeof(PN532_CFG_S));
    s_pn532_ctx.state = PN532_STATE_UNINIT;

    /* Create mutex for thread-safe UART access */
    OPERATE_RET rt = tal_mutex_create_init(&s_pn532_ctx.mutex);
    if (rt != OPRT_OK) {
        TAL_PR_ERR("PN532 mutex create failed: %d", rt);
        return rt;
    }

    /*
     * Initialize UART via tal_uart (non-blocking open_mode=0).
     *
     * tal_uart_init internally calls:
     *   1. tkl_uart_init     → bk_uart_init (HW UART + BK ISR with SW FIFO)
     *   2. tkl_uart_rx_irq_cb_reg → bk_uart_disable_sw_fifo (SW FIFO off!)
     *                              + bk_uart_register_rx_isr (Tuya callback)
     *
     * With SW FIFO disabled, the BK UART ISR delivers bytes directly to
     * the Tuya callback, which fills the tal ring buffer.  tal_uart_read
     * (non-blocking) returns immediately with whatever is available.
     *
     * For writes we still use tkl_uart_write directly (one-shot, no
     * byte-by-byte issue).
     */
    TAL_UART_CFG_T uart_cfg;
    memset(&uart_cfg, 0, sizeof(TAL_UART_CFG_T));
    uart_cfg.base_cfg.baudrate = cfg->baudrate ? cfg->baudrate : PN532_DEFAULT_BAUDRATE;
    uart_cfg.base_cfg.databits = TUYA_UART_DATA_LEN_8BIT;
    uart_cfg.base_cfg.parity   = TUYA_UART_PARITY_TYPE_NONE;
    uart_cfg.base_cfg.stopbits = TUYA_UART_STOP_LEN_1BIT;
    uart_cfg.base_cfg.flowctrl = TUYA_UART_FLOWCTRL_NONE;
    uart_cfg.rx_buffer_size    = cfg->rx_buf_size ? cfg->rx_buf_size : PN532_DEFAULT_RX_BUF_SIZE;
    uart_cfg.open_mode         = 0;  /* non-blocking read */

    rt = tal_uart_init(cfg->uart_port, &uart_cfg);
    if (rt != OPRT_OK) {
        TAL_PR_ERR("PN532 UART init failed: %d", rt);
        goto ERR_EXIT;
    }

    /*
     * RF session mutex: a manual NFC op (manual_begin) holds this for its
     * whole poll+auth+read/write sequence; the auto-poller takes it only
     * around each InListPassiveTarget. This guarantees the poller cannot
     * interleave a re-SELECT that would destroy MIFARE auth state.
     */
    rt = tal_mutex_create_init(&s_rf_session_mutex);
    if (rt != OPRT_OK) {
        TAL_PR_ERR("PN532 session mutex create failed: %d", rt);
        goto ERR_EXIT;
    }

    /* Optional: configure reset pin and perform HW reset */
    if (cfg->reset_pin != 0xFF) {
        TUYA_GPIO_BASE_CFG_T gpio_cfg = {
            .direct = TUYA_GPIO_OUTPUT,
            .mode   = TUYA_GPIO_PULLUP,
            .level  = TUYA_GPIO_LEVEL_HIGH,
        };
        rt = tal_gpio_init(cfg->reset_pin, &gpio_cfg);
        if (rt == OPRT_OK) {
            tuya_pn532_hw_reset();
        }
    }

    /*
     * Production init sequence:
     *   1. Wait for PN532 firmware to boot after power-up
     *   2. SAM Configuration (coldboot=TRUE — first command, single continuous write)
     *   3. Start background card polling if detect_cb is configured
     *
     * The coldboot path in __pn532_send_command prepends 16×0x55 wakeup bytes
     * to the standard PN532 frame and sends everything in a single tkl_uart_write.
     */
    TAL_PR_INFO("PN532 waiting 500ms for firmware boot...");
    tal_system_sleep(500);

    /* SAM Configuration — mandatory before any RF operations.
     * coldboot=TRUE because this is the first command after PN532 power-up. */
    rt = __pn532_sam_config(TRUE);
    if (rt != OPRT_OK) {
        TAL_PR_ERR("PN532 SAM config failed: %d", rt);
        goto ERR_EXIT;
    }

    /* Start background polling if callback is configured */
    if (cfg->detect_cb != NULL) {
        s_polling_args.cb       = cfg->detect_cb;
        s_polling_args.arg      = cfg->cb_arg;
        s_polling_args.interval = cfg->poll_interval ? cfg->poll_interval : 300;

        THREAD_CFG_T thrd_cfg = {
            .priority   = THREAD_PRIO_3,
            .stackDepth = 2 * 1024,
            .thrdname   = "pn532_poll"
        };

        s_polling_running = TRUE;
#if defined(ENABLE_EXT_RAM) && (ENABLE_EXT_RAM == 1)
        thrd_cfg.psram_mode = 1;
#endif
        rt = tal_thread_create_and_start(&s_polling_thread, NULL, NULL,
                                          __pn532_polling_thread_entry,
                                          &s_polling_args, &thrd_cfg);
        if (rt != OPRT_OK) {
            TAL_PR_ERR("PN532 polling thread create failed: %d", rt);
            s_polling_running = FALSE;
            goto ERR_EXIT;
        }
        TAL_PR_INFO("PN532 auto-polling started (interval=%dms)", (int)s_polling_args.interval);
    }

    s_pn532_ctx.state = PN532_STATE_READY;
    s_pn532_ctx.initialized = TRUE;
    TAL_PR_INFO("PN532 init OK (uart=%d baud=%d)", cfg->uart_port, s_pn532_ctx.cfg.baudrate);
    return OPRT_OK;

ERR_EXIT:
    if (s_rf_session_mutex) {
        tal_mutex_release(s_rf_session_mutex);
        s_rf_session_mutex = NULL;
    }
    if (s_pn532_ctx.mutex) {
        tal_mutex_release(s_pn532_ctx.mutex);
        s_pn532_ctx.mutex = NULL;
    }
    tal_uart_deinit(cfg->uart_port);
    s_pn532_ctx.initialized = FALSE;
    s_pn532_ctx.state = PN532_STATE_ERROR;
    return rt;
}

OPERATE_RET tuya_pn532_deinit(VOID)
{
    if (!s_pn532_ctx.initialized) {
        return OPRT_OK;
    }

    /* Stop polling if running */
    tuya_pn532_stop_polling();

    /* Power down PN532 */
    UINT8_T resp[4];
    UINT8_T resp_len;
    __pn532_command_exchange(PN532_CMD_POWERDOWN, NULL, 0,
                               resp, sizeof(resp), &resp_len, FALSE);

    /* Deinit UART */
    tal_uart_deinit(s_pn532_ctx.cfg.uart_port);

    if (s_rf_session_mutex) {
        tal_mutex_release(s_rf_session_mutex);
        s_rf_session_mutex = NULL;
    }
    if (s_pn532_ctx.mutex) {
        tal_mutex_release(s_pn532_ctx.mutex);
        s_pn532_ctx.mutex = NULL;
    }

    s_pn532_ctx.initialized = FALSE;
    s_pn532_ctx.state = PN532_STATE_UNINIT;

    TAL_PR_INFO("PN532 deinit OK");
    return OPRT_OK;
}

OPERATE_RET tuya_pn532_get_firmware_version(OUT CHAR_T *ver_str, IN UINT8_T buf_len)
{
    if (!s_pn532_ctx.initialized) {
        return OPRT_COM_ERROR;
    }

    UINT8_T resp[8];
    UINT8_T resp_len = 0;

    OPERATE_RET rt = __pn532_command_exchange(PN532_CMD_GETFIRMWAREVERSION,
                                                NULL, 0,
                                                resp, sizeof(resp), &resp_len, FALSE);
    if (rt != OPRT_OK) {
        return rt;
    }

    /* Response format: IC(1) + Ver(1) + Rev(1) + Support(1) */
    if (resp_len >= 4 && ver_str != NULL && buf_len > 0) {
        snprintf(ver_str, buf_len, "%d.%d", resp[1], resp[2]);
    }

    return OPRT_OK;
}

OPERATE_RET tuya_pn532_poll_card(OUT PN532_CARD_INFO_S *card_info)
{
    if (!s_pn532_ctx.initialized) {
        return OPRT_COM_ERROR;
    }
    if (card_info == NULL) {
        return OPRT_INVALID_PARM;
    }

    memset(card_info, 0, sizeof(PN532_CARD_INFO_S));

    /* Poll for ISO/IEC 14443A card */
    OPERATE_RET rt = __pn532_in_list_passive_target(card_info);
    if (rt == OPRT_TIMEOUT) {
        return rt;  /* No card present -- not an error */
    }
    if (rt != OPRT_OK) {
        TAL_PR_ERR("PN532 poll failed: %d", rt);
        s_pn532_ctx.state = PN532_STATE_ERROR;
        return rt;
    }

    s_pn532_ctx.state = PN532_STATE_READY;
    return OPRT_OK;
}

OPERATE_RET tuya_pn532_read_block(IN  PN532_CARD_INFO_S  *card_info,
                                   IN  UINT8_T            block_num,
                                   IN  PN532_MIFARE_KEY_TYPE_E key_type,
                                   IN  UINT8_T            *key,
                                   OUT UINT8_T            data[16])
{
    OPERATE_RET rt = OPRT_OK;

    if (!s_pn532_ctx.initialized || card_info == NULL || data == NULL) {
        return OPRT_INVALID_PARM;
    }

    /* Validate block number based on card type
     * MIFARE 4K max block 255 fits in UINT8_T, so only 1K needs explicit check. */
    if (card_info->type == PN532_CARD_TYPE_MIFARE_1K && block_num >= 64) {
        TAL_PR_ERR("PN532 block %d out of range for MIFARE 1K (max 63)", block_num);
        return OPRT_INVALID_PARM;
    }
    if (card_info->type != PN532_CARD_TYPE_MIFARE_1K &&
        card_info->type != PN532_CARD_TYPE_MIFARE_4K) {
        TAL_PR_ERR("PN532 read_block only supports MIFARE Classic cards");
        return OPRT_INVALID_PARM;
    }

    /* Caller holds the RF session (manual_begin), so the auto-poller cannot
     * interleave an InListPassiveTarget and destroy MIFARE auth state. */

    /* Step 1: Authenticate */
    rt = __pn532_mifare_auth(card_info, block_num, key_type, key);
    if (rt != OPRT_OK) {
        goto cleanup;
    }

    /* Small delay for PN532 to stabilize after auth before next InDataExchange */
    tal_system_sleep(10);

    /* Step 2: Read block (Tg + MIFARE Read cmd 0x30 + block_num) */
    UINT8_T read_cmd_data[] = { 0x01, 0x30, block_num };
    UINT8_T resp[20];
    UINT8_T resp_len = 0;

    rt = __pn532_command_exchange(PN532_CMD_INDATAEXCHANGE,
                                    read_cmd_data, sizeof(read_cmd_data),
                                    resp, sizeof(resp), &resp_len, FALSE);
    if (rt != OPRT_OK) {
        goto cleanup;
    }

    /* Check: resp[0] = InDataExchange response code (0x41),
     * resp[1] = MIFARE status (0x00 = success), resp[2..17] = 16 bytes data */
    if (resp_len < 18 || resp[1] != 0x00) {
        UINT8_T err_code = (resp_len >= 3) ? resp[2] : 0xFF;
        TAL_PR_ERR("PN532 MIFARE read block %d failed: status=%02X err=%02X",
                   block_num, resp[1], err_code);
        rt = OPRT_COM_ERROR;
        goto cleanup;
    }

    memcpy(data, &resp[2], MIFARE_BLOCK_SIZE);

cleanup:
    return rt;
}

OPERATE_RET tuya_pn532_write_block(IN PN532_CARD_INFO_S  *card_info,
                                    IN UINT8_T            block_num,
                                    IN PN532_MIFARE_KEY_TYPE_E key_type,
                                    IN UINT8_T            *key,
                                    IN UINT8_T            data[16])
{
    OPERATE_RET rt = OPRT_OK;

    if (!s_pn532_ctx.initialized || card_info == NULL || data == NULL) {
        return OPRT_INVALID_PARM;
    }

    /* Validate block number based on card type
     * MIFARE 4K max block 255 fits in UINT8_T, so only 1K needs explicit check. */
    if (card_info->type == PN532_CARD_TYPE_MIFARE_1K && block_num >= 64) {
        TAL_PR_ERR("PN532 block %d out of range for MIFARE 1K (max 63)", block_num);
        return OPRT_INVALID_PARM;
    }
    if (card_info->type != PN532_CARD_TYPE_MIFARE_1K &&
        card_info->type != PN532_CARD_TYPE_MIFARE_4K) {
        TAL_PR_ERR("PN532 write_block only supports MIFARE Classic cards");
        return OPRT_INVALID_PARM;
    }

    /* Caller holds the RF session (manual_begin), so the auto-poller cannot
     * interleave an InListPassiveTarget and destroy MIFARE auth state. */

    /* Step 1: Authenticate */
    rt = __pn532_mifare_auth(card_info, block_num, key_type, key);
    if (rt != OPRT_OK) {
        goto cleanup;
    }

    /* Small delay for PN532 to stabilize after auth before next InDataExchange */
    tal_system_sleep(10);

    /* Step 2: Write block
     * Tg(0x01) + MIFARE Write cmd 0xA0 + block_num + 16 bytes data
     */
    UINT8_T write_cmd_data[19];
    write_cmd_data[0] = 0x01;   /* Tg: logical target number */
    write_cmd_data[1] = 0xA0;
    write_cmd_data[2] = block_num;
    memcpy(&write_cmd_data[3], data, MIFARE_BLOCK_SIZE);

    UINT8_T resp[4];
    UINT8_T resp_len = 0;

    rt = __pn532_command_exchange(PN532_CMD_INDATAEXCHANGE,
                                    write_cmd_data, sizeof(write_cmd_data),
                                    resp, sizeof(resp), &resp_len, FALSE);
    if (rt != OPRT_OK) {
        goto cleanup;
    }

    /* Check: resp[0] = InDataExchange response code (0x41),
     * resp[1] = MIFARE status (0x00 = success) */
    if (resp_len < 2 || resp[1] != 0x00) {
        UINT8_T err_code = (resp_len >= 3) ? resp[2] : 0xFF;
        TAL_PR_ERR("PN532 MIFARE write block %d failed: status=%02X err=%02X",
                   block_num, resp[1], err_code);
        rt = OPRT_COM_ERROR;
        goto cleanup;
    }

cleanup:
    return rt;
}

/****************************************************************************
 * Background polling
 ****************************************************************************/

/**
 * @brief Format a card UID as uppercase hex (e.g. "04A3F12B").
 * @note buf must hold at least uid_len*2+1 bytes; UIDs are capped to 10 bytes.
 */
STATIC VOID __pn532_uid_hex(IN CONST UINT8_T *uid, IN UINT8_T uid_len,
                             OUT CHAR_T *buf, IN UINT8_T buf_size)
{
    UINT8_T i;
    UINT8_T n   = (uid_len <= 10) ? uid_len : 10;
    UINT8_T pos = 0;

    for (i = 0; i < n && (UINT8_T)(pos + 2) < buf_size; i++) {
        pos += (UINT8_T)snprintf(buf + pos, buf_size - pos, "%02X", uid[i]);
    }
    buf[pos] = '\0';
}

STATIC VOID __pn532_polling_thread_entry(VOID *param)
{
    PN532_POLLING_ARGS_S *args = (PN532_POLLING_ARGS_S *)param;
    BOOL_T has_card = FALSE;
    PN532_CARD_INFO_S last_card;

    TAL_PR_INFO("PN532 polling thread started, interval=%dms", (int)args->interval);

    while (s_polling_running) {
        PN532_CARD_INFO_S card_info;

        /*
         * Hold the RF session lock only around the poll. A manual NFC op
         * (manual_begin) holds this lock for its whole poll+auth+read/write
         * sequence, so we cannot run an InListPassiveTarget (HLTA + re-SELECT)
         * that would destroy its MIFARE auth state. We block here until the
         * manual op finishes. The lock is released before sleeps/callbacks so
         * manual ops are not delayed by our idle time.
         */
        tal_mutex_lock(s_rf_session_mutex);
        if (!s_polling_running) {
            tal_mutex_unlock(s_rf_session_mutex);
            break;
        }

        OPERATE_RET rt = tuya_pn532_poll_card(&card_info);
        tal_mutex_unlock(s_rf_session_mutex);

        if (rt == OPRT_OK) {
            if (!has_card) {
                /* New card appeared */
                has_card = TRUE;
                memcpy(&last_card, &card_info, sizeof(PN532_CARD_INFO_S));
                {
                    CHAR_T uid_hex[21];
                    __pn532_uid_hex(card_info.uid, card_info.uid_len, uid_hex, sizeof(uid_hex));
                    TAL_PR_NOTICE("PN532 card detected: type=%d sak=%02X uid=%s",
                                  card_info.type, card_info.sak, uid_hex);
                }
                if (args->cb != NULL) {
                    args->cb(&card_info, args->arg);
                }
            } else if (card_info.uid_len != last_card.uid_len ||
                       memcmp(card_info.uid, last_card.uid, card_info.uid_len) != 0) {
                /* Card changed (different UID) */
                memcpy(&last_card, &card_info, sizeof(PN532_CARD_INFO_S));
                TAL_PR_INFO("PN532 card changed: type=%d sak=%02X uid_len=%d",
                            card_info.type, card_info.sak, card_info.uid_len);
                if (args->cb != NULL) {
                    args->cb(&card_info, args->arg);
                }
            }
            /* else: same card, skip callback (dedup) */

            /* Cooldown after detection to avoid rapid re-polling */
            tal_system_sleep(500);
        } else {
            if (has_card) {
                /* Card removed */
                has_card = FALSE;
                {
                    CHAR_T uid_hex[21];
                    __pn532_uid_hex(last_card.uid, last_card.uid_len, uid_hex, sizeof(uid_hex));
                    TAL_PR_NOTICE("PN532 card removed: uid=%s", uid_hex);
                }
                if (args->cb != NULL) {
                    args->cb(NULL, args->arg);
                }
            }
            /* else: no card and wasn't tracking one, normal idle */
        }

        tal_system_sleep(args->interval);
    }

    TAL_PR_INFO("PN532 polling thread stopped");
}

OPERATE_RET tuya_pn532_stop_polling(VOID)
{
    s_polling_running = FALSE;

    if (s_polling_thread != NULL) {
        /*
         * Wait for the polling thread to stop.  The thread loop checks
         * s_polling_running each iteration.  Max sleep inside the loop:
         *   tal_system_sleep(interval)  — up to 300ms (default)
         *   + tal_system_sleep(500)     — cooldown after card detect
         *   + tuya_pn532_poll_card()    — poll timeout ~150ms
         * So ~1s worst case, plus margin.
         */
        UINT_T wait_ms = s_polling_args.interval + 1200;
        UINT_T elapsed = 0;
        while (elapsed < wait_ms) {
            THREAD_STATE_E st = tal_thread_get_state(s_polling_thread);
            if (st == THREAD_STATE_STOP || st == THREAD_STATE_DELETE) {
                break;
            }
            tal_system_sleep(20);
            elapsed += 20;
        }
        tal_thread_delete(s_polling_thread);
        s_polling_thread = NULL;
    }

    return OPRT_OK;
}

PN532_STATE_E tuya_pn532_get_state(VOID)
{
    return s_pn532_ctx.state;
}

VOID tuya_pn532_manual_begin(VOID)
{
    /*
     * Acquire the RF session lock. This blocks until the auto-poller finishes
     * its current InListPassiveTarget, then holds the lock so the poller cannot
     * run another one until manual_end(). Callers MUST wrap any multi-step RF
     * sequence (poll_card + auth + read/write) in manual_begin/end so the
     * poller cannot interleave a re-SELECT mid-authentication.
     *
     * The mutex is non-recursive, so do NOT nest (read_block/write_block no
     * longer take it themselves — the caller owns the session).
     */
    if (s_rf_session_mutex) {
        tal_mutex_lock(s_rf_session_mutex);
    }
}

VOID tuya_pn532_manual_end(VOID)
{
    if (s_rf_session_mutex) {
        tal_mutex_unlock(s_rf_session_mutex);
    }
}

OPERATE_RET tuya_pn532_hw_reset(VOID)
{
    if (s_pn532_ctx.cfg.reset_pin == 0xFF) {
        TAL_PR_DEBUG("PN532 no reset pin configured, skipping HW reset");
        return OPRT_OK;
    }

    /* Pulse reset low for 10ms then high.
     * The 50ms wait is only for the reset pulse to stabilize;
     * firmware boot wait (500ms) is handled by the caller in tuya_pn532_init. */
    tal_gpio_write(s_pn532_ctx.cfg.reset_pin, TUYA_GPIO_LEVEL_LOW);
    tal_system_sleep(10);
    tal_gpio_write(s_pn532_ctx.cfg.reset_pin, TUYA_GPIO_LEVEL_HIGH);
    tal_system_sleep(50);

    TAL_PR_INFO("PN532 HW reset done");
    return OPRT_OK;
}
