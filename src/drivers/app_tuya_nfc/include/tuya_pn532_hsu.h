/**
 * @file tuya_pn532_hsu.h
 * @brief PN532 NFC driver over HSU (High Speed UART)
 * @version 0.1
 * @date 2026-07-07
 *
 * @copyright Copyright 2025-2026 Tuya Inc. All Rights Reserved.
 */

#ifndef __TUYA_PN532_HSU_H
#define __TUYA_PN532_HSU_H

#include <stdint.h>
#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/****************************************************************************
 * UART configuration defaults
 ****************************************************************************/
#define PN532_DEFAULT_BAUDRATE      115200
#define PN532_DEFAULT_RX_BUF_SIZE   512
#define PN532_HSU_TIMEOUT_MS        100

/****************************************************************************
 * PN532 Frame constants
 ****************************************************************************/
#define PN532_PREAMBLE              0x00
#define PN532_STARTCODE1            0x00
#define PN532_STARTCODE2            0xFF
#define PN532_POSTAMBLE             0x00
#define PN532_HOST_TO_PN532         0xD4
#define PN532_PN532_TO_HOST         0xD5

/****************************************************************************
 * PN532 Commands
 ****************************************************************************/
#define PN532_CMD_DIAGNOSE                  0x00
#define PN532_CMD_GETFIRMWAREVERSION        0x02
#define PN532_CMD_GETGENERALSTATUS          0x04
#define PN532_CMD_READREGISTER              0x06
#define PN532_CMD_WRITEREGISTER             0x08
#define PN532_CMD_READGPIO                  0x0C
#define PN532_CMD_WRITEGPIO                 0x0E
#define PN532_CMD_SETSERIALBAUDRATE         0x10
#define PN532_CMD_SETPARAMETERS             0x12
#define PN532_CMD_SAMCONFIGURATION          0x14
#define PN532_CMD_POWERDOWN                 0x16
#define PN532_CMD_RFCONFIGURATION           0x32
#define PN532_CMD_RFREGULATIONTEST          0x58
#define PN532_CMD_INJUMPFORDEP              0x56
#define PN532_CMD_INJUMPFORPSL              0x46
#define PN532_CMD_INLISTPASSIVETARGET       0x4A
#define PN532_CMD_INATR                     0x50
#define PN532_CMD_INPSL                     0x4E
#define PN532_CMD_INDATAEXCHANGE            0x40
#define PN532_CMD_INCOMMUNICATETHRU         0x42
#define PN532_CMD_INDESELECT                0x44
#define PN532_CMD_INRELEASE                 0x52
#define PN532_CMD_INSELECT                  0x54
#define PN532_CMD_INAUTOPOLL                0x60
#define PN532_CMD_TGINITASTARGET            0x8C
#define PN532_CMD_TGSETGENERALBYTES         0x92
#define PN532_CMD_TGGETDATA                 0x86
#define PN532_CMD_TGSETDATA                 0x8E
#define PN532_CMD_TGSETMETADATA             0x94
#define PN532_CMD_TGGETINITIATORCOMMAND     0x88
#define PN532_CMD_TGRESPONSETOINITIATOR     0x90
#define PN532_CMD_TGGETTARGETSTATUS         0x8A

/****************************************************************************
 * PN532 Response / Status codes
 ****************************************************************************/
#define PN532_ACK_PACKET_SIZE       6
#define PN532_RESPONSE_ACK          0x00
#define PN532_RESPONSE_NACK         0xFF
#define PN532_RESPONSE_ERROR        0x01

/****************************************************************************
 * Card types (ISO/IEC 14443A / NFC Forum tag types)
 ****************************************************************************/
typedef enum {
    PN532_CARD_TYPE_UNKNOWN     = 0x00,
    PN532_CARD_TYPE_MIFARE_1K   = 0x01,
    PN532_CARD_TYPE_MIFARE_4K   = 0x02,
    PN532_CARD_TYPE_MIFARE_UL   = 0x03,   /* Ultralight */
    PN532_CARD_TYPE_MIFARE_DESFIRE = 0x04,
    PN532_CARD_TYPE_ISO14443_4  = 0x05,
    PN532_CARD_TYPE_FELICA      = 0x10,
    PN532_CARD_TYPE_ISO14443B   = 0x20,
} PN532_CARD_TYPE_E;

/****************************************************************************
 * PN532 state
 ****************************************************************************/
typedef enum {
    PN532_STATE_UNINIT = 0,
    PN532_STATE_IDLE,
    PN532_STATE_READY,
    PN532_STATE_ERROR,
} PN532_STATE_E;

/****************************************************************************
 * MIFARE key types
 ****************************************************************************/
typedef enum {
    PN532_KEY_A = 0x60,
    PN532_KEY_B = 0x61,
} PN532_MIFARE_KEY_TYPE_E;

/****************************************************************************
 * Card information structure
 ****************************************************************************/
typedef struct {
    PN532_CARD_TYPE_E   type;
    UINT8_T             uid[10];
    UINT8_T             uid_len;
    UINT8_T             atqa[2];
    UINT8_T             sak;
} PN532_CARD_INFO_S;

/****************************************************************************
 * Callback type for card detection events
 ****************************************************************************/
typedef VOID (*PN532_CARD_DETECT_CB)(PN532_CARD_INFO_S *card_info, VOID *arg);

/****************************************************************************
 * PN532 configuration
 ****************************************************************************/
typedef struct {
    TUYA_UART_NUM_E         uart_port;
    UINT_T                  baudrate;
    UINT_T                  rx_buf_size;
    UINT8_T                 irq_pin;            /* optional, GPIO pin for PN532 IRQ */
    UINT8_T                 reset_pin;          /* optional, GPIO pin for PN532 reset */
    BOOL_T                  use_irq;            /* if TRUE, use IRQ pin instead of polling */
    PN532_CARD_DETECT_CB    detect_cb;          /* card detection callback (NULL = no auto-poll) */
    VOID                    *cb_arg;            /* user argument passed to callback */
    UINT_T                  poll_interval;      /* polling interval in ms (0 = default 300ms) */
} PN532_CFG_S;

/****************************************************************************
 * Public API
 ****************************************************************************/

/**
 * @brief Initialize the PN532 module and optionally start auto-polling
 *
 * Configures UART, wakes up PN532, performs SAM configuration.
 * If cfg->detect_cb is not NULL, starts a background polling thread
 * that periodically scans for cards and invokes the callback.
 *
 * @param[in] cfg  Pointer to PN532 configuration
 * @return OPRT_OK on success, others on error
 */
OPERATE_RET tuya_pn532_init(IN PN532_CFG_S *cfg);

/**
 * @brief Deinitialize the PN532 module
 *
 * Powers down PN532 and releases UART.
 *
 * @return OPRT_OK on success, others on error
 */
OPERATE_RET tuya_pn532_deinit(VOID);

/**
 * @brief Get firmware version from PN532
 *
 * @param[out] ver_str  Output buffer for version string (at least 16 bytes)
 * @return OPRT_OK on success, others on error
 *
 * @note ver_str format: "XX.XX" (IC version, firmware version etc.)
 */
OPERATE_RET tuya_pn532_get_firmware_version(OUT CHAR_T *ver_str, IN UINT8_T buf_len);

/**
 * @brief Scan for a passive target (card) on the RF field
 *
 * Polls for one ISO/IEC 14443A card. Maximum retries as per spec.
 *
 * @param[out] card_info  Pointer to card info structure to fill
 * @return OPRT_OK if a card is detected, OPRT_TIMEOUT if no card found
 */
OPERATE_RET tuya_pn532_poll_card(OUT PN532_CARD_INFO_S *card_info);

/**
 * @brief Read a block from MIFARE Classic card
 *
 * @param[in]  card_info  Pointer to previously detected card info
 * @param[in]  block_num  Block number to read (0-63 for 1K, 0-255 for 4K)
 * @param[in]  key_type   Key A or Key B
 * @param[in]  key        6-byte key (NULL means use default key FFFFFFFFFFFF)
 * @param[out] data       Output buffer (16 bytes required for MIFARE Classic)
 * @return OPRT_OK on success, others on error
 */
OPERATE_RET tuya_pn532_read_block(IN  PN532_CARD_INFO_S  *card_info,
                                   IN  UINT8_T            block_num,
                                   IN  PN532_MIFARE_KEY_TYPE_E key_type,
                                   IN  UINT8_T            *key,
                                   OUT UINT8_T            data[16]);

/**
 * @brief Write a block to MIFARE Classic card
 *
 * @param[in] card_info  Pointer to previously detected card info
 * @param[in] block_num  Block number to write
 * @param[in] key_type   Key A or Key B
 * @param[in] key        6-byte key (NULL means default key FFFFFFFFFFFF)
 * @param[in] data       16 bytes of data to write
 * @return OPRT_OK on success, others on error
 */
OPERATE_RET tuya_pn532_write_block(IN PN532_CARD_INFO_S  *card_info,
                                    IN UINT8_T            block_num,
                                    IN PN532_MIFARE_KEY_TYPE_E key_type,
                                    IN UINT8_T            *key,
                                    IN UINT8_T            data[16]);

/**
 * @brief Stop continuous card polling (auto-started by init)
 *
 * @return OPRT_OK on success, others on error
 */
OPERATE_RET tuya_pn532_stop_polling(VOID);

/**
 * @brief Acquire the RF session lock — call before a manual NFC poll/read/write
 *
 * Blocks until the auto-poller finishes its current scan, then holds an exclusive
 * lock so the poller cannot run another InListPassiveTarget (which would re-SELECT
 * the card and destroy MIFARE auth state) until tuya_pn532_manual_end().
 *
 * The caller MUST wrap any multi-step sequence (tuya_pn532_poll_card + auth +
 * read/write) in manual_begin/end. The underlying mutex is non-recursive, so do
 * NOT nest: read_block/write_block no longer take it themselves.
 */
VOID tuya_pn532_manual_begin(VOID);

/**
 * @brief Release the RF session lock — call after a manual NFC read/write
 *
 * Allows the auto-poller to resume scanning.
 */
VOID tuya_pn532_manual_end(VOID);

/**
 * @brief Get the current PN532 state
 *
 * @return Current state
 */
PN532_STATE_E tuya_pn532_get_state(VOID);

/**
 * @brief Reset the PN532 via reset pin (hardware reset)
 *
 * @return OPRT_OK on success, others on error
 */
OPERATE_RET tuya_pn532_hw_reset(VOID);

#ifdef __cplusplus
}
#endif

#endif /* __TUYA_PN532_HSU_H */
