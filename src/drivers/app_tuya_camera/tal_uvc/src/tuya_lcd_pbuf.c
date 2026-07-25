/**
 * @file tuya_lcd_pbuf.c
 * @brief LCD Pixel Buffer Pool Management - Implementation
 *
 * This file implements a thread-safe, multi-buffer pool for asynchronous LCD display operations.
 * The pool decouples frame producers (camera) from consumers (LCD SPI), enabling non-blocking
 * display refresh and improving system throughput.
 * 
 * Data Flow Architecture:
 * ┌─────────────────────────────────────────────────────────────┐
 * │  Frame Producer (UVC/Camera)                                │
 * │  ↓                                                           │
 * │  tuya_lcd_pbuf_acquire() ← Wait for available buffer        │
 * │  ↓                                                           │
 * │  Copy & Process (JPEG decode, byte swap, etc.)              │
 * │  ↓                                                           │
 * │  tuya_ai_display_flush(&buffer->frame) ← Async SPI start    │
 * │  ↓                                                           │
 * │  Return immediately (non-blocking)                          │
 * │  ↓                                                           │
 * │  [SPI DMA Transfer in Background]                           │
 * │  ↓                                                           │
 * │  SPI Complete Interrupt → frame_flush_callback()            │
 * │  ↓                                                           │
 * │  tuya_lcd_pbuf_release() ← Return buffer to pool            │
 * └─────────────────────────────────────────────────────────────┘
 * 
 * Performance: Eliminates SPI blocking (23 ticks → 0), achieving 3.3x throughput improvement
 * 
 * For detailed architecture, usage examples, and integration guide, see:
 * @see ../README.md for complete documentation
 * 
 * @author linch
 * @date 2025-01-04
 * @version 1.0.0
 * 
 * @copyright Copyright (c) 2025 Tuya Inc. All rights reserved.
 */

#include "tuya_lcd_pbuf.h"
#include "tal_log.h"
#include "tal_memory.h"
#include "tal_semaphore.h"
#include "tal_mutex.h"

/**
 * @brief LCD pixel buffer pool control structure
 */
typedef struct {
    SLIST_HEAD free_list;         // Free buffer linked list
    MUTEX_HANDLE mutex;           // Mutex for list operations
    SEM_HANDLE sem;               // Semaphore (count = number of buffers)
    UINT8_T total_count;          // Total buffer count
    UINT8_T free_count;           // Current free buffer count (for debugging)
} tuya_lcd_pbuf_t;

// Global pool instance pointer (NULL = not initialized)
static tuya_lcd_pbuf_t *s_tuya_lcd_pbuf = NULL;



STATIC VOID_T __lcd_pbuf_free_callback(ty_frame_buffer_t *frame)
{   
    if (frame && frame->pdata) {
        tuya_lcd_pbuf_node_t *pbuf_node = (tuya_lcd_pbuf_node_t *)frame->pdata;
        tuya_lcd_pbuf_release(pbuf_node);
        TAL_PR_TRACE("LCD frame flush complete, buffer released");
    }
}

/**
 * @brief Initialize LCD pixel buffer pool
 */
OPERATE_RET tuya_lcd_pbuf_init(UINT16_T width, UINT16_T height, UINT8_T num)
{
    OPERATE_RET ret = OPRT_OK;
    
    // Parameter validation
    if (num == 0 || width == 0 || height == 0) {
        TAL_PR_ERR("Invalid parameters: num=%d, width=%d, height=%d", num, width, height);
        return OPRT_INVALID_PARM;
    }
    
    if (s_tuya_lcd_pbuf != NULL) {
        TAL_PR_WARN("LCD pbuf pool already initialized, deinit first");
        return OPRT_OK;
    }
    
    TAL_PR_DEBUG("Initializing LCD pbuf pool: num=%d, %dx%d", num, width, height);
    
    // Calculate buffer size (local variable, no need to store in pool)
    UINT32_T buffer_size = width * height * 2;  // RGB565 = 2 bytes per pixel
    
    // Allocate pool structure
    s_tuya_lcd_pbuf = (tuya_lcd_pbuf_t *)tal_malloc(sizeof(tuya_lcd_pbuf_t));
    if (!s_tuya_lcd_pbuf) {
        TAL_PR_ERR("Failed to allocate pool structure");
        return OPRT_MALLOC_FAILED;
    }
    
    // Initialize pool structure
    memset(s_tuya_lcd_pbuf, 0, sizeof(tuya_lcd_pbuf_t));
    INIT_SLIST_HEAD(&s_tuya_lcd_pbuf->free_list);
    s_tuya_lcd_pbuf->total_count = num;
    s_tuya_lcd_pbuf->free_count = 0;
    
    // Create mutex for list protection
    ret = tal_mutex_create_init(&s_tuya_lcd_pbuf->mutex);
    if (ret != OPRT_OK) {
        TAL_PR_ERR("Failed to create mutex: %d", ret);
        goto err_mutex;
    }
    
    // Create semaphore with initial count = buffer count
    ret = tal_semaphore_create_init(&s_tuya_lcd_pbuf->sem, 0, num);
    if (ret != OPRT_OK) {
        TAL_PR_ERR("Failed to create semaphore: %d", ret);
        goto err_sem;
    }
    
    // Pre-allocate buffers and add to free list
    for (UINT8_T i = 0; i < num; i++) {
        // Allocate node structure
        tuya_lcd_pbuf_node_t *node = (tuya_lcd_pbuf_node_t *)tal_malloc(sizeof(tuya_lcd_pbuf_node_t));
        if (!node) {
            TAL_PR_ERR("Failed to allocate node %d", i);
            ret = OPRT_MALLOC_FAILED;
            goto err_alloc;
        }
        memset(node, 0, sizeof(tuya_lcd_pbuf_node_t));
        
        // Allocate buffer from PSRAM
        UINT8_T *buffer = (UINT8_T *)tal_psram_malloc(buffer_size);
        if (!buffer) {
            TAL_PR_ERR("Failed to allocate PSRAM buffer %d (size=%d)", i, buffer_size);
            tal_free(node);
            ret = OPRT_MALLOC_FAILED;
            goto err_alloc;
        }
        
        // Initialize list node
        tuya_init_slist_node(&node->list);
        
        // Pre-initialize frame structure (all metadata + buffer pointer stored here)
        node->frame.fmt = TY_PIXEL_FMT_RGB565;
        node->frame.type = TYPE_PSRAM;
        node->frame.frame = buffer;       // Store buffer pointer in frame
        node->frame.x_start = 0;
        node->frame.y_start = 0;
        node->frame.width = width;
        node->frame.height = height;
        node->frame.len = buffer_size;
        node->frame.free_cb = __lcd_pbuf_free_callback;  // Will be set by caller
        node->frame.pdata = (UINT8_T *)node;  // Store self-reference for release
        
        // Add to free list (tail insertion for FIFO behavior)
        tuya_slist_add_tail(&s_tuya_lcd_pbuf->free_list, &node->list);
        s_tuya_lcd_pbuf->free_count++;
        
        // Post semaphore to indicate available buffer
        tal_semaphore_post(s_tuya_lcd_pbuf->sem);
        
        TAL_PR_DEBUG("Buffer %d allocated: node=%p, buffer=%p, size=%d", 
                     i, node, buffer, buffer_size);
    }
    
    TAL_PR_DEBUG("LCD pbuf pool initialized successfully: %d buffers, %d bytes each",
                 num, buffer_size);
    
    return OPRT_OK;

err_alloc:
    // Free all allocated buffers so far
    {
        SLIST_HEAD *pos, *n;
        tuya_lcd_pbuf_node_t *node;
        
        SLIST_FOR_EACH_SAFE(pos, n, &s_tuya_lcd_pbuf->free_list) {
            node = SLIST_ENTRY(pos, tuya_lcd_pbuf_node_t, list);
            tuya_slist_del(&s_tuya_lcd_pbuf->free_list, pos);
            if (node->frame.frame) {
                tal_psram_free(node->frame.frame);
            }
            tal_free(node);
        }
    }
    tal_semaphore_release(s_tuya_lcd_pbuf->sem);
    
err_sem:
    tal_mutex_release(s_tuya_lcd_pbuf->mutex);
    
err_mutex:
    tal_free(s_tuya_lcd_pbuf);
    s_tuya_lcd_pbuf = NULL;
    return ret;
}

/**
 * @brief Acquire an available buffer from the pool
 */
tuya_lcd_pbuf_node_t* tuya_lcd_pbuf_acquire(UINT32_T timeout_ms)
{
    tuya_lcd_pbuf_node_t *node = NULL;
    SLIST_HEAD *pos = NULL;
    
    if (!s_tuya_lcd_pbuf) {
        TAL_PR_ERR("Buffer pool not initialized");
        return NULL;
    }
    
    // Wait for available buffer (semaphore count > 0)
    OPERATE_RET ret = tal_semaphore_wait(s_tuya_lcd_pbuf->sem, timeout_ms);
    if (ret != OPRT_OK) {
        TAL_PR_WARN("Failed to acquire buffer (timeout=%d): %d", timeout_ms, ret);
        return NULL;
    }
    
    // Lock and extract node from free list
    tal_mutex_lock(s_tuya_lcd_pbuf->mutex);
    
    // Get first node from free list (head)
    pos = s_tuya_lcd_pbuf->free_list.next;
    if (pos == NULL) {
        // This should never happen if semaphore is correct
        TAL_PR_ERR("Critical: free list empty but semaphore available!");
        tal_mutex_unlock(s_tuya_lcd_pbuf->mutex);
        tal_semaphore_post(s_tuya_lcd_pbuf->sem);  // Restore semaphore
        return NULL;
    }
    
    // Extract node from list
    node = SLIST_ENTRY(pos, tuya_lcd_pbuf_node_t, list);
    tuya_slist_del(&s_tuya_lcd_pbuf->free_list, pos);
    s_tuya_lcd_pbuf->free_count--;
    
    tal_mutex_unlock(s_tuya_lcd_pbuf->mutex);
    
    TAL_PR_TRACE("Buffer acquired: node=%p, buffer=%p, free=%d/%d",
                 node, node->frame.frame, s_tuya_lcd_pbuf->free_count, s_tuya_lcd_pbuf->total_count);
    
    return node;
}

/**
 * @brief Release a buffer back to the pool
 */
OPERATE_RET tuya_lcd_pbuf_release(tuya_lcd_pbuf_node_t *node)
{
    if (!node) {
        TAL_PR_ERR("Invalid node pointer");
        return OPRT_INVALID_PARM;
    }
    
    if (!s_tuya_lcd_pbuf) {
        TAL_PR_ERR("Buffer pool not initialized");
        return OPRT_COM_ERROR;
    }
    
    // Lock and return node to free list
    tal_mutex_lock(s_tuya_lcd_pbuf->mutex);
    
    // Add to tail of free list (FIFO behavior)
    tuya_slist_add_tail(&s_tuya_lcd_pbuf->free_list, &node->list);
    s_tuya_lcd_pbuf->free_count++;

    /* Reset fields that may have been modified by the display driver
     * (e.g. tuya_display_flush accumulates x/y offsets in-place). */
    node->frame.x_start = 0;
    node->frame.y_start = 0;
    
    tal_mutex_unlock(s_tuya_lcd_pbuf->mutex);
    
    // Signal that a buffer is now available
    tal_semaphore_post(s_tuya_lcd_pbuf->sem);
    
    TAL_PR_TRACE("Buffer released: node=%p, buffer=%p, free=%d/%d",
                 node, node->frame.frame, s_tuya_lcd_pbuf->free_count, s_tuya_lcd_pbuf->total_count);
    
    return OPRT_OK;
}

/**
 * @brief Destroy the buffer pool and free all resources
 */
OPERATE_RET tuya_lcd_pbuf_deinit(VOID)
{
    if (!s_tuya_lcd_pbuf) {
        TAL_PR_WARN("Buffer pool not initialized");
        return OPRT_OK;
    }
    
    TAL_PR_DEBUG("Deinitializing LCD pbuf pool...");
    
    // Free all buffers
    SLIST_HEAD *pos, *n;
    tuya_lcd_pbuf_node_t *node;
    UINT8_T freed_count = 0;
    
    tal_mutex_lock(s_tuya_lcd_pbuf->mutex);
    
    SLIST_FOR_EACH_SAFE(pos, n, &s_tuya_lcd_pbuf->free_list) {
        node = SLIST_ENTRY(pos, tuya_lcd_pbuf_node_t, list);
        tuya_slist_del(&s_tuya_lcd_pbuf->free_list, pos);
        
        if (node->frame.frame) {
            tal_psram_free(node->frame.frame);
        }
        tal_free(node);
        freed_count++;
    }
    
    tal_mutex_unlock(s_tuya_lcd_pbuf->mutex);
    
    if (freed_count != s_tuya_lcd_pbuf->total_count) {
        TAL_PR_WARN("Not all buffers returned: freed=%d, total=%d", 
                    freed_count, s_tuya_lcd_pbuf->total_count);
    }
    
    // Release synchronization primitives
    tal_semaphore_release(s_tuya_lcd_pbuf->sem);
    tal_mutex_release(s_tuya_lcd_pbuf->mutex);
    
    // Free pool structure and set to NULL
    tal_free(s_tuya_lcd_pbuf);
    s_tuya_lcd_pbuf = NULL;
    
    TAL_PR_DEBUG("LCD pbuf pool deinitialized: freed %d buffers", freed_count);
    
    return OPRT_OK;
}

/**
 * @brief Get buffer pool statistics
 */
OPERATE_RET tuya_lcd_pbuf_get_stats(UINT8_T *total_count, UINT8_T *free_count)
{
    if (!total_count || !free_count) {
        return OPRT_INVALID_PARM;
    }
    
    if (!s_tuya_lcd_pbuf) {
        *total_count = 0;
        *free_count = 0;
        return OPRT_COM_ERROR;
    }
    
    tal_mutex_lock(s_tuya_lcd_pbuf->mutex);
    *total_count = s_tuya_lcd_pbuf->total_count;
    *free_count = s_tuya_lcd_pbuf->free_count;
    tal_mutex_unlock(s_tuya_lcd_pbuf->mutex);
    
    return OPRT_OK;
}

