#include "tal_display_spi.h"
#include "tkl_gpio.h"
#include "tkl_spi.h"
#include "tal_semaphore.h"
#include "tal_queue.h"
#include "tal_thread.h"
#include "tkl_system.h"
#include "tkl_memory.h"
#include "utilities/uni_log.h"
#include "tuya_app_config.h"

#ifndef TAL_DISPLAY_PRI
#define TAL_DISPLAY_PRI THREAD_PRIO_0
#endif

#if defined(ENABLE_SPI) && (ENABLE_SPI==1)
#define DISPLAY_SPI_DEVICE_CASET        0x2A
#define DISPLAY_SPI_DEVICE_RASET        0x2B
#define DISPLAY_SPI_DEVICE_RAMWR        0x2C
#define DISPLAY_SPI_MSG_DATA            0xAA
#define DISPLAY_SPI_MSG_EXIT            0xBB

#define DISPLAY_SPI_EXIT               0x00
#define DISPLAY_SPI_RUNNING            0x01

#define UNACTIVE_LEVEL(x) ((x == 0)? 1: 0)

typedef struct {
    SEM_HANDLE tx_sem;
    SEM_HANDLE state_sem;
    QUEUE_HANDLE queue;
    THREAD_HANDLE spi_task;
    UINT32_T spi_task_state;
} display_spi_sync_t;

typedef struct {
    ty_frame_buffer_t *frame_buff;
    uint8_t msg_type;
} display_spi_msg_t;

display_spi_sync_t display_spi_sync[TUYA_SPI_NUM_MAX] = {0};


VOID_T spi_isr_cb(TUYA_SPI_NUM_E port, TUYA_SPI_IRQ_EVT_E event)
{
    if(event == TUYA_SPI_EVENT_TX_COMPLETE) {
        if(display_spi_sync[port].tx_sem) {
            tal_semaphore_post(display_spi_sync[port].tx_sem);
        }       
   }
}

static OPERATE_RET display_spi_device_gpio_init(ty_display_spi_device_t *device)
{
    TUYA_GPIO_BASE_CFG_T cfg = {.direct = TUYA_GPIO_OUTPUT};
    OPERATE_RET ret = OPRT_OK;
    
    if(device->display_cfg->power_ctrl.pin < TUYA_GPIO_NUM_MAX) {
        cfg.level = device->display_cfg->power_ctrl.active_level;
        ret = tkl_gpio_init(device->display_cfg->power_ctrl.pin, &cfg);
    }

    if(device->display_cfg->reset.pin < TUYA_GPIO_NUM_MAX) {
        cfg.level = UNACTIVE_LEVEL(device->display_cfg->reset.active_level);
        ret |= tkl_gpio_init(device->display_cfg->reset.pin, &cfg);
    }

    if(device->display_cfg->rs.pin < TUYA_GPIO_NUM_MAX) {
        cfg.level = UNACTIVE_LEVEL(device->display_cfg->rs.active_level);
        ret |= tkl_gpio_init(device->display_cfg->rs.pin, &cfg);
    }

    if(device->display_cfg->bl.pin < TUYA_GPIO_NUM_MAX) {
        cfg.level = UNACTIVE_LEVEL(device->display_cfg->bl.active_level);
        ret |= tkl_gpio_init(device->display_cfg->bl.pin, &cfg);
    }

    if(device->display_cfg->soft_cs.pin < TUYA_GPIO_NUM_MAX) {
        cfg.level = UNACTIVE_LEVEL(device->display_cfg->soft_cs.active_level);
        ret |= tkl_gpio_init(device->display_cfg->soft_cs.pin, &cfg);
    }
    
    return ret;
}

static OPERATE_RET display_spi_device_gpio_deinit(ty_display_spi_device_t *device)
{
    //reset pin deinit
    //rs pin deinit
    //bl pin deinit
    OPERATE_RET ret = 0;
    if(device->display_cfg->reset.pin < TUYA_GPIO_NUM_MAX) {
        ret = tkl_gpio_deinit(device->display_cfg->reset.pin);
    }

    if(device->display_cfg->power_ctrl.pin < TUYA_GPIO_NUM_MAX) {
        ret |= tkl_gpio_write(device->display_cfg->power_ctrl.pin, UNACTIVE_LEVEL(device->display_cfg->power_ctrl.active_level));
        ret |= tkl_gpio_deinit(device->display_cfg->power_ctrl.pin);//是否需要deinit，调低功耗时再确认
    }

    if(device->display_cfg->rs.pin < TUYA_GPIO_NUM_MAX) {
        ret |= tkl_gpio_deinit(device->display_cfg->rs.pin);
    }

    if(device->display_cfg->bl.pin < TUYA_GPIO_NUM_MAX) {
        ret |= tkl_gpio_deinit(device->display_cfg->bl.pin);
    }

    if(device->display_cfg->soft_cs.pin < TUYA_GPIO_NUM_MAX) {
        ret |= tkl_gpio_deinit(device->display_cfg->soft_cs.pin);
    }

    return ret;
}

static OPERATE_RET _display_spi_send(ty_display_spi_device_t *device, VOID_T *data, UINT32_T size)
{
    if(display_spi_sync[device->display_cfg->port].tx_sem == NULL) {
        return -2;
    }
        
    OPERATE_RET ret = 0;
    UINT32_T left_len = size;
    UINT32_T send_len = 0;
    UINT32_T dma_max_size = tkl_spi_get_max_dma_data_length();
    while(left_len > 0) {
        send_len = (left_len > dma_max_size) ? dma_max_size : (left_len);

        if(device->display_cfg->soft_cs.pin < TUYA_GPIO_NUM_MAX) {
            ret = tkl_gpio_write(device->display_cfg->soft_cs.pin, device->display_cfg->soft_cs.active_level);
            if(ret) return ret;
        }
        
        ret = tkl_spi_send(device->display_cfg->port, data + size - left_len, send_len);
        if(ret) return ret;

        ret = tal_semaphore_wait(display_spi_sync[device->display_cfg->port].tx_sem, 5000);
        if(ret)  return ret;

        if(device->display_cfg->soft_cs.pin < TUYA_GPIO_NUM_MAX) {
            ret = tkl_gpio_write(device->display_cfg->soft_cs.pin, UNACTIVE_LEVEL(device->display_cfg->soft_cs.active_level));
            if(ret) return ret;
        }

        left_len -= send_len;
    }

    return ret;
}

static OPERATE_RET display_spi_send_cmd(ty_display_spi_device_t *device, uint8_t cmd)
{
    //set rs pin low
    OPERATE_RET ret = tkl_gpio_write(device->display_cfg->rs.pin, TUYA_GPIO_LEVEL_LOW);
    if(ret) return ret;

    return _display_spi_send(device, &cmd, 1);
}

static OPERATE_RET display_spi_send_data(ty_display_spi_device_t *device, uint8_t *data, uint32_t data_len)
{
    //set rs pin high
    OPERATE_RET ret = tkl_gpio_write(device->display_cfg->rs.pin, TUYA_GPIO_LEVEL_HIGH);
    if(ret) return ret;

    return _display_spi_send(device, data, data_len);
}


static OPERATE_RET display_spi_pixel_send(ty_display_spi_device_t *device, ty_frame_buffer_t *frame_buff)
{
    OPERATE_RET ret = OPRT_OK;
    
    UINT16_T x0 = frame_buff->x_start;
    UINT16_T x1 = frame_buff->x_start + frame_buff->width - 1;
    UINT16_T y0 = frame_buff->y_start;
    UINT16_T y1 = frame_buff->y_start + frame_buff->height - 1;
    UINT8_T column_value[4] = {0};
    UINT8_T row_value[4] = {0};
    column_value[0] = (x0 >> 8) & 0xFF;
    column_value[1] = (x0 & 0xFF);
    column_value[2] = (x1 >> 8) & 0xFF;
    column_value[3] = (x1 & 0xFF);
    row_value[0] = (y0 >> 8) & 0xFF;
    row_value[1] = (y0 & 0xFF);
    row_value[2] = (y1 >> 8) & 0xFF;
    row_value[3] = (y1 & 0xFF);

    display_spi_send_cmd(device, DISPLAY_SPI_DEVICE_CASET);
    display_spi_send_data(device, column_value, 4);
    display_spi_send_cmd(device, DISPLAY_SPI_DEVICE_RASET);
    display_spi_send_data(device, row_value, 4);

    display_spi_send_cmd(device, DISPLAY_SPI_DEVICE_RAMWR);
    display_spi_send_data(device, frame_buff->frame, frame_buff->width * frame_buff->height * 2);
    return ret;
}

static void __spi_task(void *args)
{
    ty_display_spi_device_t *device = (ty_display_spi_device_t *)args;
    UINT32_T irq_mask = 0;
    display_spi_msg_t msg = {0};
    ty_frame_buffer_t *frame_buff = NULL;
    while (display_spi_sync[device->display_cfg->port].spi_task_state) {
        if (tal_queue_fetch(display_spi_sync[device->display_cfg->port].queue, &msg, SEM_WAIT_FOREVER) == OPRT_OK) {
            if (msg.msg_type == DISPLAY_SPI_MSG_EXIT) {
                irq_mask = tkl_system_enter_critical();
                display_spi_sync[device->display_cfg->port].spi_task_state = DISPLAY_SPI_EXIT;
                tkl_system_exit_critical(irq_mask);
                if(display_spi_sync[device->display_cfg->port].state_sem) {
                    //设置退出状态后，flush无法再向队列中存放数据,清空队列
                    while (tal_queue_fetch(display_spi_sync[device->display_cfg->port].queue, &msg, 0) == OPRT_OK) {
                        frame_buff = msg.frame_buff;
                        //临界状态下在退出消息和实际置退出状态之间会有有效数据传入，此时不发送，但需通知应用数据已处理
                        if (frame_buff != NULL) {
                            if (frame_buff->free_cb) {
                                frame_buff->free_cb(frame_buff);
                            }
                        }
                    }
                    tal_semaphore_post(display_spi_sync[device->display_cfg->port].state_sem);
                }
                break;
            } else {
                frame_buff = msg.frame_buff;
                if ((device != NULL) && (frame_buff != NULL) && (frame_buff->frame != NULL)) {
                    display_spi_pixel_send(device, frame_buff);
                    if (frame_buff->free_cb) {
                        frame_buff->free_cb(frame_buff);
                    }
                }
            }
        }
    }
    
    tal_thread_delete(display_spi_sync[device->display_cfg->port].spi_task);
}

#endif
/**
* @brief tal_display_spi_open
*
* @param[in] device: device info
*
* @note This API is used to open display device.
*
* @return id >= 0 on success. Others on error
*/
OPERATE_RET tal_display_spi_open(ty_display_spi_device_t *device)
{
    #if defined(ENABLE_SPI) && (ENABLE_SPI==1)
    UINT32_T irq_mask = 0;
    OPERATE_RET ret = OPRT_OK;
    UINT32_T zero_count = 0;
    
    // 统计为0的引脚数量
    if (device->display_cfg->reset.pin == 0) zero_count++;
    if (device->display_cfg->bl.pin == 0) zero_count++;
    if (device->display_cfg->rs.pin == 0) zero_count++;
    if (device->display_cfg->power_ctrl.pin == 0) zero_count++;
    if (device->display_cfg->soft_cs.pin == 0) zero_count++;
    
    if (zero_count > 1) {
        TAL_PR_NOTICE("Error, must set valid pin num\n", zero_count);
        return OPRT_COM_ERROR;
    }
    if (zero_count == 1) {
        TAL_PR_NOTICE("Please make sure your pin number, reset:%d bl:%d rs:%d power_ctrl:%d soft_cs:%d\n", 
            device->display_cfg->reset.pin, device->display_cfg->bl.pin,
            device->display_cfg->rs.pin, device->display_cfg->power_ctrl.pin,
            device->display_cfg->soft_cs.pin);
    }
    
    ret = tkl_spi_init(device->display_cfg->port, &(device->cfg));
    if(ret) goto _exit;

    //spi driver init
    ret = tkl_spi_irq_init(device->display_cfg->port, spi_isr_cb);
    if(ret) goto _exit;

    ret = tkl_spi_irq_enable(device->display_cfg->port);
    if(ret) goto _exit;

    //gpio init
    ret = display_spi_device_gpio_init(device);
    if(ret) goto _exit;
    
    //sem init
    ret = tal_semaphore_create_init(&(display_spi_sync[device->display_cfg->port].tx_sem), 0, 1);
    if(ret) return ret;

    ret = tal_semaphore_create_init(&(display_spi_sync[device->display_cfg->port].state_sem), 0, 1);
    if(ret) goto _exit;

    ret = tal_queue_create_init(&(display_spi_sync[device->display_cfg->port].queue), sizeof(display_spi_msg_t), 4);
    if(ret) goto _exit;

    //spi lcd init cmd & data
    if (device->init_seq != NULL) {
        DISPLAY_INIT_SEQ_T *seq = device->init_seq;
        for (;;) {
            switch (seq->type) {
                case TY_INIT_RST: {
                    // reset
                    DISPLAY_RESET_DATA_T *reset = seq->reset;
                    int rst_gpio = device->display_cfg->reset.pin;
                    TUYA_GPIO_LEVEL_E active_level = device->display_cfg->reset.active_level;
                    TUYA_GPIO_LEVEL_E unactive_level = UNACTIVE_LEVEL(device->display_cfg->reset.active_level);
                
                    tkl_gpio_write(rst_gpio, unactive_level);
                    tkl_system_sleep(reset[0].delay);

                    tkl_gpio_write(rst_gpio, active_level);
                    tkl_system_sleep(reset[1].delay);

                    tkl_gpio_write(rst_gpio, unactive_level);
                    tkl_system_sleep(reset[2].delay);

                    break;
                }

                case TY_INIT_REG: {
                    // set reg value
                    DISPLAY_REG_DATA_T *reg = &seq->reg;
                    display_spi_send_cmd(device, reg->r);
                    if (reg->len != 0) {
                        display_spi_send_data(device, reg->v, reg->len);
                    }

                    break;
                }

                case TY_INIT_DELAY: {
                    if (seq->delay_time != 0) {
                        tkl_system_sleep(seq->delay_time);
                    }

                    break;
                }

                case TY_INIT_CONF_END: {
                    THREAD_CFG_T thread_cfg = {2048, TAL_DISPLAY_PRI, "spi_task"};
                    irq_mask = tkl_system_enter_critical();
                    display_spi_sync[device->display_cfg->port].spi_task_state = DISPLAY_SPI_RUNNING;
                    tkl_system_exit_critical(irq_mask);
                    ret = tal_thread_create_and_start(&(display_spi_sync[device->display_cfg->port].spi_task), NULL, NULL, __spi_task, device, &thread_cfg);
                    if(ret) goto _exit;
                    return 0;
                }

                default: {
                    return -1;
                }
            }
            seq++;
        }
    } else {
        TAL_PR_NOTICE("lcd spi device init cmd is null");
    }
    return 0;
_exit:
    //此时任务尚未退出open，flush等接口尚未向队列中存放数据，任务未成功运行，可直接置状态
    irq_mask = tkl_system_enter_critical();
    display_spi_sync[device->display_cfg->port].spi_task_state = DISPLAY_SPI_EXIT;
    tkl_system_exit_critical(irq_mask);
    if (display_spi_sync[device->display_cfg->port].queue) {
        tal_queue_free(display_spi_sync[device->display_cfg->port].queue);
        display_spi_sync[device->display_cfg->port].queue = NULL;
    }

    if (display_spi_sync[device->display_cfg->port].state_sem) {
        //deint sem
        ret |= tal_semaphore_release(display_spi_sync[device->display_cfg->port].state_sem);
        display_spi_sync[device->display_cfg->port].state_sem = NULL;
    }

    if (display_spi_sync[device->display_cfg->port].tx_sem) {
        ret |= tal_semaphore_release(display_spi_sync[device->display_cfg->port].tx_sem);
        display_spi_sync[device->display_cfg->port].tx_sem = NULL;
    }

    //set reset pin low
    if(device->display_cfg->reset.pin < TUYA_GPIO_NUM_MAX) {
        TUYA_GPIO_LEVEL_E unactive_level = UNACTIVE_LEVEL(device->display_cfg->reset.active_level);
        ret = tkl_gpio_write(device->display_cfg->reset.pin, unactive_level);
    }

    //power ctrl引脚给屏断电
    if(device->display_cfg->power_ctrl.pin < TUYA_GPIO_NUM_MAX) {
        ret = tkl_gpio_write(device->display_cfg->power_ctrl.pin, UNACTIVE_LEVEL(device->display_cfg->power_ctrl.active_level));
    }

    //gpio deinit
    ret |= display_spi_device_gpio_deinit(device);

    //disable spi irq
    ret |= tkl_spi_irq_disable(device->display_cfg->port);

    //spi deint
    ret |= tkl_spi_deinit(device->display_cfg->port);
    return ret;
#else
    return -1;
#endif
}

/**
* @brief tal_display_close
*
* @param[in] device: device info
*
* @note This API is used to close display device.
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
OPERATE_RET tal_display_spi_close(ty_display_spi_device_t *device)
{
#if defined(ENABLE_SPI) && (ENABLE_SPI==1)
    OPERATE_RET ret = 0;
    display_spi_msg_t msg = {0};

    msg.frame_buff = NULL;
    msg.msg_type = DISPLAY_SPI_MSG_EXIT;
    ret |= tal_queue_post(display_spi_sync[device->display_cfg->port].queue, &msg, SEM_WAIT_FOREVER);

    ret |= tal_semaphore_wait(display_spi_sync[device->display_cfg->port].state_sem, 5000);

    tal_queue_free(display_spi_sync[device->display_cfg->port].queue);
    display_spi_sync[device->display_cfg->port].queue = NULL;

    //deint sem
    ret |= tal_semaphore_release(display_spi_sync[device->display_cfg->port].state_sem);
    display_spi_sync[device->display_cfg->port].state_sem = NULL;

    ret |= tal_semaphore_release(display_spi_sync[device->display_cfg->port].tx_sem);
    display_spi_sync[device->display_cfg->port].tx_sem = NULL;

    //set reset pin low
    if(device->display_cfg->reset.pin < TUYA_GPIO_NUM_MAX) {
        TUYA_GPIO_LEVEL_E unactive_level = UNACTIVE_LEVEL(device->display_cfg->reset.active_level);
        ret = tkl_gpio_write(device->display_cfg->reset.pin, unactive_level);
    }

    //power ctrl引脚给屏断电
    if(device->display_cfg->power_ctrl.pin < TUYA_GPIO_NUM_MAX) {
        ret = tkl_gpio_write(device->display_cfg->power_ctrl.pin, UNACTIVE_LEVEL(device->display_cfg->power_ctrl.active_level));
    }

    //disable spi irq
    ret |= tkl_spi_irq_disable(device->display_cfg->port);

    //spi deint
    ret |= tkl_spi_deinit(device->display_cfg->port);

    //gpio deinit
    ret |= display_spi_device_gpio_deinit(device);
    return ret;
#else
    return -1;
#endif

}

/**
* @brief tal_display_spi_flush
*
* @param[in] device: device info
* @param[in] frame_buff: frame buff
*
* @note This API is used to flush display device.
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
OPERATE_RET tal_display_spi_flush(ty_display_spi_device_t *device, ty_frame_buffer_t *frame_buff)
{
    OPERATE_RET ret = OPRT_OK;
#if defined(ENABLE_SPI) && (ENABLE_SPI==1)
    display_spi_msg_t msg = {0};
    //无论单缓存多缓存，拷贝完推入队列，当前任务直接返回继续执行
    msg.frame_buff = frame_buff;
    msg.msg_type = DISPLAY_SPI_MSG_DATA;

    if ((display_spi_sync[device->display_cfg->port].spi_task_state == DISPLAY_SPI_RUNNING) && (display_spi_sync[device->display_cfg->port].queue != NULL)) {
        ret = tal_queue_post(display_spi_sync[device->display_cfg->port].queue, &msg, SEM_WAIT_FOREVER);
    } else {
        ret = OPRT_COM_ERROR;
    }
    
    return ret;
#else
    return -1;
#endif
}
