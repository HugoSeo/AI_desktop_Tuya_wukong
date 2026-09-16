#include "tal_display_8080.h"
#include "tal_semaphore.h"
#include "tkl_gpio.h"
#include "uni_log.h"
#if defined(ENABLE_MCU8080) && (ENABLE_MCU8080==1)
#include "tkl_8080.h"

#define UNACTIVE_LEVEL(x) ((x == 0)? 1: 0)
typedef struct {
    SEM_HANDLE tx_sem;
    SEM_HANDLE te_sem;
    UINT16_T width;
    UINT16_T height;
    DISPLAY_PIXEL_FORMAT_E fmt;
    UINT8_T has_flushed_flag;
    UINT8_T flush_start_flag;//帧同步标志，避免屏幕显示撕裂
} display_8080_manage;

display_8080_manage lcd8080_manage = {NULL, NULL, 0, 0,  0xff, 0, 0};

static VOID_T display_8080_isr(TUYA_8080_EVENT_E event)
{
    tkl_8080_transfer_stop();
    if(MCU8080_OUTPUT_FINISH ==event && lcd8080_manage.tx_sem) {
       tal_semaphore_post(lcd8080_manage.tx_sem); 
    }   
}
static VOID_T __te_isr_cb(VOID_T *args)
{
    if(lcd8080_manage.te_sem && lcd8080_manage.flush_start_flag) {
        tal_semaphore_post(lcd8080_manage.te_sem);
    }     
}

static OPERATE_RET display_8080_gpio_init(ty_display_8080_device_t *device)
{
    TUYA_GPIO_BASE_CFG_T cfg = {.direct = TUYA_GPIO_OUTPUT};
    OPERATE_RET ret = OPRT_OK;

    if(device->display_cfg->bl.pin < TUYA_GPIO_NUM_MAX) {
        cfg.level = UNACTIVE_LEVEL(device->display_cfg->bl.active_level);
        ret = tkl_gpio_init(device->display_cfg->bl.pin, &cfg);
    }

    if(device->display_cfg->power_ctrl.pin < TUYA_GPIO_NUM_MAX) {
        cfg.level = device->display_cfg->power_ctrl.active_level;
        ret |= tkl_gpio_init(device->display_cfg->power_ctrl.pin, &cfg);
    }

    if(device->display_cfg->te_pin < TUYA_GPIO_NUM_MAX) {
        cfg.direct = TUYA_GPIO_INPUT;
        if(device->display_cfg->te_mode == TUYA_GPIO_IRQ_RISE)
            cfg.mode = TUYA_GPIO_PULLDOWN;
        else if(device->display_cfg->te_mode == TUYA_GPIO_IRQ_FALL)
            cfg.mode = TUYA_GPIO_PULLUP;

        ret |= tkl_gpio_init(device->display_cfg->te_pin, &cfg);

        TUYA_GPIO_IRQ_T irq_cfg = {.mode = device->display_cfg->te_mode, .cb = __te_isr_cb, NULL};  
        ret |= tkl_gpio_irq_init(device->display_cfg->te_pin, &irq_cfg);

        ret |= tkl_gpio_irq_enable(device->display_cfg->te_pin);
    }

    return ret;
}

static OPERATE_RET display_8080_gpio_deinit(ty_display_8080_device_t *device)
{
    OPERATE_RET ret = OPRT_OK;
    if(device->display_cfg->bl.pin < TUYA_GPIO_NUM_MAX) {
        ret = tkl_gpio_deinit(device->display_cfg->bl.pin);
    }
    
    if(device->display_cfg->power_ctrl.pin < TUYA_GPIO_NUM_MAX) {
        ret |= tkl_gpio_write(device->display_cfg->power_ctrl.pin, UNACTIVE_LEVEL(device->display_cfg->power_ctrl.active_level));//先给设备掉电
        tkl_gpio_deinit(device->display_cfg->power_ctrl.pin);//是否需要deinit，调低功耗时再确认
    }

    if(device->display_cfg->te_pin < TUYA_GPIO_NUM_MAX) {
        ret |= tkl_gpio_irq_disable(device->display_cfg->te_pin);
        tkl_gpio_deinit(device->display_cfg->te_pin);
    }

    return ret;
}
#endif

/**
* @brief tal_display_8080_open
*
* @param[in] device: device info
*
* @note This API is used to open display device.
*
* @return id >= 0 on success. Others on error
*/
OPERATE_RET tal_display_8080_open(ty_display_8080_device_t *device)
{
#if defined(ENABLE_MCU8080) && (ENABLE_MCU8080==1)
    //gpio init
    OPERATE_RET ret = display_8080_gpio_init(device);

    //tx_sem init
    ret |= tkl_semaphore_create_init(&lcd8080_manage.tx_sem, 0, 1);

    //te_sem init
    ret |= tkl_semaphore_create_init(&lcd8080_manage.te_sem, 0, 1);
    
    //8080 driver init
    ret |= tkl_8080_init(&(device->cfg));

    if(device->init) {
        device->init();
    }
        
    //8080 isr callback
    ret |= tkl_8080_irq_cb_register(display_8080_isr);

    return ret;
#else
    return -1;
#endif
}

/**
* @brief tal_display_8080_close
*
* @param[in] device: device info
*
* @note This API is used to close display device.
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
OPERATE_RET tal_display_8080_close(ty_display_8080_device_t *device)
{
#if defined(ENABLE_MCU8080) && (ENABLE_MCU8080==1)
    //deinit 8080
    OPERATE_RET ret = tkl_8080_deinit();

    //gpio deinit
    ret |= display_8080_gpio_deinit(device);

    //sem deinit
    ret |= tal_semaphore_release(lcd8080_manage.tx_sem);
    lcd8080_manage.tx_sem = NULL;
    ret |= tal_semaphore_release(lcd8080_manage.te_sem);
    lcd8080_manage.te_sem = NULL;
    lcd8080_manage.has_flushed_flag = 0;
    lcd8080_manage.flush_start_flag = 0;
    lcd8080_manage.fmt = 0xff;
    lcd8080_manage.width = 0;
    lcd8080_manage.height = 0;

    return ret;

#else
    return -1;
#endif
}

/**
* @brief tal_display_8080_flush
*
* @param[in] device: device info
* @param[in] frame_buff: frame buff
*
* @note This API is used to flush display device.
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
OPERATE_RET tal_display_8080_flush(ty_display_8080_device_t *device, ty_frame_buffer_t *frame_buff)
{
#if defined(ENABLE_MCU8080) && (ENABLE_MCU8080==1)
    OPERATE_RET ret = 0;
    if(lcd8080_manage.width != frame_buff->width || lcd8080_manage.height != frame_buff->height) {
        tkl_8080_ppi_set(frame_buff->width, frame_buff->height);
        lcd8080_manage.width = frame_buff->width;
        lcd8080_manage.height = frame_buff->height;
    }
   
    if(lcd8080_manage.fmt != frame_buff->fmt) {
        tkl_8080_pixel_mode_set(frame_buff->fmt);
        lcd8080_manage.fmt = frame_buff->fmt;
    }

    tkl_8080_base_addr_set(frame_buff->frame);

    /*等屏幕内部一帧刷完给出TE中断再开始发送数据去改写屏幕的帧缓存，避免屏幕显示有撕裂 */
    /*如果模组没有接屏的TE管脚，要将te_ipn置成TUYA_GPIO_NUM_MAX*/
    if(device->display_cfg->te_pin < TUYA_GPIO_NUM_MAX) {
        lcd8080_manage.flush_start_flag = 1;//帧同步中断判断flag为1时，post sem（这里的上下文不需要担心访问不同步的问题，并不会影响功能）
        ret = tal_semaphore_wait(lcd8080_manage.te_sem, 5000);
        lcd8080_manage.flush_start_flag = 0;
        if(ret) {
            PR_ERR("flush error(%d)...", ret);
            return ret;
        }
    }

    if(!lcd8080_manage.has_flushed_flag) {
        if(device->set_display_area)
            device->set_display_area();

        if(device->transfer_start)
            device->transfer_start();

        lcd8080_manage.has_flushed_flag = 1;
    }else {
        if(device->transfer_continue)
            device->transfer_continue();     
    }
    
    tkl_8080_transfer_start();

    return tal_semaphore_wait(lcd8080_manage.tx_sem, SEM_WAIT_FOREVER);
#else
    return -1;
#endif
}
