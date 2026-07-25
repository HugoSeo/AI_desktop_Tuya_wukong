# TAL UVC - Tuya Abstraction Layer for UVC Camera

**Author:** linch  
**Version:** 1.0.0  
**Date:** 2025-01-04

---

## 📖 Overview

TAL UVC provides a unified abstraction layer for UVC (USB Video Class) camera operations on TuyaOS platform. It integrates hardware JPEG decoding, LCD buffer pool management, and asynchronous display refresh, optimized for BK7258 platform.

### Key Features

- 🎥 **UVC Camera Control**: Initialization, start/stop, configuration
- 🔄 **Dual-Path Output**: Decoded RGB565 (for LCD) + Original MJPEG (for cloud)
- ⚡ **Hardware Acceleration**: JPEG hardware decode (MJPEG → RGB565)
- 📦 **Multi-Buffer Pool**: Asynchronous LCD refresh with 3x performance boost
- 🔀 **Byte Order Conversion**: CPU little-endian ↔ SPI big-endian
- 🧪 **CLI Test Commands**: Built-in testing and debugging tools

---

## 🏗️ Architecture

### System Components

```
┌─────────────────────────────────────────────────────────────────┐
│                    Application Layer                            │
│  - tuya_ai_toy_camera.c (Camera control logic)                  │
│  - UVC frame callback (process RGB565 & JPEG)                   │
└────────────────────┬────────────────────────────────────────────┘
                     │
                     ↓
┌─────────────────────────────────────────────────────────────────┐
│                    TAL UVC Layer (tal_uvc)                      │
│  - tal_uvc_init/start/stop/deinit                               │
│  - Hardware JPEG decode (MJPEG → RGB565)                        │
│  - Byte swap (little-endian → big-endian)                       │
│  - Dual callback: RGB565 + MJPEG                                │
└────────────────────┬────────────────────────────────────────────┘
                     │
                     ↓
┌─────────────────────────────────────────────────────────────────┐
│              LCD Buffer Pool (tuya_lcd_pbuf)                    │
│  - Multi-buffer management (3-5 buffers)                        │
│  - Semaphore + Mutex synchronization                            │
│  - Asynchronous buffer release                                  │
└────────────────────┬────────────────────────────────────────────┘
                     │
                     ↓
┌─────────────────────────────────────────────────────────────────┐
│                   Hardware Layer                                │
│  - BK7258 JPEG Hardware Decoder                                 │
│  - SPI DMA (LCD Display)                                        │
│  - Media Driver (UVC Camera)                                    │
└─────────────────────────────────────────────────────────────────┘
```

### Data Flow Pipeline

```
UVC Camera (MJPEG)
    ↓
tal_uvc_read_start() → Register frame_callback
    ↓
[Frame Arrives from UVC]
    ↓
__tal_uvc_frame_callback_wrapper()
    ↓
    ├─→ Hardware JPEG Decode (MJPEG → RGB565)
    │   └─→ Byte Swap (CPU LE → SPI BE)
    │       └─→ User Callback #1 (fmt=RGB565) → LCD Display
    │
    └─→ User Callback #2 (fmt=JPEG) → Cloud Upload

LCD Display Path (Asynchronous):
    ↓
tuya_lcd_pbuf_acquire(300ms) → Get buffer from pool
    ↓
Copy & Byte Swap → RGB565 data
    ↓
tuya_ai_display_flush(&buffer->frame) → Async SPI start
    ↓
[Return Immediately - Non-blocking]
    ↓
[SPI DMA Transfer in Background - 23 ticks]
    ↓
SPI Complete Interrupt → frame_flush_callback()
    ↓
tuya_lcd_pbuf_release() → Return buffer to pool
```

---

## 🚀 Quick Start

### 1. Initialization

```c
#include "tal_uvc.h"

// Configure UVC camera
TAL_UVC_CFG_T uvc_cfg = {
    .width = 320,
    .height = 240,
    .fmt = TUYA_FRAME_FMT_JPEG,    // MJPEG input
    .fps = 15,
    .power_pin = TUYA_GPIO_NUM_26,
    .active_level = TUYA_GPIO_LEVEL_HIGH,
    .frame_cb = my_frame_callback,  // Your callback
    .args = NULL
};

// Initialize UVC
TAL_UVC_HANDLE_T uvc_handle;
OPERATE_RET ret = tal_uvc_init(&uvc_handle, &uvc_cfg);
if (ret != OPRT_OK) {
    TAL_PR_ERR("UVC init failed: %d", ret);
    return ret;
}

// Initialize LCD buffer pool (for display path)
ret = tuya_lcd_pbuf_init(320, 172, 3);  // 3 buffers for 320x172 LCD
if (ret != OPRT_OK) {
    TAL_PR_ERR("LCD pbuf init failed: %d", ret);
    return ret;
}

// Start camera capture
ret = tal_uvc_start(uvc_handle);
```

### 2. Frame Callback Implementation

```c
void my_frame_callback(TAL_UVC_HANDLE_T handle, TAL_UVC_FRAME_T *frame, void *args)
{
    // You will receive TWO callbacks per camera frame:
    
    // Callback #1: RGB565 (decoded, for LCD display)
    if (frame->fmt == TUYA_FRAME_FMT_RGB565) {
        // Acquire LCD buffer
        tuya_lcd_pbuf_node_t *lcd_buf = tuya_lcd_pbuf_acquire(300);
        if (!lcd_buf) {
            TAL_PR_WARN("Buffer pool full, frame dropped");
            return;
        }
        
        // Copy and byte swap
        uint16_t *src = (uint16_t *)frame->frame;
        uint16_t *dst = (uint16_t *)lcd_buf->frame.frame;
        for (uint32_t i = 0; i < 320 * 172; i++) {
            dst[i] = ((src[i] & 0xff00) >> 8) | ((src[i] & 0x00ff) << 8);
        }
        
        // Setup callback and flush (async)
        lcd_buf->frame.pdata = (uint8_t *)lcd_buf;
        lcd_buf->frame.free_cb = lcd_flush_callback;
        tuya_ai_display_flush(&lcd_buf->frame);
    }
    
    // Callback #2: JPEG (original, for cloud upload)
    else if (frame->fmt == TUYA_FRAME_FMT_JPEG) {
        // Upload to cloud or save to file
        upload_to_cloud(frame->frame, frame->length);
    }
}

// SPI completion callback (auto-release buffer)
void lcd_flush_callback(ty_frame_buffer_t *frame)
{
    tuya_lcd_pbuf_node_t *buf = (tuya_lcd_pbuf_node_t *)frame->pdata;
    tuya_lcd_pbuf_release(buf);
}
```

### 3. Cleanup

```c
// Stop capture
tal_uvc_stop(uvc_handle);

// Cleanup resources
tal_uvc_deinit(uvc_handle);
tuya_lcd_pbuf_deinit();
```

---

## 📊 Performance Analysis

### Single Buffer (Blocking) vs Multi-Buffer Pool (Non-blocking)

#### **Before: Single Buffer Blocking Model**

```
Camera Frame → JPEG Decode (8 ticks) 
             → Byte Swap (2 ticks) 
             → SPI Flush (23 ticks) [BLOCKING] 
             → Next Frame

Total: 33 ticks/frame
Problem: Camera thread blocked during SPI transmission
Frame Rate: ~3 fps effective (when SPI is bottleneck)
```

#### **After: Multi-Buffer Pool Model**

```
Camera Frame → JPEG Decode (8 ticks) 
             → Byte Swap (2 ticks) 
             → SPI Flush (0 ticks) [ASYNC] 
             → Next Frame (immediately)

Total: 10 ticks/frame
Benefit: SPI transfer happens in parallel
Frame Rate: ~15 fps (full camera output)
```

### Performance Metrics

| Metric | Single Buffer | Multi-Buffer (3x) | Improvement |
|--------|---------------|-------------------|-------------|
| **Frame Processing** | 33 ticks | 10 ticks | 3.3x faster |
| **SPI Wait Time** | 23 ticks (blocking) | 0 ticks (async) | ✅ Eliminated |
| **Effective FPS** | ~3 fps | ~15 fps | 5x throughput |
| **Frame Drop Rate** | High (>50%) | Low (<5%) | 10x better |
| **Memory Usage** | 110 KB | 330 KB | +220 KB |

**Trade-off:** 220 KB PSRAM for 5x throughput improvement ✅

---

## 🧰 API Reference

### TAL UVC Core APIs

#### `tal_uvc_init()`
```c
OPERATE_RET tal_uvc_init(TAL_UVC_HANDLE_T *handle, TAL_UVC_CFG_T *cfg);
```
- **Purpose**: Initialize UVC camera and allocate resources
- **Parameters**:
  - `handle`: Output handle pointer (allocated internally)
  - `cfg`: Camera configuration (resolution, format, callback, etc.)
- **Returns**: `OPRT_OK` on success
- **Note**: Also initializes hardware JPEG decoder

#### `tal_uvc_start()`
```c
OPERATE_RET tal_uvc_start(TAL_UVC_HANDLE_T handle);
```
- **Purpose**: Start camera capture (calls media driver)
- **Note**: Frames will arrive via callback set in `cfg->frame_cb`

#### `tal_uvc_stop()`
```c
OPERATE_RET tal_uvc_stop(TAL_UVC_HANDLE_T handle);
```
- **Purpose**: Stop camera capture

#### `tal_uvc_deinit()`
```c
OPERATE_RET tal_uvc_deinit(TAL_UVC_HANDLE_T handle);
```
- **Purpose**: Cleanup and free all resources
- **Note**: Also deinitializes hardware JPEG decoder

### LCD Buffer Pool APIs

#### `tuya_lcd_pbuf_init()`
```c
OPERATE_RET tuya_lcd_pbuf_init(UINT16_T width, UINT16_T height, UINT8_T num);
```
- **Purpose**: Pre-allocate buffer pool
- **Parameters**:
  - `width`, `height`: Buffer dimensions (pixels)
  - `num`: Number of buffers (recommended: 3-5)
- **Memory**: `num × width × height × 2` bytes PSRAM
- **Example**: `tuya_lcd_pbuf_init(320, 172, 3)` → 330 KB

#### `tuya_lcd_pbuf_acquire()`
```c
tuya_lcd_pbuf_node_t* tuya_lcd_pbuf_acquire(UINT32_T timeout_ms);
```
- **Purpose**: Get an available buffer (blocking if pool full)
- **Returns**: Buffer node pointer or `NULL` on timeout
- **Thread-safe**: Yes (semaphore + mutex)

#### `tuya_lcd_pbuf_release()`
```c
OPERATE_RET tuya_lcd_pbuf_release(tuya_lcd_pbuf_node_t *node);
```
- **Purpose**: Return buffer to pool
- **Context**: Safe to call from interrupt (SPI callback)
- **Note**: Signals waiting producers via semaphore

#### `tuya_lcd_pbuf_deinit()`
```c
OPERATE_RET tuya_lcd_pbuf_deinit(VOID);
```
- **Purpose**: Free all buffers and synchronization primitives
- **Note**: Ensure all buffers are released before calling

---

## 🧪 CLI Testing Commands

### UVC Camera Test (when `TAL_UVC_TEST=1`)

#### `uvc init <width> <height>`
```bash
uvc init 320 240
```
- Initialize UVC camera with specified MJPEG resolution
- Automatically initializes LCD buffer pool

#### `uvc start`
```bash
uvc start
```
- Start camera capture and LCD display
- Pauses main UI display (`tuya_ai_display_pause()`)

#### `uvc stop`
```bash
uvc stop
```
- Stop camera capture
- Resumes main UI display (`tuya_ai_display_resume()`)

#### `uvc deinit`
```bash
uvc deinit
```
- Cleanup all resources

### Hardware Test Commands

#### `hw_test mjpeg`
```bash
hw_test mjpeg
```
- Test JPEG hardware decode with embedded MJPEG data
- Displays result on LCD

#### `hw_test rgb`
```bash
hw_test rgb
```
- Test DMA2D RGB565 conversion
- Uses embedded RGB565 test pattern

#### `hw_test red|green|blue`
```bash
hw_test red
hw_test green
hw_test blue
```
- Fill LCD with solid color (for display verification)

---

## 🔧 Configuration

### Compile-Time Options

#### Enable Test Functions
```c
// In tal_uvc.h or build config
#define TAL_UVC_TEST 1  // 0: disable, 1: enable
```

#### Debug Logging
```c
// In tal_uvc.c
#define TAL_UVC_LOG_ENABLE 1  // Enable verbose logs in callbacks
```

### Runtime Configuration

#### Buffer Pool Size
```c
// Trade-off: Memory vs Performance
tuya_lcd_pbuf_init(320, 172, 3);  // Recommended: 3 buffers
tuya_lcd_pbuf_init(320, 172, 5);  // More buffers = less drop, more memory
```

#### Acquire Timeout
```c
// Adjust based on expected SPI transfer time
tuya_lcd_pbuf_acquire(300);  // 300ms timeout (default)
tuya_lcd_pbuf_acquire(500);  // Longer timeout for slower displays
```

---

## 🔍 Memory Layout Details

### Buffer Pool Control Structure
```c
tuya_lcd_pbuf_t {
    SLIST_HEAD free_list;         // Free buffer linked list
    MUTEX_HANDLE mutex;           // Protects list operations
    SEM_HANDLE sem;               // Tracks available count
    UINT8_T total_count;          // Total buffers
    UINT8_T free_count;           // Current free (debug)
}
```

### Buffer Node Structure
```c
tuya_lcd_pbuf_node_t {
    SLIST_HEAD list;              // Linked list node
    ty_frame_buffer_t frame;      // Integrated frame metadata
        ├─ fmt: TY_PIXEL_FMT_RGB565
        ├─ type: TYPE_PSRAM
        ├─ frame: [PSRAM Buffer] → [110KB RGB565 Data]
        ├─ width, height, len: Dimensions
        ├─ pdata: Self-reference → tuya_lcd_pbuf_node_t*
        └─ free_cb: Auto-release callback
}
```

### Memory Footprint Example (320x172 LCD)
```
Single Buffer:  110,080 bytes (107.5 KB)
Node Overhead:       64 bytes
Per Buffer:     110,144 bytes

3 Buffers:      330,432 bytes (322.7 KB) ✅ Recommended
5 Buffers:      550,720 bytes (537.8 KB) ⚠️ For high-throughput
```

---

## 🛡️ Thread Safety

### Acquire Operation
- **Semaphore Wait**: Blocks if no buffers available
- **Mutex Lock**: Protects `free_list` during node extraction
- **Safe for**: Multiple producer threads (not recommended, single producer optimal)

### Release Operation
- **Mutex Lock**: Protects `free_list` during node insertion
- **Semaphore Post**: Signals waiting producers
- **Safe for**: Interrupt context (SPI callback) ✅

### Init/Deinit
- **NOT thread-safe**: Must be called during single-threaded init/shutdown

---

## 📂 File Structure

```
tal_uvc/
├── README.md                 # This file (complete documentation)
├── include/
│   └── tal_uvc.h             # Public API: UVC camera interface
├── src/
│   ├── tal_uvc.c             # UVC driver implementation
│   ├── tuya_lcd_pbuf.h       # LCD buffer pool API
│   └── tuya_lcd_pbuf.c       # LCD buffer pool implementation
└── local.mk                  # Build configuration
```

---

## 🐛 Troubleshooting

### Issue: "Failed to acquire LCD buffer, frame dropped"
**Cause**: Buffer pool exhausted (all buffers in SPI transmission)  
**Solutions**:
1. Increase buffer count: `tuya_lcd_pbuf_init(320, 172, 5)`
2. Increase timeout: `tuya_lcd_pbuf_acquire(500)`
3. Check SPI transfer speed (may be too slow)

### Issue: Display shows corrupted colors
**Cause**: Missing byte swap or incorrect swap direction  
**Solutions**:
1. Ensure byte swap: `dst[i] = ((src[i] & 0xff00) >> 8) | ((src[i] & 0x00ff) << 8)`
2. Check LCD endianness configuration

### Issue: Memory leak or crash in buffer pool
**Cause**: Buffer released twice or not released  
**Solutions**:
1. Verify `pdata` self-reference: `lcd_buf->frame.pdata = (uint8_t *)lcd_buf`
2. Check callback is registered: `lcd_buf->frame.free_cb = lcd_flush_callback`
3. Debug with `tuya_lcd_pbuf_get_stats(&total, &free)`

---

## 📚 References

- **Hardware JPEG Decoder**: BK7258 SDK Documentation
- **DMA2D**: `vendor/T5/tuyaos/tuyaos_adapter/include/dma2d/tkl_dma2d.h`
- **Media Driver**: `vendor/T5/t5_os/build/bk7258/armino_as_lib/include/driver/media_types.h`
- **Frame Buffer**: `include/ty_frame_buff.h`
- **Singly Linked List**: `vendor/T5/tuyaos/tuyaos_adapter/include/utilities/include/tuya_slist.h`

---

## 📝 Change Log

### Version 1.0.0 (2025-01-04)
- Initial release with UVC camera abstraction
- Hardware JPEG decode integration
- Multi-buffer LCD pool implementation
- Dual-path output (RGB565 + MJPEG)
- CLI test commands
- Performance optimization (3.3x throughput)

---

## 📧 Contact

**Author:** linch  
**Organization:** Tuya Inc.  
**Copyright:** © 2025 Tuya Inc. All rights reserved.

For issues or questions, please refer to TuyaOS documentation or contact the development team.
