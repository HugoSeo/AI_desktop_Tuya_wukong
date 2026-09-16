/**
 * @file tuya_lcd_pbuf.h
 * @brief LCD Pixel Buffer Pool Management - Multi-buffer Pool for Asynchronous LCD Refresh
 * 
 * This module provides a thread-safe, multi-buffer pool management system for LCD display operations.
 * It decouples frame processing from SPI transmission, enabling non-blocking LCD refresh.
 * 
 * Key Features:
 * - Pre-allocated buffer pool with configurable size (recommended 3-5 buffers)
 * - Thread-safe acquire/release operations using semaphore + mutex
 * - Asynchronous buffer release via SPI completion callback
 * - Zero-copy design: buffers allocated from PSRAM once during init
 * - Single-linked list management for efficient buffer tracking
 * 
 * Performance Benefits:
 * - Eliminates blocking wait for SPI transmission (23 ticks → 0 ticks)
 * - Enables frame processing pipeline: decode → swap → flush (overlap operations)
 * - Typical throughput improvement: 2-3x at 15 fps camera input
 * 
 * @author linch
 * @date 2025-01-04
 * @version 1.0.0
 * 
 * @copyright Copyright (c) 2025 Tuya Inc. All rights reserved.
 * 
 * @note For detailed architecture and usage examples, see tuya_lcd_pbuf.c
 * @see tuya_lcd_pbuf.c for implementation details and usage examples
 * @see ty_frame_buff.h for ty_frame_buffer_t structure definition
 * @see tuya_slist.h for single-linked list implementation
 */

#ifndef __TUYA_LCD_PBUF_H__
#define __TUYA_LCD_PBUF_H__

#include "tuya_cloud_types.h"
#include "tuya_slist.h"
#include "ty_frame_buff.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief LCD pixel buffer node structure
 * 
 * Each node represents a pre-allocated buffer in the pool.
 * Managed by single-linked list (tuya_slist.h)
 * 
 * @note The ty_frame_buffer_t is pre-allocated and contains ALL buffer information:
 *       - Buffer pointer: frame.frame
 *       - Dimensions: frame.width, frame.height
 *       - Size: frame.len
 *       - Format: frame.fmt
 *       This eliminates ALL redundant fields, achieving maximum data consolidation.
 */
typedef struct tuya_lcd_pbuf_node {
    SLIST_HEAD list;              // Linked list head (must be first member)
    ty_frame_buffer_t frame;      // Pre-allocated frame with ALL metadata (including buffer ptr)
} tuya_lcd_pbuf_node_t;

/**
 * @brief Initialize LCD pixel buffer pool
 * 
 * Pre-allocates specified number of buffers from PSRAM and constructs
 * the free list. Creates synchronization primitives (mutex + semaphore).
 * 
 * @param[in] width Buffer width in pixels
 * @param[in] height Buffer height in pixels
 * @param[in] num Number of buffers to pre-allocate (recommended 3-5)
 * 
 * @return OPERATE_RET
 *   @retval OPRT_OK Success
 *   @retval OPRT_MALLOC_FAILED Memory allocation failed
 *   @retval OPRT_INVALID_PARM Invalid parameters (num=0 or width/height=0)
 * 
 * @note
 *   - Each buffer size = width * height * 2 bytes (RGB565 format)
 *   - Must call before using tuya_lcd_pbuf_acquire()
 *   - Should be called only once during initialization
 */
OPERATE_RET tuya_lcd_pbuf_init(UINT16_T width, UINT16_T height, UINT8_T num);

/**
 * @brief Acquire an available buffer from the pool
 * 
 * Blocks until a buffer becomes available or timeout expires.
 * Thread-safe operation using semaphore + mutex.
 * 
 * @param[in] timeout_ms Timeout in milliseconds (0 = no wait, WAIT_FOREVER = infinite)
 * 
 * @return tuya_lcd_pbuf_node_t* 
 *   @retval Non-NULL Pointer to acquired buffer node
 *   @retval NULL Failed to acquire buffer (timeout or pool not initialized)
 * 
 * @note
 *   - Caller must release the buffer via tuya_lcd_pbuf_release() after use
 *   - Typical usage: acquire -> decode JPEG -> flush to LCD -> auto-release in callback
 */
tuya_lcd_pbuf_node_t* tuya_lcd_pbuf_acquire(UINT32_T timeout_ms);

/**
 * @brief Release a buffer back to the pool
 * 
 * Returns the buffer to the free list and signals waiting threads.
 * Typically called from SPI completion callback (frame_flush_callback).
 * 
 * @param[in] node Buffer node to release (obtained from tuya_lcd_pbuf_acquire)
 * 
 * @return OPERATE_RET
 *   @retval OPRT_OK Success
 *   @retval OPRT_INVALID_PARM node is NULL
 * 
 * @note
 *   - Safe to call from interrupt context (callback)
 *   - Node pointer becomes invalid after release, don't reuse
 */
OPERATE_RET tuya_lcd_pbuf_release(tuya_lcd_pbuf_node_t *node);

/**
 * @brief Destroy the buffer pool and free all resources
 * 
 * Frees all allocated buffers, nodes, mutex, and semaphore.
 * Should be called during system shutdown or module deinitialization.
 * 
 * @return OPERATE_RET
 *   @retval OPRT_OK Success
 * 
 * @note
 *   - Caller must ensure no buffers are in use (all returned to pool)
 *   - After calling, tuya_lcd_pbuf_init() must be called again to reuse
 */
OPERATE_RET tuya_lcd_pbuf_deinit(VOID);

/**
 * @brief Get buffer pool statistics (for debugging)
 * 
 * @param[out] total_count Total number of buffers in pool
 * @param[out] free_count Current number of free buffers
 * 
 * @return OPERATE_RET
 *   @retval OPRT_OK Success
 *   @retval OPRT_INVALID_PARM Output pointers are NULL
 */
OPERATE_RET tuya_lcd_pbuf_get_stats(UINT8_T *total_count, UINT8_T *free_count);

#ifdef __cplusplus
}
#endif

#endif /* __TUYA_LCD_PBUF_H__ */

